/**
 *@file n_kafka.c
 *@brief Generic kafka consume and produce event functions
 *@author Castagnier Mickael
 *@version 1.0
 *@date 23/11/2022
 */

#include "nilorea/n_kafka.h"
#include "nilorea/n_common.h"
#include "nilorea/n_base64.h"

/**
 * @brief get a schema from the first 4 bytes of a char *string, returning ntohl of the value
 * @param string char *source from where to read schema id
 * @return zero or positive number on success, -1 on error
 */
int32_t n_kafka_get_schema_from_char(char* string) {
    __n_assert(string, return -1);

    uint32_t raw_schema_id = 0;
    memcpy(&raw_schema_id, string + 1, sizeof(uint32_t));

    return (int32_t)ntohl(raw_schema_id);
}

/**
 * @brief get a schema from the first 4 bytes of a N_STR *string, returning ntohl of the value
 * @param string N_STR *source from where to read schema id
 * @return zero or positive number on success, -1 on error
 */
int32_t n_kafka_get_schema_from_nstr(N_STR* string) {
    __n_assert(string, return -1);
    __n_assert(string->data, return -1);
    __n_assert((string->written >= sizeof(int32_t)), return -1);
    return n_kafka_get_schema_from_char(string->data);
}

/**
 * @brief put a htonl schema id into the first 4 bytes of a char *string
 * @param string char *destination where to write schema id
 * @param schema_id the schema_id to write
 * @return zero or positive number on success, -1 on error
 */
int n_kafka_put_schema_in_char(char* string, int schema_id) {
    __n_assert(string, return FALSE);
    uint32_t schema_id_htonl = htonl((uint32_t)schema_id);  // cast to unsigned to avoid warning
    memcpy(string + 1, &schema_id_htonl, sizeof(uint32_t));
    return TRUE;
}

/**
 * @brief put a htonl schema id into the first 4 bytes of a N_STR *string
 * @param string N_STR *destination where to write schema id
 * @param schema_id the schema_id to write
 * @return zero or positive number on success, -1 on error
 */
int n_kafka_put_schema_in_nstr(N_STR* string, int schema_id) {
    __n_assert(string, return FALSE);
    __n_assert(string->data, return FALSE);
    __n_assert((string->written >= sizeof(int32_t)), return FALSE);
    return n_kafka_put_schema_in_char(string->data, schema_id);
}

/**
 * @brief return the queues status
 * @param kafka handler to use
 * @param nb_queued pointer to queue number holder
 * @param nb_waiting pointer to waiting number holder
 * @param nb_error pointer to error number holder
 * @return TRUE or FALSE
 */
int n_kafka_get_status(N_KAFKA* kafka, size_t* nb_queued, size_t* nb_waiting, size_t* nb_error) {
    __n_assert(kafka, return FALSE);
    __n_assert(nb_waiting, return FALSE);
    __n_assert(nb_error, return FALSE);
    read_lock(kafka->rwlock);
    int status = kafka->polling_thread_status;
    *nb_queued = kafka->nb_queued;
    *nb_waiting = kafka->nb_waiting;
    *nb_error = kafka->nb_error;
    unlock(kafka->rwlock);
    return status;
}

/**
 *@brief Message delivery report callback.
 * This callback is called exactly once per message, indicating if the message was succesfully delivered
 * The callback is triggered from rd_kafka_poll() and executes on
 * the application's thread
 *@param rk kafka rd_kafka_t kafka handle
 *@param rkmessage pointer to the received event in kafka form
 *@param opaque opaque structure holding the pointer of a sent event
 */
static void n_kafka_delivery_message_callback(rd_kafka_t* rk, const rd_kafka_message_t* rkmessage, void* opaque) {
    (void)opaque;

    __n_assert(rk, n_log(LOG_ERR, "rk=NULL is not a valid kafka handle"); return);
    __n_assert(rkmessage, n_log(LOG_ERR, "rkmessage=NULL is not a valid kafka message"); return);

    N_KAFKA_EVENT* event = (N_KAFKA_EVENT*)rkmessage->_private;

    if (rkmessage->err) {
        n_log(LOG_ERR, "message delivery failed: %s", rd_kafka_err2str(rkmessage->err));
        if (!event) {
            n_log(LOG_ERR, "fatal: event is NULL");
            return;
        }
        if (event && event->parent_table)
            write_lock(event->parent_table->rwlock);
        event->status = N_KAFKA_EVENT_ERROR;
        if (event && event->parent_table)
            unlock(event->parent_table->rwlock);
    } else {
        n_log(LOG_DEBUG, "message delivered (%ld bytes, partition %d)", rkmessage->len, rkmessage->partition);
        if (event) {
            // lock
            if (event && event->parent_table)
                write_lock(event->parent_table->rwlock);
            // delete produce event linked files
            if (event->event_files_to_delete) {
                char** files_to_delete = split(_nstr(event->event_files_to_delete), ";", 0);
                if (files_to_delete) {
                    if (split_count(files_to_delete) > 0) {
                        int index = 0;
                        while (files_to_delete[index]) {
                            int ret = unlink(files_to_delete[index]);
                            int error = errno;
                            if (ret == 0) {
                                n_log(LOG_DEBUG, "deleted on produce ack: %s", files_to_delete[index]);
                            } else {
                                n_log(LOG_ERR, "couldn't delete \"%s\": %s", files_to_delete[index], strerror(error));
                            }
                            index++;
                        }
                    } else {
                        n_log(LOG_ERR, "split result is empty !");
                    }
                }
                free_split_result(&files_to_delete);
            }
            // set status
            event->status = N_KAFKA_EVENT_OK;
            // unlock
            if (event && event->parent_table)
                unlock(event->parent_table->rwlock);
            n_log(LOG_INFO, "kafka event %p received an ack !", event);
        } else {
            n_log(LOG_ERR, "fatal: event is NULL");
        }
    }
    return;
    /* The rkmessage is destroyed automatically by librdkafka */
}

/**
 *@brief delete a N_KAFKA handle
 *@param kafka N_KAFKA handle to delete
 */
void n_kafka_delete(N_KAFKA* kafka) {
    __n_assert(kafka, return);

    FreeNoLog(kafka->topic);
    FreeNoLog(kafka->topics);
    FreeNoLog(kafka->groupid);

    n_kafka_stop_polling_thread(kafka);

    if (kafka->rd_kafka_handle) {
        if (kafka->mode == RD_KAFKA_CONSUMER) {
            rd_kafka_consumer_close(kafka->rd_kafka_handle);
            rd_kafka_topic_partition_list_destroy(kafka->subscription);
        }
        if (kafka->mode == RD_KAFKA_PRODUCER) {
            rd_kafka_flush(kafka->rd_kafka_handle, kafka->poll_timeout);
        }
        rd_kafka_destroy(kafka->rd_kafka_handle);
        n_log(LOG_DEBUG, "kafka handle destroyed");
    }

    if (kafka->rd_kafka_conf) {
        rd_kafka_conf_destroy(kafka->rd_kafka_conf);
    }

    if (kafka->configuration)
        cJSON_Delete(kafka->configuration);

    if (kafka->errstr)
        free_nstr(&kafka->errstr);

    if (kafka->events_to_send)
        list_destroy(&kafka->events_to_send);

    if (kafka->received_events)
        list_destroy(&kafka->received_events);

    rw_lock_destroy(kafka->rwlock);

    if (kafka->rd_kafka_topic)
        rd_kafka_topic_destroy(kafka->rd_kafka_topic);

    Free(kafka->bootstrap_servers);

    Free(kafka);
    return;
}

/**
 *@brief allocate a new kafka handle
 *@param poll_interval set polling interval
 *@param poll_timeout set polling
 *@param errstr_len set the size of the error string buffer
 *@return a new empty N_KAFKA *kafka handle or NULL
 */
N_KAFKA* n_kafka_new(int32_t poll_interval, int32_t poll_timeout, size_t errstr_len) {
    N_KAFKA* kafka = NULL;
    Malloc(kafka, N_KAFKA, 1);
    __n_assert(kafka, return NULL);

    kafka->errstr = new_nstr(errstr_len);
    __n_assert(kafka->errstr, Free(kafka); return NULL);

    kafka->events_to_send = NULL;
    kafka->received_events = NULL;
    kafka->rd_kafka_conf = NULL;
    kafka->rd_kafka_handle = NULL;
    kafka->configuration = NULL;
    kafka->groupid = NULL;
    kafka->topics = NULL;
    kafka->subscription = NULL;
    kafka->topic = NULL;
    kafka->mode = -1;
    kafka->schema_id = -1;
    kafka->poll_timeout = poll_timeout;
    kafka->poll_interval = poll_interval;
    kafka->monitored_directory_interval = 3000 ; // 3000 msecs as a default
    kafka->nb_queued = 0;
    kafka->nb_waiting = 0;
    kafka->nb_error = 0;
    kafka->event_consumption_enabled = TRUE ;
    kafka->event_production_enabled = TRUE ;
    kafka->polling_thread_status = 0;
    kafka->bootstrap_servers = NULL;

    kafka->events_to_send = new_generic_list(MAX_LIST_ITEMS);
    __n_assert(kafka->events_to_send, n_kafka_delete(kafka); return NULL);

    kafka->received_events = new_generic_list(MAX_LIST_ITEMS);
    __n_assert(kafka->events_to_send, n_kafka_delete(kafka); return NULL);

    if (init_lock(kafka->rwlock) != 0) {
        n_log(LOG_ERR, "could not init kafka rwlock in kafka structure at address %p", kafka);
        n_kafka_delete(kafka);
        return NULL;
    }

    return kafka;
}

/**
 *@brief load a kafka configuration from a file
 *@param config_file path and filename of the config file
 *@param mode RD_KAFKA_CONSUMER or RD_KAFKA_PRODUCER
 *@return an allocated and configured N_KAFKA *kafka handle or NULL
 */
N_KAFKA* n_kafka_load_config(char* config_file, int mode) {
    __n_assert(config_file, return NULL);

    N_KAFKA* kafka = NULL;

    kafka = n_kafka_new(100, -1, 1024);
    __n_assert(kafka, return NULL);

    // initialize kafka object
    kafka->rd_kafka_conf = rd_kafka_conf_new();

    // load config file
    N_STR* config_string = NULL;
    config_string = file_to_nstr(config_file);
    if (!config_string) {
        n_log(LOG_ERR, "unable to read config from file %s !", config_file);
        n_kafka_delete(kafka);
        return NULL;
    }
    cJSON* json = NULL;
    json = cJSON_Parse(_nstrp(config_string));
    if (!json) {
        n_log(LOG_ERR, "unable to parse json %s", config_string);
        free_nstr(&config_string);
        n_kafka_delete(kafka);
        return NULL;
    }

    int jsonIndex;
    for (jsonIndex = 0; jsonIndex < cJSON_GetArraySize(json); jsonIndex++) {
        cJSON* entry = cJSON_GetArrayItem(json, jsonIndex);

        if (!entry) continue;
        __n_assert(entry->string, continue);
        if (!entry->valuestring) {
            n_log(LOG_DEBUG, "no valuestring for entry %s", _str(entry->string));
            continue;
        }

        if (entry->string[0] != '-') {
            // if it's not one of the optionnal parameters not managed by kafka, then we can use rd_kafka_conf_set on them
            if (strcmp("topic", entry->string) != 0 && 
                    strcmp("topics", entry->string) != 0 && 
                    strcmp("value.schema.id", entry->string) != 0 && 
                    strcmp("value.schema.type", entry->string) != 0 && 
                    strcmp("poll.interval", entry->string) != 0 && 
                    strcmp("poll.timeout", entry->string) != 0 && 
                    strcmp("group.id.autogen", entry->string) != 0 &&
                    strcmp("monitored.directory.interval", entry->string) != 0 ) {
                if (!strcmp("group.id", entry->string)) {
                    // exclude group id for producer
                    if (mode == RD_KAFKA_PRODUCER)
                        continue;
                    kafka->groupid = strdup(entry->valuestring);
                }

                if (rd_kafka_conf_set(kafka->rd_kafka_conf, entry->string, entry->valuestring, _nstr(kafka->errstr), kafka->errstr->length) != RD_KAFKA_CONF_OK) {
                    n_log(LOG_ERR, "kafka config: %s", _nstr(kafka->errstr));
                } else {
                    n_log(LOG_DEBUG, "kafka config enabled: %s => %s", entry->string, entry->valuestring);
                }
            }
        } else {
            n_log(LOG_DEBUG, "kafka disabled config: %s => %s", entry->string, entry->valuestring);
        }
    }

    // other parameters, not directly managed by kafka API (will cause an error if used along rd_kafka_conf_set )
    // producer topic
    cJSON* jstr = NULL;
    jstr = cJSON_GetObjectItem(json, "topic");
    if (jstr && jstr->valuestring) {
        kafka->topic = strdup(jstr->valuestring);
        n_log(LOG_DEBUG, "kafka producer topic: %s", kafka->topic);
    } else {
        if (mode == RD_KAFKA_PRODUCER) {
            n_log(LOG_ERR, "no topic configured !");
            n_kafka_delete(kafka);
            return NULL;
        }
    }
    // consumer topics
    jstr = cJSON_GetObjectItem(json, "topics");
    if (jstr && jstr->valuestring) {
        kafka->topics = split(jstr->valuestring, ",", 0);
        n_log(LOG_DEBUG, "kafka consumer topics: %s", jstr->valuestring);
    } else {
        if (mode == RD_KAFKA_CONSUMER) {
            n_log(LOG_ERR, "no topics configured !");
            n_kafka_delete(kafka);
            return NULL;
        }
    }
    jstr = cJSON_GetObjectItem(json, "value.schema.id");
    if (jstr && jstr->valuestring) {
        int schem_v = atoi(jstr->valuestring);
        if (schem_v < -1 || schem_v > 9999) {
            n_log(LOG_ERR, "invalid schema id %d", schem_v);
            n_kafka_delete(kafka);
            return NULL;
        }
        n_log(LOG_DEBUG, "kafka schema id: %d", schem_v);
        kafka->schema_id = schem_v;
    }

    jstr = cJSON_GetObjectItem(json, "poll.interval");
    if (jstr && jstr->valuestring) {
        kafka->poll_interval = atoi(jstr->valuestring);
        n_log(LOG_DEBUG, "kafka poll interval: %d", kafka->poll_interval);
    }

    jstr = cJSON_GetObjectItem(json, "poll.timeout");
    if (jstr && jstr->valuestring) {
        kafka->poll_timeout = atoi(jstr->valuestring);
        n_log(LOG_DEBUG, "kafka poll timeout: %d", kafka->poll_timeout);
    }

    jstr = cJSON_GetObjectItem(json, "monitored.directory.interval");
    if (jstr && jstr->valuestring) {
        kafka->monitored_directory_interval = atoi(jstr->valuestring);
        n_log(LOG_DEBUG, "kafka monitored directory interval: %d", kafka->monitored_directory_interval);
    }


    // saving bootstrap servers in struct
    jstr = cJSON_GetObjectItem(json, "bootstrap.servers");
    if (jstr && jstr->valuestring) {
        kafka->bootstrap_servers = strdup(jstr->valuestring);
        n_log(LOG_DEBUG, "kafka bootstrap server: %s", kafka->bootstrap_servers);
    }

    if (mode == RD_KAFKA_PRODUCER) {
        // set delivery callback
        rd_kafka_conf_set_dr_msg_cb(kafka->rd_kafka_conf, n_kafka_delivery_message_callback);

        kafka->rd_kafka_handle = rd_kafka_new(RD_KAFKA_PRODUCER, kafka->rd_kafka_conf, _nstr(kafka->errstr), kafka->errstr->length);
        if (!kafka->rd_kafka_handle) {
            n_log(LOG_ERR, "failed to create new producer: %s", _nstr(kafka->errstr));
            n_kafka_delete(kafka);
            return NULL;
        }
        // conf is now owned by kafka handle
        kafka->rd_kafka_conf = NULL;
        // Create topic object
        kafka->rd_kafka_topic = rd_kafka_topic_new(kafka->rd_kafka_handle, kafka->topic, NULL);
        if (!kafka->rd_kafka_topic) {
            n_kafka_delete(kafka);
            return NULL;
        }
        kafka->mode = RD_KAFKA_PRODUCER;
    } else if (mode == RD_KAFKA_CONSUMER) {
        /* If there is no previously committed offset for a partition
         * the auto.offset.reset strategy will be used to decide where
         * in the partition to start fetching messages.
         * By setting this to earliest the consumer will read all messages
         * in the partition if there was no previously committed offset. */
        /*if (rd_kafka_conf_set(kafka -> rd_kafka_conf, "auto.offset.reset", "earliest", _nstr( kafka -> errstr ) , kafka -> errstr -> length) != RD_KAFKA_CONF_OK) {
          n_log( LOG_ERR  , "kafka conf set: %s", kafka -> errstr);
          n_kafka_delete( kafka );
          return NULL ;
          }*/

        // if groupid is not set, generate a unique one
        if (!kafka->groupid) {
            char computer_name[1024] = "";
            get_computer_name(computer_name, 1024);
            N_STR* groupid = new_nstr(1024);
            char* topics = join(kafka->topics, "_");
            // generating group id
            jstr = cJSON_GetObjectItem(json, "group.id.autogen");
            if (jstr && jstr->valuestring) {
                if (strcmp(jstr->valuestring, "host-topic-group") == 0) {
                    // nstrprintf( groupid , "%s_%s_%s" , computer_name , topics ,kafka -> bootstrap_servers );
                    nstrprintf(groupid, "%s_%s", computer_name, topics);
                } else if (strcmp(jstr->valuestring, "unique-group") == 0) {
                    // nstrprintf( groupid , "%s_%s_%s_%d" , computer_name , topics , kafka -> bootstrap_servers , getpid() );
                    nstrprintf(groupid, "%s_%s_%d", computer_name, topics, getpid());
                }
            } else  // default unique group
            {
                // nstrprintf( groupid , "%s_%s_%s_%d" , computer_name , topics , kafka -> bootstrap_servers , getpid() );
                nstrprintf(groupid, "%s_%s_%d", computer_name, topics, getpid());
                n_log(LOG_DEBUG, "group.id is not set and group.id.autogen is not set, generated unique group id: %s", _nstr(groupid));
            }
            free(topics);
            kafka->groupid = groupid->data;
            groupid->data = NULL;
            free_nstr(&groupid);
        }
        if (rd_kafka_conf_set(kafka->rd_kafka_conf, "group.id", kafka->groupid, _nstr(kafka->errstr), kafka->errstr->length) != RD_KAFKA_CONF_OK) {
            n_log(LOG_ERR, "kafka consumer group.id error: %s", _nstr(kafka->errstr));
            n_kafka_delete(kafka);
            return NULL;
        } else {
            n_log(LOG_DEBUG, "kafka consumer group.id => %s", kafka->groupid);
        }

        /* Create consumer instance.
         * NOTE: rd_kafka_new() takes ownership of the conf object
         *       and the application must not reference it again after
         *       this call.
         */
        kafka->rd_kafka_handle = rd_kafka_new(RD_KAFKA_CONSUMER, kafka->rd_kafka_conf, _nstr(kafka->errstr), kafka->errstr->length);
        if (!kafka->rd_kafka_handle) {
            n_log(LOG_ERR, "%% Failed to create new consumer: %s", kafka->errstr);
            n_kafka_delete(kafka);
            return NULL;
        }
        // conf is now owned by kafka handle
        kafka->rd_kafka_conf = NULL;

        /* Redirect all messages from per-partition queues to
         * the main queue so that messages can be consumed with one
         * call from all assigned partitions.
         *
         * The alternative is to poll the main queue (for events)
         * and each partition queue separately, which requires setting
         * up a rebalance callback and keeping track of the assignment:
         * but that is more complex and typically not recommended. */
        rd_kafka_poll_set_consumer(kafka->rd_kafka_handle);

        /* Convert the list of topics to a format suitable for librdkafka */
        int topic_cnt = split_count(kafka->topics);
        kafka->subscription = rd_kafka_topic_partition_list_new(topic_cnt);
        for (int i = 0; i < topic_cnt; i++)
            rd_kafka_topic_partition_list_add(kafka->subscription, kafka->topics[i],
                                              /* the partition is ignored
                                               * by subscribe() */
                                              RD_KAFKA_PARTITION_UA);
        /* Assign the topic. This method is disabled as it does noat allow dynamic partition assignement
           int err = rd_kafka_assign(kafka -> rd_kafka_handle, kafka -> subscription);
           if( err )
           {
           n_log( LOG_ERR , "kafka consumer: failed to assign %d topics: %s", kafka -> subscription->cnt, rd_kafka_err2str(err));
           n_kafka_delete( kafka );
           return NULL;
           } */

        /* Subscribe to the list of topics */
        int err = rd_kafka_subscribe(kafka->rd_kafka_handle, kafka->subscription);
        if (err) {
            n_log(LOG_ERR, "kafka consumer: failed to subscribe to %d topics: %s", kafka->subscription->cnt, rd_kafka_err2str(err));
            n_kafka_delete(kafka);
            return NULL;
        }

        n_log(LOG_DEBUG, "kafka consumer created and subscribed to %d topic(s), waiting for rebalance and messages...", kafka->subscription->cnt);

        kafka->mode = RD_KAFKA_CONSUMER;
    } else {
        n_log(LOG_ERR, "invalid mode %d", mode);
        n_kafka_delete(kafka);
        return NULL;
    }

    return kafka;
} /* n_kafka_load_config */

/**
 *@brief create a new empty event
 *@param schema_id schema id that the event is using, -1 if not using avro schema_id formatted events
 *@return a new empty N_KAFKA_EVENT *event
 */
N_KAFKA_EVENT* n_kafka_new_event(int schema_id) {
    N_KAFKA_EVENT* event = NULL;
    Malloc(event, N_KAFKA_EVENT, 1);
    __n_assert(event, return NULL);

    event->event_string = NULL;
    event->event_files_to_delete = NULL;
    event->from_topic = NULL;
    event->rd_kafka_headers = NULL;
    event->received_headers = NULL;
    event->schema_id = schema_id;
    event->status = N_KAFKA_EVENT_CREATED;
    event->parent_table = NULL;

    return event;
} /* n_kafka_new_event */

/**
 *@brief allocate a headers array for the event
 *@param event target event
 *@param count size in elements of the created headers array
 *@return TRUE or FALSE
 */
int n_kafka_new_headers(N_KAFKA_EVENT* event, size_t count) {
    __n_assert(event, return FALSE);

    if (count == 0)
        count = 1;

    if (!event->rd_kafka_headers) {
        event->rd_kafka_headers = rd_kafka_headers_new(count);
        __n_assert(event->rd_kafka_headers, return FALSE);
    } else {
        n_log(LOG_ERR, "event headers already allocated for event %p", event);
        return FALSE;
    }
    return TRUE;
}

/**
 *@brief add a header entry to an event. headers array must have been allocated before
 *@param event target event
 *@param key string of the key
 *@param key_length size of the key string
 *@param value string of the value
 *@param value_length size of the value string
 *@return TRUE or FALSE
 */
int n_kafka_add_header_ex(N_KAFKA_EVENT* event, char* key, size_t key_length, char* value, size_t value_length) {
    __n_assert(event, return FALSE);
    __n_assert(event->rd_kafka_headers, return FALSE);
    __n_assert(key, return FALSE);
    __n_assert(value, return FALSE);

    if (key_length < 1 || key_length > SSIZE_MAX) {
        n_log(LOG_ERR, "Invalid key length (%zu) for header in event %p", key_length, event);
        return FALSE;
    }

    if (value_length < 1 || value_length > SSIZE_MAX) {
        n_log(LOG_ERR, "Invalid value length (%zu) for key '%s' in event %p", value_length, key, event);
        return FALSE;
    }

    rd_kafka_resp_err_t err = rd_kafka_header_add(event->rd_kafka_headers, key, (ssize_t)key_length, value, (ssize_t)value_length);

    if (err) {
        n_log(LOG_ERR, "Failed to add header [%s:%zu=%s:%zu] to event %p: %s",
              key, key_length, value, value_length, event, rd_kafka_err2str(err));
        return FALSE;
    }

    return TRUE;
}

/**
 *@brief add a header entry to an event. headers array must have been allocated before
 *@param event target event
 *@param key N_STR *string of the key
 *@param value N_STR *string of the value
 *@return TRUE or FALSE
 */
int n_kafka_add_header(N_KAFKA_EVENT* event, N_STR* key, N_STR* value) {
    __n_assert(event, return FALSE);
    __n_assert(key, return FALSE);
    __n_assert(key->data, return FALSE);
    __n_assert(value, return FALSE);
    __n_assert(value->data, return FALSE);

    return n_kafka_add_header_ex(event, key->data, key->written, value->data, value->written);
}

/**
 * @brief put an event in the events_to_send list
 * @param kafka the producer N_KAFKA handle to use
 * @param event event to send. Will be owned by N_KAFKA *kafka handle once done
 * @return TRUE or FALSE
 */
int n_kafka_produce(N_KAFKA* kafka, N_KAFKA_EVENT* event) {
    __n_assert(kafka, return FALSE);
    __n_assert(event, return FALSE);

    event->parent_table = kafka;

    write_lock(kafka->rwlock);
    kafka->nb_queued++;
    list_push(kafka->events_to_send, event, &n_kafka_event_destroy_ptr);
    unlock(kafka->rwlock);

    // Success to *enqueue* event
    n_log(LOG_DEBUG, "successfully enqueued event %p in producer %p waitlist, topic: %s", event, kafka->rd_kafka_handle, kafka->topic);

    return TRUE;
} /* n_kafka_produce */

/**
 * @brief produce an event on a N_KAFKA *kafka handle
 * @param kafka the producer handle to use
 * @param event event to send
 * @return TRUE or FALSE
 */
int n_kafka_produce_ex(N_KAFKA* kafka, N_KAFKA_EVENT* event) {
    rd_kafka_resp_err_t err = 0;

    char* event_string = NULL;
    size_t event_length = 0;

    event->parent_table = kafka;

    event_string = event->event_string->data;
    event_length = event->event_string->length;

    if (event->rd_kafka_headers) {
        rd_kafka_headers_t* hdrs_copy;
        hdrs_copy = rd_kafka_headers_copy(event->rd_kafka_headers);

        err = rd_kafka_producev(
            kafka->rd_kafka_handle, RD_KAFKA_V_RKT(kafka->rd_kafka_topic),
            RD_KAFKA_V_PARTITION(RD_KAFKA_PARTITION_UA),
            RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
            RD_KAFKA_V_VALUE(event_string, event_length),
            RD_KAFKA_V_HEADERS(hdrs_copy),
            RD_KAFKA_V_OPAQUE((void*)event),
            RD_KAFKA_V_END);

        if (err) {
            rd_kafka_headers_destroy(hdrs_copy);
            event->status = N_KAFKA_EVENT_ERROR;
            n_log(LOG_ERR, "failed to produce event: %p with headers %p, producer: %p, topic: %s, error: %s", event, event->rd_kafka_headers, kafka->rd_kafka_handle, kafka->topic, rd_kafka_err2str(err));
            return FALSE;
        }
    } else {
        if (rd_kafka_produce(kafka->rd_kafka_topic, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY, event_string, event_length, NULL, 0, event) == -1) {
            int error = errno;
            event->status = N_KAFKA_EVENT_ERROR;
            n_log(LOG_ERR, "failed to produce event: %p, producer: %p, topic: %s, error: %s", event, kafka->rd_kafka_handle, kafka->topic, strerror(error));
            return FALSE;
        }
    }
    // Success to *enqueue* event
    n_log(LOG_DEBUG, "successfully enqueued event %p in local producer %p : %s", event, kafka->rd_kafka_handle, kafka->topic);

    event->status = N_KAFKA_EVENT_WAITING_ACK;

    return TRUE;
} /* n_kafka_produce_ex */

/**
 *@brief make a new event from a char *string
 *@param string source string
 *@param written lenght of the string
 *@param schema_id the schema id to use. Pass -1 for non avro formatted events
 *@return a new N_KAFKA *event or NULL
 */
N_KAFKA_EVENT* n_kafka_new_event_from_char(char* string, size_t written, int schema_id) {
    __n_assert(string, return NULL);

    size_t offset = 0;
    if (schema_id != -1)
        offset = 5;

    N_KAFKA_EVENT* event = n_kafka_new_event(schema_id);
    __n_assert(event, return NULL);

    Malloc(event->event_string, N_STR, 1);
    __n_assert(event->event_string, free(event); return NULL);

    // allocate the size of the event + (the size of the schema id + magic byte) + one ending \0
    size_t length = written + offset;
    Malloc(event->event_string->data, char, length);
    __n_assert(event->event_string, free(event->event_string); free(event); return NULL);
    event->event_string->length = length;

    // copy incomming body
    memcpy(event->event_string->data + offset, string, written);
    event->event_string->written = written + offset;

    if (schema_id != -1)
        n_kafka_put_schema_in_nstr(event->event_string, schema_id);

    // set status and schema id
    event->status = N_KAFKA_EVENT_QUEUED;

    return event;
} /* n_kafka_new_event_from_char */

/**
 *@brief make a new event from a N_STR *string
 *@param string source string
 *@param schema_id the schema id to use. Pass -1 for non avro formatted events
 *@return a new N_KAFKA *event or NULL
 */
N_KAFKA_EVENT* n_kafka_new_event_from_string(N_STR* string, int schema_id) {
    __n_assert(string, return NULL);
    __n_assert(string->data, return NULL);

    N_KAFKA_EVENT* event = n_kafka_new_event_from_char(string->data, string->written, schema_id);

    return event;
} /* n_kafka_new_event_from_string */

/**
 *@brief make a new event from a N_STR *string
 *@param filename source file path and filename
 *@param schema_id the schema id to use. Pass -1 for non avro formatted events
 *@return a new N_KAFKA *event or NULL
 */
N_KAFKA_EVENT* n_kafka_new_event_from_file(char* filename, int schema_id) {
    __n_assert(filename, return NULL);

    N_STR* from = file_to_nstr(filename);
    __n_assert(from, return NULL);

    return n_kafka_new_event_from_string(from, schema_id);
} /* n_kafka_new_event_from_file */

/**
 *@brief festroy a kafka event
 *@param event_ptr void pointer to target
 */
void n_kafka_event_destroy_ptr(void* event_ptr) {
    __n_assert(event_ptr, return);
    N_KAFKA_EVENT* event = (N_KAFKA_EVENT*)event_ptr;
    __n_assert(event, return);

    if (event->event_string)
        free_nstr(&event->event_string);

    if (event->event_files_to_delete)
        free_nstr(&event->event_files_to_delete);

    FreeNoLog(event->from_topic);

    if (event->rd_kafka_headers)
        rd_kafka_headers_destroy(event->rd_kafka_headers);

    if (event->received_headers)
        list_destroy(&event->received_headers);

    free(event);
    return;
} /* n_kafka_event_destroy_ptr */

/**
 *@brief destroy a kafka event and set it's pointer to NULL
 *@param event event to delete
 *@return TRUE or FALSE
 */
int n_kafka_event_destroy(N_KAFKA_EVENT** event) {
    __n_assert(event && (*event), return FALSE);
    n_kafka_event_destroy_ptr((*event));
    (*event) = NULL;
    return TRUE;
} /* n_kafka_event_destroy */

/**
 *@brief Poll kafka handle in producer or consumer mode
 *@param kafka kafka handle to use
 *@return TRUE or FALSE
 */
int n_kafka_poll(N_KAFKA* kafka) {
    __n_assert(kafka, return FALSE);
    
    int event_consumption_enabled = TRUE ;
    int event_production_enabled = TRUE ;

    read_lock(kafka->rwlock);
        event_consumption_enabled = kafka -> event_consumption_enabled ; 
        event_production_enabled = kafka -> event_production_enabled ; 
    unlock(kafka->rwlock);

    // wait poll interval msecs for kafka response
    if (kafka->mode == RD_KAFKA_PRODUCER) {
        if( event_production_enabled ) {
            int nb_events = rd_kafka_poll(kafka->rd_kafka_handle, kafka->poll_interval);
            (void)nb_events;
            write_lock(kafka->rwlock);
            // check events status in event table
            LIST_NODE* node = kafka->events_to_send->start;
            while (node) {
                N_KAFKA_EVENT* event = (N_KAFKA_EVENT*)node->ptr;
                if (event->status == N_KAFKA_EVENT_OK) {
                    kafka->nb_waiting--;
                    n_log(LOG_DEBUG, "removing event OK %p", event);
                    LIST_NODE* node_to_kill = node;
                    node = node->next;
                    N_KAFKA_EVENT* event_to_kill = remove_list_node(kafka->events_to_send, node_to_kill, N_KAFKA_EVENT);
                    n_kafka_event_destroy(&event_to_kill);
                    continue;
                } else if (event->status == N_KAFKA_EVENT_QUEUED) {
                    if (n_kafka_produce_ex(kafka, event) == FALSE) {
                        kafka->nb_error++;
                    } else {
                        kafka->nb_waiting++;
                        kafka->nb_queued--;
                    }
                } else if (event->status == N_KAFKA_EVENT_ERROR) {
                    kafka->nb_error++;
                    // TODO :
                    // Re try sending errored events after error_timeout is done
                    //
                }
                node = node->next;
            }
            unlock(kafka->rwlock);
        } else {
            usleep(1000 * kafka->poll_interval);  // production is disabled, sleep instead of producing
        }
    } else if (kafka->mode == RD_KAFKA_CONSUMER) {
        if( event_consumption_enabled ) {
            rd_kafka_message_t* rkm = NULL;
            while ((rkm = rd_kafka_consumer_poll(kafka->rd_kafka_handle, kafka->poll_interval))) {
                read_lock(kafka->rwlock);
                event_consumption_enabled = kafka -> event_consumption_enabled ; 
                unlock(kafka->rwlock);
                if( event_consumption_enabled == FALSE ) {
                    return TRUE ;
                }
                if (rkm->err) {
                    /* Consumer errors are generally to be considered
                     * informational as the consumer will automatically
                     * try to recover from all types of errors. */
                    n_log(LOG_ERR, "consumer: %s", rd_kafka_message_errstr(rkm));
                    usleep(1000 * kafka->poll_interval);
                    return FALSE ;
                }
                // Reminder of rkm contents
                n_log(LOG_DEBUG, "Message on %s [%" PRId32 "] at offset %" PRId64 " (leader epoch %" PRId32 ")", rd_kafka_topic_name(rkm->rkt), rkm->partition, rkm->offset, rd_kafka_message_leader_epoch(rkm));

                // Print the message key
                if (rkm->key && rkm->key_len > 0)
                    n_log(LOG_DEBUG, "Key: %.*s", (int)rkm->key_len, (const char*)rkm->key);
                else if (rkm->key)
                    n_log(LOG_DEBUG, "Key: (%d bytes)", (int)rkm->key_len);

                if (rkm->payload && rkm->len > 0) {
                    write_lock(kafka->rwlock);
                    // make a copy of the event for further processing
                    N_KAFKA_EVENT* event = NULL;
                    event = n_kafka_new_event_from_char(rkm->payload, rkm->len, -1);  // no schema id because we want a full raw copy here
                    event->parent_table = kafka;
                    // test if there are headers, save them
                    rd_kafka_headers_t* hdrs = NULL;
                    if (!rd_kafka_message_headers(rkm, &hdrs)) {
                        size_t idx = 0;
                        const char* name = NULL;
                        const void* val = NULL;
                        size_t size = 0;
                        event->received_headers = new_generic_list(MAX_LIST_ITEMS);
                        while (!rd_kafka_header_get_all(hdrs, idx, &name, &val, &size)) {
                            N_STR* header_entry = NULL;
                            nstrprintf(header_entry, "%s=%s", _str(name), _str((char*)val));
                            list_push(event->received_headers, header_entry, &free_nstr_ptr);
                            idx++;
                        }
                    }
                    // save originating topic (can help sorting if there are multiples one)
                    event->from_topic = strdup(rd_kafka_topic_name(rkm->rkt));
                    if (kafka->schema_id != -1)
                        event->schema_id = n_kafka_get_schema_from_nstr(event->event_string);
                    list_push(kafka->received_events, event, &n_kafka_event_destroy_ptr);
                    n_log(LOG_DEBUG, "Consumer received event of (%d bytes) from topic %s", (int)rkm->len, event->from_topic);
                    unlock(kafka->rwlock);
                }
                rd_kafka_message_destroy(rkm);
            }
        } else {
            usleep(1000 * kafka->poll_interval);  // consumption is disabled, sleep instead of consuming
        }
    }
    // n_log( LOG_DEBUG , "kafka poll for handle %p returned %d elements" , kafka -> rd_kafka_handle , nb_events );
    return TRUE;
} /* n_kafka_poll */

/**
 * @brief kafka produce or consume polling thread function
 * @param ptr (void *)kafka handle
 * @return NULL
 */
void* n_kafka_polling_thread(void* ptr) {
    N_KAFKA* kafka = (N_KAFKA*)ptr;

    int status = 1;

    N_TIME chrono;

    if (kafka->mode == RD_KAFKA_PRODUCER)
        n_log(LOG_DEBUG, "starting polling thread for kafka handler %p mode PRODUCER (%d) topic %s", kafka->rd_kafka_handle, RD_KAFKA_PRODUCER, kafka->topic);
    if (kafka->mode == RD_KAFKA_CONSUMER) {
        char* topiclist = join(kafka->topics, ",");
        n_log(LOG_DEBUG, "starting polling thread for kafka handler %p mode CONSUMER (%d) topic %s", kafka->rd_kafka_handle, RD_KAFKA_CONSUMER, _str(topiclist));
        FreeNoLog(topiclist);
    }

    start_HiTimer(&chrono);

    int64_t remaining_time = kafka->poll_timeout * 1000;
    while (status == 1) {
        if (n_kafka_poll(kafka) == FALSE) {
            if (kafka->mode == RD_KAFKA_PRODUCER) {
                n_log(LOG_ERR, "failed to poll kafka producer handle %p with topic %s", kafka->rd_kafka_handle, rd_kafka_topic_name(kafka->rd_kafka_topic));
            } else if (kafka->topics) {
                char* topiclist = join(kafka->topics, ",");
                n_log(LOG_ERR, "failed to poll kafka consumer handle %p with topic %s", kafka->rd_kafka_handle, _str(topiclist));
                FreeNoLog(topiclist);
            }
        }

        read_lock(kafka->rwlock);
        status = kafka->polling_thread_status;
        unlock(kafka->rwlock);

        if (status == 2)
            break;

        int64_t elapsed_time = get_usec(&chrono);
        if (kafka->poll_timeout != -1) {
            remaining_time -= elapsed_time;
            if (remaining_time < 0) {
                if (kafka->mode == RD_KAFKA_PRODUCER) {
                    n_log(LOG_DEBUG, "timeouted on kafka handle %p", kafka->rd_kafka_handle);
                } else if (kafka->mode == RD_KAFKA_CONSUMER) {
                    n_log(LOG_DEBUG, "timeouted on kafka handle %p", kafka->rd_kafka_handle);
                }
                remaining_time = 0;
                status = 2;
                break;
            }
        }
        // n_log( LOG_DEBUG , "remaining time: %d on kafka handle %p" , remaining_time , kafka -> rd_kafka_handle );
    }

    write_lock(kafka->rwlock);
    kafka->polling_thread_status = 0;
    unlock(kafka->rwlock);

    n_log(LOG_DEBUG, "exiting polling thread for kafka handler %p mode %s", kafka->rd_kafka_handle, (kafka->mode == RD_KAFKA_PRODUCER) ? "PRODUCER" : "CONSUMER");
    pthread_exit(NULL);
    return NULL;
} /* n_kafka_polling_thread */

/**
 * @brief start the polling thread of a kafka handle
 * @param kafka kafka handle to use
 * @return TRUE or FALSE
 */
int n_kafka_start_polling_thread(N_KAFKA* kafka) {
    __n_assert(kafka, return FALSE);

    read_lock(kafka->rwlock);
    int status = kafka->polling_thread_status;
    unlock(kafka->rwlock);

    if (status != 0) {
        n_log(LOG_ERR, "kafka polling thread already started for handle %p", kafka);
        return FALSE;
    }

    write_lock(kafka->rwlock);
    kafka->polling_thread_status = 1;

    if (pthread_create(&kafka->polling_thread, NULL, n_kafka_polling_thread, (void*)kafka) != 0) {
        n_log(LOG_ERR, "unable to create polling_thread for kafka handle %p", kafka);
        unlock(kafka->rwlock);
        return FALSE;
    }
    unlock(kafka->rwlock);

    n_log(LOG_DEBUG, "pthread_create sucess for kafka handle %p->%p", kafka, kafka->rd_kafka_handle);

    return TRUE;
} /* n_kafka_start_polling_thread */

/**
 * @brief stop the polling thread of a kafka handle
 * @param kafka target kafka handle
 * @return TRUE or FALSE
 */
int n_kafka_stop_polling_thread(N_KAFKA* kafka) {
    __n_assert(kafka, return FALSE);

    read_lock(kafka->rwlock);
    int polling_thread_status = kafka->polling_thread_status;
    unlock(kafka->rwlock);

    if (polling_thread_status == 0) {
        n_log(LOG_DEBUG, "kafka polling thread already stopped for handle %p", kafka);
        return FALSE;
    }
    if (polling_thread_status == 2) {
        n_log(LOG_DEBUG, "kafka polling ask for stop thread already done for handle %p", kafka);
        return FALSE;
    }

    write_lock(kafka->rwlock);
    kafka->polling_thread_status = 2;
    unlock(kafka->rwlock);

    pthread_join(kafka->polling_thread, NULL);

    return TRUE;
} /* n_kafka_stop_polling_thread */

/**
 * @brief enable event consumption
 * @param state TRUE or FALSE
 * @return TRUE or FALSE
 */
int n_kafka_enable_event_consumption( N_KAFKA* kafka) {
    __n_assert(kafka, return FALSE);
    write_lock(kafka->rwlock);
        kafka -> event_consumption_enabled = TRUE ; 
    unlock(kafka->rwlock);
    return TRUE ;
} /* n_kafka_set_consumer_event_polling */

/**
 * @brief disable event consumption
 * @param state TRUE or FALSE
 * @return TRUE or FALSE
 */
int n_kafka_disabled_event_consumption( N_KAFKA* kafka) {
    __n_assert(kafka, return FALSE);
    write_lock(kafka->rwlock);
        kafka -> event_consumption_enabled = FALSE ; 
    unlock(kafka->rwlock);
    return TRUE ;
} /* n_kafka_set_consumer_event_polling */

/**
 * @brief enable event production
 * @param state TRUE or FALSE
 * @return TRUE or FALSE
 */
int n_kafka_enable_event_production( N_KAFKA* kafka) {
    __n_assert(kafka, return FALSE);
    write_lock(kafka->rwlock);
        kafka -> event_production_enabled = TRUE ; 
    unlock(kafka->rwlock);
    return TRUE ;
} /* n_kafka_set_consumer_event_polling */

/**
 * @brief disable event production
 * @param state TRUE or FALSE
 * @return TRUE or FALSE
 */
int n_kafka_disabled_event_production( N_KAFKA* kafka) {
    __n_assert(kafka, return FALSE);
    write_lock(kafka->rwlock);
        kafka -> event_production_enabled = FALSE ; 
    unlock(kafka->rwlock);
    return TRUE ;
} /* n_kafka_set_consumer_event_polling */

/**
 * @brief dump unprocessed/unset events
 * @param kafka kafka handle to use
 * @param directory the directory in which to dump the events
 * @return TRUE or FALSE
 */
int n_kafka_dump_unprocessed(N_KAFKA* kafka, char* directory) {
    __n_assert(kafka, return FALSE);
    __n_assert(directory, return FALSE);

    int status = 0;
    size_t nb_todump = 0;
    read_lock(kafka->rwlock);
    status = kafka->polling_thread_status;
    nb_todump = kafka->nb_queued + kafka->nb_waiting + kafka->nb_error;
    if (status != 0) {
        n_log(LOG_ERR, "kafka handle %p thread polling func is still running, aborting dump", kafka);
        unlock(kafka->rwlock);
        return FALSE;
    }
    if (nb_todump == 0) {
        n_log(LOG_DEBUG, "kafka handle %p: nothing to dump, all events processed correctly", kafka);
        unlock(kafka->rwlock);
        return TRUE;
    }

    N_STR* dumpstr = new_nstr(0);
    list_foreach(node, kafka->events_to_send) {
        N_KAFKA_EVENT* event = node->ptr;
        if (event->status != N_KAFKA_EVENT_OK) {
            size_t offset = 0;
            if (event->schema_id != -1)
                offset = 5;

            N_STR* filename = NULL;
            nstrprintf(filename, "%s/%s+%p", directory, kafka->topic, event);
            n_log(LOG_DEBUG, "Dumping unprocessed events to %s", _nstr(filename));
            // dump event here
            dumpstr->data = event->event_string->data + offset;
            dumpstr->written = event->event_string->written - offset;
            dumpstr->length = event->event_string->length;
            nstr_to_file(dumpstr, _nstr(filename));
            free_nstr(&filename);
        }
    }
    unlock(kafka->rwlock);
    dumpstr->data = NULL;
    free_nstr(&dumpstr);
    return TRUE;
} /* n_kafka_dump_unprocessed */

/**
 * @brief load unprocessed/unset events
 * @param kafka kafka handle to use
 * @param directory the directory from which to load the events
 * @return TRUE or FALSE
 */
int n_kafka_load_unprocessed(N_KAFKA* kafka, char* directory) {
    __n_assert(kafka, return FALSE);
    __n_assert(directory, return FALSE);

    write_lock(kafka->rwlock);
    // TODO:load events from filename
    // TODO: use base64decode( filename ) split( result , '+' ) to get brokersname+topic to check against what's saved with the event and what's inside kafka's handle conf
    unlock(kafka->rwlock);

    return TRUE;
} /* n_kafka_load_unprocessed */

/**
 * @brief get a received event from the N_KAFKA kafka handle
 * @param kafka kafka handle to use
 * @return a received event ror NULL
 */
N_KAFKA_EVENT* n_kafka_get_event(N_KAFKA* kafka) {
    __n_assert(kafka, return NULL);

    N_KAFKA_EVENT* event = NULL;

    write_lock(kafka->rwlock);
    if (kafka->received_events->start)
        event = remove_list_node(kafka->received_events, kafka->received_events->start, N_KAFKA_EVENT);
    unlock(kafka->rwlock);

    return event;
} /* n_kafka_get_event */

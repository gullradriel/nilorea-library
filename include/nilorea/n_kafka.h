/**\file n_kafka.h
 * kafka generic produce and consume event header
 *\author Castagnier Mickael
 *\version 1.0
 *\date 23/11/2022
 */

#ifndef __N_KAFKA
#define __N_KAFKA

#ifdef __cplusplus
extern "C" {
#endif

/**\defgroup N_KAFKA KAFKA: generic event producer and consumer functions
  \addtogroup N_KAFKA
  @{
  */

#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_network.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>

#include "rdkafka.h"

/*! state of a queued event */
#define N_KAFKA_EVENT_QUEUED 0
/*! state of a sent event waiting for acknowledgement */
#define N_KAFKA_EVENT_WAITING_ACK 1
/*! state of an errored event */
#define N_KAFKA_EVENT_ERROR 2
/*! state of an OK event */
#define N_KAFKA_EVENT_OK 4
/*! state of a freshly created event */
#define N_KAFKA_EVENT_CREATED 5

/*! structure of a KAFKA message */
typedef struct N_KAFKA_EVENT {
    /*! string containing the topic id + payload */
    N_STR* event_string;
    /*! string containing the original event source file name if it is to be deleted when event is produced. List separated with ',' else it's NULL */
    N_STR* event_files_to_delete;
    /*! in case of received event, else NULL */
    char* from_topic;
    /*! state of the event: N_KAFKA_EVENT_CREATED ,N_KAFKA_EVENT_QUEUED , N_KAFKA_EVENT_WAITING_ACK , N_KAFKA_EVENT_ERROR , N_KAFKA_EVENT_OK  */
    unsigned int status;
    /*! kafka produce event headers structure handle */
    rd_kafka_headers_t* rd_kafka_headers;
    /*! kafka consume event headers structure handle */
    LIST* received_headers;
    /*! kafka schema_id */
    int schema_id;
    /*! access lock */
    struct N_KAFKA* parent_table;
} N_KAFKA_EVENT;

/*! structure of a KAFKA consumer or producer handle */
typedef struct N_KAFKA {
    /*! list of N_KAFKA_EVENT to send */
    LIST* events_to_send;
    /*! list of received N_KAFKA_EVENT */
    LIST* received_events;
    /*! consumer group id */
    char* groupid;
    /* list of topics to subscribe to */
    char** topics;
    /* subscribed topics */
    rd_kafka_topic_partition_list_t* subscription;
    /*! kafka structure handle */
    rd_kafka_conf_t* rd_kafka_conf;
    /*! kafka handle (producer or consumer) */
    rd_kafka_t* rd_kafka_handle;
    /*! kafka topic string */
    char* topic;
    /*! kafka bootstrap servers string */
    char* bootstrap_servers;
    /*! kafka topic handle */
    rd_kafka_topic_t* rd_kafka_topic;
    /*! kafka error string holder */
    N_STR* errstr;
    /*! kafka schema id in network order */
    int schema_id;
    /*! access lock */
    pthread_rwlock_t rwlock;
    /*! kafka handle mode: RD_KAFKA_CONSUMER or RD_KAFKA_PRODUCER */
    int mode;
    /*! kafka json configuration holder */
    cJSON* configuration;
    /*! poll timeout in usecs */
    int64_t poll_timeout;
    /*! poll interval in usecs */
    int64_t poll_interval;
    /*! pooling thread status, 0 => off , 1 => on , 2 => wants to stop, will be turned out to 0 by exiting pooling thread */
    int pooling_thread_status;
    /*! pooling thread id */
    pthread_t pooling_thread;
    /* number of waiting events in the producer waiting list */
    size_t nb_queued;
    /* number of events waiting for an ack in the waiting list */
    size_t nb_waiting;
    /* number of errored events */
    size_t nb_error;
} N_KAFKA;

int32_t n_kafka_get_schema_from_char(char* string);
int32_t n_kafka_get_schema_from_nstr(N_STR* string);
int n_kafka_put_schema_in_char(char* string, int schema_id);
int n_kafka_put_schema_in_nstr(N_STR* string, int schema_id);

int n_kafka_get_status(N_KAFKA* kafka, int* nb_queued, int* nb_waiting, int* nb_error);
void n_kafka_delete(N_KAFKA* kafka);
N_KAFKA* n_kafka_new(int64_t poll_timeout, int64_t poll_interval, size_t errstr_len);
N_KAFKA* n_kafka_load_config(char* config_file, int mode);

int n_kafka_new_headers(N_KAFKA_EVENT* event, size_t count);
int n_kafka_add_header_ex(N_KAFKA_EVENT* event, char* key, size_t key_length, char* value, size_t value_length);
int n_kafka_add_header(N_KAFKA_EVENT* event, N_STR* key, N_STR* value);

int n_kafka_produce(N_KAFKA* kafka, N_KAFKA_EVENT* event);

N_KAFKA_EVENT* n_kafka_new_event(int schema_id);
N_KAFKA_EVENT* n_kafka_new_event_from_char(char* string, size_t written, int schema_id);
N_KAFKA_EVENT* n_kafka_new_event_from_string(N_STR* string, int schema_id);
N_KAFKA_EVENT* n_kafka_new_event_from_file(char* filename, int schema_id);
void n_kafka_event_destroy_ptr(void* event);
int n_kafka_event_destroy(N_KAFKA_EVENT** event);

int n_kafka_poll(N_KAFKA* kafka);
int n_kafka_start_pooling_thread(N_KAFKA* kafka);
int n_kafka_stop_pooling_thread(N_KAFKA* kafka);

int n_kafka_dump_unprocessed(N_KAFKA* kafka, char* directory);
int n_kafka_load_unprocessed(N_KAFKA* kafka, char* directory);

N_KAFKA_EVENT* n_kafka_get_event(N_KAFKA* kafka);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif  // header guard

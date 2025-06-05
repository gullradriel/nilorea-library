#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <libgen.h>
#include <errno.h>

#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_kafka.h"
#include "cJSON.h"

#include "rdkafka.h"

#define OK 0
#define ERROR -1
#define NB_TEST_EVENTS 10

char *config_file = NULL,
     *event_string = NULL,
     *event_file = NULL,
     *event_log_file = NULL,
     *log_prefix = NULL;

int log_level = LOG_ERR, /* default log level */
    getoptret = 0,       /* getopt return value */
    KAFKA_MODE = -1,
    PRODUCER_MODE = 0,
    run = 1;

// help func
void usage(void) {
    // TODO: add -F unprocessed event file
    fprintf(stderr,
            "Syntax is: ex_kafka -v -c config_file [-s event or -f eventfile] -o event_log_file -V LOGLEVEL\n"
            "   -v version: print version and exit\n"
            "   -c config_file: [required] Kproducer config file\n"
            "   -s : string of the event to send\n"
            "   -f : file containing the event to send\n"
            "   -C : start a consumer (default output received in terminal)"
            "   -P : start a producer and produce event"
            "   -o : optionnal, set a log file instead of default (stderr/stdout)\n"
            "   -p : optionnal, set a log prefix\n"
            "   -V verbosity: specify a log level for console output\n"
            "                 Supported: LOG_ EMERG,ALERT,CRIT,ERR,WARNING,NOTICE,INFO,DEBUG\n");
}

// stop handler
static void stop(int sig) {
    (void)sig;
    run = 0;
}

int main(int argc, char* argv[]) {
    /* Signal handler for clean shutdown */
    signal(SIGINT, stop);

    /* temporary header structure */
    rd_kafka_headers_t* headers = NULL;

    /* Analysing arguments */
    while ((getoptret = getopt(argc, argv, "vhCPH:c:s:f:V:o:p:")) != -1) {
        switch (getoptret) {
            case 'v':
                fprintf(stderr, "     Version compiled on %s at %s\n", __DATE__, __TIME__);
                exit(TRUE);
            case 'h':
                usage();
                exit(0);
                break;
            case 'C':
                if (KAFKA_MODE == -1) {
                    KAFKA_MODE = RD_KAFKA_CONSUMER;
                } else {
                    fprintf(stderr, "-C and -P can not be used at the ame time!");
                    exit(TRUE);
                }
                break;
            case 'P':
                if (KAFKA_MODE == -1) {
                    KAFKA_MODE = RD_KAFKA_PRODUCER;
                } else {
                    fprintf(stderr, "-C and -P can not be used at the ame time!");
                    exit(TRUE);
                }
                break;
            case 'H': {
                char *name = NULL, *val = NULL;
                size_t name_sz = -1;

                name = optarg;
                val = strchr(name, '=');
                if (val) {
                    name_sz = (size_t)(val - name);
                    val++; /* past the '=' */
                }

                if (!headers)
                    headers = rd_kafka_headers_new(16);

                int err = rd_kafka_header_add(headers, name, name_sz, val, -1);
                if (err) {
                    fprintf(stderr,
                            "%% Failed to add header %s: %s\n",
                            name, rd_kafka_err2str(err));
                    exit(1);
                }
            } break;
            case 'c':
                config_file = local_strdup(optarg);
                break;
            case 's':
                Malloc(event_string, char, strlen(optarg) + 1);
                strcpy(event_string, optarg);
                break;
            case 'f':
                event_file = local_strdup(optarg);
                break;
            case 'o':
                event_log_file = local_strdup(optarg);
                break;
            case 'p':
                log_prefix = local_strdup(optarg);
                break;
            case 'V':
                if (!strncmp("LOG_NULL", optarg, 8)) {
                    log_level = LOG_NULL;
                } else if (!strncmp("LOG_NOTICE", optarg, 10)) {
                    log_level = LOG_NOTICE;
                } else if (!strncmp("LOG_INFO", optarg, 8)) {
                    log_level = LOG_INFO;
                } else if (!strncmp("LOG_ERR", optarg, 7)) {
                    log_level = LOG_ERR;
                } else if (!strncmp("LOG_DEBUG", optarg, 9)) {
                    log_level = LOG_DEBUG;
                } else {
                    fprintf(stderr, "%s n'est pas un niveau de log valide.\n", optarg);
                    exit(-1);
                }
                break;
            case '?':
                if (optopt == 'c' || optopt == 's' || optopt == 'f') {
                    fprintf(stderr, "Option -%c need a parameter\n", optopt);
                    exit(FALSE);
                }
                FALL_THROUGH;
            default:
                usage();
                exit(-1);
                break;
        }
    }

    if (KAFKA_MODE == -1) {
        n_log(LOG_ERR, "consumer (-C) or producer (-P) mode is not defined !", log_prefix);
        exit(1);
    }

    if (!log_prefix) {
        log_prefix = strdup("");
    }

    set_log_level(log_level);
    if (event_log_file) {
        int log_file_ret = set_log_file(event_log_file);
        n_log(LOG_DEBUG, "%s log to file: %s , %d  , %p", log_prefix, event_log_file, log_file_ret, get_log_file());
    }

    /* testing parameters */
    if (!config_file) {
        n_log(LOG_ERR, "%s parameter config_file needs to be set !", log_prefix);
        exit(1);
    }

    if (KAFKA_MODE == RD_KAFKA_PRODUCER) {
        if (!event_string && !event_file) {
            n_log(LOG_ERR, "%s one of (event_string|event_file) needs to be set !", log_prefix);
            exit(1);
        }
    }

    if (event_string && event_file) {
        n_log(LOG_ERR, "%s do not define event_string AND event_file, only one needs to be set !", log_prefix);
        exit(1);
    }

    // load kafka config file
    unsigned int exit_code = 0;
    N_KAFKA* kafka_handle = n_kafka_load_config(config_file, KAFKA_MODE);
    __n_assert(kafka_handle, n_log(LOG_ERR, "kafka handke is NULL !!"); exit(1));

    n_kafka_start_pooling_thread(kafka_handle);

    int nb_queued = 0;
    int nb_waiting = 0;
    int nb_error = 0;
    int poll_status = 1;
    N_KAFKA_EVENT* event = NULL;

    if (KAFKA_MODE == RD_KAFKA_PRODUCER) {
        // create a new kafka event from a string or from a file
        if (event_string) {
            event = n_kafka_new_event_from_char(event_string, strlen(event_string), kafka_handle->schema_id);
        }
        if (event_file) {
            event = n_kafka_new_event_from_file(event_file, kafka_handle->schema_id);
        }
        // set headers if any
        if (headers) {
            event->rd_kafka_headers = rd_kafka_headers_copy(headers);
            // clean them if no more needed
            rd_kafka_headers_destroy(headers);
        }
        // produce the event, API is charging itself of destroying it
        if (n_kafka_produce(kafka_handle, event) == FALSE) {
            n_log(LOG_ERR, "n_kafka_produce returned an error for event %p", event);
        } else {
            n_log(LOG_INFO, "n_kafka_produce returned OK for event %p", event);
        }
    }

    // loop on pool
    do {
        poll_status = n_kafka_get_status(kafka_handle, &nb_queued, &nb_waiting, &nb_error);
        n_log(LOG_DEBUG, "polling kafka handle, status: %d, %d in queue, %d waiting for ack, %d on error", poll_status, nb_queued, nb_waiting, nb_error);

        // if we were waiting only for producing elemens we could use a test like this to break out of the loop
        if (KAFKA_MODE == RD_KAFKA_PRODUCER) {
            usleep(30000);
            if (nb_queued == 0 && nb_waiting == 0 && nb_error == 0)
                break;
        } else {
            event = n_kafka_get_event(kafka_handle);
            if (event) {
                if (kafka_handle->schema_id != -1)
                    n_log(LOG_INFO, "received event schema id %d string:\n%s", event->schema_id, _str(event->event_string->data + 4));
                else
                    n_log(LOG_INFO, "received event string:\n%s", event->event_string->data);

                list_foreach(node, event->received_headers) {
                    N_STR* header = (N_STR*)node->ptr;
                    n_log(LOG_INFO, "headers: %s", _nstr(header));
                }
                n_kafka_event_destroy(&event);
            } else {
                usleep(30000);
            }
        }
    } while (run && poll_status > 0);

    n_log(LOG_INFO, "kafka_handle: %d queued, %d waiting ack, %d on error", nb_queued, nb_waiting, nb_error);
    if (nb_error > 0 || nb_waiting > 0) {
        n_log(LOG_ERR, "kafka_handle: %d events are still waiting for ack, and %d are on error !", nb_waiting, nb_error);
        n_kafka_dump_unprocessed(kafka_handle, "DATAS/kafka/unprocessed");
    }

    // log unprocessed events
    list_foreach(node, kafka_handle->received_events) {
        if (node) {
            if (kafka_handle->schema_id != -1)
                n_log(LOG_INFO, "[unprocessed]received event schema id %d string:\n%s", event->schema_id, event->event_string->data + 4);
            else
                n_log(LOG_INFO, "[unprocessed]received event string:\n%s", event->event_string->data);
        }
    }

    // closing kafka handle
    n_kafka_delete(kafka_handle);

    exit(exit_code);
}

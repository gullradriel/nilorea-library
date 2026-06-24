/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 *@example ex_network_mock.c
 *@brief Mock HTTP server example using Nilorea network module
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/03/2026
 */

#include "nilorea/n_str.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_common.h"

#include <getopt.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/*! mock server port */
#define MOCK_PORT 19090

/*! global server pointer for thread */
static N_MOCK_SERVER* g_server = NULL;

/**
 * @brief Request handler: returns JSON for GET /api/test, 404 otherwise.
 * @param req parsed HTTP request
 * @param resp response to fill
 * @param user_data unused
 */
static void on_request(N_HTTP_REQUEST* req, N_HTTP_RESPONSE* resp, void* user_data) {
    (void)user_data;
    n_log(LOG_INFO, "mock: %s %s", req->method, req->path);

    if (strcmp(req->method, "GET") == 0 &&
        strcmp(req->path, "/api/test") == 0) {
        resp->status_code = 200;
        strncpy(resp->content_type, "application/json",
                sizeof(resp->content_type) - 1);
        resp->body = char_to_nstr("{\"status\":\"ok\"}");
    } else {
        resp->status_code = 404;
        strncpy(resp->content_type, "text/plain",
                sizeof(resp->content_type) - 1);
        resp->body = char_to_nstr("Not Found");
    }
}

/**
 * @brief Thread function that runs the server accept loop.
 * @param arg unused
 * @return NULL
 */
static void* server_thread(void* arg) {
    (void)arg;
    n_mock_server_run(g_server);
    return NULL;
}

int main(int argc, char* argv[]) {
    int log_level = LOG_ERR;
    int getoptret = 0;

    while ((getoptret = getopt(argc, argv, "V:h")) != -1) {
        switch (getoptret) {
            case 'V':
                if (strcmp(optarg, "LOG_DEBUG") == 0)
                    log_level = LOG_DEBUG;
                else if (strcmp(optarg, "LOG_INFO") == 0)
                    log_level = LOG_INFO;
                else if (strcmp(optarg, "LOG_NOTICE") == 0)
                    log_level = LOG_NOTICE;
                else if (strcmp(optarg, "LOG_ERR") == 0)
                    log_level = LOG_ERR;
                break;
            case 'h':
            default:
                fprintf(stderr, "Usage: %s [-V LOG_LEVEL]\n", argv[0]);
                return 1;
        }
    }
    set_log_level(log_level);

    /* Start mock server */
    g_server = n_mock_server_start(MOCK_PORT, on_request, NULL);
    if (!g_server) {
        fprintf(stderr, "FAIL: could not start mock server\n");
        return 1;
    }
    printf("CHECK start server ... PASS\n");

    /* Run server in a thread */
    pthread_t thr;
    if (pthread_create(&thr, NULL, server_thread, NULL) != 0) {
        fprintf(stderr, "FAIL: could not create server thread\n");
        n_mock_server_free(&g_server);
        return 1;
    }

    /* Give the server thread time to start accepting */
    usleep(100000); /* 100ms */

    /* Connect and send a GET /api/test request */
    SOCKET sock_fd = INVALID_SOCKET;
    {
        struct addrinfo hints;
        struct addrinfo* res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", MOCK_PORT);
        if (getaddrinfo("127.0.0.1", port_str, &hints, &res) != 0 || !res) {
            fprintf(stderr, "FAIL: getaddrinfo\n");
            n_mock_server_stop(g_server);
            pthread_join(thr, NULL);
            n_mock_server_free(&g_server);
            return 1;
        }
        sock_fd = socket(res->ai_family, res->ai_socktype,
                         res->ai_protocol);
        if (sock_fd == INVALID_SOCKET ||
            connect(sock_fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
            fprintf(stderr, "FAIL: connect\n");
            freeaddrinfo(res);
            n_mock_server_stop(g_server);
            pthread_join(thr, NULL);
            n_mock_server_free(&g_server);
            return 1;
        }
        freeaddrinfo(res);
    }

    /* Send HTTP request */
    const char* http_req =
        "GET /api/test HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";
    ssize_t sent = send(sock_fd, http_req, NETW_BUFLEN_CAST(strlen(http_req)), 0);
    if (sent <= 0) {
        fprintf(stderr, "FAIL: send\n");
        closesocket(sock_fd);
        n_mock_server_stop(g_server);
        pthread_join(thr, NULL);
        n_mock_server_free(&g_server);
        return 1;
    }

    /* Read response */
    char resp_buf[4096];
    memset(resp_buf, 0, sizeof(resp_buf));
    ssize_t total = 0;
    ssize_t nr = 0;
    while ((nr = recv(sock_fd, resp_buf + total,
                      NETW_BUFLEN_CAST(sizeof(resp_buf) - 1 - (size_t)total), 0)) > 0) {
        total += nr;
    }
    closesocket(sock_fd);

    printf("Response:\n%s\n", resp_buf);

    /* Verify response contains expected data */
    int pass = 1;
    if (strstr(resp_buf, "200") == NULL) {
        fprintf(stderr, "FAIL: expected 200 in response\n");
        pass = 0;
    }
    if (strstr(resp_buf, "{\"status\":\"ok\"}") == NULL) {
        fprintf(stderr, "FAIL: expected JSON body in response\n");
        pass = 0;
    }
    printf("CHECK GET /api/test ... %s\n", pass ? "PASS" : "FAIL");

    /* Stop server and clean up */
    n_mock_server_stop(g_server);
    pthread_join(thr, NULL);
    n_mock_server_free(&g_server);

    printf("CHECK cleanup ... PASS\n");
    return pass ? 0 : 1;
}

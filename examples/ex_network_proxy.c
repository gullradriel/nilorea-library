/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 *@example ex_network_proxy.c
 *@brief Demonstrates n_proxy_cfg_parse / n_proxy_cfg_free and netw_adopt_client_fd.
 *@author Castagnier Mickael
 *@version 1.0
 *@date 27/03/2026
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"

#include <stdio.h>
#include <string.h>
#ifndef __windows__
#include <sys/socket.h>
#include <unistd.h>
#endif

int main(int argc, char* argv[]) {
    int errors = 0;

    /* parse log level from -V flag if present */
    int log_level = LOG_INFO;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-V") == 0 && i + 1 < argc) {
            if (strcmp(argv[i + 1], "LOG_DEBUG") == 0)
                log_level = LOG_DEBUG;
            else if (strcmp(argv[i + 1], "LOG_ERR") == 0)
                log_level = LOG_ERR;
            else if (strcmp(argv[i + 1], "LOG_NOTICE") == 0)
                log_level = LOG_NOTICE;
        }
    }
    set_log_level(log_level);

    n_log(LOG_INFO, "ex_network_proxy: proxy URL parser demo");

    /* Test 1: authenticated HTTP proxy */
    {
        N_PROXY_CFG* cfg = n_proxy_cfg_parse("http://user:pass@proxy.corp:3128");
        if (cfg) {
            n_log(LOG_INFO, "  scheme=%s host=%s port=%d user=%s pass=%s",
                  cfg->scheme, cfg->host, cfg->port,
                  cfg->username ? cfg->username : "(null)",
                  cfg->password ? cfg->password : "(null)");
            if (strcmp(cfg->scheme, "http") != 0) errors++;
            if (strcmp(cfg->host, "proxy.corp") != 0) errors++;
            if (cfg->port != 3128) errors++;
            if (!cfg->username || strcmp(cfg->username, "user") != 0) errors++;
            if (!cfg->password || strcmp(cfg->password, "pass") != 0) errors++;
            n_proxy_cfg_free(&cfg);
        } else {
            n_log(LOG_ERR, "  FAIL: parse returned NULL");
            errors++;
        }
    }

    /* Test 2: unauthenticated SOCKS5 proxy */
    {
        N_PROXY_CFG* cfg = n_proxy_cfg_parse("socks5://proxy.corp:1080");
        if (cfg) {
            n_log(LOG_INFO, "  scheme=%s host=%s port=%d user=%s",
                  cfg->scheme, cfg->host, cfg->port,
                  cfg->username ? cfg->username : "(null)");
            if (strcmp(cfg->scheme, "socks5") != 0) errors++;
            if (strcmp(cfg->host, "proxy.corp") != 0) errors++;
            if (cfg->port != 1080) errors++;
            if (cfg->username != NULL) errors++;
            n_proxy_cfg_free(&cfg);
        } else {
            n_log(LOG_ERR, "  FAIL: parse returned NULL");
            errors++;
        }
    }

    /* Test 3: authenticated HTTPS proxy */
    {
        N_PROXY_CFG* cfg = n_proxy_cfg_parse("https://admin:secret@secure-proxy.corp:8443");
        if (cfg) {
            n_log(LOG_INFO, "  scheme=%s host=%s port=%d user=%s pass=%s",
                  cfg->scheme, cfg->host, cfg->port,
                  cfg->username ? cfg->username : "(null)",
                  cfg->password ? cfg->password : "(null)");
            if (strcmp(cfg->scheme, "https") != 0) errors++;
            if (strcmp(cfg->host, "secure-proxy.corp") != 0) errors++;
            if (cfg->port != 8443) errors++;
            if (!cfg->username || strcmp(cfg->username, "admin") != 0) errors++;
            if (!cfg->password || strcmp(cfg->password, "secret") != 0) errors++;
            n_proxy_cfg_free(&cfg);
        } else {
            n_log(LOG_ERR, "  FAIL: parse returned NULL");
            errors++;
        }
    }

    /* Test 4: HTTPS proxy with default port */
    {
        N_PROXY_CFG* cfg = n_proxy_cfg_parse("https://secure-proxy.corp");
        if (cfg) {
            n_log(LOG_INFO, "  scheme=%s host=%s port=%d",
                  cfg->scheme, cfg->host, cfg->port);
            if (strcmp(cfg->scheme, "https") != 0) errors++;
            if (cfg->port != 3128) errors++;
            n_proxy_cfg_free(&cfg);
        } else {
            n_log(LOG_ERR, "  FAIL: parse returned NULL");
            errors++;
        }
    }

    /* Test 5: invalid URL */
    {
        N_PROXY_CFG* cfg = n_proxy_cfg_parse("not-a-url");
        if (cfg) {
            n_log(LOG_ERR, "  FAIL: expected NULL for invalid URL");
            n_proxy_cfg_free(&cfg);
            errors++;
        } else {
            n_log(LOG_INFO, "  invalid URL correctly returned NULL");
        }
    }

    /* Test 6: adopt an already-connected socket fd into a client NETWORK and
     * round-trip raw bytes over its plain send/recv vtable. A socketpair stands
     * in for the fd a proxy CONNECT tunnel would return. POSIX-only: Winsock has
     * no socketpair()/AF_UNIX equivalent. */
#ifndef __windows__
    {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
            n_log(LOG_ERR, "  FAIL: socketpair failed");
            errors++;
        } else {
            NETWORK* netw = NULL;
            if (netw_adopt_client_fd(&netw, (SOCKET)sv[0], "tunnel.target", "443") == TRUE && netw) {
                char rbuf[8];
                /* peer -> NETWORK */
                if (send(sv[1], "PING", 4, 0) != 4) errors++;
                memset(rbuf, 0, sizeof(rbuf));
                if (netw->recv_data(netw, rbuf, 4) != 4 || memcmp(rbuf, "PING", 4) != 0) {
                    n_log(LOG_ERR, "  FAIL: adopted recv mismatch");
                    errors++;
                }
                /* NETWORK -> peer */
                if (netw->send_data(netw, "PONG", 4) != 4) errors++;
                memset(rbuf, 0, sizeof(rbuf));
                if (recv(sv[1], rbuf, 4, 0) != 4 || memcmp(rbuf, "PONG", 4) != 0) {
                    n_log(LOG_ERR, "  FAIL: adopted send mismatch");
                    errors++;
                }
                if (errors == 0)
                    n_log(LOG_INFO, "  adopted fd round-trip OK");
                netw_close(&netw); /* closes sv[0] */
            } else {
                n_log(LOG_ERR, "  FAIL: netw_adopt_client_fd returned FALSE");
                errors++;
                closesocket(sv[0]);
            }
            closesocket(sv[1]);
        }
    }
#endif /* !__windows__ */

    /* Test 7: netw_adopt_client_fd rejects a non-empty target and a bad fd. */
    {
        NETWORK* netw = NULL;
        if (netw_adopt_client_fd(&netw, INVALID_SOCKET, "h", "1") != FALSE) {
            n_log(LOG_ERR, "  FAIL: expected FALSE for INVALID_SOCKET");
            errors++;
        } else {
            n_log(LOG_INFO, "  invalid fd correctly returned FALSE");
        }
    }

    n_log(LOG_INFO, "ex_network_proxy: %s (%d error%s)",
          errors == 0 ? "PASS" : "FAIL", errors, errors == 1 ? "" : "s");
    return errors > 0 ? 1 : 0;
}

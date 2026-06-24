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
 *@example ex_network_proxy.c
 *@brief Demonstrates n_proxy_cfg_parse and n_proxy_cfg_free.
 *@author Castagnier Mickael
 *@version 1.0
 *@date 27/03/2026
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"

#include <stdio.h>
#include <string.h>

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

    n_log(LOG_INFO, "ex_network_proxy: %s (%d error%s)",
          errors == 0 ? "PASS" : "FAIL", errors, errors == 1 ? "" : "s");
    return errors > 0 ? 1 : 0;
}

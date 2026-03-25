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
 *@file ex_avro.c
 *@brief Avro encoding/decoding example: JSON <-> Avro conversion
 *@author Castagnier Mickael
 *@version 1.0
 *@date 11/03/2026
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_avro.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]) {
    set_log_level(LOG_DEBUG);

    if (argc == 5) {
        /* file-based mode: ex_avro <encode|decode> <schema.json> <input> <output> */
        if (strcmp(argv[1], "encode") == 0) {
            n_log(LOG_INFO, "Encoding JSON to Avro: %s + %s -> %s", argv[3], argv[2], argv[4]);
            if (avro_json_to_file(argv[4], argv[2], argv[3]) == TRUE) {
                n_log(LOG_INFO, "Success!");
            } else {
                n_log(LOG_ERR, "Encoding failed!");
                return 1;
            }
        } else if (strcmp(argv[1], "decode") == 0) {
            n_log(LOG_INFO, "Decoding Avro to JSON: %s + %s -> %s", argv[3], argv[2], argv[4]);
            if (avro_file_to_json(argv[3], argv[2], argv[4]) == TRUE) {
                n_log(LOG_INFO, "Success!");
            } else {
                n_log(LOG_ERR, "Decoding failed!");
                return 1;
            }
        } else {
            n_log(LOG_ERR, "Unknown command: %s (use 'encode' or 'decode')", argv[1]);
            return 1;
        }
        return 0;
    }

    /* default: in-memory demo with embedded data */
    n_log(LOG_INFO, "=== Avro In-Memory Demo ===");

    /* define a schema */
    const char* schema_json =
        "{"
        "  \"type\": \"record\","
        "  \"name\": \"User\","
        "  \"namespace\": \"com.example\","
        "  \"fields\": ["
        "    {\"name\": \"name\", \"type\": \"string\"},"
        "    {\"name\": \"age\", \"type\": \"int\"},"
        "    {\"name\": \"email\", \"type\": [\"null\", \"string\"]},"
        "    {\"name\": \"score\", \"type\": \"double\"},"
        "    {\"name\": \"active\", \"type\": \"boolean\"}"
        "  ]"
        "}";

    /* define some JSON records */
    const char* json_data =
        "["
        "  {"
        "    \"name\": \"Alice\","
        "    \"age\": 30,"
        "    \"email\": \"alice@example.com\","
        "    \"score\": 95.5,"
        "    \"active\": true"
        "  },"
        "  {"
        "    \"name\": \"Bob\","
        "    \"age\": 25,"
        "    \"email\": null,"
        "    \"score\": 87.3,"
        "    \"active\": false"
        "  },"
        "  {"
        "    \"name\": \"Charlie\","
        "    \"age\": 35,"
        "    \"email\": \"charlie@example.com\","
        "    \"score\": 92.1,"
        "    \"active\": true"
        "  }"
        "]";

    /* convert to N_STR for the N_STR-based API */
    N_STR* schema_nstr = char_to_nstr(schema_json);
    N_STR* json_nstr = char_to_nstr(json_data);

    /* encode JSON -> Avro using N_STR API */
    n_log(LOG_INFO, "Encoding JSON to Avro (in-memory)...");
    N_STR* avro_nstr = avro_nstr_json_to_avro(schema_nstr, json_nstr);
    if (!avro_nstr) {
        n_log(LOG_ERR, "Encoding failed!");
        free_nstr(&schema_nstr);
        free_nstr(&json_nstr);
        return 1;
    }
    n_log(LOG_INFO, "Avro data size: %zu bytes (from %zu bytes JSON)", avro_nstr->written, json_nstr->written);

    /* decode Avro -> JSON using N_STR API */
    n_log(LOG_INFO, "Decoding Avro to JSON (in-memory)...");
    N_STR* result_json = avro_nstr_avro_to_json(schema_nstr, avro_nstr);
    if (!result_json) {
        n_log(LOG_ERR, "Decoding failed!");
        free_nstr(&schema_nstr);
        free_nstr(&json_nstr);
        free_nstr(&avro_nstr);
        return 1;
    }

    n_log(LOG_INFO, "Decoded JSON:\n%s", result_json->data);

    /* also test the schema parse/serialize round-trip */
    n_log(LOG_INFO, "--- Schema round-trip test ---");
    AVRO_SCHEMA* schema = avro_schema_parse_nstr(schema_nstr);
    if (schema) {
        char* schema_back = avro_schema_to_json(schema);
        if (schema_back) {
            n_log(LOG_INFO, "Schema round-trip: %s", schema_back);
            Free(schema_back);
        }
        avro_schema_free(&schema);
    }

    /* also demo the file-based round-trip */
    n_log(LOG_INFO, "--- File-based round-trip test ---");
    nstr_to_file(schema_nstr, (char*)"/tmp/test_avro_schema.json");
    nstr_to_file(json_nstr, (char*)"/tmp/test_avro_input.json");

    if (avro_json_to_file("/tmp/test_output.avro", "/tmp/test_avro_schema.json", "/tmp/test_avro_input.json") == TRUE) {
        n_log(LOG_INFO, "File encoding succeeded");

        if (avro_file_to_json("/tmp/test_output.avro", "/tmp/test_avro_schema.json", "/tmp/test_avro_output.json") == TRUE) {
            n_log(LOG_INFO, "File decoding succeeded");
            N_STR* output = file_to_nstr((char*)"/tmp/test_avro_output.json");
            if (output) {
                n_log(LOG_INFO, "File round-trip JSON:\n%s", output->data);
                free_nstr(&output);
            }
        }
    }

    /* cleanup */
    free_nstr(&schema_nstr);
    free_nstr(&json_nstr);
    free_nstr(&avro_nstr);
    free_nstr(&result_json);

    n_log(LOG_INFO, "=== Done ===");
    return 0;
}

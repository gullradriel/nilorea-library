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
 *@file n_avro.h
 *@brief Avro binary format encoding/decoding with JSON conversion
 *@author Castagnier Mickael
 *@version 1.0
 *@date 11/03/2026
 */

#ifndef __NILOREA_AVRO__
#define __NILOREA_AVRO__

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup AVRO AVRO: binary format encoding/decoding with JSON conversion
   @addtogroup AVRO
   @{
 */

#include "nilorea/n_common.h"
#include "nilorea/n_str.h"
#include "nilorea/n_list.h"
#include "nilorea/n_hash.h"
#include "nilorea/n_log.h"

#include <cJSON.h>

/*! Avro object container file magic bytes */
#define AVRO_MAGIC "Obj\x01"
/*! Avro magic length */
#define AVRO_MAGIC_LEN 4
/*! Avro sync marker length */
#define AVRO_SYNC_LEN 16

/*! Avro schema type enumeration */
typedef enum AVRO_TYPE {
    AVRO_NULL = 0,
    AVRO_BOOLEAN,
    AVRO_INT,
    AVRO_LONG,
    AVRO_FLOAT,
    AVRO_DOUBLE,
    AVRO_BYTES,
    AVRO_STRING,
    AVRO_RECORD,
    AVRO_ENUM,
    AVRO_ARRAY,
    AVRO_MAP,
    AVRO_UNION,
    AVRO_FIXED
} AVRO_TYPE;

/*! Forward declaration */
typedef struct AVRO_SCHEMA AVRO_SCHEMA;

/*! Avro schema field (for records) */
typedef struct AVRO_FIELD {
    /*! field name */
    char* name;
    /*! field schema */
    AVRO_SCHEMA* schema;
} AVRO_FIELD;

/*! Avro schema definition */
struct AVRO_SCHEMA {
    /*! schema type */
    AVRO_TYPE type;
    /*! name (for record, enum, fixed) */
    char* name;
    /*! namespace (for record, enum) */
    char* namespace;
    /*! fields array (for record) */
    AVRO_FIELD* fields;
    /*! number of fields (for record) */
    size_t nb_fields;
    /*! item schema (for array) */
    AVRO_SCHEMA* items;
    /*! value schema (for map) */
    AVRO_SCHEMA* values;
    /*! union branch schemas */
    AVRO_SCHEMA** union_branches;
    /*! number of union branches */
    size_t nb_branches;
    /*! enum symbols */
    char** symbols;
    /*! number of enum symbols */
    size_t nb_symbols;
    /*! fixed size */
    size_t fixed_size;
};

/*! Avro read cursor for decoding */
typedef struct AVRO_READER {
    /*! data buffer */
    const unsigned char* data;
    /*! total size */
    size_t size;
    /*! current position */
    size_t pos;
} AVRO_READER;

/* ========== Schema functions ========== */

/*! @brief Parse an Avro schema from a JSON string */
AVRO_SCHEMA* avro_schema_parse(const char* json_str);

/*! @brief Parse an Avro schema from a cJSON object */
AVRO_SCHEMA* avro_schema_from_cjson(const cJSON* json);

/*! @brief Free an Avro schema */
void avro_schema_free(AVRO_SCHEMA** schema);

/*! @brief Convert an Avro schema back to JSON string (caller must free) */
char* avro_schema_to_json(const AVRO_SCHEMA* schema);

/* ========== Zig-zag varint encoding ========== */

/*! @brief Encode a 64-bit signed integer as zig-zag varint into N_STR */
int avro_encode_long(N_STR** dest, int64_t value);

/*! @brief Decode a zig-zag varint from reader into a 64-bit signed integer */
int avro_decode_long(AVRO_READER* reader, int64_t* value);

/* ========== Binary encoding (datum -> N_STR) ========== */

/*! @brief Encode a cJSON value as Avro binary according to schema */
int avro_encode_datum(N_STR** dest, const AVRO_SCHEMA* schema, const cJSON* json);

/* ========== Binary decoding (N_STR -> datum) ========== */

/*! @brief Decode an Avro binary datum into cJSON according to schema */
cJSON* avro_decode_datum(AVRO_READER* reader, const AVRO_SCHEMA* schema);

/* ========== Object Container File I/O ========== */

/*! @brief Write an Avro object container file from a JSON file and schema file */
int avro_json_to_file(const char* avro_filename, const char* schema_filename, const char* json_filename);

/*! @brief Read an Avro object container file and produce a JSON file using a schema file */
int avro_file_to_json(const char* avro_filename, const char* schema_filename, const char* json_filename);

/* ========== In-memory N_STR conversions ========== */

/*! @brief Encode a cJSON array of records into Avro container format N_STR */
N_STR* avro_encode_container(const AVRO_SCHEMA* schema, const cJSON* records);

/*! @brief Decode an Avro container format N_STR into a cJSON array of records */
cJSON* avro_decode_container(const AVRO_SCHEMA* schema, const N_STR* avro_data);

/*! @brief Convert JSON N_STR to Avro N_STR using schema N_STR (all in-memory) */
N_STR* avro_nstr_json_to_avro(const N_STR* schema_nstr, const N_STR* json_nstr);

/*! @brief Convert Avro N_STR to JSON N_STR using schema N_STR (all in-memory) */
N_STR* avro_nstr_avro_to_json(const N_STR* schema_nstr, const N_STR* avro_nstr);

/*! @brief Parse schema from N_STR */
AVRO_SCHEMA* avro_schema_parse_nstr(const N_STR* schema_nstr);

/**@}*/

#ifdef __cplusplus
}
#endif
#endif /* __NILOREA_AVRO__ */

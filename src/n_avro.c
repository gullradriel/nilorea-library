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
 *@file n_avro.c
 *@brief Avro binary format encoding/decoding with JSON conversion
 *@author Castagnier Mickael
 *@version 1.0
 *@date 11/03/2026
 */

#include "nilorea/n_avro.h"

#include <string.h>
#include <time.h>

/* ========================================================================== */
/*                          Schema parsing                                     */
/* ========================================================================== */

/*! helper to get AVRO_TYPE from a type name string */
static AVRO_TYPE avro_type_from_string(const char* type_str) {
    if (strcmp(type_str, "null") == 0) return AVRO_NULL;
    if (strcmp(type_str, "boolean") == 0) return AVRO_BOOLEAN;
    if (strcmp(type_str, "int") == 0) return AVRO_INT;
    if (strcmp(type_str, "long") == 0) return AVRO_LONG;
    if (strcmp(type_str, "float") == 0) return AVRO_FLOAT;
    if (strcmp(type_str, "double") == 0) return AVRO_DOUBLE;
    if (strcmp(type_str, "bytes") == 0) return AVRO_BYTES;
    if (strcmp(type_str, "string") == 0) return AVRO_STRING;
    return AVRO_NULL;
}

/*! helper to get type name from AVRO_TYPE */
static const char* avro_type_to_string(AVRO_TYPE type) {
    switch (type) {
        case AVRO_NULL:
            return "null";
        case AVRO_BOOLEAN:
            return "boolean";
        case AVRO_INT:
            return "int";
        case AVRO_LONG:
            return "long";
        case AVRO_FLOAT:
            return "float";
        case AVRO_DOUBLE:
            return "double";
        case AVRO_BYTES:
            return "bytes";
        case AVRO_STRING:
            return "string";
        default:
            return "null";
    }
}

AVRO_SCHEMA* avro_schema_from_cjson(const cJSON* json) {
    __n_assert(json, return NULL);

    AVRO_SCHEMA* schema = NULL;
    Malloc(schema, AVRO_SCHEMA, 1);
    __n_assert(schema, return NULL);

    /* primitive type as string: "null", "boolean", "int", etc. */
    if (cJSON_IsString(json)) {
        schema->type = avro_type_from_string(cJSON_GetStringValue((cJSON*)json));
        return schema;
    }

    /* union: JSON array of schemas */
    if (cJSON_IsArray(json)) {
        schema->type = AVRO_UNION;
        int nb = cJSON_GetArraySize(json);
        schema->nb_branches = (size_t)nb;
        Malloc(schema->union_branches, AVRO_SCHEMA*, (size_t)nb);
        __n_assert(schema->union_branches, Free(schema); return NULL);
        for (int i = 0; i < nb; i++) {
            schema->union_branches[i] = avro_schema_from_cjson(cJSON_GetArrayItem(json, i));
        }
        return schema;
    }

    /* complex type as object */
    if (cJSON_IsObject(json)) {
        const cJSON* type_node = cJSON_GetObjectItemCaseSensitive(json, "type");
        __n_assert(type_node, Free(schema); return NULL);

        const char* type_str = cJSON_GetStringValue(type_node);

        /* type field can itself be a complex type (array or object) */
        if (!type_str) {
            /* nested complex type in the "type" field */
            AVRO_SCHEMA* inner = avro_schema_from_cjson(type_node);
            if (inner) {
                memcpy(schema, inner, sizeof(AVRO_SCHEMA));
                Free(inner);
            }
            /* copy name/namespace from outer object if present */
            const cJSON* name_node = cJSON_GetObjectItemCaseSensitive(json, "name");
            if (name_node && cJSON_IsString(name_node)) {
                schema->name = local_strdup(cJSON_GetStringValue(name_node));
            }
            return schema;
        }

        if (strcmp(type_str, "record") == 0) {
            schema->type = AVRO_RECORD;

            const cJSON* name_node = cJSON_GetObjectItemCaseSensitive(json, "name");
            if (name_node && cJSON_IsString(name_node)) {
                schema->name = local_strdup(cJSON_GetStringValue(name_node));
            }

            const cJSON* ns_node = cJSON_GetObjectItemCaseSensitive(json, "namespace");
            if (ns_node && cJSON_IsString(ns_node)) {
                schema->namespace = local_strdup(cJSON_GetStringValue(ns_node));
            }

            const cJSON* fields_node = cJSON_GetObjectItemCaseSensitive(json, "fields");
            if (fields_node && cJSON_IsArray(fields_node)) {
                int nb = cJSON_GetArraySize(fields_node);
                schema->nb_fields = (size_t)nb;
                Malloc(schema->fields, AVRO_FIELD, (size_t)nb);
                __n_assert(schema->fields, avro_schema_free(&schema); return NULL);

                for (int i = 0; i < nb; i++) {
                    const cJSON* field = cJSON_GetArrayItem(fields_node, i);
                    const cJSON* fname = cJSON_GetObjectItemCaseSensitive(field, "name");
                    const cJSON* ftype = cJSON_GetObjectItemCaseSensitive(field, "type");

                    if (fname && cJSON_IsString(fname)) {
                        schema->fields[i].name = local_strdup(cJSON_GetStringValue(fname));
                    }
                    schema->fields[i].schema = avro_schema_from_cjson(ftype);
                }
            }
        } else if (strcmp(type_str, "array") == 0) {
            schema->type = AVRO_ARRAY;
            const cJSON* items_node = cJSON_GetObjectItemCaseSensitive(json, "items");
            schema->items = avro_schema_from_cjson(items_node);
        } else if (strcmp(type_str, "map") == 0) {
            schema->type = AVRO_MAP;
            const cJSON* values_node = cJSON_GetObjectItemCaseSensitive(json, "values");
            schema->values = avro_schema_from_cjson(values_node);
        } else if (strcmp(type_str, "enum") == 0) {
            schema->type = AVRO_ENUM;

            const cJSON* name_node = cJSON_GetObjectItemCaseSensitive(json, "name");
            if (name_node && cJSON_IsString(name_node)) {
                schema->name = local_strdup(cJSON_GetStringValue(name_node));
            }

            const cJSON* ns_node = cJSON_GetObjectItemCaseSensitive(json, "namespace");
            if (ns_node && cJSON_IsString(ns_node)) {
                schema->namespace = local_strdup(cJSON_GetStringValue(ns_node));
            }

            const cJSON* symbols_node = cJSON_GetObjectItemCaseSensitive(json, "symbols");
            if (symbols_node && cJSON_IsArray(symbols_node)) {
                int nb = cJSON_GetArraySize(symbols_node);
                schema->nb_symbols = (size_t)nb;
                Malloc(schema->symbols, char*, (size_t)nb);
                __n_assert(schema->symbols, avro_schema_free(&schema); return NULL);
                for (int i = 0; i < nb; i++) {
                    const cJSON* sym = cJSON_GetArrayItem(symbols_node, i);
                    if (cJSON_IsString(sym)) {
                        schema->symbols[i] = local_strdup(cJSON_GetStringValue(sym));
                    }
                }
            }
        } else if (strcmp(type_str, "fixed") == 0) {
            schema->type = AVRO_FIXED;

            const cJSON* name_node = cJSON_GetObjectItemCaseSensitive(json, "name");
            if (name_node && cJSON_IsString(name_node)) {
                schema->name = local_strdup(cJSON_GetStringValue(name_node));
            }

            const cJSON* size_node = cJSON_GetObjectItemCaseSensitive(json, "size");
            if (size_node && cJSON_IsNumber(size_node)) {
                schema->fixed_size = (size_t)cJSON_GetNumberValue(size_node);
            }
        } else {
            /* primitive type name in object form */
            schema->type = avro_type_from_string(type_str);
        }
        return schema;
    }

    /* fallback */
    schema->type = AVRO_NULL;
    return schema;
}

AVRO_SCHEMA* avro_schema_parse(const char* json_str) {
    __n_assert(json_str, return NULL);

    cJSON* json = cJSON_Parse(json_str);
    __n_assert(json, return NULL);

    AVRO_SCHEMA* schema = avro_schema_from_cjson(json);
    cJSON_Delete(json);
    return schema;
}

void avro_schema_free(AVRO_SCHEMA** schema_ptr) {
    if (!schema_ptr || !*schema_ptr) return;

    AVRO_SCHEMA* schema = *schema_ptr;

    FreeNoLog(schema->name);
    FreeNoLog(schema->namespace);

    if (schema->fields) {
        for (size_t i = 0; i < schema->nb_fields; i++) {
            FreeNoLog(schema->fields[i].name);
            avro_schema_free(&schema->fields[i].schema);
        }
        FreeNoLog(schema->fields);
    }

    avro_schema_free(&schema->items);
    avro_schema_free(&schema->values);

    if (schema->union_branches) {
        for (size_t i = 0; i < schema->nb_branches; i++) {
            avro_schema_free(&schema->union_branches[i]);
        }
        FreeNoLog(schema->union_branches);
    }

    if (schema->symbols) {
        for (size_t i = 0; i < schema->nb_symbols; i++) {
            FreeNoLog(schema->symbols[i]);
        }
        FreeNoLog(schema->symbols);
    }

    FreeNoLog(*schema_ptr);
}

char* avro_schema_to_json(const AVRO_SCHEMA* schema) {
    __n_assert(schema, return NULL);

    cJSON* json = NULL;

    switch (schema->type) {
        case AVRO_NULL:
        case AVRO_BOOLEAN:
        case AVRO_INT:
        case AVRO_LONG:
        case AVRO_FLOAT:
        case AVRO_DOUBLE:
        case AVRO_BYTES:
        case AVRO_STRING:
            /* for simple types used inside complex structures, return just the string */
            json = cJSON_CreateString(avro_type_to_string(schema->type));
            break;

        case AVRO_RECORD: {
            json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "type", "record");
            if (schema->name) {
                cJSON_AddStringToObject(json, "name", schema->name);
            }
            if (schema->namespace) {
                cJSON_AddStringToObject(json, "namespace", schema->namespace);
            }
            cJSON* fields_arr = cJSON_AddArrayToObject(json, "fields");
            for (size_t i = 0; i < schema->nb_fields; i++) {
                cJSON* field = cJSON_CreateObject();
                cJSON_AddStringToObject(field, "name", schema->fields[i].name);
                char* ftype_str = avro_schema_to_json(schema->fields[i].schema);
                if (ftype_str) {
                    cJSON* ftype = cJSON_Parse(ftype_str);
                    cJSON_AddItemToObject(field, "type", ftype);
                    Free(ftype_str);
                }
                cJSON_AddItemToArray(fields_arr, field);
            }
            break;
        }

        case AVRO_ARRAY: {
            json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "type", "array");
            char* items_str = avro_schema_to_json(schema->items);
            if (items_str) {
                cJSON* items = cJSON_Parse(items_str);
                cJSON_AddItemToObject(json, "items", items);
                Free(items_str);
            }
            break;
        }

        case AVRO_MAP: {
            json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "type", "map");
            char* values_str = avro_schema_to_json(schema->values);
            if (values_str) {
                cJSON* values = cJSON_Parse(values_str);
                cJSON_AddItemToObject(json, "values", values);
                Free(values_str);
            }
            break;
        }

        case AVRO_ENUM: {
            json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "type", "enum");
            if (schema->name) {
                cJSON_AddStringToObject(json, "name", schema->name);
            }
            if (schema->namespace) {
                cJSON_AddStringToObject(json, "namespace", schema->namespace);
            }
            cJSON* syms = cJSON_AddArrayToObject(json, "symbols");
            for (size_t i = 0; i < schema->nb_symbols; i++) {
                cJSON_AddItemToArray(syms, cJSON_CreateString(schema->symbols[i]));
            }
            break;
        }

        case AVRO_UNION: {
            json = cJSON_CreateArray();
            for (size_t i = 0; i < schema->nb_branches; i++) {
                char* branch_str = avro_schema_to_json(schema->union_branches[i]);
                if (branch_str) {
                    cJSON* branch = cJSON_Parse(branch_str);
                    cJSON_AddItemToArray(json, branch);
                    Free(branch_str);
                }
            }
            break;
        }

        case AVRO_FIXED: {
            json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "type", "fixed");
            if (schema->name) {
                cJSON_AddStringToObject(json, "name", schema->name);
            }
            cJSON_AddNumberToObject(json, "size", (double)schema->fixed_size);
            break;
        }
    }

    char* result = NULL;
    if (json) {
        result = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);
    }
    return result;
}

/* ========================================================================== */
/*                     Zig-zag varint encoding/decoding                        */
/* ========================================================================== */

int avro_encode_long(N_STR** dest, int64_t value) {
    __n_assert(dest, return FALSE);

    /* zig-zag encode: portable sign-extension without signed shift UB */
    uint64_t n = ((uint64_t)value << 1) ^ -(uint64_t)(value < 0);

    unsigned char buf[10];
    int len = 0;
    while (n > 0x7F) {
        buf[len++] = (unsigned char)((n & 0x7F) | 0x80);
        n >>= 7;
    }
    buf[len++] = (unsigned char)n;

    nstrcat_ex(dest, buf, (NSTRBYTE)len, 1);
    return TRUE;
}

int avro_decode_long(AVRO_READER* reader, int64_t* value) {
    __n_assert(reader, return FALSE);
    __n_assert(value, return FALSE);

    uint64_t n = 0;
    int shift = 0;

    while (reader->pos < reader->size) {
        unsigned char b = reader->data[reader->pos++];
        n |= ((uint64_t)(b & 0x7F)) << shift;
        if ((b & 0x80) == 0) {
            /* zig-zag decode */
            *value = (int64_t)((n >> 1) ^ (~(n & 1) + 1));
            return TRUE;
        }
        shift += 7;
        if (shift >= 70) {
            n_log(LOG_ERR, "varint too long");
            return FALSE;
        }
    }

    n_log(LOG_ERR, "unexpected end of data while reading varint");
    return FALSE;
}

/* ========================================================================== */
/*                         Binary encoding                                     */
/* ========================================================================== */

/*! helper: encode raw bytes with length prefix */
static int avro_encode_bytes_raw(N_STR** dest, const unsigned char* data, size_t len) {
    int ret = avro_encode_long(dest, (int64_t)len);
    if (ret != TRUE) return ret;
    if (len > 0) {
        nstrcat_ex(dest, (void*)data, (NSTRBYTE)len, 1);
    }
    return TRUE;
}

/*! helper: find which union branch matches a cJSON value */
static int avro_find_union_branch(const AVRO_SCHEMA* schema, const cJSON* json) {
    for (size_t i = 0; i < schema->nb_branches; i++) {
        AVRO_SCHEMA* branch = schema->union_branches[i];
        switch (branch->type) {
            case AVRO_NULL:
                if (cJSON_IsNull(json)) return (int)i;
                break;
            case AVRO_BOOLEAN:
                if (cJSON_IsBool(json)) return (int)i;
                break;
            case AVRO_INT:
            case AVRO_LONG:
            case AVRO_FLOAT:
            case AVRO_DOUBLE:
                if (cJSON_IsNumber(json)) return (int)i;
                break;
            case AVRO_STRING:
                if (cJSON_IsString(json)) return (int)i;
                break;
            case AVRO_BYTES:
                if (cJSON_IsString(json)) return (int)i;
                break;
            case AVRO_RECORD:
            case AVRO_MAP:
                if (cJSON_IsObject(json)) return (int)i;
                break;
            case AVRO_ARRAY:
                if (cJSON_IsArray(json)) return (int)i;
                break;
            case AVRO_ENUM:
                if (cJSON_IsString(json)) return (int)i;
                break;
            case AVRO_UNION:
            case AVRO_FIXED:
                break;
        }
    }
    return -1;
}

int avro_encode_datum(N_STR** dest, const AVRO_SCHEMA* schema, const cJSON* json) {
    __n_assert(dest, return FALSE);
    __n_assert(schema, return FALSE);

    switch (schema->type) {
        case AVRO_NULL:
            /* null is zero bytes */
            return TRUE;

        case AVRO_BOOLEAN: {
            unsigned char b = cJSON_IsTrue(json) ? 1 : 0;
            nstrcat_ex(dest, &b, 1, 1);
            return TRUE;
        }

        case AVRO_INT: {
            int32_t val = 0;
            if (cJSON_IsNumber(json)) {
                val = (int32_t)cJSON_GetNumberValue(json);
            }
            return avro_encode_long(dest, (int64_t)val);
        }

        case AVRO_LONG: {
            int64_t val = 0;
            if (cJSON_IsNumber(json)) {
                val = (int64_t)cJSON_GetNumberValue(json);
            }
            return avro_encode_long(dest, val);
        }

        case AVRO_FLOAT: {
            float val = 0.0f;
            if (cJSON_IsNumber(json)) {
                val = (float)cJSON_GetNumberValue(json);
            }
            /* Avro float is 4 bytes little-endian IEEE 754 */
            unsigned char buf[4];
            memcpy(buf, &val, 4);
#if BYTEORDER_ENDIAN == BYTEORDER_BIG_ENDIAN
            unsigned char tmp;
            tmp = buf[0];
            buf[0] = buf[3];
            buf[3] = tmp;
            tmp = buf[1];
            buf[1] = buf[2];
            buf[2] = tmp;
#endif
            nstrcat_ex(dest, buf, 4, 1);
            return TRUE;
        }

        case AVRO_DOUBLE: {
            double val = 0.0;
            if (cJSON_IsNumber(json)) {
                val = cJSON_GetNumberValue(json);
            }
            /* Avro double is 8 bytes little-endian IEEE 754 */
            unsigned char buf[8];
            memcpy(buf, &val, 8);
#if BYTEORDER_ENDIAN == BYTEORDER_BIG_ENDIAN
            unsigned char tmp;
            tmp = buf[0];
            buf[0] = buf[7];
            buf[7] = tmp;
            tmp = buf[1];
            buf[1] = buf[6];
            buf[6] = tmp;
            tmp = buf[2];
            buf[2] = buf[5];
            buf[5] = tmp;
            tmp = buf[3];
            buf[3] = buf[4];
            buf[4] = tmp;
#endif
            nstrcat_ex(dest, buf, 8, 1);
            return TRUE;
        }

        case AVRO_BYTES: {
            const char* str = cJSON_GetStringValue((cJSON*)json);
            if (str) {
                return avro_encode_bytes_raw(dest, (const unsigned char*)str, strlen(str));
            }
            return avro_encode_bytes_raw(dest, NULL, 0);
        }

        case AVRO_STRING: {
            const char* str = cJSON_GetStringValue((cJSON*)json);
            if (str) {
                return avro_encode_bytes_raw(dest, (const unsigned char*)str, strlen(str));
            }
            return avro_encode_bytes_raw(dest, (const unsigned char*)"", 0);
        }

        case AVRO_RECORD: {
            if (!cJSON_IsObject(json)) {
                n_log(LOG_ERR, "expected JSON object for record type");
                return FALSE;
            }
            for (size_t i = 0; i < schema->nb_fields; i++) {
                const cJSON* field_val = cJSON_GetObjectItemCaseSensitive(json, schema->fields[i].name);
                if (!field_val) {
                    /* try with a null placeholder */
                    cJSON null_val;
                    memset(&null_val, 0, sizeof(null_val));
                    null_val.type = cJSON_NULL;
                    if (avro_encode_datum(dest, schema->fields[i].schema, &null_val) != TRUE) {
                        return FALSE;
                    }
                } else {
                    if (avro_encode_datum(dest, schema->fields[i].schema, field_val) != TRUE) {
                        return FALSE;
                    }
                }
            }
            return TRUE;
        }

        case AVRO_ENUM: {
            const char* sym = cJSON_GetStringValue((cJSON*)json);
            if (!sym) {
                n_log(LOG_ERR, "expected string for enum type");
                return FALSE;
            }
            for (size_t i = 0; i < schema->nb_symbols; i++) {
                if (strcmp(sym, schema->symbols[i]) == 0) {
                    return avro_encode_long(dest, (int64_t)i);
                }
            }
            n_log(LOG_ERR, "unknown enum symbol: %s", sym);
            return FALSE;
        }

        case AVRO_ARRAY: {
            if (!cJSON_IsArray(json)) {
                n_log(LOG_ERR, "expected JSON array for array type");
                return FALSE;
            }
            int count = cJSON_GetArraySize(json);
            if (count > 0) {
                /* write block with count */
                if (avro_encode_long(dest, (int64_t)count) != TRUE) return FALSE;
                for (int i = 0; i < count; i++) {
                    if (avro_encode_datum(dest, schema->items, cJSON_GetArrayItem(json, i)) != TRUE) {
                        return FALSE;
                    }
                }
            }
            /* terminating zero-count block */
            return avro_encode_long(dest, 0);
        }

        case AVRO_MAP: {
            if (!cJSON_IsObject(json)) {
                n_log(LOG_ERR, "expected JSON object for map type");
                return FALSE;
            }
            int count = cJSON_GetArraySize(json);
            if (count > 0) {
                if (avro_encode_long(dest, (int64_t)count) != TRUE) return FALSE;
                const cJSON* item = NULL;
                cJSON_ArrayForEach(item, json) {
                    /* encode key as string */
                    const char* key = item->string;
                    if (avro_encode_bytes_raw(dest, (const unsigned char*)key, strlen(key)) != TRUE) {
                        return FALSE;
                    }
                    /* encode value */
                    if (avro_encode_datum(dest, schema->values, item) != TRUE) {
                        return FALSE;
                    }
                }
            }
            return avro_encode_long(dest, 0);
        }

        case AVRO_UNION: {
            int branch_idx = avro_find_union_branch(schema, json);
            if (branch_idx < 0) {
                n_log(LOG_ERR, "no matching union branch for JSON value");
                return FALSE;
            }
            if (avro_encode_long(dest, (int64_t)branch_idx) != TRUE) return FALSE;
            return avro_encode_datum(dest, schema->union_branches[branch_idx], json);
        }

        case AVRO_FIXED: {
            const char* str = cJSON_GetStringValue((cJSON*)json);
            if (!str) {
                n_log(LOG_ERR, "expected string for fixed type");
                return FALSE;
            }
            size_t len = strlen(str);
            if (len > schema->fixed_size) {
                len = schema->fixed_size;
            }
            /* write exactly fixed_size bytes, pad with zeros if needed */
            nstrcat_ex(dest, (void*)str, (NSTRBYTE)len, 1);
            if (len < schema->fixed_size) {
                size_t pad = schema->fixed_size - len;
                unsigned char* zeros = NULL;
                Malloc(zeros, unsigned char, pad);
                if (zeros) {
                    nstrcat_ex(dest, zeros, (NSTRBYTE)pad, 1);
                    Free(zeros);
                }
            }
            return TRUE;
        }
    }

    return FALSE;
}

/* ========================================================================== */
/*                         Binary decoding                                     */
/* ========================================================================== */

/*! helper: read raw bytes with length prefix */
static int avro_decode_bytes_raw(AVRO_READER* reader, unsigned char** out, size_t* out_len) {
    int64_t len = 0;
    if (avro_decode_long(reader, &len) != TRUE) return FALSE;
    if (len < 0) {
        n_log(LOG_ERR, "negative byte length: %" PRId64, len);
        return FALSE;
    }
    *out_len = (size_t)len;
    if ((size_t)len > reader->size - reader->pos) {
        n_log(LOG_ERR, "not enough data for bytes: need %zu, have %zu", (size_t)len, reader->size - reader->pos);
        return FALSE;
    }
    Malloc(*out, unsigned char, (size_t)len + 1);
    __n_assert(*out, return FALSE);
    if (len > 0) {
        memcpy(*out, reader->data + reader->pos, (size_t)len);
    }
    (*out)[(size_t)len] = '\0';
    reader->pos += (size_t)len;
    return TRUE;
}

cJSON* avro_decode_datum(AVRO_READER* reader, const AVRO_SCHEMA* schema) {
    __n_assert(reader, return NULL);
    __n_assert(schema, return NULL);

    switch (schema->type) {
        case AVRO_NULL:
            return cJSON_CreateNull();

        case AVRO_BOOLEAN: {
            if (reader->pos >= reader->size) {
                n_log(LOG_ERR, "unexpected end of data reading boolean");
                return NULL;
            }
            unsigned char b = reader->data[reader->pos++];
            return cJSON_CreateBool(b != 0);
        }

        case AVRO_INT: {
            int64_t val = 0;
            if (avro_decode_long(reader, &val) != TRUE) return NULL;
            return cJSON_CreateNumber((double)(int32_t)val);
        }

        case AVRO_LONG: {
            int64_t val = 0;
            if (avro_decode_long(reader, &val) != TRUE) return NULL;
            return cJSON_CreateNumber((double)val);
        }

        case AVRO_FLOAT: {
            if (reader->pos + 4 > reader->size) {
                n_log(LOG_ERR, "unexpected end of data reading float");
                return NULL;
            }
            unsigned char buf[4];
            memcpy(buf, reader->data + reader->pos, 4);
            reader->pos += 4;
#if BYTEORDER_ENDIAN == BYTEORDER_BIG_ENDIAN
            unsigned char tmp;
            tmp = buf[0];
            buf[0] = buf[3];
            buf[3] = tmp;
            tmp = buf[1];
            buf[1] = buf[2];
            buf[2] = tmp;
#endif
            float val;
            memcpy(&val, buf, 4);
            return cJSON_CreateNumber((double)val);
        }

        case AVRO_DOUBLE: {
            if (reader->pos + 8 > reader->size) {
                n_log(LOG_ERR, "unexpected end of data reading double");
                return NULL;
            }
            unsigned char buf[8];
            memcpy(buf, reader->data + reader->pos, 8);
            reader->pos += 8;
#if BYTEORDER_ENDIAN == BYTEORDER_BIG_ENDIAN
            unsigned char tmp;
            tmp = buf[0];
            buf[0] = buf[7];
            buf[7] = tmp;
            tmp = buf[1];
            buf[1] = buf[6];
            buf[6] = tmp;
            tmp = buf[2];
            buf[2] = buf[5];
            buf[5] = tmp;
            tmp = buf[3];
            buf[3] = buf[4];
            buf[4] = tmp;
#endif
            double val;
            memcpy(&val, buf, 8);
            return cJSON_CreateNumber(val);
        }

        case AVRO_BYTES: {
            unsigned char* data = NULL;
            size_t len = 0;
            if (avro_decode_bytes_raw(reader, &data, &len) != TRUE) return NULL;
            cJSON* result = cJSON_CreateString((const char*)data);
            Free(data);
            return result;
        }

        case AVRO_STRING: {
            unsigned char* data = NULL;
            size_t len = 0;
            if (avro_decode_bytes_raw(reader, &data, &len) != TRUE) return NULL;
            cJSON* result = cJSON_CreateString((const char*)data);
            Free(data);
            return result;
        }

        case AVRO_RECORD: {
            cJSON* obj = cJSON_CreateObject();
            for (size_t i = 0; i < schema->nb_fields; i++) {
                cJSON* field_val = avro_decode_datum(reader, schema->fields[i].schema);
                if (!field_val) {
                    cJSON_Delete(obj);
                    return NULL;
                }
                cJSON_AddItemToObject(obj, schema->fields[i].name, field_val);
            }
            return obj;
        }

        case AVRO_ENUM: {
            int64_t idx = 0;
            if (avro_decode_long(reader, &idx) != TRUE) return NULL;
            if (idx < 0 || (size_t)idx >= schema->nb_symbols) {
                n_log(LOG_ERR, "enum index %" PRId64 " out of range (0-%zu)", idx, schema->nb_symbols - 1);
                return NULL;
            }
            return cJSON_CreateString(schema->symbols[(size_t)idx]);
        }

        case AVRO_ARRAY: {
            cJSON* arr = cJSON_CreateArray();
            int64_t block_count = 0;
            for (;;) {
                if (avro_decode_long(reader, &block_count) != TRUE) {
                    cJSON_Delete(arr);
                    return NULL;
                }
                if (block_count == 0) break;
                if (block_count < 0) {
                    /* negative count means block size follows */
                    block_count = -block_count;
                    int64_t block_size = 0;
                    if (avro_decode_long(reader, &block_size) != TRUE) {
                        cJSON_Delete(arr);
                        return NULL;
                    }
                    (void)block_size;
                }
                for (int64_t i = 0; i < block_count; i++) {
                    cJSON* item = avro_decode_datum(reader, schema->items);
                    if (!item) {
                        cJSON_Delete(arr);
                        return NULL;
                    }
                    cJSON_AddItemToArray(arr, item);
                }
            }
            return arr;
        }

        case AVRO_MAP: {
            cJSON* obj = cJSON_CreateObject();
            int64_t block_count = 0;
            for (;;) {
                if (avro_decode_long(reader, &block_count) != TRUE) {
                    cJSON_Delete(obj);
                    return NULL;
                }
                if (block_count == 0) break;
                if (block_count < 0) {
                    block_count = -block_count;
                    int64_t block_size = 0;
                    if (avro_decode_long(reader, &block_size) != TRUE) {
                        cJSON_Delete(obj);
                        return NULL;
                    }
                    (void)block_size;
                }
                for (int64_t i = 0; i < block_count; i++) {
                    /* decode key */
                    unsigned char* key = NULL;
                    size_t key_len = 0;
                    if (avro_decode_bytes_raw(reader, &key, &key_len) != TRUE) {
                        cJSON_Delete(obj);
                        return NULL;
                    }
                    /* decode value */
                    cJSON* val = avro_decode_datum(reader, schema->values);
                    if (!val) {
                        Free(key);
                        cJSON_Delete(obj);
                        return NULL;
                    }
                    cJSON_AddItemToObject(obj, (const char*)key, val);
                    Free(key);
                }
            }
            return obj;
        }

        case AVRO_UNION: {
            int64_t branch_idx = 0;
            if (avro_decode_long(reader, &branch_idx) != TRUE) return NULL;
            if (branch_idx < 0 || (size_t)branch_idx >= schema->nb_branches) {
                n_log(LOG_ERR, "union branch index %" PRId64 " out of range (0-%zu)", branch_idx, schema->nb_branches - 1);
                return NULL;
            }
            return avro_decode_datum(reader, schema->union_branches[(size_t)branch_idx]);
        }

        case AVRO_FIXED: {
            if (reader->pos + schema->fixed_size > reader->size) {
                n_log(LOG_ERR, "unexpected end of data reading fixed(%zu)", schema->fixed_size);
                return NULL;
            }
            char* buf = NULL;
            Malloc(buf, char, schema->fixed_size + 1);
            __n_assert(buf, return NULL);
            memcpy(buf, reader->data + reader->pos, schema->fixed_size);
            buf[schema->fixed_size] = '\0';
            reader->pos += schema->fixed_size;
            cJSON* result = cJSON_CreateString(buf);
            Free(buf);
            return result;
        }
    }

    return NULL;
}

/* ========================================================================== */
/*                     Object Container File format                            */
/* ========================================================================== */

N_STR* avro_encode_container(const AVRO_SCHEMA* schema, const cJSON* records) {
    __n_assert(schema, return NULL);
    __n_assert(records, return NULL);

    if (!cJSON_IsArray(records)) {
        n_log(LOG_ERR, "records must be a JSON array");
        return NULL;
    }

    N_STR* output = NULL;
    nstrprintf(output, "%s", "");
    __n_assert(output, return NULL);

    /* 1. Magic bytes */
    nstrcat_ex(&output, (void*)AVRO_MAGIC, AVRO_MAGIC_LEN, 1);

    /* 2. File metadata as Avro map */
    char* schema_json = avro_schema_to_json(schema);
    __n_assert(schema_json, free_nstr(&output); return NULL);

    /* metadata map: 1 block with 2 entries, then 0 block */
    /* block count = 2 (avro.schema + avro.codec) */
    avro_encode_long(&output, 2);

    /* entry 1: avro.schema */
    const char* key1 = "avro.schema";
    avro_encode_long(&output, (int64_t)strlen(key1));
    nstrcat_ex(&output, (void*)key1, (NSTRBYTE)strlen(key1), 1);
    avro_encode_long(&output, (int64_t)strlen(schema_json));
    nstrcat_ex(&output, (void*)schema_json, (NSTRBYTE)strlen(schema_json), 1);

    /* entry 2: avro.codec = "null" (no compression) */
    const char* key2 = "avro.codec";
    avro_encode_long(&output, (int64_t)strlen(key2));
    nstrcat_ex(&output, (void*)key2, (NSTRBYTE)strlen(key2), 1);
    const char* codec = "null";
    avro_encode_long(&output, (int64_t)strlen(codec));
    nstrcat_ex(&output, (void*)codec, (NSTRBYTE)strlen(codec), 1);

    /* end of metadata map */
    avro_encode_long(&output, 0);

    Free(schema_json);

    /* 3. Generate sync marker (16 bytes) */
    unsigned char sync[AVRO_SYNC_LEN];
    srand((unsigned)time(NULL));
    for (int i = 0; i < AVRO_SYNC_LEN; i++) {
        sync[i] = (unsigned char)(rand() % 256);
    }
    nstrcat_ex(&output, sync, AVRO_SYNC_LEN, 1);

    /* 4. Data block: encode all records into a single block */
    int count = cJSON_GetArraySize(records);

    N_STR* block_data = NULL;
    nstrprintf(block_data, "%s", "");
    __n_assert(block_data, free_nstr(&output); return NULL);

    for (int i = 0; i < count; i++) {
        const cJSON* record = cJSON_GetArrayItem(records, i);
        if (avro_encode_datum(&block_data, schema, record) != TRUE) {
            n_log(LOG_ERR, "failed to encode record %d", i);
            free_nstr(&block_data);
            free_nstr(&output);
            return NULL;
        }
    }

    /* write block header: object count, then byte size of serialized data */
    avro_encode_long(&output, (int64_t)count);
    avro_encode_long(&output, (int64_t)block_data->written);

    /* write serialized data */
    nstrcat_ex(&output, block_data->data, (NSTRBYTE)block_data->written, 1);
    free_nstr(&block_data);

    /* write sync marker */
    nstrcat_ex(&output, sync, AVRO_SYNC_LEN, 1);

    return output;
}

cJSON* avro_decode_container(const AVRO_SCHEMA* schema, const N_STR* avro_data) {
    __n_assert(schema, return NULL);
    __n_assert(avro_data, return NULL);
    __n_assert(avro_data->data, return NULL);

    AVRO_READER reader;
    reader.data = (const unsigned char*)avro_data->data;
    reader.size = avro_data->written;
    reader.pos = 0;

    /* 1. Check magic */
    if (reader.size < AVRO_MAGIC_LEN + AVRO_SYNC_LEN) {
        n_log(LOG_ERR, "data too small for Avro container");
        return NULL;
    }
    if (memcmp(reader.data, AVRO_MAGIC, AVRO_MAGIC_LEN) != 0) {
        n_log(LOG_ERR, "invalid Avro magic bytes");
        return NULL;
    }
    reader.pos = AVRO_MAGIC_LEN;

    /* 2. Read metadata map (skip it, we use the provided schema) */
    int64_t block_count = 0;
    for (;;) {
        if (avro_decode_long(&reader, &block_count) != TRUE) return NULL;
        if (block_count == 0) break;
        if (block_count < 0) {
            /* negative count: skip block_size bytes */
            int64_t block_size = 0;
            if (avro_decode_long(&reader, &block_size) != TRUE) return NULL;
            if (block_size < 0 || (size_t)block_size > reader.size - reader.pos) {
                n_log(LOG_ERR, "invalid metadata block size");
                return NULL;
            }
            reader.pos += (size_t)block_size;
            continue;
        }
        /* positive count: read key-value pairs */
        for (int64_t i = 0; i < block_count; i++) {
            unsigned char* key = NULL;
            size_t key_len = 0;
            if (avro_decode_bytes_raw(&reader, &key, &key_len) != TRUE) return NULL;
            Free(key);
            unsigned char* val = NULL;
            size_t val_len = 0;
            if (avro_decode_bytes_raw(&reader, &val, &val_len) != TRUE) return NULL;
            Free(val);
        }
    }

    /* 3. Read sync marker */
    if (reader.pos + AVRO_SYNC_LEN > reader.size) {
        n_log(LOG_ERR, "unexpected end of data reading sync marker");
        return NULL;
    }
    unsigned char sync[AVRO_SYNC_LEN];
    memcpy(sync, reader.data + reader.pos, AVRO_SYNC_LEN);
    reader.pos += AVRO_SYNC_LEN;

    /* 4. Read data blocks */
    cJSON* all_records = cJSON_CreateArray();

    while (reader.pos < reader.size) {
        int64_t obj_count = 0;
        if (avro_decode_long(&reader, &obj_count) != TRUE) {
            cJSON_Delete(all_records);
            return NULL;
        }
        if (obj_count <= 0) break;

        int64_t block_byte_size = 0;
        if (avro_decode_long(&reader, &block_byte_size) != TRUE) {
            cJSON_Delete(all_records);
            return NULL;
        }

        if (block_byte_size < 0 || (size_t)block_byte_size > reader.size - reader.pos) {
            n_log(LOG_ERR, "invalid data block size: %" PRId64, block_byte_size);
            cJSON_Delete(all_records);
            return NULL;
        }

        size_t block_end = reader.pos + (size_t)block_byte_size;

        for (int64_t i = 0; i < obj_count; i++) {
            cJSON* record = avro_decode_datum(&reader, schema);
            if (!record) {
                n_log(LOG_ERR, "failed to decode record %" PRId64, i);
                cJSON_Delete(all_records);
                return NULL;
            }
            cJSON_AddItemToArray(all_records, record);
        }

        /* verify we consumed exactly the block */
        if (reader.pos != block_end) {
            n_log(LOG_WARNING, "block size mismatch: expected pos %zu, got %zu", block_end, reader.pos);
            reader.pos = block_end;
        }

        /* read and verify sync marker */
        if (reader.pos + AVRO_SYNC_LEN > reader.size) {
            n_log(LOG_ERR, "unexpected end of data reading block sync marker");
            cJSON_Delete(all_records);
            return NULL;
        }
        if (memcmp(reader.data + reader.pos, sync, AVRO_SYNC_LEN) != 0) {
            n_log(LOG_ERR, "sync marker mismatch");
            cJSON_Delete(all_records);
            return NULL;
        }
        reader.pos += AVRO_SYNC_LEN;
    }

    return all_records;
}

/* ========================================================================== */
/*                     File-level convenience functions                         */
/* ========================================================================== */

int avro_json_to_file(const char* avro_filename, const char* schema_filename, const char* json_filename) {
    __n_assert(avro_filename, return FALSE);
    __n_assert(schema_filename, return FALSE);
    __n_assert(json_filename, return FALSE);

    /* load schema file */
    N_STR* schema_str = file_to_nstr((char*)schema_filename);
    __n_assert(schema_str, return FALSE);

    AVRO_SCHEMA* schema = avro_schema_parse(schema_str->data);
    free_nstr(&schema_str);
    __n_assert(schema, return FALSE);

    /* load JSON file */
    N_STR* json_str = file_to_nstr((char*)json_filename);
    if (!json_str) {
        avro_schema_free(&schema);
        return FALSE;
    }

    cJSON* json = cJSON_Parse(json_str->data);
    free_nstr(&json_str);
    if (!json) {
        n_log(LOG_ERR, "failed to parse JSON file %s: %s", json_filename, _str(cJSON_GetErrorPtr()));
        avro_schema_free(&schema);
        return FALSE;
    }

    /* if json is a single object, wrap it in an array */
    cJSON* records = json;
    int wrapped = 0;
    if (cJSON_IsObject(json)) {
        records = cJSON_CreateArray();
        cJSON_AddItemToArray(records, cJSON_Duplicate(json, 1));
        wrapped = 1;
    }

    /* encode to Avro container */
    N_STR* avro_data = avro_encode_container(schema, records);

    if (wrapped) {
        cJSON_Delete(records);
    }
    cJSON_Delete(json);
    avro_schema_free(&schema);

    if (!avro_data) {
        n_log(LOG_ERR, "failed to encode Avro container");
        return FALSE;
    }

    /* write Avro file */
    int ret = nstr_to_file(avro_data, (char*)avro_filename);
    free_nstr(&avro_data);

    if (ret != TRUE) {
        n_log(LOG_ERR, "failed to write Avro file %s", avro_filename);
        return FALSE;
    }

    n_log(LOG_INFO, "wrote Avro file %s", avro_filename);
    return TRUE;
}

int avro_file_to_json(const char* avro_filename, const char* schema_filename, const char* json_filename) {
    __n_assert(avro_filename, return FALSE);
    __n_assert(schema_filename, return FALSE);
    __n_assert(json_filename, return FALSE);

    /* load schema file */
    N_STR* schema_str = file_to_nstr((char*)schema_filename);
    __n_assert(schema_str, return FALSE);

    AVRO_SCHEMA* schema = avro_schema_parse(schema_str->data);
    free_nstr(&schema_str);
    __n_assert(schema, return FALSE);

    /* load Avro file */
    N_STR* avro_data = file_to_nstr((char*)avro_filename);
    if (!avro_data) {
        avro_schema_free(&schema);
        return FALSE;
    }

    /* decode from Avro container */
    cJSON* records = avro_decode_container(schema, avro_data);
    free_nstr(&avro_data);
    avro_schema_free(&schema);

    if (!records) {
        n_log(LOG_ERR, "failed to decode Avro file %s", avro_filename);
        return FALSE;
    }

    /* write JSON file */
    char* json_output = cJSON_Print(records);
    cJSON_Delete(records);

    if (!json_output) {
        n_log(LOG_ERR, "failed to serialize JSON");
        return FALSE;
    }

    N_STR* json_nstr = char_to_nstr(json_output);
    free(json_output);

    if (!json_nstr) {
        return FALSE;
    }

    int ret = nstr_to_file(json_nstr, (char*)json_filename);
    free_nstr(&json_nstr);

    if (ret != TRUE) {
        n_log(LOG_ERR, "failed to write JSON file %s", json_filename);
        return FALSE;
    }

    n_log(LOG_INFO, "wrote JSON file %s", json_filename);
    return TRUE;
}

/* ========================================================================== */
/*                     N_STR in-memory convenience functions                    */
/* ========================================================================== */

AVRO_SCHEMA* avro_schema_parse_nstr(const N_STR* schema_nstr) {
    __n_assert(schema_nstr, return NULL);
    __n_assert(schema_nstr->data, return NULL);

    return avro_schema_parse(schema_nstr->data);
}

N_STR* avro_nstr_json_to_avro(const N_STR* schema_nstr, const N_STR* json_nstr) {
    __n_assert(schema_nstr, return NULL);
    __n_assert(schema_nstr->data, return NULL);
    __n_assert(json_nstr, return NULL);
    __n_assert(json_nstr->data, return NULL);

    AVRO_SCHEMA* schema = avro_schema_parse(schema_nstr->data);
    __n_assert(schema, return NULL);

    cJSON* json = cJSON_Parse(json_nstr->data);
    if (!json) {
        n_log(LOG_ERR, "failed to parse JSON: %s", _str(cJSON_GetErrorPtr()));
        avro_schema_free(&schema);
        return NULL;
    }

    /* if json is a single object, wrap it in an array */
    cJSON* records = json;
    int wrapped = 0;
    if (cJSON_IsObject(json)) {
        records = cJSON_CreateArray();
        cJSON_AddItemToArray(records, cJSON_Duplicate(json, 1));
        wrapped = 1;
    }

    N_STR* avro_data = avro_encode_container(schema, records);

    if (wrapped) {
        cJSON_Delete(records);
    }
    cJSON_Delete(json);
    avro_schema_free(&schema);

    return avro_data;
}

N_STR* avro_nstr_avro_to_json(const N_STR* schema_nstr, const N_STR* avro_nstr) {
    __n_assert(schema_nstr, return NULL);
    __n_assert(schema_nstr->data, return NULL);
    __n_assert(avro_nstr, return NULL);
    __n_assert(avro_nstr->data, return NULL);

    AVRO_SCHEMA* schema = avro_schema_parse(schema_nstr->data);
    __n_assert(schema, return NULL);

    cJSON* records = avro_decode_container(schema, avro_nstr);
    avro_schema_free(&schema);

    if (!records) {
        n_log(LOG_ERR, "failed to decode Avro container");
        return NULL;
    }

    char* json_output = cJSON_Print(records);
    cJSON_Delete(records);

    if (!json_output) {
        n_log(LOG_ERR, "failed to serialize JSON");
        return NULL;
    }

    N_STR* result = char_to_nstr(json_output);
    free(json_output);

    return result;
}

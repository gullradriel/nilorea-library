/**
 *@file n_base64.c
 *@brief Base64 encode decode function, adapted from https://opensource.apple.com/source/QuickTimeStreamingServer/QuickTimeStreamingServer-452/CommonUtilitiesLib/base64.c
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/11/2022
 */

#include "nilorea/n_base64.h"
#include <string.h>

/*! static lookup ascii table */
static const unsigned char pr2six[256] =
    {
        /* ASCII table */
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
        64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
        64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

/*! static upper case lookup ascii table */
static const bool ascii_upper_case_lookup_table[256] =
    {
        /* ASCII table , upper from 65 to 90 */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 16
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 64
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // 256
};

/*! static lower case lookup ascii table */
static const bool ascii_lower_case_lookup_table[256] =
    {
        /* ASCII table , upper from 97 to 122 */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 16
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 64
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 128
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // 256
};

/*! static lookup base64 alphabet */
static const char basis_64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 *@brief test if char c is uppercase
 *@param c charater to look up
 *@return TRUE or FALSE
 */
bool n_isupper(char c) {
    return ascii_upper_case_lookup_table[(uint8_t)c];
}

/**
 *@brief test if char c is lowercase
 *@param c charater to look up
 *@return TRUE or FALSE
 */
bool n_islower(char c) {
    return ascii_lower_case_lookup_table[(uint8_t)c];
}

/**
 *@brief is_alpha
 *@param c charater to look up
 *@return TRUE or FALSE
 */
bool n_isalpha(char c) {
    return (ascii_lower_case_lookup_table[(uint8_t)c] || ascii_upper_case_lookup_table[(uint8_t)c]);
}

/**
 *@brief is_alpha
 *@param c charater to look up
 *@return uppercased char c
 */
char n_toupper(char c) {
    if (ascii_lower_case_lookup_table[(uint8_t)c])
        return (char)(c - 32);
    return c;
}

/**
 *@brief is_alpha
 *@param c charater to look up
 *@return lowercased char c
 */
char n_tolower(char c) {
    if (ascii_upper_case_lookup_table[(uint8_t)c])
        return (char)(c + 32);
    return c;
}

/**
 * @brief get the length of 'input' if it was base64 decoded
 * @param input the N_STR *string for which we need the decoded size
 * @return length of the decoded string or 0 on error / invalid input
 */
size_t n_base64_decode_len(N_STR* string) {
    __n_assert(string, return 0);
    size_t nbytesdecoded = 0;
    size_t nprbytes = 0;

    const unsigned char* bufin = (const unsigned char*)string->data;
    const unsigned char* bufend = bufin + string->length;  // End of the buffer

    while (pr2six[*(bufin++)] <= 63) {
        if (bufin >= bufend) {
            n_log(LOG_ERR, "could not detect end of encoded string for N_STR %p", string);
            return 0;
        }
    }

    // Safely calculate the difference
    if (bufin > (const unsigned char*)string->data) {
        nprbytes = (size_t)(bufin - (const unsigned char*)string->data - 1);
    } else {
        // Handle unexpected cases where subtraction could be invalid
        n_log(LOG_ERR, "invalid size of 0 for N_STR %p", string);
        return 0;
    }

    nbytesdecoded = ((nprbytes + 3) / 4) * 3;

    return nbytesdecoded;
}

/**
 * @brief decode a N_STR *string
 * @param bufcoded the N_STR *string to decode
 * @return a new base64 N_STR *decoded string or NULL
 */
N_STR* n_base64_decode(N_STR* bufcoded) {
    __n_assert(bufcoded, return NULL);

    size_t nbytesdecoded = 0;
    size_t nprbytes = 0;

    const unsigned char* bufin = (const unsigned char*)bufcoded->data;
    const unsigned char* bufend = bufin + bufcoded->length;  // End of the buffer

    while (pr2six[*(bufin++)] <= 63) {
        if (bufin >= bufend) {
            n_log(LOG_ERR, "could not detect end of bufcoded string for N_STR %p", bufcoded);
            return NULL;
        }
    }

    // Safely calculate the difference
    if (bufin > (const unsigned char*)bufcoded->data) {
        nprbytes = (size_t)(bufin - (const unsigned char*)bufcoded->data - 1);
    } else {
        // Handle unexpected cases where subtraction could be invalid
        n_log(LOG_ERR, "invalid encoded size of 0 for bufcoded N_STR %p", bufcoded);
        return NULL;
    }

    nbytesdecoded = ((nprbytes + 3) / 4) * 3;

    N_STR* bufplain = new_nstr(nbytesdecoded + 1);
    __n_assert(bufplain, return NULL);

    unsigned char* bufout = (unsigned char*)bufplain->data;
    bufin = (const unsigned char*)bufcoded->data;

    while (nprbytes > 4) {
        *(bufout++) =
            (unsigned char)(pr2six[*bufin] << 2 | pr2six[bufin[1]] >> 4);
        *(bufout++) =
            (unsigned char)(pr2six[bufin[1]] << 4 | pr2six[bufin[2]] >> 2);
        *(bufout++) =
            (unsigned char)(pr2six[bufin[2]] << 6 | pr2six[bufin[3]]);
        bufin += 4;
        nprbytes -= 4;
    }

    /* Note: (nprbytes == 1) would be an error, so just ignore that case */
    if (nprbytes > 1) {
        *(bufout++) =
            (unsigned char)(pr2six[*bufin] << 2 | pr2six[bufin[1]] >> 4);
    }
    if (nprbytes > 2) {
        *(bufout++) =
            (unsigned char)(pr2six[bufin[1]] << 4 | pr2six[bufin[2]] >> 2);
    }
    if (nprbytes > 3) {
        *(bufout++) =
            (unsigned char)(pr2six[bufin[2]] << 6 | pr2six[bufin[3]]);
    }

    *(bufout++) = '\0';
    nbytesdecoded -= (4 - nprbytes) & 3;

    bufplain->written = nbytesdecoded;

    return bufplain;
}

/**
 * @brief get the length of string if it was base64 encoded
 * @param string the N_STR *string for which we need the encoded size
 * @return length of the encoded string or 0 on error
 */
size_t n_base64_encode_len(N_STR* string) {
    __n_assert(string, return 0);

    return ((string->written + 2) / 3 * 4) + 1;
}

/**
 * @brief encode a N_STR *string
 * @param input the N_STR *string to encode
 * @return a new base64 N_STR *encoded string or NULL
 */
N_STR* n_base64_encode(N_STR* input) {
    __n_assert(input, return NULL);

    size_t i = 0;
    char* p = NULL;
    char* string = input->data;
    size_t len = input->written;

    size_t output_length = n_base64_encode_len(input);
    if (output_length == 0) {
        n_log(LOG_ERR, "invalid encoded size of 0 for N_STR *input %p", input);
        return NULL;
    }

    N_STR* encoded = new_nstr(output_length + 1);
    __n_assert(encoded, return NULL);

    p = encoded->data;
    for (i = 0; i < len - 2; i += 3) {
        *p++ = basis_64[(string[i] >> 2) & 0x3F];
        *p++ = basis_64[((string[i] & 0x3) << 4) |
                        ((int)(string[i + 1] & 0xF0) >> 4)];
        *p++ = basis_64[((string[i + 1] & 0xF) << 2) |
                        ((int)(string[i + 2] & 0xC0) >> 6)];
        *p++ = basis_64[string[i + 2] & 0x3F];
    }
    if (i < len) {
        *p++ = basis_64[(string[i] >> 2) & 0x3F];
        if (i == (len - 1)) {
            *p++ = basis_64[((string[i] & 0x3) << 4)];
            *p++ = '=';
        } else {
            *p++ = basis_64[((string[i] & 0x3) << 4) |
                            ((int)(string[i + 1] & 0xF0) >> 4)];
            *p++ = basis_64[((string[i + 1] & 0xF) << 2)];
        }
        *p++ = '=';
    }

    *p++ = '\0';

    if ((p - encoded->data) > 0) {
        encoded->written = (size_t)(p - encoded->data);
    } else {
        n_log(LOG_ERR, "invalid size of %d for encoded string from N_STR *input %p", (p - encoded->data), input);
        free_nstr(&encoded);
        return NULL;
    }

    return encoded;
}

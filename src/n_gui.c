/**\file n_gui.c
 *  GUI functions
 *\author Castagnier Mickael
 *\version 1.0
 *\date 15/06/2024
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_gui.h"

/*! N_GUI context codes */
N_ENUM_DEFINE(N_ENUM_N_GUI_TOKEN,
              /*! type for a token */
              __N_GUI_TOKEN);

/*!\fn void n_gui_init_tokenizer(N_GUI_TOKENIZER *tokenizer, const char *input)
 * \brief Initialize tokenizer
 * \param tokenizer the tokenizer to init
 * \param input the input on which we want to start with a fresh tokenizer
 */
void n_gui_init_tokenizer(N_GUI_TOKENIZER* tokenizer, const char* input) {
    tokenizer->input = input;
    tokenizer->position = 0;
}

/*!\fn char n_gui_next_char(N_GUI_TOKENIZER *tokenizer)
 * \brief Get next character from input
 * \param tokenizer the tokenizer to read data from
 * \return the next character in input
 */
char n_gui_next_char(N_GUI_TOKENIZER* tokenizer) {
    return tokenizer->input[tokenizer->position++];
}

/*!\fn char n_gui_peek_char(N_GUI_TOKENIZER *tokenizer)
 * \brief Peek at next character without advancing the position
 * \param tokenizer the tokenizer from which we want to peek data from
 * \return the next character in the tokenizer input data
 */
char n_gui_peek_char(N_GUI_TOKENIZER* tokenizer) {
    return tokenizer->input[tokenizer->position];
}

/*!\fn int n_gui_is_eof(N_GUI_TOKENIZER *tokenizer)
 * \brief Check if end of input is reached
 * \param tokenizer the tokenizer from which we want to test if finished
 * \return TRUE or FALSE
 */
int n_gui_is_eof(N_GUI_TOKENIZER* tokenizer) {
    return tokenizer->input[tokenizer->position] == '\0';
}

/*!\fn N_GUI_TOKEN n_gui_next_token(N_GUI_TOKENIZER *tokenizer, bool *inside_open_tag)
 * \brief Read next N_GUI_TOKEN from input
 * \param tokenizer the tokenizer from which we want the next token from
 * \param inside_open_tag pointer to a boolean that will reflect the status of tag detection
 * \return the next N_GUI_TOKEN from the tokenizer input
 */
N_GUI_TOKEN n_gui_next_token(N_GUI_TOKENIZER* tokenizer, bool* inside_open_tag) {
    N_GUI_TOKEN token = {.type = N_GUI_TOKEN_EOF, .value = {0}};
    char ch;

    // Skip whitespace
    while (isspace(n_gui_peek_char(tokenizer))) {
        n_gui_next_char(tokenizer);
    }

    ch = n_gui_peek_char(tokenizer);

    // Handle end of file
    if (ch == '\0') {
        token.type = N_GUI_TOKEN_EOF;
        return token;
    }

    // Handle opening tag '<'
    if (ch == '<') {
        n_gui_next_char(tokenizer);
        ch = n_gui_peek_char(tokenizer);
        if (ch == '/') {
            n_gui_next_char(tokenizer);
            *inside_open_tag = false;
            token.type = N_GUI_TOKEN_TAG_CLOSE;
            strncpy(token.value, "/", N_GUI_MAX_TOKEN_SIZE - 1);
        } else {
            *inside_open_tag = true;
            token.type = N_GUI_TOKEN_TAG_OPEN;
            strncpy(token.value, "<", N_GUI_MAX_TOKEN_SIZE - 1);
        }
        return token;
    }

    // Handle closing tag '>'
    if (ch == '>') {
        n_gui_next_char(tokenizer);
        *inside_open_tag = false;
        token.type = N_GUI_TOKEN_TAG_CLOSE;
        strncpy(token.value, ">", N_GUI_MAX_TOKEN_SIZE - 1);
        return token;
    }

    // Handle attribute values
    if (ch == '=') {
        n_gui_next_char(tokenizer);  // consume '='
        token.type = N_GUI_TOKEN_ATTR_VALUE;
        ch = n_gui_peek_char(tokenizer);
        if (ch == '"' || ch == '\'') {
            char quote = n_gui_next_char(tokenizer);
            int pos = 0;
            while (n_gui_peek_char(tokenizer) != quote && !n_gui_is_eof(tokenizer)) {
                if (pos < N_GUI_MAX_TOKEN_SIZE - 1) {
                    token.value[pos++] = n_gui_next_char(tokenizer);
                }
            }
            n_gui_next_char(tokenizer);  // consume closing quote
            token.value[pos] = '\0';
        }
        return token;
    }

    // Handle tag names and attribute names
    if (*inside_open_tag && (isalpha(ch) || ch == '-')) {
        int pos = 0;
        while (isalnum(n_gui_peek_char(tokenizer)) || n_gui_peek_char(tokenizer) == '-' || n_gui_peek_char(tokenizer) == '_') {
            if (pos < N_GUI_MAX_TOKEN_SIZE - 1) {
                token.value[pos++] = n_gui_next_char(tokenizer);
            }
        }
        token.value[pos] = '\0';

        // Check if the next character is '=' to identify as attribute name
        if (n_gui_peek_char(tokenizer) == '=') {
            token.type = N_GUI_TOKEN_ATTR_NAME;
        } else {
            token.type = N_GUI_TOKEN_TAG_NAME;
        }
        return token;
    }

    // Handle text content
    if (ch != '<' && ch != '>') {
        if (!*inside_open_tag) {  // Only capture text when not inside a tag
            int pos = 0;
            while (ch != '<' && ch != '>' && !n_gui_is_eof(tokenizer)) {
                if (pos < N_GUI_MAX_TOKEN_SIZE - 1) {
                    token.value[pos++] = n_gui_next_char(tokenizer);
                }
                ch = n_gui_peek_char(tokenizer);
            }
            token.value[pos] = '\0';
            token.type = N_GUI_TOKEN_TEXT;

            // Handle text content if there is a valid length
            if (pos > 0) {
                return token;
            }
        }
    }
    // Default case for unknown or unsupported characters
    n_gui_next_char(tokenizer);  // consume character
    return n_gui_next_token(tokenizer, inside_open_tag);
}

/*!\fn void n_gui_print_token(N_GUI_TOKEN token)
 * \brief Function to print a token (for debugging purposes)
 * \param token the N_GUI_TOKEN token to print
 */
void n_gui_print_token(N_GUI_TOKEN token) {
    switch (token.type) {
        case N_GUI_TOKEN_TAG_OPEN:
            printf("N_GUI_TOKEN_TAG_OPEN: %s\n", token.value);
            break;
        case N_GUI_TOKEN_TAG_CLOSE:
            printf("N_GUI_TOKEN_TAG_CLOSE: %s\n", token.value);
            break;
        case N_GUI_TOKEN_TAG_NAME:
            printf("N_GUI_TOKEN_TAG_NAME: %s\n", token.value);
            break;
        case N_GUI_TOKEN_ATTR_NAME:
            printf("N_GUI_TOKEN_ATTR_NAME: %s\n", token.value);
            break;
        case N_GUI_TOKEN_ATTR_VALUE:
            printf("N_GUI_TOKEN_ATTR_VALUE: %s\n", token.value);
            break;
        case N_GUI_TOKEN_TEXT:
            printf("N_GUI_TOKEN_TEXT: %s\n", token.value);
            break;
        case N_GUI_TOKEN_EOF:
            printf("N_GUI_TOKEN_EOF\n");
            break;
    }
}

/*!\fn N_GUI_DIALOG *n_gui_load_dialog( char *html, char *css )
 * \brief Load a html + css file into a N_GUI_DIALOG
 * \param html path to the html file
 * \param css path to the css style file
 * \return a loaded N_GUI_DIALOG *dialog or NULL
 */
N_GUI_DIALOG* n_gui_load_dialog(char* html, char* css) {
    __n_assert(html, return NULL);
    N_STR* nstr_html = file_to_nstr(html);
    __n_assert(nstr_html, return NULL);

    // no assert for css, html can have no style X-D
    // __n_assert( html , return NULL );
    N_STR* nstr_css = NULL;
    if (css) {
        nstr_css = file_to_nstr(css);
    }

    N_GUI_DIALOG* gui = NULL;
    Malloc(gui, N_GUI_DIALOG, 1);
    __n_assert(gui, free_nstr(&nstr_html); free_nstr(&nstr_css); return NULL);

    N_GUI_TOKENIZER tokenizer;
    n_gui_init_tokenizer(&tokenizer, _nstr(nstr_html));
    N_GUI_TOKEN token;
    bool inside_open_tag = false;
    while ((token = n_gui_next_token(&tokenizer, &inside_open_tag)).type != N_GUI_TOKEN_EOF) {
        switch (token.type) {
            case N_GUI_TOKEN_TAG_OPEN: {
            } break;
            case N_GUI_TOKEN_TAG_CLOSE: {
            }
            case N_GUI_TOKEN_TAG_NAME: {
            }
            case N_GUI_TOKEN_ATTR_NAME: {
            }
            case N_GUI_TOKEN_ATTR_VALUE: {
            }
            case N_GUI_TOKEN_TEXT: {
            }
            case N_GUI_TOKEN_EOF: {
            }
        }
    }

    free_nstr(&nstr_html);
    free_nstr(&nstr_css);

    return NULL;
}

#include "lexer.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================
 * JSC$+ — Lexer (implementação)
 * ============================================================ */

void lexer_init(Lexer *lx, const char *src) {
    lx->src    = src;
    lx->pos    = 0;
    lx->line   = 1;
    lx->column = 1;
}

static char peek(Lexer *lx) {
    return lx->src[lx->pos];
}

static char peek_next(Lexer *lx) {
    if (lx->src[lx->pos] == '\0') return '\0';
    return lx->src[lx->pos + 1];
}

static char advance(Lexer *lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') {
        lx->line++;
        lx->column = 1;
    } else {
        lx->column++;
    }
    return c;
}

static int is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static Token make_token(TokenType type, const char *start, size_t len, int line, int col) {
    Token t;
    t.type   = type;
    t.lexeme = (char *)malloc(len + 1);
    if (t.lexeme) {
        memcpy(t.lexeme, start, len);
        t.lexeme[len] = '\0';
    }
    t.number = 0.0;
    t.line   = line;
    t.column = col;
    return t;
}

static TokenType keyword_type(const char *s) {
    /* Palavras-chave v3.0 */
    if (strcmp(s, "ENQ")      == 0) return TOK_ENQ;       /* while */
    if (strcmp(s, "GWB")      == 0) return TOK_GWB;       /* return */
    if (strcmp(s, "base")     == 0) return TOK_BASE;      /* class */
    if (strcmp(s, "extends")  == 0) return TOK_EXTENDS;
    if (strcmp(s, "this")     == 0) return TOK_THIS;
    if (strcmp(s, "in")       == 0) return TOK_IN;
    if (strcmp(s, "break")    == 0) return TOK_BREAK;
    if (strcmp(s, "continue") == 0) return TOK_CONTINUE;
    if (strcmp(s, "true")     == 0) return TOK_TRUE;
    if (strcmp(s, "false")    == 0) return TOK_FALSE;
    if (strcmp(s, "vazio")    == 0) return TOK_VAZIO;

    /* Nativos */
    if (strcmp(s, "jsc")      == 0) return TOK_JSC;
    if (strcmp(s, "printj")   == 0) return TOK_PRINTJ;
    if (strcmp(s, "input")    == 0) return TOK_INPUT;
    if (strcmp(s, "import")   == 0) return TOK_IMPORT;

    return TOK_IDENT;
}

static void skip_whitespace_and_comments(Lexer *lx) {
    for (;;) {
        char c = peek(lx);

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx);
            continue;
        }

        /* Comentário: @ até fim da linha */
        if (c == '@') {
            while (peek(lx) != '\0' && peek(lx) != '\n') {
                advance(lx);
            }
            continue;
        }

        break;
    }
}

Token lexer_next(Lexer *lx) {
    skip_whitespace_and_comments(lx);

    int   start_line = lx->line;
    int   start_col  = lx->column;
    char  c          = peek(lx);

    if (c == '\0') {
        Token t;
        t.type   = TOK_EOF;
        t.lexeme = NULL;
        t.number = 0.0;
        t.line   = start_line;
        t.column = start_col;
        return t;
    }

    /* ============================================================
     * SIMBOLOS COM $: $+ $> $>|
     * ============================================================ */
    if (c == '$') {
        char n1 = peek_next(lx);
        if (n1 == '+') {
            advance(lx); advance(lx);
            return make_token(TOK_TERMINATOR, "$+", 2, start_line, start_col);
        }
        if (n1 == '>') {
            advance(lx); advance(lx);
            /* Verifica se é $>| (for each) */
            if (peek(lx) == '|') {
                advance(lx);
                return make_token(TOK_FOR_EACH, "$>|", 3, start_line, start_col);
            }
            return make_token(TOK_FOR, "$>", 2, start_line, start_col);
        }
        /* $ sozinho — erro */
        advance(lx);
        return make_token(TOK_UNKNOWN, "$", 1, start_line, start_col);
    }

    /* ============================================================
     * IDENTIFICADORES E PALAVRAS-CHAVE
     * ============================================================ */
    if (is_ident_start(c)) {
        size_t start = lx->pos;
        while (is_ident_char(peek(lx))) {
            advance(lx);
        }
        size_t len = lx->pos - start;
        char  *buf = (char *)malloc(len + 1);
        memcpy(buf, lx->src + start, len);
        buf[len] = '\0';

        TokenType type = keyword_type(buf);

        /* Operadores lógicos JSC$+: a+, o+, n+ */
        if (type == TOK_IDENT && len == 1) {
            char next = peek(lx);
            if (next == '+') {
                if (buf[0] == 'a') { advance(lx); free(buf); return make_token(TOK_AND, "a+", 2, start_line, start_col); }
                if (buf[0] == 'o') { advance(lx); free(buf); return make_token(TOK_OR,  "o+", 2, start_line, start_col); }
                if (buf[0] == 'n') { advance(lx); free(buf); return make_token(TOK_NOT, "n+", 2, start_line, start_col); }
            }
        }

        Token t = make_token(type, buf, len, start_line, start_col);
        free(buf);
        return t;
    }

    /* ============================================================
     * NÚMEROS
     * ============================================================ */
    if (isdigit((unsigned char)c)) {
        size_t start = lx->pos;
        while (isdigit((unsigned char)peek(lx))) advance(lx);
        if (peek(lx) == '.' && isdigit((unsigned char)peek_next(lx))) {
            advance(lx);
            while (isdigit((unsigned char)peek(lx))) advance(lx);
        }
        size_t len = lx->pos - start;
        Token t = make_token(TOK_NUMBER, lx->src + start, len, start_line, start_col);
        t.number = atof(t.lexeme);
        return t;
    }

    /* ============================================================
     * STRINGS
     * ============================================================ */
    if (c == '"' || c == '\'') {
        char quote = c;
        advance(lx);

        /* Constroi a string processando escapes */
        size_t cap = 64;
        size_t len = 0;
        char *buf = (char *)malloc(cap);

        while (peek(lx) != '\0' && peek(lx) != quote) {
            char ch = peek(lx);

            if (ch == '\\') {
                advance(lx);  /* consome a barra */
                char esc = peek(lx);
                switch (esc) {
                    case 'n':  ch = '\n'; break;
                    case 't':  ch = '\t'; break;
                    case 'r':  ch = '\r'; break;
                    case '\\': ch = '\\'; break;
                    case '"':  ch = '"';  break;
                    case '\'': ch = '\''; break;
                    default:   ch = esc;  break;
                }
                advance(lx);  /* consome o caractere de escape */
            } else {
                advance(lx);  /* consome o caractere normal */
            }

            if (len + 1 >= cap) {
                cap *= 2;
                buf = (char *)realloc(buf, cap);
            }
            buf[len++] = ch;
        }
        buf[len] = '\0';

        Token t = make_token(TOK_STRING, buf, len, start_line, start_col);
        free(buf);
        if (peek(lx) == quote) advance(lx);
        return t;
    }

    /* ============================================================
     * OPERADORES E PONTUAÇÃO
     * ============================================================ */
    switch (c) {
        case '+':
            advance(lx);
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_PLUS_EQ, "+=", 2, start_line, start_col); }
            return make_token(TOK_PLUS, "+", 1, start_line, start_col);
        case '-':
            advance(lx);
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_MINUS_EQ, "-=", 2, start_line, start_col); }
            return make_token(TOK_MINUS, "-", 1, start_line, start_col);
        case '*':
            advance(lx);
            if (peek(lx) == '*') { advance(lx); return make_token(TOK_POW, "**", 2, start_line, start_col); }
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_STAR_EQ, "*=", 2, start_line, start_col); }
            return make_token(TOK_STAR, "*", 1, start_line, start_col);
        case '/':
            advance(lx);
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_SLASH_EQ, "/=", 2, start_line, start_col); }
            return make_token(TOK_SLASH, "/", 1, start_line, start_col);
        case '%':
            advance(lx);
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_PERCENT_EQ, "%=", 2, start_line, start_col); }
            return make_token(TOK_PERCENT, "%", 1, start_line, start_col);

        case '=':
            advance(lx);
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_EQ, "==", 2, start_line, start_col); }
            return make_token(TOK_ASSIGN, "=", 1, start_line, start_col);
        case '!':
            advance(lx);
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_NEQ, "!=", 2, start_line, start_col); }
            if (peek(lx) == '!') { advance(lx); return make_token(TOK_TRY_OP, "!!", 2, start_line, start_col); }
            return make_token(TOK_UNKNOWN, "!", 1, start_line, start_col);
        case '<':
            advance(lx);
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_LTE, "<=", 2, start_line, start_col); }
            if (peek(lx) == '<') { advance(lx); return make_token(TOK_SHL, "<<", 2, start_line, start_col); }
            return make_token(TOK_LT, "<", 1, start_line, start_col);
        case '>':
            advance(lx);
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_GTE, ">=", 2, start_line, start_col); }
            if (peek(lx) == '>') { advance(lx); return make_token(TOK_SHR, ">>", 2, start_line, start_col); }
            return make_token(TOK_GT, ">", 1, start_line, start_col);

        case '&':
            advance(lx);
            return make_token(TOK_BIT_AND, "&", 1, start_line, start_col);
        case '|':
            advance(lx);
            return make_token(TOK_BIT_OR, "|", 1, start_line, start_col);
        case '^':
            advance(lx);
            return make_token(TOK_BIT_XOR, "^", 1, start_line, start_col);
        /* Pipe: ~> ou finally: ~~ ou bit-not: ~ */
        case '~':
            advance(lx);
            if (peek(lx) == '>') {
                advance(lx);
                return make_token(TOK_PIPE, "~>", 2, start_line, start_col);
            }
            if (peek(lx) == '~') {
                advance(lx);
                return make_token(TOK_FINALLY_OP, "~~", 2, start_line, start_col);
            }
            return make_token(TOK_BIT_NOT, "~", 1, start_line, start_col);

        /* Retorno curto: #> */
        case '#':
            advance(lx);
            if (peek(lx) == '>') {
                advance(lx);
                return make_token(TOK_RETURN_SHORT, "#>", 2, start_line, start_col);
            }
            return make_token(TOK_UNKNOWN, "#", 1, start_line, start_col);

        case '.':
            advance(lx);
            if (peek(lx) == '.') { advance(lx); return make_token(TOK_RANGE, "..", 2, start_line, start_col); }
            return make_token(TOK_DOT, ".", 1, start_line, start_col);

        case '?':
            advance(lx);
            if (peek(lx) == '?') { advance(lx); return make_token(TOK_CATCH_OP, "??", 2, start_line, start_col); }
            return make_token(TOK_QUESTION, "?", 1, start_line, start_col);
        case ':':
            advance(lx);
            return make_token(TOK_COLON, ":", 1, start_line, start_col);

        case '(': advance(lx); return make_token(TOK_LPAREN,   "(", 1, start_line, start_col);
        case ')': advance(lx); return make_token(TOK_RPAREN,   ")", 1, start_line, start_col);
        case '{': advance(lx); return make_token(TOK_LBRACE,   "{", 1, start_line, start_col);
        case '}': advance(lx); return make_token(TOK_RBRACE,   "}", 1, start_line, start_col);
        case '[': advance(lx); return make_token(TOK_LBRACKET, "[", 1, start_line, start_col);
        case ']': advance(lx); return make_token(TOK_RBRACKET, "]", 1, start_line, start_col);
        case ',': advance(lx); return make_token(TOK_COMMA,    ",", 1, start_line, start_col);
    }

    advance(lx);
    return make_token(TOK_UNKNOWN, &c, 1, start_line, start_col);
}

void token_free(Token *t) {
    if (t->lexeme) {
        free(t->lexeme);
        t->lexeme = NULL;
    }
}

const char *token_type_name(TokenType t) {
    switch (t) {
        case TOK_EOF:          return "EOF";
        case TOK_TERMINATOR:   return "TERMINATOR($+)";
        case TOK_IDENT:        return "IDENT";
        case TOK_NUMBER:       return "NUMBER";
        case TOK_STRING:       return "STRING";

        /* Palavras-chave v3.0 */
        case TOK_ENQ:          return "ENQ";
        case TOK_GWB:          return "GWB";
        case TOK_BASE:         return "BASE";
        case TOK_FN_PLUS:      return "FN_PLUS";
        case TOK_EXTENDS:      return "EXTENDS";
        case TOK_THIS:         return "THIS";
        case TOK_IN:           return "IN";
        case TOK_BREAK:        return "BREAK";
        case TOK_CONTINUE:     return "CONTINUE";
        case TOK_TRUE:         return "TRUE";
        case TOK_FALSE:        return "FALSE";
        case TOK_VAZIO:        return "VAZIO";

        /* Simbolos novos v3.0 */
        case TOK_FOR:          return "FOR($>)";
        case TOK_FOR_EACH:     return "FOR_EACH($>|)";
        case TOK_PIPE:         return "PIPE(~>)";
        case TOK_RETURN_SHORT: return "RETURN_SHORT(#>)";
        case TOK_JSC:          return "JSC";
        case TOK_PRINTJ:       return "PRINTJ";
        case TOK_INPUT:        return "INPUT";
        case TOK_SPAWN:        return "SPAWN";
        case TOK_IMPORT:       return "IMPORT";

        /* Aritmeticos */
        case TOK_PLUS:         return "PLUS";
        case TOK_MINUS:        return "MINUS";
        case TOK_STAR:         return "STAR";
        case TOK_SLASH:        return "SLASH";
        case TOK_PERCENT:      return "PERCENT";
        case TOK_POW:          return "POW";

        /* Comparacao */
        case TOK_EQ:           return "EQ";
        case TOK_NEQ:          return "NEQ";
        case TOK_LT:           return "LT";
        case TOK_GT:           return "GT";
        case TOK_LTE:          return "LTE";
        case TOK_GTE:          return "GTE";

        /* Logicos */
        case TOK_AND:          return "AND(a+)";
        case TOK_OR:           return "OR(o+)";
        case TOK_NOT:          return "NOT(n+)";

        /* Bitwise */
        case TOK_BIT_AND:      return "BIT_AND";
        case TOK_BIT_OR:       return "BIT_OR";
        case TOK_BIT_XOR:      return "BIT_XOR";
        case TOK_BIT_NOT:      return "BIT_NOT";
        case TOK_SHL:          return "SHL";
        case TOK_SHR:          return "SHR";

        /* Atribuicao */
        case TOK_ASSIGN:       return "ASSIGN";
        case TOK_PLUS_EQ:      return "PLUS_EQ";
        case TOK_MINUS_EQ:     return "MINUS_EQ";
        case TOK_STAR_EQ:      return "STAR_EQ";
        case TOK_SLASH_EQ:     return "SLASH_EQ";
        case TOK_PERCENT_EQ:   return "PERCENT_EQ";

        /* Range */
        case TOK_RANGE:        return "RANGE(..)";

        /* Ternario */
        case TOK_QUESTION:     return "QUESTION";
        case TOK_COLON:        return "COLON";

        /* Pontuacao */
        case TOK_LPAREN:       return "LPAREN";
        case TOK_RPAREN:       return "RPAREN";
        case TOK_LBRACE:       return "LBRACE";
        case TOK_RBRACE:       return "RBRACE";
        case TOK_LBRACKET:     return "LBRACKET";
        case TOK_RBRACKET:     return "RBRACKET";
        case TOK_COMMA:        return "COMMA";
        case TOK_DOT:          return "DOT";

        /* Excecao v3.0 */
        case TOK_TRY_OP:       return "TRY(!!)";
        case TOK_CATCH_OP:     return "CATCH(??)";
        case TOK_FINALLY_OP:   return "FINALLY(~~)";

        case TOK_UNKNOWN:      return "UNKNOWN";
    }
    return "???";
}

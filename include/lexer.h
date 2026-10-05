#ifndef JSC_LEXER_H
#define JSC_LEXER_H

#include <stddef.h>

/* ============================================================
 * JSC$+ — Lexer
 * Autor: Phelipe Gabriel Mendes Silva
 * Linguagem: JSC$+ (JusSec)
 * Terminador: $+
 * Comentário: @
 * Saída: printj
 * ============================================================ */

typedef enum {
    TOK_EOF = 0,
    TOK_TERMINATOR,     /* $+  */
    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,

    /* Palavras-chave v3.0 */
    TOK_ENQ,            /* while */
    TOK_GWB,            /* return */
    TOK_BASE,           /* class */
    TOK_FN_PLUS,        /* fn+ */
    TOK_EXTENDS,        /* extends */
    TOK_THIS,           /* this */
    TOK_IN,             /* in */
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_TRUE,
    TOK_FALSE,
    TOK_VAZIO,          /* null */

    /* Simbolos novos v3.0 */
    TOK_FOR,            /* $>   (for com range) */
    TOK_FOR_EACH,       /* $>|  (for each) */
    TOK_PIPE,           /* ~>   (pipe) */
    TOK_RETURN_SHORT,   /* #>   (retorno curto) */
    TOK_JSC,            /* jsc modulo$+ */
    TOK_PRINTJ,         /* printj */
    TOK_INPUT,          /* input */
    TOK_SPAWN,          /* spawn */
    TOK_IMPORT,         /* import */

    /* Aritmeticos */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT, TOK_POW,

    /* Comparacao */
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LTE, TOK_GTE,

    /* Logicos JSC$+ */
    TOK_AND, TOK_OR, TOK_NOT,

    /* Bitwise */
    TOK_BIT_AND, TOK_BIT_OR, TOK_BIT_XOR, TOK_BIT_NOT,
    TOK_SHL, TOK_SHR,

    /* Atribuicao */
    TOK_ASSIGN, TOK_PLUS_EQ, TOK_MINUS_EQ, TOK_STAR_EQ,
    TOK_SLASH_EQ, TOK_PERCENT_EQ,

    /* Range */
    TOK_RANGE,

    /* Ternario */
    TOK_QUESTION, TOK_COLON,

    /* Pontuacao */
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_DOT,

    /* Excecao v3.0 */
    TOK_TRY_OP,         /* !!  */
    TOK_CATCH_OP,       /* ??  */
    TOK_FINALLY_OP,     /* ~~  */

    TOK_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char     *lexeme;
    double    number;
    int       line;
    int       column;
} Token;

typedef struct {
    const char *src;
    size_t      pos;
    int         line;
    int         column;
} Lexer;

void  lexer_init(Lexer *lx, const char *src);
Token lexer_next(Lexer *lx);
void  token_free(Token *t);
const char *token_type_name(TokenType t);

#endif /* JSC_LEXER_H */

#ifndef JSC_PARSER_H
#define JSC_PARSER_H

#include "lexer.h"
#include "ast.h"

/* ============================================================
 * JSC$+ — Parser
 * Converte lista de tokens em AST
 * ============================================================ */

typedef struct {
    Lexer   lexer;
    Token   current;    /* token atual (lookahead de 1) */
    Token   previous;   /* token anterior (pra debug) */
    int     had_error;  /* flag de erro */
    const char *filename;
} Parser;

/* Inicializa o parser com o código-fonte */
void  parser_init(Parser *p, const char *src, const char *filename);

/* Avança pro próximo token */
void  parser_advance(Parser *p);

/* Verifica se o token atual é do tipo T */
int   parser_check(Parser *p, TokenType type);

/* Consome o token se for do tipo T, senão erro */
int   parser_match(Parser *p, TokenType type);

/* Exige um token do tipo T, senão erro fatal */
Token parser_expect(Parser *p, TokenType type, const char *msg);

/* Reporta erro */
void  parser_error(Parser *p, const char *msg);
void  parser_optional_terminator(Parser *p);

/* -------- Entrada principal -------- */

/* Parse de um programa completo */
Node *parser_parse_program(Parser *p);

/* -------- Comandos -------- */

/* Despacha pro comando certo baseado no token atual */
Node *parser_parse_statement(Parser *p);

/* Bloco: { cmd cmd cmd } */
Node *parser_parse_block(Parser *p);

/* printj(expr)$+ */
Node *parser_parse_printj(Parser *p);

/* nome = expr$+   ou   tipo nome = expr$+ */
Node *parser_parse_var_decl_or_assign(Parser *p);
Node *parser_parse_this_statement(Parser *p);

/* if cond { } else { }$+ */
Node *parser_parse_if(Parser *p);

/* while cond { }$+ */
Node *parser_parse_while(Parser *p);

/* for var in a..b { }$+ */
Node *parser_parse_for(Parser *p);

/* return expr$+ */
Node *parser_parse_return(Parser *p);

/* function nome(args) { }$+ */
Node *parser_parse_function(Parser *p);

/* class Nome { }$+ */
Node *parser_parse_class(Parser *p);

/* jsc modulo$+ */
Node *parser_parse_import(Parser *p);
Node *parser_parse_try(Parser *p);
/* -------- Expressões (camadas de precedência) -------- */

/* Nível mais alto (chamada externa) */
Node *parser_parse_expression(Parser *p);

/* Níveis internos — cada um lida com uma precedência */
Node *parser_parse_ternary(Parser *p);
Node *parser_parse_or(Parser *p);
Node *parser_parse_and(Parser *p);
Node *parser_parse_equality(Parser *p);
Node *parser_parse_comparison(Parser *p);
Node *parser_parse_bit_or(Parser *p);
Node *parser_parse_bit_xor(Parser *p);
Node *parser_parse_bit_and(Parser *p);
Node *parser_parse_shift(Parser *p);
Node *parser_parse_term(Parser *p);       /* + - */
Node *parser_parse_factor(Parser *p);     /* * / % */
Node *parser_parse_unary(Parser *p);      /* - n+ ~ ! */
Node *parser_parse_power(Parser *p);      /* ** */
Node *parser_parse_postfix(Parser *p);    /* chamada, index, field */
Node *parser_parse_primary(Parser *p);    /* número, string, ident, (expr) */

#endif /* JSC_PARSER_H */

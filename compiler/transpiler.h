#ifndef JSC_TRANSPILER_H
#define JSC_TRANSPILER_H

#include "ast.h"
#include <stdio.h>
#include <stdlib.h>

/* ============================================================
 * JSC$+ Transpilador
 * Converte AST pra codigo C.
 * ============================================================ */

typedef struct {
    /* Parametros ativos (sao variaveis C diretas) */
    char  *params[32];
    int    n_params;
    int    em_metodo;  /* 1 = dentro de metodo de classe */
    
    /* Classes conhecidas */
    char  *classes[64];
    char  *pais[64];
    int    n_classes;
    
    NodeList prog_statements;
    NodeList declared_vars;  /* statements do programa */
    char  *buf;       /* buffer de saida */
    size_t len;       /* tamanho atual */
    size_t cap;       /* capacidade */
    int    indent;    /* nivel de indentacao */
    int    tmp_id;    /* contador de temporarios */
    char  **strings;  /* pool de strings */
    int    n_strings;
} Transpiler;

/* Ciclo de vida */
Transpiler *transpiler_new(void);
void        transpiler_free(Transpiler *t);

/* Funcoes principais */
void transpile_program(Transpiler *t, Node *prog);
void transpile_statement(Transpiler *t, Node *node);
void transpile_block(Transpiler *t, Node *block);
void transpile_func_decl(Transpiler *t, Node *node);
void transpile_class_decl(Transpiler *t, Node *node);
void transpile_expression(Transpiler *t, Node *node);

/* Helpers */
void tp_append(Transpiler *t, const char *s);
void tp_appendf(Transpiler *t, const char *fmt, ...);
void tp_indent(Transpiler *t);
void tp_newline(Transpiler *t);

#endif

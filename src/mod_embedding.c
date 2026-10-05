/* ============================================================
 * JSC$+ — modulo embedding
 * Converte tokens (IDs) em vetores densos
 * ============================================================ */

#include "eval.h"
#include "value.h"
#include "env.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ============================================================
 * Estado global (uma tabela de embedding por processo)
 * ============================================================ */

typedef struct {
    int vocab_size;    /* numero de tokens */
    int dim;           /* dimensao do embedding */
    double *weights;   /* [vocab_size, dim] */
    int inicializado;
} EmbeddingTable;

static EmbeddingTable g_emb = {0, 0, NULL, 0};

/* Helper: erro */
static void emb_error(EvalState *state, const char *msg) {
    if (state) {
        snprintf(state->error_message, sizeof(state->error_message), "%s", msg);
        state->had_error = 1;
    }
    fprintf(stderr, "JSC$+ erro: %s\n", msg);
}

/* Inicializa tabela com valores aleatorios pequenos */
static void emb_init(int vocab_size, int dim) {
    if (g_emb.weights) free(g_emb.weights);

    g_emb.vocab_size = vocab_size;
    g_emb.dim = dim;
    g_emb.weights = (double *)malloc(sizeof(double) * vocab_size * dim);

    if (!g_emb.weights) return;

    /* Xavier initialization: sqrt(1/dim) */
    double scale = sqrt(1.0 / dim);

    for (int i = 0; i < vocab_size * dim; i++) {
        g_emb.weights[i] = ((double)rand() / RAND_MAX) * 2.0 * scale - scale;
    }

    g_emb.inicializado = 1;
}

/* ============================================================
 * API JSC$+
 * ============================================================ */

/* embedding.init(vocab_size, dim) — cria tabela */
JscValue *embedding_init(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        emb_error(state, "embedding.init() espera 2 argumentos (vocab_size, dim)");
        return NULL;
    }

    JscValue *v = eval(n->args.items[0], env, state);
    JscValue *d = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    int vocab_size = (int)jsc_value_to_int(v);
    int dim = (int)jsc_value_to_int(d);
    jsc_value_free(v);
    jsc_value_free(d);

    emb_init(vocab_size, dim);
    return jsc_vazio();
}

/* embedding.encode(tokens) — [N] -> [N, dim] */
JscValue *embedding_encode(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (!g_emb.inicializado) {
        emb_error(state, "embedding nao inicializado. Chame embedding.init() primeiro");
        return NULL;
    }

    if (n->args.count != 1) {
        emb_error(state, "embedding.encode() espera 1 argumento");
        return NULL;
    }

    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    if (v->type != VAL_ARRAY) {
        jsc_value_free(v);
        emb_error(state, "embedding.encode() espera array de IDs");
        return NULL;
    }

    int n_tokens = (int)v->as.array.count;
    int dim = g_emb.dim;

    /* Cria matriz [n_tokens, dim] */
    JscValue *map = jsc_map();
    jsc_map_set(map, jsc_string("rows"), jsc_int(n_tokens));
    jsc_map_set(map, jsc_string("cols"), jsc_int(dim));

    JscValue *data = jsc_array();

    for (int i = 0; i < n_tokens; i++) {
        JscValue *tok = v->as.array.items[i];
        int id = (int)jsc_value_to_int(tok);

        if (id < 0 || id >= g_emb.vocab_size) {
            id = 1;  /* <UNK> */
        }

        for (int j = 0; j < dim; j++) {
            jsc_array_push(data, jsc_float(g_emb.weights[id * dim + j]));
        }
    }

    jsc_map_set(map, jsc_string("data"), data);
    jsc_value_free(v);
    return map;
}

/* embedding.vocab_size() — retorna tamanho do vocab */
JscValue *embedding_vocab_size(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    return jsc_int(g_emb.vocab_size);
}

/* embedding.dim() — retorna dimensao */
JscValue *embedding_dim(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    return jsc_int(g_emb.dim);
}

/* embedding.get(id) — retorna vetor de um token especifico */
JscValue *embedding_get(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (!g_emb.inicializado) {
        emb_error(state, "embedding nao inicializado");
        return NULL;
    }

    if (n->args.count != 1) {
        emb_error(state, "embedding.get() espera 1 argumento");
        return NULL;
    }

    JscValue *v = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    int id = (int)jsc_value_to_int(v);
    jsc_value_free(v);

    if (id < 0 || id >= g_emb.vocab_size) id = 1;

    JscValue *arr = jsc_array();
    for (int j = 0; j < g_emb.dim; j++) {
        jsc_array_push(arr, jsc_float(g_emb.weights[id * g_emb.dim + j]));
    }
    return arr;
}

/* embedding.print_info() — imprime info da tabela */
JscValue *embedding_print_info(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    printf("embedding: vocab_size=%d dim=%d\n", g_emb.vocab_size, g_emb.dim);
    if (g_emb.inicializado) {
        printf("  inicializado: sim\n");
    } else {
        printf("  inicializado: nao\n");
    }
    return jsc_vazio();
}

/* ============================================================
 * Registro
 * ============================================================ */

JscValue *make_embedding_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("init"),       jsc_native("init",       embedding_init));
    jsc_map_set(m, jsc_string("encode"),     jsc_native("encode",     embedding_encode));
    jsc_map_set(m, jsc_string("vocab_size"), jsc_native("vocab_size", embedding_vocab_size));
    jsc_map_set(m, jsc_string("dim"),        jsc_native("dim",        embedding_dim));
    jsc_map_set(m, jsc_string("get"),        jsc_native("get",        embedding_get));
    jsc_map_set(m, jsc_string("print_info"), jsc_native("print_info", embedding_print_info));
    return m;
}

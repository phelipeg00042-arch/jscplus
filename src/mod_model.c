/* ============================================================
 * JSC$+ — modulo model
 * Modelo completo (N blocos transformer + output layer)
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

static void mdl_error(EvalState *state, const char *msg) {
    if (state) {
        snprintf(state->error_message, sizeof(state->error_message), "%s", msg);
        state->had_error = 1;
    }
    fprintf(stderr, "JSC$+ erro: %s\n", msg);
}

typedef struct { int rows, cols; double *data; } Matriz;

static Matriz mat_from_val(JscValue *v) {
    Matriz m = {0, 0, NULL};
    if (!v || v->type != VAL_MAP) return m;
    JscValue *r = jsc_map_get(v, jsc_string("rows"));
    JscValue *c = jsc_map_get(v, jsc_string("cols"));
    JscValue *d = jsc_map_get(v, jsc_string("data"));
    if (!r || !c || !d || d->type != VAL_ARRAY) return m;
    m.rows = (int)jsc_value_to_int(r);
    m.cols = (int)jsc_value_to_int(c);
    m.data = malloc(sizeof(double) * m.rows * m.cols);
    for (int i = 0; i < m.rows * m.cols; i++) {
        m.data[i] = jsc_value_to_float(d->as.array.items[i]);
    }
    return m;
}

static JscValue *mat_to_val(Matriz m) {
    JscValue *map = jsc_map();
    jsc_map_set(map, jsc_string("rows"), jsc_int(m.rows));
    jsc_map_set(map, jsc_string("cols"), jsc_int(m.cols));
    JscValue *arr = jsc_array();
    for (int i = 0; i < m.rows * m.cols; i++) {
        jsc_array_push(arr, jsc_float(m.data[i]));
    }
    jsc_map_set(map, jsc_string("data"), arr);
    return map;
}

/* ============================================================
 * Output layer (projecao pro vocab)
 * ============================================================ */

static int g_out_dim = 0;
static int g_vocab_size = 0;
static double *g_W_out = NULL;

/* model.init_output(dim, vocab_size) */
JscValue *mdl_init_output(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        mdl_error(state, "model.init_output() espera (dim, vocab_size)");
        return NULL;
    }

    JscValue *dv = eval(n->args.items[0], env, state);
    JscValue *vv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    int dim = (int)jsc_value_to_int(dv);
    int vocab = (int)jsc_value_to_int(vv);
    jsc_value_free(dv); jsc_value_free(vv);

    if (g_W_out) free(g_W_out);

    g_out_dim = dim;
    g_vocab_size = vocab;
    g_W_out = malloc(sizeof(double) * dim * vocab);

    double scale = sqrt(2.0 / dim);
    srand(time(NULL));
    for (int i = 0; i < dim * vocab; i++) {
        g_W_out[i] = ((double)rand() / RAND_MAX) * 2.0 * scale - scale;
    }

    return jsc_vazio();
}

/* model.output(X) — projeta X [n, dim] -> logits [n, vocab] */
JscValue *mdl_output(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (!g_W_out) {
        mdl_error(state, "output layer nao inicializada");
        return NULL;
    }

    if (n->args.count != 1) {
        mdl_error(state, "model.output() espera 1 argumento");
        return NULL;
    }

    JscValue *xv = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz X = mat_from_val(xv);
    jsc_value_free(xv);

    if (!X.data || X.cols != g_out_dim) {
        mdl_error(state, "model.output(): X.cols deve ser dim");
        free(X.data);
        return NULL;
    }

    int n_tokens = X.rows;
    Matriz logits = {n_tokens, g_vocab_size, NULL};
    logits.data = malloc(sizeof(double) * n_tokens * g_vocab_size);

    for (int i = 0; i < n_tokens; i++) {
        for (int j = 0; j < g_vocab_size; j++) {
            double soma = 0.0;
            for (int k = 0; k < g_out_dim; k++) {
                soma += X.data[i * g_out_dim + k] * g_W_out[k * g_vocab_size + j];
            }
            logits.data[i * g_vocab_size + j] = soma;
        }
    }

    JscValue *res = mat_to_val(logits);
    free(X.data); free(logits.data);
    return res;
}

/* model.softmax_last(X) — softmax na ultima dimensao (para logits) */
JscValue *mdl_softmax_last(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        mdl_error(state, "model.softmax_last() espera 1 argumento");
        return NULL;
    }

    JscValue *xv = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz X = mat_from_val(xv);
    jsc_value_free(xv);

    if (!X.data) {
        mdl_error(state, "model.softmax_last(): argumento invalido");
        return NULL;
    }

    Matriz out = {X.rows, X.cols, NULL};
    out.data = malloc(sizeof(double) * X.rows * X.cols);

    for (int i = 0; i < X.rows; i++) {
        double max = X.data[i * X.cols];
        for (int j = 1; j < X.cols; j++)
            if (X.data[i * X.cols + j] > max) max = X.data[i * X.cols + j];
        double total = 0.0;
        for (int j = 0; j < X.cols; j++) {
            out.data[i * X.cols + j] = exp(X.data[i * X.cols + j] - max);
            total += out.data[i * X.cols + j];
        }
        for (int j = 0; j < X.cols; j++) out.data[i * X.cols + j] /= total;
    }

    JscValue *res = mat_to_val(out);
    free(X.data); free(out.data);
    return res;
}

/* model.argmax(X) — retorna array com o indice maximo de cada linha */
JscValue *mdl_argmax(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        mdl_error(state, "model.argmax() espera 1 argumento");
        return NULL;
    }

    JscValue *xv = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz X = mat_from_val(xv);
    jsc_value_free(xv);

    if (!X.data) {
        mdl_error(state, "model.argmax(): argumento invalido");
        return NULL;
    }

    JscValue *arr = jsc_array();
    for (int i = 0; i < X.rows; i++) {
        int idx = 0;
        double max = X.data[i * X.cols];
        for (int j = 1; j < X.cols; j++) {
            if (X.data[i * X.cols + j] > max) {
                max = X.data[i * X.cols + j];
                idx = j;
            }
        }
        jsc_array_push(arr, jsc_int(idx));
    }

    free(X.data);
    return arr;
}

/* model.info() */
JscValue *mdl_info(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    printf("model: dim=%d vocab_size=%d\n", g_out_dim, g_vocab_size);
    return jsc_vazio();
}

JscValue *make_model_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("init_output"), jsc_native("init_output", mdl_init_output));
    jsc_map_set(m, jsc_string("output"),      jsc_native("output",      mdl_output));
    jsc_map_set(m, jsc_string("softmax_last"),jsc_native("softmax_last",mdl_softmax_last));
    jsc_map_set(m, jsc_string("argmax"),      jsc_native("argmax",      mdl_argmax));
    jsc_map_set(m, jsc_string("info"),        jsc_native("info",        mdl_info));
    return m;
}

/* ============================================================
 * JSC$+ — modulo attention
 * Self-attention (scaled dot-product) para transformer
 * ============================================================ */

#include "eval.h"
#include "value.h"
#include "env.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Helper: erro */
static void att_error(EvalState *state, const char *msg) {
    if (state) {
        snprintf(state->error_message, sizeof(state->error_message), "%s", msg);
        state->had_error = 1;
    }
    fprintf(stderr, "JSC$+ erro: %s\n", msg);
}

/* Extrai matriz de map JSC$+ */
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
    int n = m.rows * m.cols;

    m.data = malloc(sizeof(double) * n);
    for (int i = 0; i < n; i++) {
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
 * Attention
 * ============================================================ */

/* attention.forward(Q, K, V) — scaled dot-product attention
 * Q: [n_q, d_k]
 * K: [n_k, d_k]
 * V: [n_k, d_v]
 * Retorna: [n_q, d_v]
 */
JscValue *att_forward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 3) {
        att_error(state, "attention.forward() espera 3 argumentos (Q, K, V)");
        return NULL;
    }

    JscValue *qv = eval(n->args.items[0], env, state);
    JscValue *kv = eval(n->args.items[1], env, state);
    JscValue *vv = eval(n->args.items[2], env, state);
    if (state->had_error) return NULL;

    Matriz Q = mat_from_val(qv);
    Matriz K = mat_from_val(kv);
    Matriz V = mat_from_val(vv);
    jsc_value_free(qv);
    jsc_value_free(kv);
    jsc_value_free(vv);

    if (!Q.data || !K.data || !V.data) {
        att_error(state, "attention.forward(): argumentos invalidos");
        free(Q.data); free(K.data); free(V.data);
        return NULL;
    }

    if (Q.cols != K.cols) {
        att_error(state, "attention: Q.cols deve ser igual a K.cols");
        free(Q.data); free(K.data); free(V.data);
        return NULL;
    }

    if (K.rows != V.rows) {
        att_error(state, "attention: K.rows deve ser igual a V.rows");
        free(Q.data); free(K.data); free(V.data);
        return NULL;
    }

    int n_q = Q.rows;
    int n_k = K.rows;
    int d_k = Q.cols;
    int d_v = V.cols;

    /* 1) scores = Q @ K^T / sqrt(d_k)  [n_q, n_k] */
    double scale = 1.0 / sqrt((double)d_k);

    double *scores = malloc(sizeof(double) * n_q * n_k);
    for (int i = 0; i < n_q; i++) {
        for (int j = 0; j < n_k; j++) {
            double soma = 0.0;
            for (int k = 0; k < d_k; k++) {
                soma += Q.data[i * d_k + k] * K.data[j * d_k + k];
            }
            scores[i * n_k + j] = soma * scale;
        }
    }

    /* 2) softmax por linha */
    for (int i = 0; i < n_q; i++) {
        double max = scores[i * n_k];
        for (int j = 1; j < n_k; j++) {
            if (scores[i * n_k + j] > max) max = scores[i * n_k + j];
        }
        double soma = 0.0;
        for (int j = 0; j < n_k; j++) {
            scores[i * n_k + j] = exp(scores[i * n_k + j] - max);
            soma += scores[i * n_k + j];
        }
        for (int j = 0; j < n_k; j++) {
            scores[i * n_k + j] /= soma;
        }
    }

    /* 3) output = scores @ V  [n_q, d_v] */
    Matriz out = {n_q, d_v, NULL};
    out.data = malloc(sizeof(double) * n_q * d_v);
    for (int i = 0; i < n_q; i++) {
        for (int j = 0; j < d_v; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_k; k++) {
                soma += scores[i * n_k + k] * V.data[k * d_v + j];
            }
            out.data[i * d_v + j] = soma;
        }
    }

    JscValue *res = mat_to_val(out);

    free(Q.data); free(K.data); free(V.data);
    free(scores); free(out.data);

    return res;
}

/* attention.scores(Q, K) — retorna so a matriz de scores (pos-softmax) */
JscValue *att_scores(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        att_error(state, "attention.scores() espera 2 argumentos");
        return NULL;
    }

    JscValue *qv = eval(n->args.items[0], env, state);
    JscValue *kv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz Q = mat_from_val(qv);
    Matriz K = mat_from_val(kv);
    jsc_value_free(qv);
    jsc_value_free(kv);

    if (!Q.data || !K.data || Q.cols != K.cols) {
        att_error(state, "attention.scores(): argumentos invalidos");
        free(Q.data); free(K.data);
        return NULL;
    }

    int n_q = Q.rows, n_k = K.rows, d_k = Q.cols;
    double scale = 1.0 / sqrt((double)d_k);

    Matriz s = {n_q, n_k, NULL};
    s.data = malloc(sizeof(double) * n_q * n_k);

    for (int i = 0; i < n_q; i++) {
        for (int j = 0; j < n_k; j++) {
            double soma = 0.0;
            for (int k = 0; k < d_k; k++) {
                soma += Q.data[i * d_k + k] * K.data[j * d_k + k];
            }
            s.data[i * n_k + j] = soma * scale;
        }
    }

    /* Softmax */
    for (int i = 0; i < n_q; i++) {
        double max = s.data[i * n_k];
        for (int j = 1; j < n_k; j++) if (s.data[i * n_k + j] > max) max = s.data[i * n_k + j];
        double total = 0.0;
        for (int j = 0; j < n_k; j++) {
            s.data[i * n_k + j] = exp(s.data[i * n_k + j] - max);
            total += s.data[i * n_k + j];
        }
        for (int j = 0; j < n_k; j++) s.data[i * n_k + j] /= total;
    }

    JscValue *res = mat_to_val(s);
    free(Q.data); free(K.data); free(s.data);
    return res;
}

/* attention.project(x, W, b) — projecao linear: x @ W + b
 * x: [n, d_in]
 * W: [d_in, d_out]
 * b: [d_out] ou omitido
 * Retorna: [n, d_out]
 */
JscValue *att_project(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count < 2 || n->args.count > 3) {
        att_error(state, "attention.project() espera 2-3 argumentos");
        return NULL;
    }

    JscValue *xv = eval(n->args.items[0], env, state);
    JscValue *wv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz X = mat_from_val(xv);
    Matriz W = mat_from_val(wv);
    jsc_value_free(xv);
    jsc_value_free(wv);

    if (!X.data || !W.data || X.cols != W.rows) {
        att_error(state, "attention.project(): dimensoes incompativeis");
        free(X.data); free(W.data);
        return NULL;
    }

    Matriz out = {X.rows, W.cols, NULL};
    out.data = malloc(sizeof(double) * X.rows * W.cols);

    for (int i = 0; i < X.rows; i++) {
        for (int j = 0; j < W.cols; j++) {
            double soma = 0.0;
            for (int k = 0; k < X.cols; k++) {
                soma += X.data[i * X.cols + k] * W.data[k * W.cols + j];
            }
            out.data[i * W.cols + j] = soma;
        }
    }

    JscValue *res = mat_to_val(out);
    free(X.data); free(W.data); free(out.data);
    return res;
}

JscValue *make_attention_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("forward"), jsc_native("forward", att_forward));
    jsc_map_set(m, jsc_string("scores"),  jsc_native("scores",  att_scores));
    jsc_map_set(m, jsc_string("project"), jsc_native("project", att_project));
    return m;
}

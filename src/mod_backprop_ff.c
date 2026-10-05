/* ============================================================
 * JSC$+ — backprop do feed-forward
 * ============================================================ */

#include "eval.h"
#include "value.h"
#include "env.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void bff_error(EvalState *state, const char *msg) {
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
 * ff.backward(X, W1, W2, dY)
 *   X: [n, d_in]
 *   W1: [d_in, hidden]
 *   W2: [hidden, d_out]
 *   dY: [n, d_out]  (gradiente da loss em relacao ao output)
 *
 * Retorna: {"dW1": ..., "dW2": ..., "dX": ...}
 * ============================================================ */

JscValue *ff_backward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 4) {
        bff_error(state, "ff.backward() espera (X, W1, W2, dY)");
        return NULL;
    }

    JscValue *xv = eval(n->args.items[0], env, state);
    JscValue *w1v = eval(n->args.items[1], env, state);
    JscValue *w2v = eval(n->args.items[2], env, state);
    JscValue *dyv = eval(n->args.items[3], env, state);
    if (state->had_error) return NULL;

    Matriz X = mat_from_val(xv);
    Matriz W1 = mat_from_val(w1v);
    Matriz W2 = mat_from_val(w2v);
    Matriz dY = mat_from_val(dyv);
    jsc_value_free(xv); jsc_value_free(w1v);
    jsc_value_free(w2v); jsc_value_free(dyv);

    if (!X.data || !W1.data || !W2.data || !dY.data) {
        bff_error(state, "ff.backward(): argumentos invalidos");
        free(X.data); free(W1.data); free(W2.data); free(dY.data);
        return NULL;
    }

    if (X.cols != W1.rows || W1.cols != W2.rows || W2.cols != dY.cols) {
        bff_error(state, "ff.backward(): dimensoes incompativeis");
        free(X.data); free(W1.data); free(W2.data); free(dY.data);
        return NULL;
    }

    int n_tok = X.rows;
    int d_in = X.cols;
    int hid = W1.cols;
    int d_out = W2.cols;

    /* Recomputa h1 = W1 @ X^T  -> na verdade X @ W1  [n, hid] */
    double *h1 = calloc(n_tok * hid, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < hid; j++) {
            double soma = 0.0;
            for (int k = 0; k < d_in; k++) {
                soma += X.data[i * d_in + k] * W1.data[k * hid + j];
            }
            h1[i * hid + j] = soma;
        }
    }

    /* ReLU: a1 = relu(h1) */
    double *a1 = malloc(sizeof(double) * n_tok * hid);
    double *relu_grad = malloc(sizeof(double) * n_tok * hid);
    for (int i = 0; i < n_tok * hid; i++) {
        if (h1[i] > 0.0) {
            a1[i] = h1[i];
            relu_grad[i] = 1.0;
        } else {
            a1[i] = 0.0;
            relu_grad[i] = 0.0;
        }
    }

    /* dY = dL/d(ff_out). Como ff_out = a1 @ W2, temos:  */
    /* dL/dW2 = a1^T @ dY  [hid, d_out] */
    double *dW2 = calloc(hid * d_out, sizeof(double));
    for (int i = 0; i < hid; i++) {
        for (int j = 0; j < d_out; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += a1[k * hid + i] * dY.data[k * d_out + j];
            }
            dW2[i * d_out + j] = soma;
        }
    }

    /* dL/da1 = dY @ W2^T  [n, hid] */
    double *dA1 = calloc(n_tok * hid, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < hid; j++) {
            double soma = 0.0;
            for (int k = 0; k < d_out; k++) {
                soma += dY.data[i * d_out + k] * W2.data[j * d_out + k];
            }
            dA1[i * hid + j] = soma;
        }
    }

    /* dL/dh1 = dA1 * relu_grad  [n, hid] */
    double *dH1 = malloc(sizeof(double) * n_tok * hid);
    for (int i = 0; i < n_tok * hid; i++) {
        dH1[i] = dA1[i] * relu_grad[i];
    }

    /* dL/dW1 = X^T @ dH1  [d_in, hid] */
    double *dW1 = calloc(d_in * hid, sizeof(double));
    for (int i = 0; i < d_in; i++) {
        for (int j = 0; j < hid; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += X.data[k * d_in + i] * dH1[k * hid + j];
            }
            dW1[i * hid + j] = soma;
        }
    }

    /* dL/dX = dH1 @ W1^T  [n, d_in] */
    double *dX = calloc(n_tok * d_in, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < d_in; j++) {
            double soma = 0.0;
            for (int k = 0; k < hid; k++) {
                soma += dH1[i * hid + k] * W1.data[j * hid + k];
            }
            dX[i * d_in + j] = soma;
        }
    }

    /* Empacota */
    JscValue *map = jsc_map();

    Matriz dW1_m = {d_in, hid, dW1};
    Matriz dW2_m = {hid, d_out, dW2};
    Matriz dX_m  = {n_tok, d_in, dX};

    jsc_map_set(map, jsc_string("dW1"), mat_to_val(dW1_m));
    jsc_map_set(map, jsc_string("dW2"), mat_to_val(dW2_m));
    jsc_map_set(map, jsc_string("dX"),  mat_to_val(dX_m));

    free(X.data); free(W1.data); free(W2.data); free(dY.data);
    free(h1); free(a1); free(relu_grad);
    free(dW1); free(dW2); free(dA1); free(dH1); free(dX);

    return map;
}

JscValue *make_backprop_ff_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("backward"), jsc_native("backward", ff_backward));
    return m;
}

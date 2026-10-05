/* ============================================================
 * JSC$+ — modulo backprop
 * Backpropagation do output layer
 * ============================================================ */

#include "eval.h"
#include "value.h"
#include "env.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void bp_error(EvalState *state, const char *msg) {
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
 * backprop.matmul_tn(A, B) = A^T @ B
 *   A: [n, m]
 *   B: [n, p]
 *   Resultado: [m, p]
 * ============================================================ */

JscValue *bp_matmul_tn(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        bp_error(state, "backprop.matmul_tn() espera (A, B)");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    JscValue *bv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz A = mat_from_val(av);
    Matriz B = mat_from_val(bv);
    jsc_value_free(av); jsc_value_free(bv);

    if (!A.data || !B.data || A.rows != B.rows) {
        bp_error(state, "backprop.matmul_tn(): dimensoes incompativeis");
        free(A.data); free(B.data);
        return NULL;
    }

    Matriz C = {A.cols, B.cols, NULL};
    C.data = calloc(A.cols * B.cols, sizeof(double));

    for (int i = 0; i < A.cols; i++) {
        for (int j = 0; j < B.cols; j++) {
            double soma = 0.0;
            for (int k = 0; k < A.rows; k++) {
                soma += A.data[k * A.cols + i] * B.data[k * B.cols + j];
            }
            C.data[i * B.cols + j] = soma;
        }
    }

    JscValue *res = mat_to_val(C);
    free(A.data); free(B.data); free(C.data);
    return res;
}

/* ============================================================
 * backprop.matmul_nt(A, B) = A @ B^T
 *   A: [n, m]
 *   B: [p, m]
 *   Resultado: [n, p]
 * ============================================================ */

JscValue *bp_matmul_nt(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        bp_error(state, "backprop.matmul_nt() espera (A, B)");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    JscValue *bv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz A = mat_from_val(av);
    Matriz B = mat_from_val(bv);
    jsc_value_free(av); jsc_value_free(bv);

    if (!A.data || !B.data || A.cols != B.cols) {
        bp_error(state, "backprop.matmul_nt(): dimensoes incompativeis");
        free(A.data); free(B.data);
        return NULL;
    }

    Matriz C = {A.rows, B.rows, NULL};
    C.data = calloc(A.rows * B.rows, sizeof(double));

    for (int i = 0; i < A.rows; i++) {
        for (int j = 0; j < B.rows; j++) {
            double soma = 0.0;
            for (int k = 0; k < A.cols; k++) {
                soma += A.data[i * A.cols + k] * B.data[j * B.cols + k];
            }
            C.data[i * B.rows + j] = soma;
        }
    }

    JscValue *res = mat_to_val(C);
    free(A.data); free(B.data); free(C.data);
    return res;
}

/* ============================================================
 * backprop.matmul(A, B) = A @ B
 *   A: [n, m]
 *   B: [m, p]
 *   Resultado: [n, p]
 * ============================================================ */

JscValue *bp_matmul(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        bp_error(state, "backprop.matmul() espera (A, B)");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    JscValue *bv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz A = mat_from_val(av);
    Matriz B = mat_from_val(bv);
    jsc_value_free(av); jsc_value_free(bv);

    if (!A.data || !B.data || A.cols != B.rows) {
        bp_error(state, "backprop.matmul(): dimensoes incompativeis");
        free(A.data); free(B.data);
        return NULL;
    }

    Matriz C = {A.rows, B.cols, NULL};
    C.data = calloc(A.rows * B.cols, sizeof(double));

    for (int i = 0; i < A.rows; i++) {
        for (int j = 0; j < B.cols; j++) {
            double soma = 0.0;
            for (int k = 0; k < A.cols; k++) {
                soma += A.data[i * A.cols + k] * B.data[k * B.cols + j];
            }
            C.data[i * B.cols + j] = soma;
        }
    }

    JscValue *res = mat_to_val(C);
    free(A.data); free(B.data); free(C.data);
    return res;
}

/* ============================================================
 * backprop.grad_output(X, grad_logits, W_out)
 * Calcula gradiente da loss em relacao a:
 *   - W_out:  dL/dW_out = X^T @ grad_logits  [dim, vocab]
 *   - X:      dL/dX = grad_logits @ W_out^T  [n, dim]
 *
 * Retorna map com dois tensores: {"dW": ..., "dX": ...}
 * ============================================================ */

JscValue *bp_grad_output(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 3) {
        bp_error(state, "backprop.grad_output() espera (X, grad_logits, W_out)");
        return NULL;
    }

    JscValue *xv = eval(n->args.items[0], env, state);
    JscValue *gv = eval(n->args.items[1], env, state);
    JscValue *wv = eval(n->args.items[2], env, state);
    if (state->had_error) return NULL;

    Matriz X = mat_from_val(xv);       /* [n, dim] */
    Matriz G = mat_from_val(gv);       /* [n, vocab] */
    Matriz W = mat_from_val(wv);       /* [dim, vocab] */
    jsc_value_free(xv); jsc_value_free(gv); jsc_value_free(wv);

    if (!X.data || !G.data || !W.data) {
        bp_error(state, "backprop.grad_output(): argumentos invalidos");
        free(X.data); free(G.data); free(W.data);
        return NULL;
    }

    if (X.rows != G.rows || X.cols != W.rows || G.cols != W.cols) {
        bp_error(state, "backprop.grad_output(): dimensoes incompativeis");
        free(X.data); free(G.data); free(W.data);
        return NULL;
    }

    int n_tokens = X.rows;
    int dim = X.cols;
    int vocab = W.cols;

    /* dW = X^T @ G  [dim, vocab] */
    Matriz dW = {dim, vocab, NULL};
    dW.data = calloc(dim * vocab, sizeof(double));
    for (int i = 0; i < dim; i++) {
        for (int j = 0; j < vocab; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tokens; k++) {
                soma += X.data[k * dim + i] * G.data[k * vocab + j];
            }
            dW.data[i * vocab + j] = soma;
        }
    }

    /* dX = G @ W^T  [n, dim] */
    Matriz dX = {n_tokens, dim, NULL};
    dX.data = calloc(n_tokens * dim, sizeof(double));
    for (int i = 0; i < n_tokens; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < vocab; k++) {
                soma += G.data[i * vocab + k] * W.data[j * vocab + k];
            }
            dX.data[i * dim + j] = soma;
        }
    }

    /* Empacota em map */
    JscValue *map = jsc_map();
    jsc_map_set(map, jsc_string("dW"), mat_to_val(dW));
    jsc_map_set(map, jsc_string("dX"), mat_to_val(dX));

    free(X.data); free(G.data); free(W.data);
    free(dW.data); free(dX.data);
    return map;
}

JscValue *make_backprop_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("matmul"),     jsc_native("matmul",     bp_matmul));
    jsc_map_set(m, jsc_string("matmul_tn"),  jsc_native("matmul_tn",  bp_matmul_tn));
    jsc_map_set(m, jsc_string("matmul_nt"),  jsc_native("matmul_nt",  bp_matmul_nt));
    jsc_map_set(m, jsc_string("grad_output"),jsc_native("grad_output",bp_grad_output));
    return m;
}

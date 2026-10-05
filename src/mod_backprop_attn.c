/* ============================================================
 * JSC$+ — backprop do attention (1 head)
 * ============================================================ */

#include "eval.h"
#include "value.h"
#include "env.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void batt_error(EvalState *state, const char *msg) {
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
 * attn.backward(X, Wq, Wk, Wv, attn_probs, dOut)
 *   X:        [n, dim]
 *   Wq,Wk,Wv: [dim, dim]
 *   attn_probs: [n, n]  (saida do softmax do forward)
 *   dOut:     [n, dim]  (gradiente da loss em relacao ao output)
 *
 * Retorna: {dWq, dWk, dWv, dX}
 * ============================================================ */

JscValue *attn_backward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 6) {
        batt_error(state, "attn.backward() espera (X, Wq, Wk, Wv, attn_probs, dOut)");
        return NULL;
    }

    JscValue *xv   = eval(n->args.items[0], env, state);
    JscValue *wqv  = eval(n->args.items[1], env, state);
    JscValue *wkv  = eval(n->args.items[2], env, state);
    JscValue *wvv  = eval(n->args.items[3], env, state);
    JscValue *apv  = eval(n->args.items[4], env, state);
    JscValue *dov  = eval(n->args.items[5], env, state);
    if (state->had_error) return NULL;

    Matriz X    = mat_from_val(xv);
    Matriz Wq   = mat_from_val(wqv);
    Matriz Wk   = mat_from_val(wkv);
    Matriz Wv   = mat_from_val(wvv);
    Matriz A    = mat_from_val(apv);   /* attn_probs */
    Matriz dO   = mat_from_val(dov);
    jsc_value_free(xv); jsc_value_free(wqv); jsc_value_free(wkv);
    jsc_value_free(wvv); jsc_value_free(apv); jsc_value_free(dov);

    if (!X.data || !Wq.data || !Wk.data || !Wv.data || !A.data || !dO.data) {
        batt_error(state, "attn.backward(): argumentos invalidos");
        free(X.data); free(Wq.data); free(Wk.data);
        free(Wv.data); free(A.data); free(dO.data);
        return NULL;
    }

    int n_tok = X.rows;
    int dim = X.cols;

    if (Wq.rows != dim || Wq.cols != dim ||
        Wk.rows != dim || Wk.cols != dim ||
        Wv.rows != dim || Wv.cols != dim ||
        A.rows != n_tok || A.cols != n_tok ||
        dO.rows != n_tok || dO.cols != dim) {
        batt_error(state, "attn.backward(): dimensoes incompativeis");
        free(X.data); free(Wq.data); free(Wk.data);
        free(Wv.data); free(A.data); free(dO.data);
        return NULL;
    }

    /* Forward pra pegar Q, K, V */
    double *Q = calloc(n_tok * dim, sizeof(double));
    double *K = calloc(n_tok * dim, sizeof(double));
    double *V = calloc(n_tok * dim, sizeof(double));

    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double sq = 0, sk = 0, sv = 0;
            for (int k = 0; k < dim; k++) {
                double x = X.data[i * dim + k];
                sq += x * Wq.data[k * dim + j];
                sk += x * Wk.data[k * dim + j];
                sv += x * Wv.data[k * dim + j];
            }
            Q[i * dim + j] = sq;
            K[i * dim + j] = sk;
            V[i * dim + j] = sv;
        }
    }

    double scale = 1.0 / sqrt((double)dim);

    /* 1) dV = A^T @ dO  [n, dim] */
    double *dV = calloc(n_tok * dim, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += A.data[k * n_tok + i] * dO.data[k * dim + j];
            }
            dV[i * dim + j] = soma;
        }
    }

    /* 2) dAttn = dO @ V^T  [n, n] */
    double *dA = calloc(n_tok * n_tok, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < n_tok; j++) {
            double soma = 0.0;
            for (int k = 0; k < dim; k++) {
                soma += dO.data[i * dim + k] * V[j * dim + k];
            }
            dA[i * n_tok + j] = soma;
        }
    }

    /* 3) dScores (softmax jacobian): dS_ij = A_ij * (dA_ij - sum_k A_ik * dA_ik) */
    double *dS = calloc(n_tok * n_tok, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        double soma = 0.0;
        for (int k = 0; k < n_tok; k++) {
            soma += A.data[i * n_tok + k] * dA[i * n_tok + k];
        }
        for (int j = 0; j < n_tok; j++) {
            dS[i * n_tok + j] = A.data[i * n_tok + j] * (dA[i * n_tok + j] - soma);
        }
    }

    /* 4) dQ = dS @ K * scale  [n, dim] */
    double *dQ = calloc(n_tok * dim, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += dS[i * n_tok + k] * K[k * dim + j];
            }
            dQ[i * dim + j] = soma * scale;
        }
    }

    /* 5) dK = dS^T @ Q * scale  [n, dim] */
    double *dK = calloc(n_tok * dim, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += dS[k * n_tok + i] * Q[k * dim + j];
            }
            dK[i * dim + j] = soma * scale;
        }
    }

    /* 6) dWq = X^T @ dQ  [dim, dim] */
    double *dWq = calloc(dim * dim, sizeof(double));
    for (int i = 0; i < dim; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += X.data[k * dim + i] * dQ[k * dim + j];
            }
            dWq[i * dim + j] = soma;
        }
    }

    /* 7) dWk = X^T @ dK */
    double *dWk = calloc(dim * dim, sizeof(double));
    for (int i = 0; i < dim; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += X.data[k * dim + i] * dK[k * dim + j];
            }
            dWk[i * dim + j] = soma;
        }
    }

    /* 8) dWv = X^T @ dV */
    double *dWv = calloc(dim * dim, sizeof(double));
    for (int i = 0; i < dim; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += X.data[k * dim + i] * dV[k * dim + j];
            }
            dWv[i * dim + j] = soma;
        }
    }

    /* 9) dX = dQ @ Wq^T + dK @ Wk^T + dV @ Wv^T  [n, dim] */
    double *dX = calloc(n_tok * dim, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < dim; k++) {
                soma += dQ[i * dim + k] * Wq.data[j * dim + k];
                soma += dK[i * dim + k] * Wk.data[j * dim + k];
                soma += dV[i * dim + k] * Wv.data[j * dim + k];
            }
            dX[i * dim + j] = soma;
        }
    }

    /* Empacota */
    JscValue *map = jsc_map();
    Matriz m_dWq = {dim, dim, dWq};
    Matriz m_dWk = {dim, dim, dWk};
    Matriz m_dWv = {dim, dim, dWv};
    Matriz m_dX  = {n_tok, dim, dX};

    jsc_map_set(map, jsc_string("dWq"), mat_to_val(m_dWq));
    jsc_map_set(map, jsc_string("dWk"), mat_to_val(m_dWk));
    jsc_map_set(map, jsc_string("dWv"), mat_to_val(m_dWv));
    jsc_map_set(map, jsc_string("dX"),  mat_to_val(m_dX));

    free(X.data); free(Wq.data); free(Wk.data); free(Wv.data);
    free(A.data); free(dO.data);
    free(Q); free(K); free(V);
    free(dQ); free(dK); free(dV);
    free(dA); free(dS);
    free(dWq); free(dWk); free(dWv); free(dX);

    return map;
}

JscValue *make_backprop_attn_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("backward"), jsc_native("backward", attn_backward));
    return m;
}

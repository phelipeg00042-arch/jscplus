/* ============================================================
 * JSC$+ — backprop de UMA camada transformer
 * Ordem: LN -> MHA -> Res -> LN -> FF -> Res (reverso)
 * ============================================================ */

#include "eval.h"
#include "value.h"
#include "env.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void bl_error(EvalState *state, const char *msg) {
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
 * Helpers
 * ============================================================ */

/* dLayerNorm: dL/dX dado dL/dY (apenas o gradient do LN, sem residual) */
static void layernorm_backward(const double *X, const double *dY,
                                int rows, int N, double *dX) {
    double eps = 1e-5;
    for (int i = 0; i < rows; i++) {
        double media = 0.0;
        for (int j = 0; j < N; j++) media += X[i * N + j];
        media /= N;

        double var = 0.0;
        for (int j = 0; j < N; j++) {
            double diff = X[i * N + j] - media;
            var += diff * diff;
        }
        var /= N;

        double inv_std = 1.0 / sqrt(var + eps);

        double *y = malloc(sizeof(double) * N);
        for (int j = 0; j < N; j++) {
            y[j] = (X[i * N + j] - media) * inv_std;
        }

        double sum_dy = 0.0, sum_dy_y = 0.0;
        for (int j = 0; j < N; j++) {
            sum_dy += dY[i * N + j];
            sum_dy_y += dY[i * N + j] * y[j];
        }

        for (int j = 0; j < N; j++) {
            double term = (N * dY[i * N + j] - sum_dy - y[j] * sum_dy_y);
            dX[i * N + j] = (1.0 / N) * inv_std * term;
        }

        free(y);
    }
}

/* Recomputa Q, K, V (precisa pra backward do MHA) */
static void recompute_qkv(const double *X, int n_tok, int dim,
                           const double *Wq, const double *Wk,
                           const double *Wv,
                           double *Q, double *K, double *V) {
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double sq=0, sk=0, sv=0;
            for (int k = 0; k < dim; k++) {
                double x = X[i * dim + k];
                sq += x * Wq[k * dim + j];
                sk += x * Wk[k * dim + j];
                sv += x * Wv[k * dim + j];
            }
            Q[i * dim + j] = sq;
            K[i * dim + j] = sk;
            V[i * dim + j] = sv;
        }
    }
}

/* ============================================================
 * layer.backward(X_in, dL_dOut, cache_layer, Wq, Wk, Wv, Wo, Wff1, Wff2)
 *
 * X_in:        [n, dim]  — input ORIGINAL da camada (antes do LN)
 * dL_dOut:     [n, dim]  — gradiente em relacao ao output da camada
 * cache_layer: map com X_norm1, attn, X_before_res1, X_norm2, X_before_res2
 * Wq..Wff2:    pesos da camada
 *
 * Retorna: {dL_dX_in, dWq, dWk, dWv, dWo, dWff1, dWff2}
 * ============================================================ */

JscValue *layer_backward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 9) {
        bl_error(state, "layer.backward() espera 9 argumentos");
        return NULL;
    }

    JscValue *xiv = eval(n->args.items[0], env, state);
    JscValue *dyv = eval(n->args.items[1], env, state);
    JscValue *cv  = eval(n->args.items[2], env, state);
    JscValue *wqv = eval(n->args.items[3], env, state);
    JscValue *wkv = eval(n->args.items[4], env, state);
    JscValue *wvv = eval(n->args.items[5], env, state);
    JscValue *wov = eval(n->args.items[6], env, state);
    JscValue *wf1v= eval(n->args.items[7], env, state);
    JscValue *wf2v= eval(n->args.items[8], env, state);
    if (state->had_error) return NULL;

    Matriz X_in = mat_from_val(xiv);
    Matriz dOut = mat_from_val(dyv);
    Matriz Wq = mat_from_val(wqv);
    Matriz Wk = mat_from_val(wkv);
    Matriz Wv = mat_from_val(wvv);
    Matriz Wo = mat_from_val(wov);
    Matriz Wf1 = mat_from_val(wf1v);
    Matriz Wf2 = mat_from_val(wf2v);

    jsc_value_free(xiv); jsc_value_free(dyv);
    jsc_value_free(wqv); jsc_value_free(wkv);
    jsc_value_free(wvv); jsc_value_free(wov);
    jsc_value_free(wf1v); jsc_value_free(wf2v);

    if (!X_in.data || !dOut.data || !Wq.data || !Wk.data ||
        !Wv.data || !Wo.data || !Wf1.data || !Wf2.data || !cv) {
        bl_error(state, "layer.backward(): argumentos invalidos");
        free(X_in.data); free(dOut.data);
        free(Wq.data); free(Wk.data); free(Wv.data); free(Wo.data);
        free(Wf1.data); free(Wf2.data);
        return NULL;
    }

    int n_tok = X_in.rows;
    int dim = X_in.cols;
    int hidden = Wf1.cols;

    /* Extrai do cache */
    Matriz X_norm1 = mat_from_val(jsc_map_get(cv, jsc_string("X_norm1")));
    Matriz X_norm2 = mat_from_val(jsc_map_get(cv, jsc_string("X_norm2")));
    Matriz X_before_res1 = mat_from_val(jsc_map_get(cv, jsc_string("X_before_res1")));
    Matriz X_before_res2 = mat_from_val(jsc_map_get(cv, jsc_string("X_before_res2")));

    /* ============================================================
     * BACKWARD — ordem reversa
     * ============================================================ */

    /* Passo 1: dL/dX_after_res2 = dOut (o output da camada É depois do res2) */
    double *dX_after_res2 = malloc(sizeof(double) * n_tok * dim);
    memcpy(dX_after_res2, dOut.data, sizeof(double) * n_tok * dim);

    /* Passo 2: gradient do FF output = mesmo (residual) */
    /* dL/dFF_out = dL/dX_after_res2 */
    double *dFF_out = malloc(sizeof(double) * n_tok * dim);
    memcpy(dFF_out, dX_after_res2, sizeof(double) * n_tok * dim);

    /* Passo 3: backprop pelo FF */
    /* FF = h1 @ W2, h1 = ReLU(X_norm2 @ W1) */
    /* dL/dW2 = ReLU(X_norm2 @ W1)^T @ dFF_out */
    /* dL/dh1_pre_relu = (dFF_out @ W2^T) * relu_grad */
    /* dL/dW1 = X_norm2^T @ dL/dh1_pre_relu */
    /* dL/dX_norm2 = dL/dh1_pre_relu @ W1^T */

    /* Recomputa h1 */
    double *h1_pre = malloc(sizeof(double) * n_tok * hidden);
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < hidden; j++) {
            double soma = 0.0;
            for (int k = 0; k < dim; k++) {
                soma += X_norm2.data[i * dim + k] * Wf1.data[k * hidden + j];
            }
            h1_pre[i * hidden + j] = soma;
        }
    }

    /* ReLU mask */
    double *relu_grad = malloc(sizeof(double) * n_tok * hidden);
    for (int i = 0; i < n_tok * hidden; i++) {
        relu_grad[i] = (h1_pre[i] > 0.0) ? 1.0 : 0.0;
    }

    /* dW2 = h1_relu^T @ dFF_out */
    double *dWf2 = calloc(hidden * dim, sizeof(double));
    for (int i = 0; i < hidden; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                double h1_relu = (h1_pre[k * hidden + i] > 0.0) ? h1_pre[k * hidden + i] : 0.0;
                soma += h1_relu * dFF_out[k * dim + j];
            }
            dWf2[i * dim + j] = soma;
        }
    }

    /* dL/dh1_pre = (dFF_out @ W2^T) * relu_grad */
    double *dh1 = calloc(n_tok * hidden, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < hidden; j++) {
            double soma = 0.0;
            for (int k = 0; k < dim; k++) {
                soma += dFF_out[i * dim + k] * Wf2.data[j * dim + k];
            }
            dh1[i * hidden + j] = soma * relu_grad[i * hidden + j];
        }
    }

    /* dW1 = X_norm2^T @ dh1 */
    double *dWf1 = calloc(dim * hidden, sizeof(double));
    for (int i = 0; i < dim; i++) {
        for (int j = 0; j < hidden; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += X_norm2.data[k * dim + i] * dh1[k * hidden + j];
            }
            dWf1[i * hidden + j] = soma;
        }
    }

    /* dL/dX_norm2 = dh1 @ W1^T */
    double *dX_norm2 = calloc(n_tok * dim, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < hidden; k++) {
                soma += dh1[i * hidden + k] * Wf1.data[j * hidden + k];
            }
            dX_norm2[i * dim + j] = soma;
        }
    }

    /* Passo 4: backprop pelo LayerNorm 2 (input = X_before_res2) */
    double *dX_before_res2 = malloc(sizeof(double) * n_tok * dim);
    layernorm_backward(X_before_res2.data, dX_norm2, n_tok, dim, dX_before_res2);

    /* Passo 5: adiciona gradient do residual */
    /* dL/dX_after_res1 = dL/dX_before_res2 + dL/dX_after_res2 */
    double *dX_after_res1 = malloc(sizeof(double) * n_tok * dim);
    for (int i = 0; i < n_tok * dim; i++) {
        dX_after_res1[i] = dX_before_res2[i] + dX_after_res2[i];
    }

    /* Passo 6: dL/dAttn = dX_after_res1 */
    double *dAttn = malloc(sizeof(double) * n_tok * dim);
    memcpy(dAttn, dX_after_res1, sizeof(double) * n_tok * dim);

    /* Passo 7: backprop pelo MHA */
    /* MHA output = attn_heads @ Wo, onde attn_heads = softmax(QK^T/sqrt(d)) @ V */
    /* Vamos recomputar Q, K, V */
    double *Q = malloc(sizeof(double) * n_tok * dim);
    double *K = malloc(sizeof(double) * n_tok * dim);
    double *V = malloc(sizeof(double) * n_tok * dim);
    recompute_qkv(X_norm1.data, n_tok, dim,
                  Wq.data, Wk.data, Wv.data, Q, K, V);

    /* dL/dWo = attn_heads^T @ dAttn */
    /* attn_heads = QK_softmax @ V (mas não temos attn_heads salvo) */
    /* Recomputa attn_heads */
    double *attn_heads = calloc(n_tok * dim, sizeof(double));
    double *attn_probs = calloc(n_tok * n_tok, sizeof(double));
    double scale = 1.0 / sqrt((double)dim);

    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < n_tok; j++) {
            double soma = 0.0;
            for (int k = 0; k < dim; k++) {
                soma += Q[i * dim + k] * K[j * dim + k];
            }
            attn_probs[i * n_tok + j] = soma * scale;
        }
    }
    /* softmax */
    for (int i = 0; i < n_tok; i++) {
        double max = attn_probs[i * n_tok];
        for (int j = 1; j < n_tok; j++)
            if (attn_probs[i * n_tok + j] > max) max = attn_probs[i * n_tok + j];
        double total = 0.0;
        for (int j = 0; j < n_tok; j++) {
            attn_probs[i * n_tok + j] = exp(attn_probs[i * n_tok + j] - max);
            total += attn_probs[i * n_tok + j];
        }
        for (int j = 0; j < n_tok; j++) attn_probs[i * n_tok + j] /= total;
    }
    /* attn_heads = attn_probs @ V */
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += attn_probs[i * n_tok + k] * V[k * dim + j];
            }
            attn_heads[i * dim + j] = soma;
        }
    }

    /* dWo = attn_heads^T @ dAttn */
    double *dWo = calloc(dim * dim, sizeof(double));
    for (int i = 0; i < dim; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += attn_heads[k * dim + i] * dAttn[k * dim + j];
            }
            dWo[i * dim + j] = soma;
        }
    }

    /* dL/dAttn_heads = dAttn @ Wo^T */
    double *dAttn_heads = calloc(n_tok * dim, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < dim; k++) {
                soma += dAttn[i * dim + k] * Wo.data[j * dim + k];
            }
            dAttn_heads[i * dim + j] = soma;
        }
    }

    /* dL/dV = attn_probs^T @ dAttn_heads */
    double *dV = calloc(n_tok * dim, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += attn_probs[k * n_tok + i] * dAttn_heads[k * dim + j];
            }
            dV[i * dim + j] = soma;
        }
    }

    /* dL/dAttn_probs = dAttn_heads @ V^T */
    double *dAttn_probs = calloc(n_tok * n_tok, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < n_tok; j++) {
            double soma = 0.0;
            for (int k = 0; k < dim; k++) {
                soma += dAttn_heads[i * dim + k] * V[j * dim + k];
            }
            dAttn_probs[i * n_tok + j] = soma;
        }
    }

    /* dL/dScores = softmax_backward */
    double *dScores = calloc(n_tok * n_tok, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        double soma = 0.0;
        for (int k = 0; k < n_tok; k++) {
            soma += attn_probs[i * n_tok + k] * dAttn_probs[i * n_tok + k];
        }
        for (int j = 0; j < n_tok; j++) {
            dScores[i * n_tok + j] = attn_probs[i * n_tok + j] *
                                     (dAttn_probs[i * n_tok + j] - soma);
        }
    }

    /* dQ = dScores @ K * scale */
    double *dQ = calloc(n_tok * dim, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += dScores[i * n_tok + k] * K[k * dim + j];
            }
            dQ[i * dim + j] = soma * scale;
        }
    }

    /* dK = dScores^T @ Q * scale */
    double *dK = calloc(n_tok * dim, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += dScores[k * n_tok + i] * Q[k * dim + j];
            }
            dK[i * dim + j] = soma * scale;
        }
    }

    /* dWq = X_norm1^T @ dQ */
    double *dWq = calloc(dim * dim, sizeof(double));
    for (int i = 0; i < dim; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += X_norm1.data[k * dim + i] * dQ[k * dim + j];
            }
            dWq[i * dim + j] = soma;
        }
    }

    /* dWk = X_norm1^T @ dK */
    double *dWk = calloc(dim * dim, sizeof(double));
    for (int i = 0; i < dim; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += X_norm1.data[k * dim + i] * dK[k * dim + j];
            }
            dWk[i * dim + j] = soma;
        }
    }

    /* dWv = X_norm1^T @ dV */
    double *dWv = calloc(dim * dim, sizeof(double));
    for (int i = 0; i < dim; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < n_tok; k++) {
                soma += X_norm1.data[k * dim + i] * dV[k * dim + j];
            }
            dWv[i * dim + j] = soma;
        }
    }

    /* dX_norm1 = dQ @ Wq^T + dK @ Wk^T + dV @ Wv^T */
    double *dX_norm1 = calloc(n_tok * dim, sizeof(double));
    for (int i = 0; i < n_tok; i++) {
        for (int j = 0; j < dim; j++) {
            double soma = 0.0;
            for (int k = 0; k < dim; k++) {
                soma += dQ[i * dim + k] * Wq.data[j * dim + k];
                soma += dK[i * dim + k] * Wk.data[j * dim + k];
                soma += dV[i * dim + k] * Wv.data[j * dim + k];
            }
            dX_norm1[i * dim + j] = soma;
        }
    }

    /* Passo 8: backprop pelo LayerNorm 1 */
    double *dX_before_res1 = malloc(sizeof(double) * n_tok * dim);
    layernorm_backward(X_in.data, dX_norm1, n_tok, dim, dX_before_res1);

    /* Passo 9: soma residual 1 */
    /* dL/dX_in = dX_before_res1 + dX_after_res1 */
    double *dX_in = malloc(sizeof(double) * n_tok * dim);
    for (int i = 0; i < n_tok * dim; i++) {
        dX_in[i] = dX_before_res1[i] + dX_after_res1[i];
    }

    /* ============================================================
     * Empacota resultado
     * ============================================================ */

    JscValue *res = jsc_map();
    Matriz m;
    m.rows = n_tok; m.cols = dim; m.data = dX_in;
    jsc_map_set(res, jsc_string("dX_in"), mat_to_val(m));
    m.rows = dim; m.cols = dim; m.data = dWq;
    jsc_map_set(res, jsc_string("dWq"), mat_to_val(m));
    m.data = dWk; jsc_map_set(res, jsc_string("dWk"), mat_to_val(m));
    m.data = dWv; jsc_map_set(res, jsc_string("dWv"), mat_to_val(m));
    m.data = dWo; jsc_map_set(res, jsc_string("dWo"), mat_to_val(m));
    m.rows = dim; m.cols = hidden; m.data = dWf1;
    jsc_map_set(res, jsc_string("dWf1"), mat_to_val(m));
    m.rows = hidden; m.cols = dim; m.data = dWf2;
    jsc_map_set(res, jsc_string("dWf2"), mat_to_val(m));

    /* Free */
    free(X_in.data); free(dOut.data);
    free(Wq.data); free(Wk.data); free(Wv.data); free(Wo.data);
    free(Wf1.data); free(Wf2.data);
    free(X_norm1.data); free(X_norm2.data);
    free(X_before_res1.data); free(X_before_res2.data);
    free(dX_after_res2); free(dFF_out);
    free(h1_pre); free(relu_grad); free(dWf2); free(dh1); free(dWf1); free(dX_norm2);
    free(dX_before_res2); free(dX_after_res1); free(dAttn);
    free(Q); free(K); free(V);
    free(attn_heads); free(attn_probs); free(dWo); free(dAttn_heads);
    free(dV); free(dAttn_probs); free(dScores);
    free(dQ); free(dK); free(dWq); free(dWk); free(dWv);
    free(dX_norm1); free(dX_before_res1); free(dX_in);

    return res;
}

JscValue *make_backprop_layer_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("backward"), jsc_native("backward", layer_backward));
    return m;
}

/* ============================================================
 * JSC$+ — modulo multihead
 * Multi-Head Self-Attention (transformer)
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

static void mh_error(EvalState *state, const char *msg) {
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

/* Multi-head weights (persistentes por processo) */
typedef struct {
    int dim;
    int n_heads;
    int head_dim;
    double *W_q, *W_k, *W_v, *W_o;   /* [dim, dim] cada */
    int inicializado;
} MultiHead;

static MultiHead g_mh = {0, 0, 0, NULL, NULL, NULL, NULL, 0};

static void xavier(double *w, int n_in, int n_out) {
    double scale = sqrt(6.0 / (n_in + n_out));
    for (int i = 0; i < n_in * n_out; i++) {
        w[i] = ((double)rand() / RAND_MAX) * 2.0 * scale - scale;
    }
}

/* multihead.init(dim, n_heads) */
JscValue *mh_init(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        mh_error(state, "multihead.init() espera (dim, n_heads)");
        return NULL;
    }

    JscValue *dv = eval(n->args.items[0], env, state);
    JscValue *hv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    int dim = (int)jsc_value_to_int(dv);
    int n_heads = (int)jsc_value_to_int(hv);
    jsc_value_free(dv);
    jsc_value_free(hv);

    if (dim % n_heads != 0) {
        mh_error(state, "multihead: dim deve ser divisivel por n_heads");
        return NULL;
    }

    if (g_mh.W_q) { free(g_mh.W_q); free(g_mh.W_k); free(g_mh.W_v); free(g_mh.W_o); }

    g_mh.dim = dim;
    g_mh.n_heads = n_heads;
    g_mh.head_dim = dim / n_heads;

    g_mh.W_q = malloc(sizeof(double) * dim * dim);
    g_mh.W_k = malloc(sizeof(double) * dim * dim);
    g_mh.W_v = malloc(sizeof(double) * dim * dim);
    g_mh.W_o = malloc(sizeof(double) * dim * dim);

    srand(time(NULL));
    xavier(g_mh.W_q, dim, dim);
    xavier(g_mh.W_k, dim, dim);
    xavier(g_mh.W_v, dim, dim);
    xavier(g_mh.W_o, dim, dim);

    g_mh.inicializado = 1;
    return jsc_vazio();
}

/* Helper: projecao linear X @ W -> out */
static void linear(const double *X, int n, int d_in,
                   const double *W, int d_out, double *out) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < d_out; j++) {
            double soma = 0.0;
            for (int k = 0; k < d_in; k++) {
                soma += X[i * d_in + k] * W[k * d_out + j];
            }
            out[i * d_out + j] = soma;
        }
    }
}

/* multihead.forward(X) — forward pass completo
 * X: [n, dim]
 * Retorna: [n, dim]
 */
JscValue *mh_forward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (!g_mh.inicializado) {
        mh_error(state, "multihead nao inicializado. Chame multihead.init() primeiro");
        return NULL;
    }

    if (n->args.count != 1) {
        mh_error(state, "multihead.forward() espera 1 argumento (X)");
        return NULL;
    }

    JscValue *xv = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz X = mat_from_val(xv);
    jsc_value_free(xv);

    if (!X.data || X.cols != g_mh.dim) {
        mh_error(state, "multihead.forward(): X.cols deve ser igual a dim");
        free(X.data);
        return NULL;
    }

    int n_tokens = X.rows;
    int dim = g_mh.dim;
    int n_heads = g_mh.n_heads;
    int hd = g_mh.head_dim;

    /* 1) Projeta Q, K, V: [n, dim] cada */
    double *Q = malloc(sizeof(double) * n_tokens * dim);
    double *K = malloc(sizeof(double) * n_tokens * dim);
    double *V = malloc(sizeof(double) * n_tokens * dim);

    linear(X.data, n_tokens, dim, g_mh.W_q, dim, Q);
    linear(X.data, n_tokens, dim, g_mh.W_k, dim, K);
    linear(X.data, n_tokens, dim, g_mh.W_v, dim, V);

    /* 2) Attention por head */
    double *attn_out = calloc(n_tokens * dim, sizeof(double));
    double scale = 1.0 / sqrt((double)hd);

    for (int h = 0; h < n_heads; h++) {
        int offset = h * hd;

        /* scores: [n, n] */
        double *scores = malloc(sizeof(double) * n_tokens * n_tokens);
        for (int i = 0; i < n_tokens; i++) {
            for (int j = 0; j < n_tokens; j++) {
                double soma = 0.0;
                for (int k = 0; k < hd; k++) {
                    soma += Q[i * dim + offset + k] * K[j * dim + offset + k];
                }
                scores[i * n_tokens + j] = soma * scale;
            }
        }

        /* softmax por linha */
        for (int i = 0; i < n_tokens; i++) {
            double max = scores[i * n_tokens];
            for (int j = 1; j < n_tokens; j++)
                if (scores[i * n_tokens + j] > max) max = scores[i * n_tokens + j];
            double total = 0.0;
            for (int j = 0; j < n_tokens; j++) {
                scores[i * n_tokens + j] = exp(scores[i * n_tokens + j] - max);
                total += scores[i * n_tokens + j];
            }
            for (int j = 0; j < n_tokens; j++)
                scores[i * n_tokens + j] /= total;
        }

        /* out_head = scores @ V_head  [n, hd] */
        for (int i = 0; i < n_tokens; i++) {
            for (int j = 0; j < hd; j++) {
                double soma = 0.0;
                for (int k = 0; k < n_tokens; k++) {
                    soma += scores[i * n_tokens + k] * V[k * dim + offset + j];
                }
                attn_out[i * dim + offset + j] = soma;
            }
        }
        free(scores);
    }

    /* 3) Projecao final: attn_out @ W_o */
    double *out = malloc(sizeof(double) * n_tokens * dim);
    linear(attn_out, n_tokens, dim, g_mh.W_o, dim, out);

    Matriz res = {n_tokens, dim, out};
    JscValue *result = mat_to_val(res);

    free(X.data);
    free(Q); free(K); free(V);
    free(attn_out); free(out);

    return result;
}

/* multihead.info() */
JscValue *mh_info(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    printf("multihead: dim=%d n_heads=%d head_dim=%d\n",
           g_mh.dim, g_mh.n_heads, g_mh.head_dim);
    printf("  inicializado: %s\n", g_mh.inicializado ? "sim" : "nao");
    return jsc_vazio();
}

JscValue *make_multihead_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("init"),    jsc_native("init",    mh_init));
    jsc_map_set(m, jsc_string("forward"), jsc_native("forward", mh_forward));
    jsc_map_set(m, jsc_string("info"),    jsc_native("info",    mh_info));
    return m;
}

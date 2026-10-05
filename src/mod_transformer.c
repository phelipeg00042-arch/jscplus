/* ============================================================
 * JSC$+ — modulo transformer
 * Modelo COMPLETO: embedding + N blocos + output
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

static void tf_error(EvalState *state, const char *msg) {
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
 * Pesos do modelo (globais)
 * ============================================================ */

typedef struct {
    int vocab_size;
    int dim;
    int n_heads;
    int n_layers;
    int hidden_dim;

    /* Embedding */
    double *W_emb;      /* [vocab_size, dim] */

    /* Por camada: atencao + FF */
    double **W_q;       /* [n_layers][dim*dim] */
    double **W_k;
    double **W_v;
    double **W_o;
    double **W_ff1;     /* [dim, hidden] */
    double **W_ff2;     /* [hidden, dim] */

    /* Output */
    double *W_out;      /* [dim, vocab] */

    int inicializado;
} Modelo;

static Modelo g_mdl = {0};

static void xavier(double *w, int n_in, int n_out) {
    double scale = sqrt(6.0 / (n_in + n_out));
    for (int i = 0; i < n_in * n_out; i++) {
        w[i] = ((double)rand() / RAND_MAX) * 2.0 * scale - scale;
    }
}

static void small_init(double *w, int n, double scale) {
    for (int i = 0; i < n; i++) {
        w[i] = ((double)rand() / RAND_MAX) * 2.0 * scale - scale;
    }
}

/* transformer.init(vocab_size, dim, n_heads, n_layers, hidden_dim) */
JscValue *tf_init(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 5) {
        tf_error(state, "transformer.init() espera (vocab, dim, n_heads, n_layers, hidden)");
        return NULL;
    }

    int args[5];
    for (int i = 0; i < 5; i++) {
        JscValue *v = eval(n->args.items[i], env, state);
        if (state->had_error) return NULL;
        args[i] = (int)jsc_value_to_int(v);
        jsc_value_free(v);
    }

    int vocab = args[0], dim = args[1], n_heads = args[2];
    int n_layers = args[3], hidden = args[4];

    /* Limpa modelo anterior */
    if (g_mdl.W_emb) free(g_mdl.W_emb);
    if (g_mdl.W_q) {
        for (int i = 0; i < g_mdl.n_layers; i++) {
            free(g_mdl.W_q[i]); free(g_mdl.W_k[i]);
            free(g_mdl.W_v[i]); free(g_mdl.W_o[i]);
            free(g_mdl.W_ff1[i]); free(g_mdl.W_ff2[i]);
        }
        free(g_mdl.W_q); free(g_mdl.W_k);
        free(g_mdl.W_v); free(g_mdl.W_o);
        free(g_mdl.W_ff1); free(g_mdl.W_ff2);
    }
    if (g_mdl.W_out) free(g_mdl.W_out);

    g_mdl.vocab_size = vocab;
    g_mdl.dim = dim;
    g_mdl.n_heads = n_heads;
    g_mdl.n_layers = n_layers;
    g_mdl.hidden_dim = hidden;

    srand(time(NULL));

    /* Embedding */
    g_mdl.W_emb = malloc(sizeof(double) * vocab * dim);
    small_init(g_mdl.W_emb, vocab * dim, 0.1);

    /* Camadas */
    g_mdl.W_q = malloc(sizeof(double*) * n_layers);
    g_mdl.W_k = malloc(sizeof(double*) * n_layers);
    g_mdl.W_v = malloc(sizeof(double*) * n_layers);
    g_mdl.W_o = malloc(sizeof(double*) * n_layers);
    g_mdl.W_ff1 = malloc(sizeof(double*) * n_layers);
    g_mdl.W_ff2 = malloc(sizeof(double*) * n_layers);

    for (int i = 0; i < n_layers; i++) {
        g_mdl.W_q[i] = malloc(sizeof(double) * dim * dim);
        g_mdl.W_k[i] = malloc(sizeof(double) * dim * dim);
        g_mdl.W_v[i] = malloc(sizeof(double) * dim * dim);
        g_mdl.W_o[i] = malloc(sizeof(double) * dim * dim);
        g_mdl.W_ff1[i] = malloc(sizeof(double) * dim * hidden);
        g_mdl.W_ff2[i] = malloc(sizeof(double) * hidden * dim);

        xavier(g_mdl.W_q[i], dim, dim);
        xavier(g_mdl.W_k[i], dim, dim);
        xavier(g_mdl.W_v[i], dim, dim);
        xavier(g_mdl.W_o[i], dim, dim);
        xavier(g_mdl.W_ff1[i], dim, hidden);
        xavier(g_mdl.W_ff2[i], hidden, dim);
    }

    /* Output */
    g_mdl.W_out = malloc(sizeof(double) * dim * vocab);
    small_init(g_mdl.W_out, dim * vocab, 0.1);

    g_mdl.inicializado = 1;
    return jsc_vazio();
}

/* Helper: linear X[n,d_in] @ W[d_in,d_out] -> out[n,d_out] */
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

/* Layer norm in-place */
static void layernorm(double *X, int n, int d) {
    double eps = 1e-5;
    for (int i = 0; i < n; i++) {
        double media = 0.0;
        for (int j = 0; j < d; j++) media += X[i * d + j];
        media /= d;
        double var = 0.0;
        for (int j = 0; j < d; j++) {
            double diff = X[i * d + j] - media;
            var += diff * diff;
        }
        var /= d;
        double inv = 1.0 / sqrt(var + eps);
        for (int j = 0; j < d; j++) {
            X[i * d + j] = (X[i * d + j] - media) * inv;
        }
    }
}

/* Multi-head attention forward */
static void multihead_forward(double *X, int n_tokens,
                               int dim, int n_heads,
                               double *W_q, double *W_k, double *W_v, double *W_o,
                               double *out) {
    int hd = dim / n_heads;

    double *Q = malloc(sizeof(double) * n_tokens * dim);
    double *K = malloc(sizeof(double) * n_tokens * dim);
    double *V = malloc(sizeof(double) * n_tokens * dim);
    double *attn = calloc(n_tokens * dim, sizeof(double));

    linear(X, n_tokens, dim, W_q, dim, Q);
    linear(X, n_tokens, dim, W_k, dim, K);
    linear(X, n_tokens, dim, W_v, dim, V);

    double scale = 1.0 / sqrt((double)hd);
    double *scores = malloc(sizeof(double) * n_tokens * n_tokens);

    for (int h = 0; h < n_heads; h++) {
        int off = h * hd;

        for (int i = 0; i < n_tokens; i++) {
            for (int j = 0; j < n_tokens; j++) {
                double soma = 0.0;
                for (int k = 0; k < hd; k++) {
                    soma += Q[i * dim + off + k] * K[j * dim + off + k];
                }
                scores[i * n_tokens + j] = soma * scale;
            }
        }

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

        for (int i = 0; i < n_tokens; i++) {
            for (int j = 0; j < hd; j++) {
                double soma = 0.0;
                for (int k = 0; k < n_tokens; k++) {
                    soma += scores[i * n_tokens + k] * V[k * dim + off + j];
                }
                attn[i * dim + off + j] = soma;
            }
        }
    }

    linear(attn, n_tokens, dim, W_o, dim, out);

    free(Q); free(K); free(V); free(attn); free(scores);
}

/* Feed-forward (in-place) */
static void feed_forward(double *X, int n_tokens, int dim, int hidden,
                          double *W1, double *W2, double *out) {
    double *h1 = malloc(sizeof(double) * n_tokens * hidden);
    linear(X, n_tokens, dim, W1, hidden, h1);

    for (int i = 0; i < n_tokens * hidden; i++) {
        if (h1[i] < 0.0) h1[i] = 0.0;
    }

    linear(h1, n_tokens, hidden, W2, dim, out);
    free(h1);
}

/* transformer.forward(tokens) — forward pass COMPLETO
 * tokens: array de IDs
 * Retorna: logits [n_tokens, vocab_size]
 */
JscValue *tf_forward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (!g_mdl.inicializado) {
        tf_error(state, "transformer nao inicializado");
        return NULL;
    }

    if (n->args.count != 1) {
        tf_error(state, "transformer.forward() espera array de tokens");
        return NULL;
    }

    JscValue *tv = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    if (tv->type != VAL_ARRAY) {
        jsc_value_free(tv);
        tf_error(state, "transformer.forward(): argumento deve ser array");
        return NULL;
    }

    int n_tokens = (int)tv->as.array.count;
    int dim = g_mdl.dim;

    /* 1) Embedding */
    double *X = malloc(sizeof(double) * n_tokens * dim);
    for (int i = 0; i < n_tokens; i++) {
        int id = (int)jsc_value_to_int(tv->as.array.items[i]);
        if (id < 0 || id >= g_mdl.vocab_size) id = 1;
        for (int j = 0; j < dim; j++) {
            X[i * dim + j] = g_mdl.W_emb[id * dim + j];
        }
    }
    jsc_value_free(tv);

    /* 2) N blocos */
    double *attn = malloc(sizeof(double) * n_tokens * dim);
    double *ff = malloc(sizeof(double) * n_tokens * dim);
    double *X_norm = malloc(sizeof(double) * n_tokens * dim);

    for (int layer = 0; layer < g_mdl.n_layers; layer++) {
        /* (a) Layer Norm */
        memcpy(X_norm, X, sizeof(double) * n_tokens * dim);
        layernorm(X_norm, n_tokens, dim);

        /* (b) Multi-head attention */
        multihead_forward(X_norm, n_tokens, dim, g_mdl.n_heads,
                          g_mdl.W_q[layer], g_mdl.W_k[layer],
                          g_mdl.W_v[layer], g_mdl.W_o[layer],
                          attn);

        /* (c) Residual: X = X + attn */
        for (int i = 0; i < n_tokens * dim; i++) X[i] += attn[i];

        /* (d) Layer Norm de novo */
        memcpy(X_norm, X, sizeof(double) * n_tokens * dim);
        layernorm(X_norm, n_tokens, dim);

        /* (e) Feed-Forward */
        feed_forward(X_norm, n_tokens, dim, g_mdl.hidden_dim,
                     g_mdl.W_ff1[layer], g_mdl.W_ff2[layer], ff);

        /* (f) Residual: X = X + ff */
        for (int i = 0; i < n_tokens * dim; i++) X[i] += ff[i];
    }

    /* 3) Output layer: logits */
    Matriz logits = {n_tokens, g_mdl.vocab_size, NULL};
    logits.data = malloc(sizeof(double) * n_tokens * g_mdl.vocab_size);
    linear(X, n_tokens, dim, g_mdl.W_out, g_mdl.vocab_size, logits.data);

    JscValue *res = mat_to_val(logits);

    free(X); free(attn); free(ff); free(X_norm); free(logits.data);
    return res;
}

/* transformer.info() */
JscValue *tf_info(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    printf("transformer: vocab=%d dim=%d n_heads=%d n_layers=%d hidden=%d\n",
           g_mdl.vocab_size, g_mdl.dim, g_mdl.n_heads,
           g_mdl.n_layers, g_mdl.hidden_dim);
    printf("  params: %.2fM\n",
           (g_mdl.vocab_size * g_mdl.dim +
            g_mdl.n_layers * (4 * g_mdl.dim * g_mdl.dim +
                              2 * g_mdl.dim * g_mdl.hidden_dim) +
            g_mdl.dim * g_mdl.vocab_size) / 1e6);
    printf("  inicializado: %s\n", g_mdl.inicializado ? "sim" : "nao");
    return jsc_vazio();
}

JscValue *make_transformer_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("init"),    jsc_native("init",    tf_init));
    jsc_map_set(m, jsc_string("forward"), jsc_native("forward", tf_forward));
    jsc_map_set(m, jsc_string("info"),    jsc_native("info",    tf_info));
    return m;
}

/* ============================================================
 * JSC$+ — forward pass com cache
 * Guarda todos os intermediarios necessarios pra backprop
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

static void fc_error(EvalState *state, const char *msg) {
    if (state) {
        snprintf(state->error_message, sizeof(state->error_message), "%s", msg);
        state->had_error = 1;
    }
    fprintf(stderr, "JSC$+ erro: %s\n", msg);
}

typedef struct { int rows, cols; double *data; } Matriz;

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
 * Pesos globais do modelo (para cache)
 * ============================================================ */

typedef struct {
    int vocab_size;
    int dim;
    int n_heads;
    int n_layers;
    int hidden_dim;

    double *W_emb;
    double **W_q, **W_k, **W_v, **W_o;
    double **W_ff1, **W_ff2;
    double *W_out;

    int inicializado;
} ModeloCache;

static ModeloCache g_fc = {0};

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

/* fc.init(vocab, dim, n_heads, n_layers, hidden) */
JscValue *fc_init(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 5) {
        fc_error(state, "fc.init() espera (vocab, dim, n_heads, n_layers, hidden)");
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

    /* Limpa */
    if (g_fc.W_emb) {
        free(g_fc.W_emb);
        for (int i = 0; i < g_fc.n_layers; i++) {
            free(g_fc.W_q[i]); free(g_fc.W_k[i]);
            free(g_fc.W_v[i]); free(g_fc.W_o[i]);
            free(g_fc.W_ff1[i]); free(g_fc.W_ff2[i]);
        }
        free(g_fc.W_q); free(g_fc.W_k);
        free(g_fc.W_v); free(g_fc.W_o);
        free(g_fc.W_ff1); free(g_fc.W_ff2);
        free(g_fc.W_out);
    }

    g_fc.vocab_size = vocab;
    g_fc.dim = dim;
    g_fc.n_heads = n_heads;
    g_fc.n_layers = n_layers;
    g_fc.hidden_dim = hidden;

    srand(time(NULL));

    g_fc.W_emb = malloc(sizeof(double) * vocab * dim);
    small_init(g_fc.W_emb, vocab * dim, 0.1);

    g_fc.W_q = malloc(sizeof(double*) * n_layers);
    g_fc.W_k = malloc(sizeof(double*) * n_layers);
    g_fc.W_v = malloc(sizeof(double*) * n_layers);
    g_fc.W_o = malloc(sizeof(double*) * n_layers);
    g_fc.W_ff1 = malloc(sizeof(double*) * n_layers);
    g_fc.W_ff2 = malloc(sizeof(double*) * n_layers);

    for (int i = 0; i < n_layers; i++) {
        g_fc.W_q[i] = malloc(sizeof(double) * dim * dim);
        g_fc.W_k[i] = malloc(sizeof(double) * dim * dim);
        g_fc.W_v[i] = malloc(sizeof(double) * dim * dim);
        g_fc.W_o[i] = malloc(sizeof(double) * dim * dim);
        g_fc.W_ff1[i] = malloc(sizeof(double) * dim * hidden);
        g_fc.W_ff2[i] = malloc(sizeof(double) * hidden * dim);

        xavier(g_fc.W_q[i], dim, dim);
        xavier(g_fc.W_k[i], dim, dim);
        xavier(g_fc.W_v[i], dim, dim);
        xavier(g_fc.W_o[i], dim, dim);
        xavier(g_fc.W_ff1[i], dim, hidden);
        xavier(g_fc.W_ff2[i], hidden, dim);
    }

    g_fc.W_out = malloc(sizeof(double) * dim * vocab);
    small_init(g_fc.W_out, dim * vocab, 0.1);

    g_fc.inicializado = 1;
    return jsc_vazio();
}

/* ============================================================
 * Helpers de matmul
 * ============================================================ */

static void linear_fwd(const double *X, int n, int d_in,
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

static void layernorm_fwd(const double *X, int n, int d, double *out) {
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
            out[i * d + j] = (X[i * d + j] - media) * inv;
        }
    }
}

static void softmax_linhas(double *X, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        double max = X[i * cols];
        for (int j = 1; j < cols; j++)
            if (X[i * cols + j] > max) max = X[i * cols + j];
        double total = 0.0;
        for (int j = 0; j < cols; j++) {
            X[i * cols + j] = exp(X[i * cols + j] - max);
            total += X[i * cols + j];
        }
        for (int j = 0; j < cols; j++) X[i * cols + j] /= total;
    }
}

/* Multi-head forward com cache */
static void mha_fwd(const double *X, int n_tok, int dim, int n_heads,
                    const double *Wq, const double *Wk,
                    const double *Wv, const double *Wo,
                    double *attn_probs_out,   /* [n_heads * n_tok * n_tok] */
                    double *V_cache,          /* [n_tok * dim] */
                    double *out) {
    int hd = dim / n_heads;

    double *Q = malloc(sizeof(double) * n_tok * dim);
    double *K = malloc(sizeof(double) * n_tok * dim);
    double *V = malloc(sizeof(double) * n_tok * dim);
    double *attn = calloc(n_tok * dim, sizeof(double));

    linear_fwd(X, n_tok, dim, Wq, dim, Q);
    linear_fwd(X, n_tok, dim, Wk, dim, K);
    linear_fwd(X, n_tok, dim, Wv, dim, V);

    memcpy(V_cache, V, sizeof(double) * n_tok * dim);

    double scale = 1.0 / sqrt((double)hd);
    double *scores = malloc(sizeof(double) * n_tok * n_tok);

    for (int h = 0; h < n_heads; h++) {
        int off = h * hd;

        for (int i = 0; i < n_tok; i++) {
            for (int j = 0; j < n_tok; j++) {
                double soma = 0.0;
                for (int k = 0; k < hd; k++) {
                    soma += Q[i * dim + off + k] * K[j * dim + off + k];
                }
                scores[i * n_tok + j] = soma * scale;
            }
        }

        softmax_linhas(scores, n_tok, n_tok);

        /* Salva probs no cache */
        memcpy(attn_probs_out + h * n_tok * n_tok, scores,
               sizeof(double) * n_tok * n_tok);

        for (int i = 0; i < n_tok; i++) {
            for (int j = 0; j < hd; j++) {
                double soma = 0.0;
                for (int k = 0; k < n_tok; k++) {
                    soma += scores[i * n_tok + k] * V[k * dim + off + j];
                }
                attn[i * dim + off + j] = soma;
            }
        }
    }

    linear_fwd(attn, n_tok, dim, Wo, dim, out);

    free(Q); free(K); free(V); free(attn); free(scores);
}

/* ============================================================
 * fc.forward(tokens) — forward com cache
 * Retorna: {"logits": ..., "cache": {...}}
 * ============================================================ */

JscValue *fc_forward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (!g_fc.inicializado) {
        fc_error(state, "fc nao inicializado");
        return NULL;
    }

    if (n->args.count != 1) {
        fc_error(state, "fc.forward() espera array de tokens");
        return NULL;
    }

    JscValue *tv = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    if (tv->type != VAL_ARRAY) {
        jsc_value_free(tv);
        fc_error(state, "fc.forward(): argumento deve ser array");
        return NULL;
    }

    int n_tok = (int)tv->as.array.count;
    int dim = g_fc.dim;
    int nh = g_fc.n_heads;
    int nl = g_fc.n_layers;

    /* Cache global */
    JscValue *cache = jsc_map();

    /* 1) Embedding */
    double *X = malloc(sizeof(double) * n_tok * dim);
    for (int i = 0; i < n_tok; i++) {
        int id = (int)jsc_value_to_int(tv->as.array.items[i]);
        if (id < 0 || id >= g_fc.vocab_size) id = 1;
        for (int j = 0; j < dim; j++) {
            X[i * dim + j] = g_fc.W_emb[id * dim + j];
        }
    }
    jsc_value_free(tv);

    /* Salva X inicial no cache */
    Matriz Xm = {n_tok, dim, X};
    JscValue *X_copy = mat_to_val(Xm);
    jsc_map_set(cache, jsc_string("X_emb"), X_copy);

    /* 2) Camadas */
    double *X_norm = malloc(sizeof(double) * n_tok * dim);
    double *attn = malloc(sizeof(double) * n_tok * dim);
    double *ff = malloc(sizeof(double) * n_tok * dim);

    JscValue *layers_cache = jsc_array();

    for (int layer = 0; layer < nl; layer++) {
        JscValue *layer_cache = jsc_map();

        /* Layer norm 1 */
        layernorm_fwd(X, n_tok, dim, X_norm);
        Matriz Xn = {n_tok, dim, X_norm};
        jsc_map_set(layer_cache, jsc_string("X_norm1"), mat_to_val(Xn));

        /* Multi-head com cache */
        int attn_probs_size = nh * n_tok * n_tok;
        double *attn_probs = malloc(sizeof(double) * attn_probs_size);
        double *V_cache = malloc(sizeof(double) * n_tok * dim);

        mha_fwd(X_norm, n_tok, dim, nh,
                g_fc.W_q[layer], g_fc.W_k[layer],
                g_fc.W_v[layer], g_fc.W_o[layer],
                attn_probs, V_cache, attn);

        /* Salva no cache */
        Matriz attn_m = {n_tok, dim, attn};
        jsc_map_set(layer_cache, jsc_string("attn"), mat_to_val(attn_m));

        /* Salva X antes do residual */
        Matriz Xres = {n_tok, dim, X};
        jsc_map_set(layer_cache, jsc_string("X_before_res1"), mat_to_val(Xres));

        /* Residual */
        for (int i = 0; i < n_tok * dim; i++) X[i] += attn[i];

        /* Layer norm 2 */
        layernorm_fwd(X, n_tok, dim, X_norm);
        Matriz Xn2 = {n_tok, dim, X_norm};
        jsc_map_set(layer_cache, jsc_string("X_norm2"), mat_to_val(Xn2));

        /* Feed-forward */
        double *h1 = malloc(sizeof(double) * n_tok * g_fc.hidden_dim);
        linear_fwd(X_norm, n_tok, dim, g_fc.W_ff1[layer], g_fc.hidden_dim, h1);

        double *relu_mask = malloc(sizeof(double) * n_tok * g_fc.hidden_dim);
        for (int i = 0; i < n_tok * g_fc.hidden_dim; i++) {
            if (h1[i] > 0.0) relu_mask[i] = 1.0;
            else { h1[i] = 0.0; relu_mask[i] = 0.0; }
        }

        linear_fwd(h1, n_tok, g_fc.hidden_dim, g_fc.W_ff2[layer], dim, ff);

        /* Salva no cache */
        Matriz Xres2 = {n_tok, dim, X};
        jsc_map_set(layer_cache, jsc_string("X_before_res2"), mat_to_val(Xres2));

        /* Residual 2 */
        for (int i = 0; i < n_tok * dim; i++) X[i] += ff[i];

        /* Salva cache da layer */
        jsc_array_push(layers_cache, layer_cache);

        free(attn_probs);
        free(V_cache);
        free(h1);
        free(relu_mask);
    }

    jsc_map_set(cache, jsc_string("layers"), layers_cache);

    /* Salva X final */
    Matriz Xfin = {n_tok, dim, X};
    jsc_map_set(cache, jsc_string("X_final"), mat_to_val(Xfin));

    /* 3) Output */
    Matriz logits = {n_tok, g_fc.vocab_size, NULL};
    logits.data = malloc(sizeof(double) * n_tok * g_fc.vocab_size);
    linear_fwd(X, n_tok, dim, g_fc.W_out, g_fc.vocab_size, logits.data);

    /* Retorna */
    JscValue *result = jsc_map();
    jsc_map_set(result, jsc_string("logits"), mat_to_val(logits));
    jsc_map_set(result, jsc_string("cache"), cache);

    free(X);
    free(X_norm);
    free(attn);
    free(ff);
    free(logits.data);
    return result;
}

JscValue *fc_info(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    printf("forward_cache: vocab=%d dim=%d n_heads=%d n_layers=%d hidden=%d\n",
           g_fc.vocab_size, g_fc.dim, g_fc.n_heads, g_fc.n_layers, g_fc.hidden_dim);
    return jsc_vazio();
}

JscValue *make_forward_cache_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("init"),    jsc_native("init",    fc_init));
    jsc_map_set(m, jsc_string("forward"), jsc_native("forward", fc_forward));
    jsc_map_set(m, jsc_string("info"),    jsc_native("info",    fc_info));
    return m;
}

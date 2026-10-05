/* ============================================================
 * JSC$+ — modulo block
 * Feed-Forward + Layer Norm + Residual + Transformer Block
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

static void blk_error(EvalState *state, const char *msg) {
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
 * layer_norm.forward(X) — normaliza por linha
 * ============================================================ */

JscValue *ln_forward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        blk_error(state, "layer_norm.forward() espera 1 argumento (X)");
        return NULL;
    }

    JscValue *xv = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz X = mat_from_val(xv);
    jsc_value_free(xv);

    if (!X.data) {
        blk_error(state, "layer_norm: argumento invalido");
        return NULL;
    }

    double eps = 1e-5;
    Matriz out = {X.rows, X.cols, NULL};
    out.data = malloc(sizeof(double) * X.rows * X.cols);

    for (int i = 0; i < X.rows; i++) {
        /* Media */
        double media = 0.0;
        for (int j = 0; j < X.cols; j++) media += X.data[i * X.cols + j];
        media /= X.cols;

        /* Variancia */
        double var = 0.0;
        for (int j = 0; j < X.cols; j++) {
            double d = X.data[i * X.cols + j] - media;
            var += d * d;
        }
        var /= X.cols;

        /* Normaliza */
        double inv_std = 1.0 / sqrt(var + eps);
        for (int j = 0; j < X.cols; j++) {
            out.data[i * X.cols + j] = (X.data[i * X.cols + j] - media) * inv_std;
        }
    }

    JscValue *res = mat_to_val(out);
    free(X.data); free(out.data);
    return res;
}

/* ============================================================
 * feed_forward.forward(X) — Linear -> ReLU -> Linear
 * Pesos persistentes por processo
 * ============================================================ */

typedef struct {
    int dim;
    int hidden_dim;
    double *W1, *W2;   /* [dim, hidden] e [hidden, dim] */
    int inicializado;
} FeedForward;

static FeedForward g_ff = {0, 0, NULL, NULL, 0};

static void xavier(double *w, int n_in, int n_out) {
    double scale = sqrt(6.0 / (n_in + n_out));
    for (int i = 0; i < n_in * n_out; i++) {
        w[i] = ((double)rand() / RAND_MAX) * 2.0 * scale - scale;
    }
}

/* feed_forward.init(dim, hidden_dim) */
JscValue *ff_init(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        blk_error(state, "feed_forward.init() espera (dim, hidden_dim)");
        return NULL;
    }

    JscValue *dv = eval(n->args.items[0], env, state);
    JscValue *hv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    int dim = (int)jsc_value_to_int(dv);
    int hidden = (int)jsc_value_to_int(hv);
    jsc_value_free(dv);
    jsc_value_free(hv);

    if (g_ff.W1) { free(g_ff.W1); free(g_ff.W2); }

    g_ff.dim = dim;
    g_ff.hidden_dim = hidden;
    g_ff.W1 = malloc(sizeof(double) * dim * hidden);
    g_ff.W2 = malloc(sizeof(double) * hidden * dim);

    srand(time(NULL));
    xavier(g_ff.W1, dim, hidden);
    xavier(g_ff.W2, hidden, dim);

    g_ff.inicializado = 1;
    return jsc_vazio();
}

/* feed_forward.forward(X) */
JscValue *ff_forward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (!g_ff.inicializado) {
        blk_error(state, "feed_forward nao inicializado");
        return NULL;
    }

    if (n->args.count != 1) {
        blk_error(state, "feed_forward.forward() espera 1 argumento");
        return NULL;
    }

    JscValue *xv = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz X = mat_from_val(xv);
    jsc_value_free(xv);

    if (!X.data || X.cols != g_ff.dim) {
        blk_error(state, "feed_forward: X.cols deve ser dim");
        free(X.data);
        return NULL;
    }

    int n_tokens = X.rows; int d = g_ff.dim; int h = g_ff.hidden_dim;

    /* h1 = X @ W1  [n, h] */
    double *h1 = malloc(sizeof(double) * n_tokens * h);
    for (int i = 0; i < n_tokens; i++) {
        for (int j = 0; j < h; j++) {
            double soma = 0.0;
            for (int k = 0; k < d; k++) {
                soma += X.data[i * d + k] * g_ff.W1[k * h + j];
            }
            h1[i * h + j] = soma;
        }
    }

    /* ReLU */
    for (int i = 0; i < n_tokens * h; i++) {
        if (h1[i] < 0.0) h1[i] = 0.0;
    }

    /* h2 = h1 @ W2  [n, d] */
    double *h2 = malloc(sizeof(double) * n_tokens * d);
    for (int i = 0; i < n_tokens; i++) {
        for (int j = 0; j < d; j++) {
            double soma = 0.0;
            for (int k = 0; k < h; k++) {
                soma += h1[i * h + k] * g_ff.W2[k * d + j];
            }
            h2[i * d + j] = soma;
        }
    }

    Matriz out = {n_tokens, d, h2};
    JscValue *res = mat_to_val(out);

    free(X.data); free(h1); free(h2);
    return res;
}

/* ============================================================
 * Helpers
 * ============================================================ */

/* block.add(a, b) — soma elemento-a-elemento */
JscValue *block_add(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        blk_error(state, "block.add() espera 2 argumentos");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    JscValue *bv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz A = mat_from_val(av);
    Matriz B = mat_from_val(bv);
    jsc_value_free(av); jsc_value_free(bv);

    if (!A.data || !B.data || A.rows != B.rows || A.cols != B.cols) {
        blk_error(state, "block.add: dimensoes incompativeis");
        free(A.data); free(B.data);
        return NULL;
    }

    Matriz C = {A.rows, A.cols, NULL};
    C.data = malloc(sizeof(double) * A.rows * A.cols);
    for (int i = 0; i < A.rows * A.cols; i++) {
        C.data[i] = A.data[i] + B.data[i];
    }

    JscValue *res = mat_to_val(C);
    free(A.data); free(B.data); free(C.data);
    return res;
}

JscValue *make_block_module(void) {
    JscValue *m = jsc_map();

    /* Layer Norm */
    JscValue *ln = jsc_map();
    jsc_map_set(ln, jsc_string("forward"), jsc_native("forward", ln_forward));
    jsc_map_set(m, jsc_string("layer_norm"), ln);

    /* Feed-Forward */
    JscValue *ff = jsc_map();
    jsc_map_set(ff, jsc_string("init"),    jsc_native("init",    ff_init));
    jsc_map_set(ff, jsc_string("forward"), jsc_native("forward", ff_forward));
    jsc_map_set(m, jsc_string("feed_forward"), ff);

    /* Add (residual) */
    jsc_map_set(m, jsc_string("add"), jsc_native("add", block_add));

    return m;
}

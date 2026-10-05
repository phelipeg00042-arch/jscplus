/* ============================================================
 * JSC$+ — modulo optimizer
 * AdamW (Adam com weight decay)
 * ============================================================ */

#include "eval.h"
#include "value.h"
#include "env.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void opt_error(EvalState *state, const char *msg) {
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
 * AdamW: atualiza peso com base no gradiente
 *   w = w - lr * (m_hat / (sqrt(v_hat) + eps) + wd * w)
 * ============================================================ */

typedef struct {
    double lr;
    double beta1;
    double beta2;
    double eps;
    double weight_decay;
    int t;  /* passo atual */
} AdamW;

static AdamW g_adam = {0.001, 0.9, 0.999, 1e-8, 0.01, 0};

/* optimizer.init(lr, beta1, beta2, eps, weight_decay) */
JscValue *opt_init(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 5) {
        opt_error(state, "optimizer.init() espera (lr, beta1, beta2, eps, weight_decay)");
        return NULL;
    }

    double args[5];
    for (int i = 0; i < 5; i++) {
        JscValue *v = eval(n->args.items[i], env, state);
        if (state->had_error) return NULL;
        args[i] = jsc_value_to_float(v);
        jsc_value_free(v);
    }

    g_adam.lr = args[0];
    g_adam.beta1 = args[1];
    g_adam.beta2 = args[2];
    g_adam.eps = args[3];
    g_adam.weight_decay = args[4];
    g_adam.t = 0;

    printf("optimizer: AdamW(lr=%g, b1=%g, b2=%g, eps=%g, wd=%g)\n",
           g_adam.lr, g_adam.beta1, g_adam.beta2, g_adam.eps, g_adam.weight_decay);
    return jsc_vazio();
}

/* optimizer.step(W, dW, m, v)
 *   W:  matriz de pesos (modificada in-place)
 *   dW: gradiente
 *   m:  primeira momento (modificado in-place)
 *   v:  segunda momento (modificado in-place)
 *
 * Atualiza W, m, v usando AdamW
 */
JscValue *opt_step(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 4) {
        opt_error(state, "optimizer.step() espera (W, dW, m, v)");
        return NULL;
    }

    JscValue *wv = eval(n->args.items[0], env, state);
    JscValue *gw = eval(n->args.items[1], env, state);
    JscValue *mv = eval(n->args.items[2], env, state);
    JscValue *vv = eval(n->args.items[3], env, state);
    if (state->had_error) return NULL;

    Matriz W = mat_from_val(wv);
    Matriz dW = mat_from_val(gw);
    Matriz m = mat_from_val(mv);
    Matriz v = mat_from_val(vv);
    jsc_value_free(wv); jsc_value_free(gw); jsc_value_free(mv); jsc_value_free(vv);

    if (!W.data || !dW.data || !m.data || !v.data) {
        opt_error(state, "optimizer.step(): argumentos invalidos");
        free(W.data); free(dW.data); free(m.data); free(v.data);
        return NULL;
    }

    int total = W.rows * W.cols;
    if (dW.rows * dW.cols != total ||
        m.rows * m.cols != total ||
        v.rows * v.cols != total) {
        opt_error(state, "optimizer.step(): shapes incompativeis");
        free(W.data); free(dW.data); free(m.data); free(v.data);
        return NULL;
    }

    g_adam.t++;

    double b1 = g_adam.beta1;
    double b2 = g_adam.beta2;
    double bc1 = 1.0 - pow(b1, g_adam.t);
    double bc2 = 1.0 - pow(b2, g_adam.t);

    for (int i = 0; i < total; i++) {
        double g = dW.data[i];

        /* Atualiza momentos */
        m.data[i] = b1 * m.data[i] + (1.0 - b1) * g;
        v.data[i] = b2 * v.data[i] + (1.0 - b2) * g * g;

        /* Bias correction */
        double m_hat = m.data[i] / bc1;
        double v_hat = v.data[i] / bc2;

        /* Atualiza peso */
        double update = m_hat / (sqrt(v_hat) + g_adam.eps);
        update += g_adam.weight_decay * W.data[i];

        W.data[i] -= g_adam.lr * update;
    }

    /* Retorna W atualizado + m + v */
    JscValue *res = jsc_map();
    jsc_map_set(res, jsc_string("W"), mat_to_val(W));
    jsc_map_set(res, jsc_string("m"), mat_to_val(m));
    jsc_map_set(res, jsc_string("v"), mat_to_val(v));

    free(W.data); free(dW.data); free(m.data); free(v.data);
    return res;
}

/* optimizer.zeros(rows, cols) — cria matriz de zeros para m ou v */
JscValue *opt_zeros(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        opt_error(state, "optimizer.zeros() espera (rows, cols)");
        return NULL;
    }

    JscValue *rv = eval(n->args.items[0], env, state);
    JscValue *cv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    int rows = (int)jsc_value_to_int(rv);
    int cols = (int)jsc_value_to_int(cv);
    jsc_value_free(rv); jsc_value_free(cv);

    Matriz m = {rows, cols, NULL};
    m.data = calloc(rows * cols, sizeof(double));

    JscValue *res = mat_to_val(m);
    free(m.data);
    return res;
}

JscValue *opt_info(void *call_ptr, void *env_ptr, void *state_ptr) {
    (void)call_ptr; (void)env_ptr; (void)state_ptr;
    printf("optimizer AdamW: lr=%g t=%d\n", g_adam.lr, g_adam.t);
    return jsc_vazio();
}

JscValue *make_optimizer_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("init"),  jsc_native("init",  opt_init));
    jsc_map_set(m, jsc_string("step"),  jsc_native("step",  opt_step));
    jsc_map_set(m, jsc_string("zeros"), jsc_native("zeros", opt_zeros));
    jsc_map_set(m, jsc_string("info"),  jsc_native("info",  opt_info));
    return m;
}

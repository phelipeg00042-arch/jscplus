/* ============================================================
 * JSC$+ — backprop do layer norm
 * ============================================================ */

#include "eval.h"
#include "value.h"
#include "env.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void bln_error(EvalState *state, const char *msg) {
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
 * ln.backward(X, dY)
 *   X:  [n, d]  (input original, antes do layer norm)
 *   dY: [n, d]  (gradiente da loss em relacao ao output do LN)
 *
 * Retorna: dX [n, d]
 *
 * Formula:
 *   N = d
 *   media = mean(X, axis=1)
 *   var = var(X, axis=1)
 *   inv_std = 1/sqrt(var + eps)
 *
 *   dL/dx_i = (1/N) * inv_std * (N*dY_i - sum(dY) - y_i * sum(dY * y))
 *
 *   onde y_i = (x_i - media) * inv_std
 * ============================================================ */

JscValue *ln_backward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        bln_error(state, "ln.backward() espera (X, dY)");
        return NULL;
    }

    JscValue *xv = eval(n->args.items[0], env, state);
    JscValue *dyv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz X = mat_from_val(xv);
    Matriz dY = mat_from_val(dyv);
    jsc_value_free(xv); jsc_value_free(dyv);

    if (!X.data || !dY.data || X.rows != dY.rows || X.cols != dY.cols) {
        bln_error(state, "ln.backward(): dimensoes incompativeis");
        free(X.data); free(dY.data);
        return NULL;
    }

    int rows = X.rows;
    int N = X.cols;
    double eps = 1e-5;

    Matriz dX = {rows, N, NULL};
    dX.data = malloc(sizeof(double) * rows * N);

    for (int i = 0; i < rows; i++) {
        /* Media */
        double media = 0.0;
        for (int j = 0; j < N; j++) media += X.data[i * N + j];
        media /= N;

        /* Variancia */
        double var = 0.0;
        for (int j = 0; j < N; j++) {
            double diff = X.data[i * N + j] - media;
            var += diff * diff;
        }
        var /= N;

        double inv_std = 1.0 / sqrt(var + eps);

        /* y = (x - media) * inv_std */
        double *y = malloc(sizeof(double) * N);
        for (int j = 0; j < N; j++) {
            y[j] = (X.data[i * N + j] - media) * inv_std;
        }

        /* sum(dY) e sum(dY * y) */
        double sum_dy = 0.0;
        double sum_dy_y = 0.0;
        for (int j = 0; j < N; j++) {
            sum_dy += dY.data[i * N + j];
            sum_dy_y += dY.data[i * N + j] * y[j];
        }

        /* dX */
        for (int j = 0; j < N; j++) {
            double term = (N * dY.data[i * N + j] - sum_dy - y[j] * sum_dy_y);
            dX.data[i * N + j] = (1.0 / N) * inv_std * term;
        }

        free(y);
    }

    JscValue *res = mat_to_val(dX);
    free(X.data); free(dY.data); free(dX.data);
    return res;
}

JscValue *make_backprop_ln_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("backward"), jsc_native("backward", ln_backward));
    return m;
}

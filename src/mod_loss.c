/* ============================================================
 * JSC$+ — modulo loss
 * Cross-entropy loss + helper para backprop
 * ============================================================ */

#include "eval.h"
#include "value.h"
#include "env.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void loss_error(EvalState *state, const char *msg) {
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

/* ============================================================
 * Softmax por linha (in-place)
 * ============================================================ */

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

/* ============================================================
 * Cross-entropy loss
 * loss.forward(logits, targets)
 *   logits: [n, vocab]  (antes do softmax)
 *   targets: array de n IDs
 * Retorna: scalar (loss media)
 * ============================================================ */

JscValue *loss_forward(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        loss_error(state, "loss.forward() espera (logits, targets)");
        return NULL;
    }

    JscValue *lv = eval(n->args.items[0], env, state);
    JscValue *tv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz L = mat_from_val(lv);
    jsc_value_free(lv);

    if (!L.data) {
        loss_error(state, "loss.forward(): logits invalidos");
        jsc_value_free(tv);
        return NULL;
    }

    if (tv->type != VAL_ARRAY) {
        free(L.data);
        jsc_value_free(tv);
        loss_error(state, "loss.forward(): targets devem ser array");
        return NULL;
    }

    int n_tokens = L.rows;
    int vocab = L.cols;

    if ((int)tv->as.array.count != n_tokens) {
        free(L.data);
        jsc_value_free(tv);
        loss_error(state, "loss.forward(): targets.len deve ser n_tokens");
        return NULL;
    }

    /* Aplica softmax nos logits */
    softmax_linhas(L.data, n_tokens, vocab);

    /* Cross-entropy media */
    double loss_total = 0.0;
    double eps = 1e-12;

    for (int i = 0; i < n_tokens; i++) {
        int target = (int)jsc_value_to_int(tv->as.array.items[i]);
        if (target < 0 || target >= vocab) target = 1;

        double prob = L.data[i * vocab + target];
        if (prob < eps) prob = eps;

        loss_total += -log(prob);
    }

    double loss_media = loss_total / n_tokens;

    free(L.data);
    jsc_value_free(tv);

    return jsc_float(loss_media);
}

/* ============================================================
 * loss.perplexity(logits, targets)
 * Perplexity = exp(loss)
 * ============================================================ */

JscValue *loss_perplexity(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        loss_error(state, "loss.perplexity() espera (logits, targets)");
        return NULL;
    }

    /* Reusa loss_forward */
    JscValue *loss_val = loss_forward(call_ptr, env_ptr, state_ptr);
    if (state->had_error) return NULL;

    double loss = jsc_value_to_float(loss_val);
    jsc_value_free(loss_val);

    return jsc_float(exp(loss));
}

/* ============================================================
 * loss.grad_logits(logits, targets)
 * Calcula gradiente da loss em relacao aos logits.
 *   grad = softmax(logits) - one_hot(target)
 * Retorna: [n, vocab] (gradiente)
 * ============================================================ */

JscValue *loss_grad_logits(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        loss_error(state, "loss.grad_logits() espera (logits, targets)");
        return NULL;
    }

    JscValue *lv = eval(n->args.items[0], env, state);
    JscValue *tv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz L = mat_from_val(lv);
    jsc_value_free(lv);

    if (!L.data || tv->type != VAL_ARRAY) {
        free(L.data);
        jsc_value_free(tv);
        loss_error(state, "loss.grad_logits(): argumentos invalidos");
        return NULL;
    }

    int n_tokens = L.rows;
    int vocab = L.cols;

    if ((int)tv->as.array.count != n_tokens) {
        free(L.data);
        jsc_value_free(tv);
        loss_error(state, "loss.grad_logits(): targets.len != n_tokens");
        return NULL;
    }

    /* Softmax */
    softmax_linhas(L.data, n_tokens, vocab);

    /* Subtrai 1 do target */
    for (int i = 0; i < n_tokens; i++) {
        int target = (int)jsc_value_to_int(tv->as.array.items[i]);
        if (target < 0 || target >= vocab) target = 1;
        L.data[i * vocab + target] -= 1.0;
    }

    /* Divide por n_tokens (media) */
    for (int i = 0; i < n_tokens * vocab; i++) {
        L.data[i] /= n_tokens;
    }

    /* Empacota */
    JscValue *map = jsc_map();
    jsc_map_set(map, jsc_string("rows"), jsc_int(n_tokens));
    jsc_map_set(map, jsc_string("cols"), jsc_int(vocab));
    JscValue *arr = jsc_array();
    for (int i = 0; i < n_tokens * vocab; i++) {
        jsc_array_push(arr, jsc_float(L.data[i]));
    }
    jsc_map_set(map, jsc_string("data"), arr);

    free(L.data);
    jsc_value_free(tv);
    return map;
}

JscValue *make_loss_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("forward"),      jsc_native("forward",      loss_forward));
    jsc_map_set(m, jsc_string("perplexity"),   jsc_native("perplexity",   loss_perplexity));
    jsc_map_set(m, jsc_string("grad_logits"),  jsc_native("grad_logits",  loss_grad_logits));
    return m;
}

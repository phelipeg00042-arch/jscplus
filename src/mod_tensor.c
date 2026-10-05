/* ============================================================
 * JSC$+ — modulo tensor
 * Operacoes de matriz para IA (matmul, add, activations)
 * ============================================================ */

#include "eval.h"
#include "value.h"
#include "env.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================
 * Helpers
 * ============================================================ */

typedef struct {
    int rows;
    int cols;
    double *data;
} Matriz;

/* Extrai matriz de um JscValue (map) */
static Matriz mat_from_value(JscValue *v) {
    Matriz m = {0, 0, NULL};
    if (!v || v->type != VAL_MAP) return m;

    JscValue *rows_v = jsc_map_get(v, jsc_string("rows"));
    JscValue *cols_v = jsc_map_get(v, jsc_string("cols"));
    JscValue *data_v = jsc_map_get(v, jsc_string("data"));

    if (!rows_v || !cols_v || !data_v) return m;

    m.rows = (int)jsc_value_to_int(rows_v);
    m.cols = (int)jsc_value_to_int(cols_v);

    if (data_v->type != VAL_ARRAY) return m;

    int n = (int)data_v->as.array.count;
    m.data = (double *)malloc(sizeof(double) * n);
    if (!m.data) return m;

    for (int i = 0; i < n; i++) {
        m.data[i] = jsc_value_to_float(data_v->as.array.items[i]);
    }

    return m;
}

/* Cria map JSC$+ a partir de Matriz */
static JscValue *mat_to_value(Matriz m) {
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

/* Helper: reporta erro — usa jsc_string pra mensagem */
static void tensor_error(EvalState *state, const char *msg) {
    if (state) {
        snprintf(state->error_message, sizeof(state->error_message), "%s", msg);
        state->had_error = 1;
    }
    fprintf(stderr, "JSC$+ erro: %s\n", msg);
}

/* ============================================================
 * Operacoes
 * ============================================================ */

JscValue *tensor_zeros(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        tensor_error(state, "tensor.zeros() espera 2 argumentos");
        return NULL;
    }

    JscValue *r = eval(n->args.items[0], env, state);
    JscValue *c = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    int rows = (int)jsc_value_to_int(r);
    int cols = (int)jsc_value_to_int(c);
    jsc_value_free(r);
    jsc_value_free(c);

    Matriz m = {rows, cols, NULL};
    m.data = (double *)calloc(rows * cols, sizeof(double));
    if (!m.data) return NULL;

    JscValue *res = mat_to_value(m);
    free(m.data);
    return res;
}

JscValue *tensor_random(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        tensor_error(state, "tensor.random() espera 2 argumentos");
        return NULL;
    }

    JscValue *r = eval(n->args.items[0], env, state);
    JscValue *c = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    int rows = (int)jsc_value_to_int(r);
    int cols = (int)jsc_value_to_int(c);
    jsc_value_free(r);
    jsc_value_free(c);

    Matriz m = {rows, cols, NULL};
    m.data = (double *)malloc(sizeof(double) * rows * cols);
    if (!m.data) return NULL;

    for (int i = 0; i < rows * cols; i++) {
        m.data[i] = ((double)rand() / RAND_MAX) * 2.0 - 1.0;
    }

    JscValue *res = mat_to_value(m);
    free(m.data);
    return res;
}

JscValue *tensor_matmul(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        tensor_error(state, "tensor.matmul() espera 2 argumentos");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    JscValue *bv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz a = mat_from_value(av);
    Matriz b = mat_from_value(bv);
    jsc_value_free(av);
    jsc_value_free(bv);

    if (!a.data || !b.data) {
        tensor_error(state, "tensor.matmul(): argumentos invalidos");
        free(a.data); free(b.data);
        return NULL;
    }

    if (a.cols != b.rows) {
        tensor_error(state, "tensor.matmul(): dimensoes incompativeis");
        free(a.data); free(b.data);
        return NULL;
    }

    Matriz c = {a.rows, b.cols, NULL};
    c.data = (double *)calloc(a.rows * b.cols, sizeof(double));
    if (!c.data) { free(a.data); free(b.data); return NULL; }

    for (int i = 0; i < a.rows; i++) {
        for (int j = 0; j < b.cols; j++) {
            double soma = 0.0;
            for (int k = 0; k < a.cols; k++) {
                soma += a.data[i * a.cols + k] * b.data[k * b.cols + j];
            }
            c.data[i * b.cols + j] = soma;
        }
    }

    JscValue *res = mat_to_value(c);

    free(a.data);
    free(b.data);
    free(c.data);
    return res;
}

JscValue *tensor_add(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 2) {
        tensor_error(state, "tensor.add() espera 2 argumentos");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    JscValue *bv = eval(n->args.items[1], env, state);
    if (state->had_error) return NULL;

    Matriz a = mat_from_value(av);
    Matriz b = mat_from_value(bv);
    jsc_value_free(av);
    jsc_value_free(bv);

    if (!a.data || !b.data || a.rows != b.rows || a.cols != b.cols) {
        tensor_error(state, "tensor.add(): dimensoes incompativeis");
        free(a.data); free(b.data);
        return NULL;
    }

    Matriz c = {a.rows, a.cols, NULL};
    c.data = (double *)malloc(sizeof(double) * a.rows * a.cols);
    if (!c.data) { free(a.data); free(b.data); return NULL; }

    for (int i = 0; i < a.rows * a.cols; i++) {
        c.data[i] = a.data[i] + b.data[i];
    }

    JscValue *res = mat_to_value(c);

    free(a.data);
    free(b.data);
    free(c.data);
    return res;
}

JscValue *tensor_relu(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        tensor_error(state, "tensor.relu() espera 1 argumento");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz a = mat_from_value(av);
    jsc_value_free(av);

    if (!a.data) {
        tensor_error(state, "tensor.relu(): argumento invalido");
        return NULL;
    }

    for (int i = 0; i < a.rows * a.cols; i++) {
        if (a.data[i] < 0.0) a.data[i] = 0.0;
    }

    JscValue *res = mat_to_value(a);
    free(a.data);
    return res;
}

JscValue *tensor_sigmoid(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        tensor_error(state, "tensor.sigmoid() espera 1 argumento");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz a = mat_from_value(av);
    jsc_value_free(av);

    if (!a.data) {
        tensor_error(state, "tensor.sigmoid(): argumento invalido");
        return NULL;
    }

    for (int i = 0; i < a.rows * a.cols; i++) {
        a.data[i] = 1.0 / (1.0 + exp(-a.data[i]));
    }

    JscValue *res = mat_to_value(a);
    free(a.data);
    return res;
}

JscValue *tensor_softmax(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        tensor_error(state, "tensor.softmax() espera 1 argumento");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz a = mat_from_value(av);
    jsc_value_free(av);

    if (!a.data) {
        tensor_error(state, "tensor.softmax(): argumento invalido");
        return NULL;
    }

    for (int i = 0; i < a.rows; i++) {
        double max = a.data[i * a.cols];
        for (int j = 1; j < a.cols; j++) {
            if (a.data[i * a.cols + j] > max) max = a.data[i * a.cols + j];
        }
        double soma = 0.0;
        for (int j = 0; j < a.cols; j++) {
            a.data[i * a.cols + j] = exp(a.data[i * a.cols + j] - max);
            soma += a.data[i * a.cols + j];
        }
        for (int j = 0; j < a.cols; j++) {
            a.data[i * a.cols + j] /= soma;
        }
    }

    JscValue *res = mat_to_value(a);
    free(a.data);
    return res;
}

JscValue *tensor_shape(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        tensor_error(state, "tensor.shape() espera 1 argumento");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz a = mat_from_value(av);
    jsc_value_free(av);

    if (!a.data) return jsc_array();

    JscValue *arr = jsc_array();
    jsc_array_push(arr, jsc_int(a.rows));
    jsc_array_push(arr, jsc_int(a.cols));
    free(a.data);
    return arr;
}


/* tensor.print(a) — imprime matriz formatada */
JscValue *tensor_print(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        tensor_error(state, "tensor.print() espera 1 argumento");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz a = mat_from_value(av);
    jsc_value_free(av);

    if (!a.data) {
        tensor_error(state, "tensor.print(): argumento invalido");
        return NULL;
    }

    printf("tensor(%d, %d):\n", a.rows, a.cols);
    for (int i = 0; i < a.rows; i++) {
        printf("  [");
        for (int j = 0; j < a.cols; j++) {
            printf(" %8.4f", a.data[i * a.cols + j]);
            if (j < a.cols - 1) printf(",");
        }
        printf(" ]\n");
    }
    free(a.data);
    return jsc_vazio();
}

/* tensor.to_str(a) — retorna string formatada */
JscValue *tensor_to_str(void *call_ptr, void *env_ptr, void *state_ptr) {
    NodeCall *n = (NodeCall *)call_ptr;
    Env *env = (Env *)env_ptr;
    EvalState *state = (EvalState *)state_ptr;

    if (n->args.count != 1) {
        tensor_error(state, "tensor.to_str() espera 1 argumento");
        return NULL;
    }

    JscValue *av = eval(n->args.items[0], env, state);
    if (state->had_error) return NULL;

    Matriz a = mat_from_value(av);
    jsc_value_free(av);

    if (!a.data) return jsc_string("tensor(invalido)");

    size_t cap = 256 + a.rows * a.cols * 20;
    char *buf = (char *)malloc(cap);
    size_t pos = 0;

    pos += snprintf(buf + pos, cap - pos, "tensor(%d, %d):\n", a.rows, a.cols);
    for (int i = 0; i < a.rows; i++) {
        pos += snprintf(buf + pos, cap - pos, "  [");
        for (int j = 0; j < a.cols; j++) {
            pos += snprintf(buf + pos, cap - pos, " %8.4f", a.data[i * a.cols + j]);
            if (j < a.cols - 1) pos += snprintf(buf + pos, cap - pos, ",");
        }
        pos += snprintf(buf + pos, cap - pos, " ]\n");
    }

    JscValue *res = jsc_string(buf);
    free(buf);
    free(a.data);
    return res;
}

JscValue *make_tensor_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, jsc_string("zeros"),   jsc_native("zeros",   tensor_zeros));
    jsc_map_set(m, jsc_string("random"),  jsc_native("random",  tensor_random));
    jsc_map_set(m, jsc_string("matmul"),  jsc_native("matmul",  tensor_matmul));
    jsc_map_set(m, jsc_string("add"),     jsc_native("add",     tensor_add));
    jsc_map_set(m, jsc_string("relu"),    jsc_native("relu",    tensor_relu));
    jsc_map_set(m, jsc_string("sigmoid"), jsc_native("sigmoid", tensor_sigmoid));
    jsc_map_set(m, jsc_string("softmax"), jsc_native("softmax", tensor_softmax));
    jsc_map_set(m, jsc_string("shape"),   jsc_native("shape",   tensor_shape));
    jsc_map_set(m, jsc_string("print"),   jsc_native("print",   tensor_print));
    jsc_map_set(m, jsc_string("to_str"),  jsc_native("to_str",  tensor_to_str));
    return m;
}

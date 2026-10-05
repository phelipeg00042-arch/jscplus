#ifndef JSC_RUNTIME_H
#define JSC_RUNTIME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================
 * JSC$+ Runtime
 * Funcoes auxiliares que o codigo C gerado usa.
 * ============================================================ */

typedef enum {
    JSC_VAZIO,
    JSC_BOOL,
    JSC_INT,
    JSC_FLOAT,
    JSC_STRING,
    JSC_ARRAY,
    JSC_MAP,
    JSC_NATIVE,
    JSC_THREAD
} JscType;

typedef struct JscValue JscValue;

struct JscValue {
    JscType type;
    union {
        int      b;
        long long i;
        double   f;
        char    *s;
        struct {
            JscValue **items;
            size_t     count;
            size_t     cap;
        } array;
        struct {
            char     **keys;
            JscValue **values;
            size_t     count;
            size_t     cap;
        } map;
        void *ptr;
    } as;
};

/* ---- Ciclo de vida ---- */
void jsc_init(void);
void jsc_cleanup(void);

/* ---- Construtores ---- */
JscValue *jsc_int(long long v);
JscValue *jsc_float(double v);
JscValue *jsc_bool(int b);
JscValue *jsc_string(const char *s);
JscValue *jsc_vazio(void);
JscValue *jsc_array(void);
JscValue *jsc_array_from(int n, ...);
void      jsc_array_push(JscValue *arr, JscValue *v);
JscValue *jsc_array_get(JscValue *arr, int idx);
JscValue *jsc_array_pop(JscValue *arr);
void      jsc_array_set(JscValue *arr, int idx, JscValue *v);
long long jsc_array_len(JscValue *arr);

/* ---- Maps ---- */
JscValue *jsc_map(void);
JscValue *jsc_map_from(int n, ...);
void      jsc_map_set(JscValue *map, const char *key, JscValue *v);
JscValue *jsc_map_get(JscValue *map, const char *key);
int       jsc_map_has(JscValue *map, const char *key);
long long jsc_map_len(JscValue *map);

/* ---- Metodos genericos ---- */
JscValue *jsc_index(JscValue *obj, JscValue *idx);
JscValue *jsc_field(JscValue *obj, const char *name);
JscValue *jsc_method_call(JscValue *obj, const char *name, int argc, ...);
void jsc_register_method(const char *classe, const char *metodo, void *fn);
void *jsc_lookup_method(const char *classe, const char *metodo);

/* ---- FFI (import_c, call_c) ---- */
JscValue *jsc_import_c(const char *path);

/* Threads */
JscValue *jsc_thread_new(JscValue *(*fn)(void*), void *arg);
JscValue *jsc_thread_join(JscValue *thread);
JscValue *jsc_call_c(JscValue *lib, const char *nome, const char *tipo, int argc, ...);

/* ---- Modulos nativos ---- */
JscValue *make_math_module(void);
JscValue *make_time_module(void);
JscValue *make_string_module(void);
JscValue *make_crypto_module(void);
JscValue *make_fs_module(void);
JscValue *make_os_module(void);
JscValue *make_proc_module(void);
JscValue *make_net_module(void);
JscValue *make_json_module(void);
JscValue *make_hack_module(void);

/* Helpers pra nativos */
JscValue *jsc_native(const char *name, JscValue *(*fn)(JscValue **args, int argc));
JscValue *jsc_call_native(JscValue *fn, JscValue **args, int argc);

/* Dispatcher de modulo */
JscValue *jsc_import_module(const char *name);

/* ---- Operacoes aritmeticas ---- */
JscValue *jsc_add(JscValue *a, JscValue *b);
JscValue *jsc_sub(JscValue *a, JscValue *b);
JscValue *jsc_mul(JscValue *a, JscValue *b);
JscValue *jsc_div(JscValue *a, JscValue *b);
JscValue *jsc_mod(JscValue *a, JscValue *b);
JscValue *jsc_neg(JscValue *a);

/* ---- Comparacoes ---- */
JscValue *jsc_eq(JscValue *a, JscValue *b);
JscValue *jsc_neq(JscValue *a, JscValue *b);
JscValue *jsc_lt(JscValue *a, JscValue *b);
JscValue *jsc_gt(JscValue *a, JscValue *b);
JscValue *jsc_lte(JscValue *a, JscValue *b);
JscValue *jsc_gte(JscValue *a, JscValue *b);

/* ---- Logica ---- */
int       jsc_truthy(JscValue *v);
JscValue *jsc_not(JscValue *v);

/* ---- I/O ---- */
void jsc_print(JscValue *v);
void jsc_println(JscValue *v);
void jsc_print_str(const char *s);

/* ---- Conversao ---- */
const char *jsc_to_cstr(JscValue *v);
long long   jsc_to_int(JscValue *v);
double      jsc_to_float(JscValue *v);

/* ---- GC simplificado (refcount zero: libera na hora) ---- */
void jsc_free(JscValue *v);

#endif

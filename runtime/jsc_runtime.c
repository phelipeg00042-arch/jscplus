#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "jsc_runtime.h"
#include <stdarg.h>
#include <dlfcn.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ctype.h>
#include <pthread.h>

/* ============================================================
 * JSC$+ Runtime - implementacao
 * ============================================================ */

void jsc_init(void) {
    /* Nada pra inicializar por enquanto */
}

void jsc_cleanup(void) {
    /* Nada pra limpar por enquanto */
}

/* ---- Construtores ---- */

JscValue *jsc_int(long long v) {
    JscValue *j = (JscValue *)malloc(sizeof(JscValue));
    j->type = JSC_INT;
    j->as.i = v;
    return j;
}

JscValue *jsc_float(double v) {
    JscValue *j = (JscValue *)malloc(sizeof(JscValue));
    j->type = JSC_FLOAT;
    j->as.f = v;
    return j;
}

JscValue *jsc_bool(int b) {
    JscValue *j = (JscValue *)malloc(sizeof(JscValue));
    j->type = JSC_BOOL;
    j->as.b = b ? 1 : 0;
    return j;
}

JscValue *jsc_string(const char *s) {
    JscValue *j = (JscValue *)malloc(sizeof(JscValue));
    j->type = JSC_STRING;
    j->as.s = strdup(s ? s : "");
    return j;
}

JscValue *jsc_vazio(void) {
    JscValue *j = (JscValue *)malloc(sizeof(JscValue));
    j->type = JSC_VAZIO;
    return j;
}

JscValue *jsc_array(void) {
    JscValue *j = (JscValue *)malloc(sizeof(JscValue));
    j->type = JSC_ARRAY;
    j->as.array.cap = 8;
    j->as.array.count = 0;
    j->as.array.items = (JscValue **)malloc(sizeof(JscValue *) * 8);
    return j;
}

JscValue *jsc_array_from(int n, ...) {
    JscValue *arr = jsc_array();
    va_list args;
    va_start(args, n);
    for (int i = 0; i < n; i++) {
        JscValue *v = va_arg(args, JscValue *);
        jsc_array_push(arr, v);
    }
    va_end(args);
    return arr;
}

void jsc_array_push(JscValue *arr, JscValue *v) {
    if (!arr || arr->type != JSC_ARRAY) return;
    if (arr->as.array.count >= arr->as.array.cap) {
        arr->as.array.cap *= 2;
        arr->as.array.items = (JscValue **)realloc(
            arr->as.array.items,
            sizeof(JscValue *) * arr->as.array.cap
        );
    }
    arr->as.array.items[arr->as.array.count++] = v;
}

JscValue *jsc_array_get(JscValue *arr, int idx) {
    if (!arr || arr->type != JSC_ARRAY) return jsc_vazio();
    if (idx < 0 || (size_t)idx >= arr->as.array.count) return jsc_vazio();
    return arr->as.array.items[idx];
}

JscValue *jsc_array_pop(JscValue *arr) {
    if (!arr || arr->type != JSC_ARRAY) return jsc_vazio();
    if (arr->as.array.count == 0) return jsc_vazio();
    return arr->as.array.items[--arr->as.array.count];
}

void jsc_array_set(JscValue *arr, int idx, JscValue *v) {
    if (!arr || arr->type != JSC_ARRAY) return;
    if (idx < 0 || (size_t)idx >= arr->as.array.count) return;
    arr->as.array.items[idx] = v;
}

long long jsc_array_len(JscValue *arr) {
    if (!arr || arr->type != JSC_ARRAY) return 0;
    return (long long)arr->as.array.count;
}

/* ---- Maps ---- */

JscValue *jsc_map(void) {
    JscValue *j = (JscValue *)malloc(sizeof(JscValue));
    j->type = JSC_MAP;
    j->as.map.cap = 8;
    j->as.map.count = 0;
    j->as.map.keys = (char **)malloc(sizeof(char *) * 8);
    j->as.map.values = (JscValue **)malloc(sizeof(JscValue *) * 8);
    return j;
}

JscValue *jsc_map_from(int n, ...) {
    JscValue *map = jsc_map();
    va_list args;
    va_start(args, n);
    for (int i = 0; i < n; i++) {
        const char *key = va_arg(args, const char *);
        JscValue *val = va_arg(args, JscValue *);
        jsc_map_set(map, key, val);
    }
    va_end(args);
    return map;
}

void jsc_map_set(JscValue *map, const char *key, JscValue *v) {
    if (!map || map->type != JSC_MAP) return;
    
    /* Procura chave existente */
    for (size_t i = 0; i < map->as.map.count; i++) {
        if (strcmp(map->as.map.keys[i], key) == 0) {
            map->as.map.values[i] = v;
            return;
        }
    }
    
    /* Adiciona nova */
    if (map->as.map.count >= map->as.map.cap) {
        map->as.map.cap *= 2;
        map->as.map.keys = (char **)realloc(map->as.map.keys,
            sizeof(char *) * map->as.map.cap);
        map->as.map.values = (JscValue **)realloc(map->as.map.values,
            sizeof(JscValue *) * map->as.map.cap);
    }
    map->as.map.keys[map->as.map.count] = strdup(key);
    map->as.map.values[map->as.map.count] = v;
    map->as.map.count++;
}

JscValue *jsc_map_get(JscValue *map, const char *key) {
    if (!map || map->type != JSC_MAP) return jsc_vazio();
    for (size_t i = 0; i < map->as.map.count; i++) {
        if (strcmp(map->as.map.keys[i], key) == 0) {
            return map->as.map.values[i];
        }
    }
    return jsc_vazio();
}

int jsc_map_has(JscValue *map, const char *key) {
    if (!map || map->type != JSC_MAP) return 0;
    for (size_t i = 0; i < map->as.map.count; i++) {
        if (strcmp(map->as.map.keys[i], key) == 0) return 1;
    }
    return 0;
}

long long jsc_map_len(JscValue *map) {
    if (!map || map->type != JSC_MAP) return 0;
    return (long long)map->as.map.count;
}

/* ---- Metodos genericos ---- */

JscValue *jsc_index(JscValue *obj, JscValue *idx) {
    if (!obj) return jsc_vazio();
    
    if (obj->type == JSC_ARRAY) {
        int i = (int)jsc_to_int(idx);
        return jsc_array_get(obj, i);
    }
    if (obj->type == JSC_MAP) {
        if (idx->type == JSC_STRING) {
            return jsc_map_get(obj, idx->as.s);
        }
        return jsc_vazio();
    }
    if (obj->type == JSC_STRING) {
        int i = (int)jsc_to_int(idx);
        const char *s = obj->as.s;
        if (i < 0 || i >= (int)strlen(s)) return jsc_vazio();
        char buf[2] = { s[i], 0 };
        return jsc_string(buf);
    }
    return jsc_vazio();
}

JscValue *jsc_field(JscValue *obj, const char *name) {
    if (!obj) return jsc_vazio();
    /* Se for MAP, acessa pela chave */
    if (obj->type == JSC_MAP) {
        return jsc_map_get(obj, name);
    }
    return jsc_vazio();
}


/* ============================================================
 * Metodos de classe (registrados por nome)
 * ============================================================ */

typedef JscValue *(*ClassMethodPtr)(JscValue *this_, ...);

/* Tabela global: classe -> metodo -> funcao C */
#define MAX_METHODS 256
typedef struct {
    char *classe;
    char *metodo;
    void *fn;
} MethodEntry;

static MethodEntry method_table[MAX_METHODS];
static int method_count = 0;

void jsc_register_method(const char *classe, const char *metodo, void *fn) {
    if (method_count < MAX_METHODS) {
        method_table[method_count].classe = strdup(classe);
        method_table[method_count].metodo = strdup(metodo);
        method_table[method_count].fn = fn;
        method_count++;
    }
}

void *jsc_lookup_method(const char *classe, const char *metodo) {
    for (int i = 0; i < method_count; i++) {
        if (strcmp(method_table[i].classe, classe) == 0 &&
            strcmp(method_table[i].metodo, metodo) == 0) {
            return method_table[i].fn;
        }
    }
    return NULL;
}

JscValue *jsc_method_call(JscValue *obj, const char *name, int argc, ...) {
    va_list args;
    va_start(args, argc);
    
    /* .len() */
    if (strcmp(name, "len") == 0) {
        va_end(args);
        if (obj->type == JSC_ARRAY) return jsc_int(jsc_array_len(obj));
        if (obj->type == JSC_MAP)   return jsc_int(jsc_map_len(obj));
        if (obj->type == JSC_STRING) return jsc_int(strlen(obj->as.s));
        return jsc_int(0);
    }
    
    /* .push(x) */
    if (strcmp(name, "push") == 0 && obj->type == JSC_ARRAY) {
        if (argc >= 1) {
            JscValue *v = va_arg(args, JscValue *);
            jsc_array_push(obj, v);
        }
        va_end(args);
        return obj;
    }
    
    /* .pop() */
    if (strcmp(name, "pop") == 0 && obj->type == JSC_ARRAY) {
        va_end(args);
        return jsc_array_pop(obj);
    }
    
    /* Metodo de classe registrado */
    {
        JscValue *cls_v = jsc_map_get(obj, "__class__");
        if (cls_v && cls_v->type == JSC_STRING) {
            void *fn = jsc_lookup_method(cls_v->as.s, name);
            if (fn) {
                /* Coleta args num array */
                JscValue *argv[16];
                for (int i = 0; i < argc && i < 16; i++) {
                    argv[i] = va_arg(args, JscValue *);
                }
                va_end(args);
                
                /* Chama com base no numero de args */
                switch (argc) {
                    case 0: {
                        typedef JscValue *(*f_t)(JscValue *);
                        return ((f_t)fn)(obj);
                    }
                    case 1: {
                        typedef JscValue *(*f_t)(JscValue *, JscValue *);
                        return ((f_t)fn)(obj, argv[0]);
                    }
                    case 2: {
                        typedef JscValue *(*f_t)(JscValue *, JscValue *, JscValue *);
                        return ((f_t)fn)(obj, argv[0], argv[1]);
                    }
                    case 3: {
                        typedef JscValue *(*f_t)(JscValue *, JscValue *, JscValue *, JscValue *);
                        return ((f_t)fn)(obj, argv[0], argv[1], argv[2]);
                    }
                    case 4: {
                        typedef JscValue *(*f_t)(JscValue *, JscValue *, JscValue *, JscValue *, JscValue *);
                        return ((f_t)fn)(obj, argv[0], argv[1], argv[2], argv[3]);
                    }
                    default:
                        return jsc_vazio();
                }
            }
        }
    }
    
    /* .has(k) */
    if (strcmp(name, "has") == 0 && obj->type == JSC_MAP) {
        if (argc >= 1) {
            JscValue *k = va_arg(args, JscValue *);
            if (k->type == JSC_STRING) {
                int r = jsc_map_has(obj, k->as.s);
                va_end(args);
                return jsc_bool(r);
            }
        }
        va_end(args);
        return jsc_bool(0);
    }
    
    va_end(args);
    return jsc_vazio();
}

/* ---- Helpers internos ---- */

static double to_num(JscValue *v) {
    if (!v) return 0.0;
    switch (v->type) {
        case JSC_INT:   return (double)v->as.i;
        case JSC_FLOAT: return v->as.f;
        case JSC_BOOL:  return (double)v->as.b;
        default:        return 0.0;
    }
}

static int is_num(JscValue *v) {
    if (!v) return 0;
    return v->type == JSC_INT || v->type == JSC_FLOAT || v->type == JSC_BOOL;
}

static int is_int_like(JscValue *v) {
    return v && (v->type == JSC_INT || v->type == JSC_BOOL);
}

/* ---- Aritmetica ---- */

JscValue *jsc_add(JscValue *a, JscValue *b) {
    /* String + String = concatenacao */
    if (a && a->type == JSC_STRING && b && b->type == JSC_STRING) {
        size_t la = strlen(a->as.s);
        size_t lb = strlen(b->as.s);
        char *buf = (char *)malloc(la + lb + 1);
        memcpy(buf, a->as.s, la);
        memcpy(buf + la, b->as.s, lb);
        buf[la + lb] = 0;
        JscValue *r = jsc_string(buf);
        free(buf);
        return r;
    }
    
    /* Numeros */
    if (is_int_like(a) && is_int_like(b)) {
        return jsc_int(a->as.i + b->as.i);
    }
    return jsc_float(to_num(a) + to_num(b));
}

JscValue *jsc_sub(JscValue *a, JscValue *b) {
    if (is_int_like(a) && is_int_like(b)) {
        return jsc_int(a->as.i - b->as.i);
    }
    return jsc_float(to_num(a) - to_num(b));
}

JscValue *jsc_mul(JscValue *a, JscValue *b) {
    if (is_int_like(a) && is_int_like(b)) {
        return jsc_int(a->as.i * b->as.i);
    }
    return jsc_float(to_num(a) * to_num(b));
}

JscValue *jsc_div(JscValue *a, JscValue *b) {
    double den = to_num(b);
    if (den == 0.0) {
        fprintf(stderr, "JSC$+ erro: divisao por zero\n");
        return jsc_float(0.0);
    }
    return jsc_float(to_num(a) / den);
}

JscValue *jsc_mod(JscValue *a, JscValue *b) {
    if (is_int_like(a) && is_int_like(b)) {
        return jsc_int(a->as.i % b->as.i);
    }
    return jsc_float(fmod(to_num(a), to_num(b)));
}

JscValue *jsc_neg(JscValue *a) {
    if (is_int_like(a)) return jsc_int(-a->as.i);
    return jsc_float(-to_num(a));
}

/* ---- Comparacoes ---- */

JscValue *jsc_eq(JscValue *a, JscValue *b) {
    if (a && b && a->type == JSC_STRING && b->type == JSC_STRING) {
        return jsc_bool(strcmp(a->as.s, b->as.s) == 0);
    }
    if (is_num(a) && is_num(b)) {
        return jsc_bool(to_num(a) == to_num(b));
    }
    return jsc_bool(0);
}

JscValue *jsc_neq(JscValue *a, JscValue *b) {
    JscValue *eq = jsc_eq(a, b);
    int r = eq->as.b;
    jsc_free(eq);
    return jsc_bool(!r);
}

JscValue *jsc_lt(JscValue *a, JscValue *b) {
    if (is_num(a) && is_num(b)) return jsc_bool(to_num(a) < to_num(b));
    if (a && b && a->type == JSC_STRING && b->type == JSC_STRING)
        return jsc_bool(strcmp(a->as.s, b->as.s) < 0);
    return jsc_bool(0);
}

JscValue *jsc_gt(JscValue *a, JscValue *b) {
    if (is_num(a) && is_num(b)) return jsc_bool(to_num(a) > to_num(b));
    if (a && b && a->type == JSC_STRING && b->type == JSC_STRING)
        return jsc_bool(strcmp(a->as.s, b->as.s) > 0);
    return jsc_bool(0);
}

JscValue *jsc_lte(JscValue *a, JscValue *b) {
    if (is_num(a) && is_num(b)) return jsc_bool(to_num(a) <= to_num(b));
    return jsc_bool(0);
}

JscValue *jsc_gte(JscValue *a, JscValue *b) {
    if (is_num(a) && is_num(b)) return jsc_bool(to_num(a) >= to_num(b));
    return jsc_bool(0);
}

/* ---- Logica ---- */

int jsc_truthy(JscValue *v) {
    if (!v) return 0;
    switch (v->type) {
        case JSC_VAZIO:  return 0;
        case JSC_BOOL:   return v->as.b;
        case JSC_INT:    return v->as.i != 0;
        case JSC_FLOAT:  return v->as.f != 0.0;
        case JSC_STRING: return v->as.s && v->as.s[0] != 0;
        case JSC_ARRAY:  return v->as.array.count > 0;
        default:         return 1;
    }
}

JscValue *jsc_not(JscValue *v) {
    return jsc_bool(!jsc_truthy(v));
}

/* ---- Conversao ---- */

const char *jsc_to_cstr(JscValue *v) {
    static char buf[64];
    if (!v) return "";
    switch (v->type) {
        case JSC_VAZIO:  return "vazio";
        case JSC_BOOL:   return v->as.b ? "true" : "false";
        case JSC_INT:    snprintf(buf, sizeof(buf), "%lld", v->as.i); return buf;
        case JSC_FLOAT:  snprintf(buf, sizeof(buf), "%g", v->as.f); return buf;
        case JSC_STRING: return v->as.s;
        case JSC_ARRAY:  return "[array]";
        default:         return "?";
    }
}

long long jsc_to_int(JscValue *v) {
    if (!v) return 0;
    switch (v->type) {
        case JSC_INT:    return v->as.i;
        case JSC_FLOAT:  return (long long)v->as.f;
        case JSC_BOOL:   return v->as.b;
        case JSC_STRING: return atoll(v->as.s);
        default:         return 0;
    }
}

double jsc_to_float(JscValue *v) {
    return to_num(v);
}

/* ---- I/O ---- */

void jsc_print(JscValue *v) {
    printf("%s", jsc_to_cstr(v));
    printf("\n");
    fflush(stdout);
}

void jsc_println(JscValue *v) {
    printf("%s\n", jsc_to_cstr(v));
    fflush(stdout);
}

void jsc_print_str(const char *s) {
    printf("%s\n", s);
    fflush(stdout);
}

/* ---- Free ---- */

void jsc_free(JscValue *v) {
    if (!v) return;
    if (v->type == JSC_STRING && v->as.s) free(v->as.s);
    if (v->type == JSC_ARRAY) {
        for (size_t i = 0; i < v->as.array.count; i++) {
            jsc_free(v->as.array.items[i]);
        }
        free(v->as.array.items);
    }
    if (v->type == JSC_MAP) {
        for (size_t i = 0; i < v->as.map.count; i++) {
            free(v->as.map.keys[i]);
            jsc_free(v->as.map.values[i]);
        }
        free(v->as.map.keys);
        free(v->as.map.values);
    }
    free(v);
}


/* ============================================================
 * FFI — import_c + call_c
 * ============================================================ */

/* import_c(path) — carrega lib .so e retorna map com handle */
JscValue *jsc_import_c(const char *path) {
    void *handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "JSC$+ erro: import_c: %s\n", dlerror());
        return jsc_vazio();
    }
    
    JscValue *m = jsc_map();
    jsc_map_set(m, "handle", jsc_int((long long)(intptr_t)handle));
    jsc_map_set(m, "path", jsc_string(path));
    return m;
}

/* call_c(lib, nome, tipo, ...) — chama funcao C */
JscValue *jsc_call_c(JscValue *lib, const char *nome, const char *tipo, int argc, ...) {
    if (!lib || lib->type != JSC_MAP) {
        fprintf(stderr, "JSC$+ erro: call_c: lib invalida\n");
        return jsc_vazio();
    }
    
    JscValue *handle_val = jsc_map_get(lib, "handle");
    if (!handle_val) return jsc_vazio();
    
    void *handle = (void *)(intptr_t)handle_val->as.i;
    void *sym = dlsym(handle, nome);
    if (!sym) {
        fprintf(stderr, "JSC$+ erro: call_c: '%s': %s\n", nome, dlerror());
        return jsc_vazio();
    }
    
    char t0 = tipo[0];
    char t1 = tipo[1];
    int tlen = (int)strlen(tipo);
    
    va_list args;
    va_start(args, argc);
    
    JscValue *result = NULL;
    
    /* "V" — char* func(void) */
    if (t0 == 'V' && t1 == '\0') {
        typedef const char *(*f_t)(void);
        f_t f = (f_t)sym;
        const char *r = f();
        result = jsc_string(r ? r : "");
    }
    /* "v" — int func(void) */
    else if (t0 == 'v' && t1 == '\0') {
        typedef int (*f_t)(void);
        f_t f = (f_t)sym;
        result = jsc_int((long long)f());
    }
    /* "i" — int func(int) ou int func(int, int) */
    else if (t0 == 'i' && t1 == '\0') {
        if (argc == 1) {
            JscValue *a = va_arg(args, JscValue *);
            int x = (int)jsc_to_int(a);
            typedef int (*f_t)(int);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(x));
        } else if (argc == 2) {
            JscValue *a = va_arg(args, JscValue *);
            JscValue *b = va_arg(args, JscValue *);
            int x = (int)jsc_to_int(a);
            int y = (int)jsc_to_int(b);
            typedef int (*f_t)(int, int);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(x, y));
        }
    }
    /* "d" — double func(double) ou double func(double, double) */
    else if (t0 == 'd' && t1 == '\0') {
        if (argc == 1) {
            JscValue *a = va_arg(args, JscValue *);
            double x = jsc_to_float(a);
            typedef double (*f_t)(double);
            f_t f = (f_t)sym;
            result = jsc_float(f(x));
        } else if (argc == 2) {
            JscValue *a = va_arg(args, JscValue *);
            JscValue *b = va_arg(args, JscValue *);
            double x = jsc_to_float(a);
            double y = jsc_to_float(b);
            typedef double (*f_t)(double, double);
            f_t f = (f_t)sym;
            result = jsc_float(f(x, y));
        }
    }
    /* "p" — size_t func(char*) */
    else if (t0 == 'p' && t1 == '\0') {
        if (argc >= 1) {
            JscValue *a = va_arg(args, JscValue *);
            const char *s = (a->type == JSC_STRING) ? a->as.s : "";
            typedef size_t (*f_t)(const char *);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(s));
        }
    }
    /* "s" — int func(char*) */
    else if (t0 == 's' && t1 == '\0') {
        if (argc >= 1) {
            JscValue *a = va_arg(args, JscValue *);
            const char *s = (a->type == JSC_STRING) ? a->as.s : "";
            typedef int (*f_t)(const char *);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(s));
        }
    }
    /* "ss" — int func(char*, char*) */
    else if (t0 == 's' && t1 == 's' && tlen == 2) {
        if (argc >= 2) {
            JscValue *a = va_arg(args, JscValue *);
            JscValue *b = va_arg(args, JscValue *);
            const char *s1 = (a->type == JSC_STRING) ? a->as.s : "";
            const char *s2 = (b->type == JSC_STRING) ? b->as.s : "";
            typedef int (*f_t)(const char *, const char *);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(s1, s2));
        }
    }
    /* "ssi" — int func(char*, char*, int) */
    else if (t0 == 's' && t1 == 's' && tlen == 3 && tipo[2] == 'i') {
        if (argc >= 3) {
            JscValue *a = va_arg(args, JscValue *);
            JscValue *b = va_arg(args, JscValue *);
            JscValue *d = va_arg(args, JscValue *);
            const char *s1 = (a->type == JSC_STRING) ? a->as.s : "";
            const char *s2 = (b->type == JSC_STRING) ? b->as.s : "";
            int n = (int)jsc_to_int(d);
            typedef int (*f_t)(const char *, const char *, size_t);
            f_t f = (f_t)sym;
            result = jsc_int((long long)f(s1, s2, (size_t)n));
        }
    }
    /* "vp" — void func(char*) */
    else if (t0 == 'v' && t1 == 'p') {
        if (argc >= 1) {
            JscValue *a = va_arg(args, JscValue *);
            const char *s = (a->type == JSC_STRING) ? a->as.s : "";
            typedef void (*f_t)(const char *);
            f_t f = (f_t)sym;
            f(s);
            result = jsc_vazio();
        }
    }
    
    va_end(args);
    return result ? result : jsc_vazio();
}


/* ============================================================
 * Sistema de nativos (funcoes que ficam em modulos)
 * ============================================================ */

typedef JscValue *(*NativeFunc)(JscValue **args, int argc);

static JscValue *native_math_sqrt(JscValue **args, int argc) {
    (void)argc;
    return jsc_float(sqrt(jsc_to_float(args[0])));
}
static JscValue *native_math_pow(JscValue **args, int argc) {
    (void)argc;
    return jsc_float(pow(jsc_to_float(args[0]), jsc_to_float(args[1])));
}
static JscValue *native_math_abs(JscValue **args, int argc) {
    (void)argc;
    if (args[0]->type == JSC_INT) return jsc_int(llabs(args[0]->as.i));
    return jsc_float(fabs(jsc_to_float(args[0])));
}
static JscValue *native_math_floor(JscValue **args, int argc) {
    (void)argc;
    return jsc_int((long long)floor(jsc_to_float(args[0])));
}
static JscValue *native_math_ceil(JscValue **args, int argc) {
    (void)argc;
    return jsc_int((long long)ceil(jsc_to_float(args[0])));
}
static JscValue *native_math_round(JscValue **args, int argc) {
    (void)argc;
    return jsc_int((long long)round(jsc_to_float(args[0])));
}
static JscValue *native_math_sin(JscValue **args, int argc) {
    (void)argc;
    return jsc_float(sin(jsc_to_float(args[0])));
}
static JscValue *native_math_cos(JscValue **args, int argc) {
    (void)argc;
    return jsc_float(cos(jsc_to_float(args[0])));
}
static JscValue *native_math_tan(JscValue **args, int argc) {
    (void)argc;
    return jsc_float(tan(jsc_to_float(args[0])));
}
static JscValue *native_math_log(JscValue **args, int argc) {
    (void)argc;
    return jsc_float(log(jsc_to_float(args[0])));
}
static JscValue *native_math_log10(JscValue **args, int argc) {
    (void)argc;
    return jsc_float(log10(jsc_to_float(args[0])));
}
static JscValue *native_math_exp(JscValue **args, int argc) {
    (void)argc;
    return jsc_float(exp(jsc_to_float(args[0])));
}
static JscValue *native_math_min(JscValue **args, int argc) {
    (void)argc;
    double a = jsc_to_float(args[0]);
    double b = jsc_to_float(args[1]);
    return jsc_float(a < b ? a : b);
}
static JscValue *native_math_max(JscValue **args, int argc) {
    (void)argc;
    double a = jsc_to_float(args[0]);
    double b = jsc_to_float(args[1]);
    return jsc_float(a > b ? a : b);
}
static JscValue *native_math_random(JscValue **args, int argc) {
    (void)args; (void)argc;
    return jsc_float((double)rand() / RAND_MAX);
}
static JscValue *native_math_random_int(JscValue **args, int argc) {
    (void)argc;
    int lo = (int)jsc_to_int(args[0]);
    int hi = (int)jsc_to_int(args[1]);
    return jsc_int(lo + rand() % (hi - lo + 1));
}
static JscValue *native_math_pi(JscValue **args, int argc) {
    (void)args; (void)argc;
    return jsc_float(M_PI);
}
static JscValue *native_math_e(JscValue **args, int argc) {
    (void)args; (void)argc;
    return jsc_float(M_E);
}

JscValue *make_math_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, "sqrt",       jsc_native("sqrt",       native_math_sqrt));
    jsc_map_set(m, "pow",        jsc_native("pow",        native_math_pow));
    jsc_map_set(m, "abs",        jsc_native("abs",        native_math_abs));
    jsc_map_set(m, "floor",      jsc_native("floor",      native_math_floor));
    jsc_map_set(m, "ceil",       jsc_native("ceil",       native_math_ceil));
    jsc_map_set(m, "round",      jsc_native("round",      native_math_round));
    jsc_map_set(m, "sin",        jsc_native("sin",        native_math_sin));
    jsc_map_set(m, "cos",        jsc_native("cos",        native_math_cos));
    jsc_map_set(m, "tan",        jsc_native("tan",        native_math_tan));
    jsc_map_set(m, "log",        jsc_native("log",        native_math_log));
    jsc_map_set(m, "log10",      jsc_native("log10",      native_math_log10));
    jsc_map_set(m, "exp",        jsc_native("exp",        native_math_exp));
    jsc_map_set(m, "min",        jsc_native("min",        native_math_min));
    jsc_map_set(m, "max",        jsc_native("max",        native_math_max));
    jsc_map_set(m, "random",     jsc_native("random",     native_math_random));
    jsc_map_set(m, "random_int", jsc_native("random_int", native_math_random_int));
    jsc_map_set(m, "pi",         native_math_pi(NULL, 0));
    jsc_map_set(m, "e",          native_math_e(NULL, 0));
    return m;
}

/* ============================================================
 * Modulo time
 * ============================================================ */

static JscValue *native_time_now(JscValue **args, int argc) {
    (void)args; (void)argc;
    return jsc_int((long long)time(NULL));
}
static JscValue *native_time_ms(JscValue **args, int argc) {
    (void)args; (void)argc;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return jsc_int(ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL);
}
static JscValue *native_time_sleep(JscValue **args, int argc) {
    (void)argc;
    double s = jsc_to_float(args[0]);
    struct timespec ts;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
    return jsc_vazio();
}
static JscValue *native_time_clock(JscValue **args, int argc) {
    (void)args; (void)argc;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return jsc_int(ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL);
}

JscValue *make_time_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, "now",   jsc_native("now",   native_time_now));
    jsc_map_set(m, "ms",    jsc_native("ms",    native_time_ms));
    jsc_map_set(m, "sleep", jsc_native("sleep", native_time_sleep));
    jsc_map_set(m, "clock", jsc_native("clock", native_time_clock));
    return m;
}

/* ============================================================
 * Sistema de "jsc_native" — empacota a funcao C num JscValue
 * ============================================================ */

typedef struct {
    char *name;
    NativeFunc fn;
} NativeData;

JscValue *jsc_native(const char *name, JscValue *(*fn)(JscValue **args, int argc)) {
    JscValue *v = (JscValue *)malloc(sizeof(JscValue));
    v->type = JSC_NATIVE;
    NativeData *nd = (NativeData *)malloc(sizeof(NativeData));
    nd->name = strdup(name);
    nd->fn = fn;
    v->as.ptr = nd;
    return v;
}

/* chama uma funcao nativa com args */
JscValue *jsc_call_native(JscValue *fn, JscValue **args, int argc) {
    if (!fn || fn->type != JSC_NATIVE) return jsc_vazio();
    NativeData *nd = (NativeData *)fn->as.ptr;
    return nd->fn(args, argc);
}

/* ============================================================
 * Import de modulos
 * ============================================================ */

JscValue *jsc_import_module(const char *name) {
    if (strcmp(name, "math") == 0)   return make_math_module();
    if (strcmp(name, "time") == 0)   return make_time_module();
    if (strcmp(name, "string") == 0) return make_string_module();
    if (strcmp(name, "crypto") == 0) return make_crypto_module();
    if (strcmp(name, "fs") == 0)     return make_fs_module();
    if (strcmp(name, "os") == 0)     return make_os_module();
    if (strcmp(name, "proc") == 0)   return make_proc_module();
    if (strcmp(name, "net") == 0)    return make_net_module();
    if (strcmp(name, "json") == 0)   return make_json_module();
    if (strcmp(name, "hack") == 0)   return make_hack_module();
    return NULL;
}


/* ============================================================
 * STUBS — a serem implementados
 * ============================================================ */


/* ============================================================
 * Modulo CRYPTO
 * ============================================================ */

static void hash_to_hex(const unsigned char *hash, int len, char *out) {
    for (int i = 0; i < len; i++) {
        sprintf(out + i*2, "%02x", hash[i]);
    }
    out[len*2] = 0;
}

static JscValue *native_crypto_md5(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5((unsigned char*)s, strlen(s), hash);
    char hex[MD5_DIGEST_LENGTH * 2 + 1];
    hash_to_hex(hash, MD5_DIGEST_LENGTH, hex);
    return jsc_string(hex);
}

static JscValue *native_crypto_sha1(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)s, strlen(s), hash);
    char hex[SHA_DIGEST_LENGTH * 2 + 1];
    hash_to_hex(hash, SHA_DIGEST_LENGTH, hex);
    return jsc_string(hex);
}

static JscValue *native_crypto_sha256(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)s, strlen(s), hash);
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    hash_to_hex(hash, SHA256_DIGEST_LENGTH, hex);
    return jsc_string(hex);
}

static JscValue *native_crypto_sha512(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512((unsigned char*)s, strlen(s), hash);
    char hex[SHA512_DIGEST_LENGTH * 2 + 1];
    hash_to_hex(hash, SHA512_DIGEST_LENGTH, hex);
    return jsc_string(hex);
}

static JscValue *native_crypto_base64_encode(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    int len = (int)strlen(s);
    int out_len = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(out_len + 1);
    EVP_EncodeBlock((unsigned char*)out, (unsigned char*)s, len);
    out[out_len] = 0;
    JscValue *r = jsc_string(out);
    free(out);
    return r;
}

static JscValue *native_crypto_base64_decode(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    int len = (int)strlen(s);
    unsigned char *out = (unsigned char *)malloc(len);
    int out_len = EVP_DecodeBlock(out, (unsigned char*)s, len);
    if (out_len < 0) out_len = 0;
    /* Ajusta padding */
    while (out_len > 0 && (s[len-1] == '=' || s[len-1] == '\n')) {
        if (s[len-1] == '=') out_len--;
        len--;
    }
    out[out_len] = 0;
    JscValue *r = jsc_string((char*)out);
    free(out);
    return r;
}

static JscValue *native_crypto_random_bytes(JscValue **args, int argc) {
    (void)argc;
    int n = (int)jsc_to_int(args[0]);
    unsigned char *buf = (unsigned char *)malloc(n);
    RAND_bytes(buf, n);
    JscValue *arr = jsc_array();
    for (int i = 0; i < n; i++) {
        jsc_array_push(arr, jsc_int(buf[i]));
    }
    free(buf);
    return arr;
}

static JscValue *native_crypto_random_hex(JscValue **args, int argc) {
    (void)argc;
    int n = (int)jsc_to_int(args[0]);
    unsigned char *buf = (unsigned char *)malloc(n);
    RAND_bytes(buf, n);
    char *hex = (char *)malloc(n * 2 + 1);
    hash_to_hex(buf, n, hex);
    free(buf);
    JscValue *r = jsc_string(hex);
    free(hex);
    return r;
}

JscValue *make_crypto_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, "md5",           jsc_native("md5",           native_crypto_md5));
    jsc_map_set(m, "sha1",          jsc_native("sha1",          native_crypto_sha1));
    jsc_map_set(m, "sha256",        jsc_native("sha256",        native_crypto_sha256));
    jsc_map_set(m, "sha512",        jsc_native("sha512",        native_crypto_sha512));
    jsc_map_set(m, "base64_encode", jsc_native("base64_encode", native_crypto_base64_encode));
    jsc_map_set(m, "base64_decode", jsc_native("base64_decode", native_crypto_base64_decode));
    jsc_map_set(m, "random_bytes",  jsc_native("random_bytes",  native_crypto_random_bytes));
    jsc_map_set(m, "random_hex",    jsc_native("random_hex",    native_crypto_random_hex));
    return m;
}







/* ============================================================
 * Modulo FS
 * ============================================================ */

static JscValue *native_fs_read(JscValue **args, int argc) {
    (void)argc;
    const char *path = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    FILE *f = fopen(path, "rb");
    if (!f) return jsc_string("");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    if (sz > 0) fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    JscValue *r = jsc_string(buf);
    free(buf);
    return r;
}

static JscValue *native_fs_write(JscValue **args, int argc) {
    (void)argc;
    const char *path = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    const char *data = (args[1]->type == JSC_STRING) ? args[1]->as.s : "";
    FILE *f = fopen(path, "wb");
    if (!f) return jsc_bool(0);
    fwrite(data, 1, strlen(data), f);
    fclose(f);
    return jsc_bool(1);
}

static JscValue *native_fs_append(JscValue **args, int argc) {
    (void)argc;
    const char *path = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    const char *data = (args[1]->type == JSC_STRING) ? args[1]->as.s : "";
    FILE *f = fopen(path, "ab");
    if (!f) return jsc_bool(0);
    fwrite(data, 1, strlen(data), f);
    fclose(f);
    return jsc_bool(1);
}

static JscValue *native_fs_exists(JscValue **args, int argc) {
    (void)argc;
    const char *path = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    struct stat st;
    return jsc_bool(stat(path, &st) == 0);
}

static JscValue *native_fs_size(JscValue **args, int argc) {
    (void)argc;
    const char *path = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    struct stat st;
    if (stat(path, &st) != 0) return jsc_int(0);
    return jsc_int(st.st_size);
}

static JscValue *native_fs_remove(JscValue **args, int argc) {
    (void)argc;
    const char *path = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    return jsc_bool(remove(path) == 0);
}

static JscValue *native_fs_list(JscValue **args, int argc) {
    (void)argc;
    const char *path = (args[0]->type == JSC_STRING) ? args[0]->as.s : ".";
    DIR *d = opendir(path);
    JscValue *arr = jsc_array();
    if (!d) return arr;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        jsc_array_push(arr, jsc_string(e->d_name));
    }
    closedir(d);
    return arr;
}

JscValue *make_fs_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, "read",   jsc_native("read",   native_fs_read));
    jsc_map_set(m, "write",  jsc_native("write",  native_fs_write));
    jsc_map_set(m, "append", jsc_native("append", native_fs_append));
    jsc_map_set(m, "exists", jsc_native("exists", native_fs_exists));
    jsc_map_set(m, "size",   jsc_native("size",   native_fs_size));
    jsc_map_set(m, "remove", jsc_native("remove", native_fs_remove));
    jsc_map_set(m, "list",   jsc_native("list",   native_fs_list));
    return m;
}

/* ============================================================
 * Modulo OS
 * ============================================================ */

static JscValue *native_os_getcwd(JscValue **args, int argc) {
    (void)args; (void)argc;
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) return jsc_string("");
    return jsc_string(buf);
}

static JscValue *native_os_home(JscValue **args, int argc) {
    (void)args; (void)argc;
    const char *h = getenv("HOME");
    return jsc_string(h ? h : "");
}

static JscValue *native_os_env(JscValue **args, int argc) {
    (void)argc;
    const char *key = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    const char *v = getenv(key);
    return jsc_string(v ? v : "");
}

static JscValue *native_os_pid(JscValue **args, int argc) {
    (void)args; (void)argc;
    return jsc_int((long long)getpid());
}

static JscValue *native_os_tmpdir(JscValue **args, int argc) {
    (void)args; (void)argc;
    return jsc_string("/tmp");
}

static JscValue *native_os_mkdir(JscValue **args, int argc) {
    (void)argc;
    const char *path = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    return jsc_bool(mkdir(path, 0755) == 0);
}

JscValue *make_os_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, "getcwd", jsc_native("getcwd", native_os_getcwd));
    jsc_map_set(m, "home",   jsc_native("home",   native_os_home));
    jsc_map_set(m, "env",    jsc_native("env",    native_os_env));
    jsc_map_set(m, "pid",    jsc_native("pid",    native_os_pid));
    jsc_map_set(m, "tmpdir", jsc_native("tmpdir", native_os_tmpdir));
    jsc_map_set(m, "mkdir",  jsc_native("mkdir",  native_os_mkdir));
    return m;
}

/* ============================================================
 * Modulo PROC
 * ============================================================ */

static JscValue *native_proc_run(JscValue **args, int argc) {
    (void)argc;
    const char *cmd = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    int r = system(cmd);
    return jsc_int((long long)WEXITSTATUS(r));
}

static JscValue *native_proc_capture(JscValue **args, int argc) {
    (void)argc;
    const char *cmd = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    FILE *f = popen(cmd, "r");
    if (!f) return jsc_string("");
    size_t cap = 4096, len = 0;
    char *out = (char *)malloc(cap);
    size_t r;
    while ((r = fread(out + len, 1, cap - len - 1, f)) > 0) {
        len += r;
        if (len + 1024 >= cap) { cap *= 2; out = (char *)realloc(out, cap); }
    }
    out[len] = 0;
    pclose(f);
    JscValue *res = jsc_string(out);
    free(out);
    return res;
}

static JscValue *native_proc_exit(JscValue **args, int argc) {
    (void)argc;
    int code = (int)jsc_to_int(args[0]);
    exit(code);
    return jsc_vazio();
}

JscValue *make_proc_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, "run",     jsc_native("run",     native_proc_run));
    jsc_map_set(m, "capture", jsc_native("capture", native_proc_capture));
    jsc_map_set(m, "exit",    jsc_native("exit",    native_proc_exit));
    return m;
}
/* ============================================================
 * Modulo NET (hacking)
 * ============================================================ */

static JscValue *native_net_resolve(JscValue **args, int argc) {
    (void)argc;
    const char *host = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    struct hostent *h = gethostbyname(host);
    if (!h) return jsc_string("");
    struct in_addr **addrs = (struct in_addr **)h->h_addr_list;
    if (!addrs[0]) return jsc_string("");
    return jsc_string(inet_ntoa(*addrs[0]));
}

static JscValue *native_net_port_open(JscValue **args, int argc) {
    (void)argc;
    const char *host = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    int port = (int)jsc_to_int(args[1]);
    
    struct hostent *he = gethostbyname(host);
    if (!he) return jsc_bool(0);
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return jsc_bool(0);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    int r = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);
    return jsc_bool(r == 0);
}

static JscValue *native_net_scan(JscValue **args, int argc) {
    (void)argc;
    const char *host = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    int port_min = (int)jsc_to_int(args[1]);
    int port_max = (int)jsc_to_int(args[2]);
    
    struct hostent *he = gethostbyname(host);
    JscValue *arr = jsc_array();
    if (!he) return arr;
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    
    for (int p = port_min; p <= port_max; p++) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;
        
        addr.sin_port = htons(p);
        struct timeval tv = {0, 500000};  /* 500ms */
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        int r = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
        close(sock);
        
        if (r == 0) {
            jsc_array_push(arr, jsc_int(p));
        }
    }
    return arr;
}

JscValue *make_net_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, "resolve",   jsc_native("resolve",   native_net_resolve));
    jsc_map_set(m, "port_open", jsc_native("port_open", native_net_port_open));
    jsc_map_set(m, "scan",      jsc_native("scan",      native_net_scan));
    return m;
}

/* ============================================================
 * Modulo HACK (banner, headers, etc)
 * ============================================================ */

static JscValue *native_hack_banner(JscValue **args, int argc) {
    (void)argc;
    const char *host = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    int port = (int)jsc_to_int(args[1]);
    
    struct hostent *he = gethostbyname(host);
    if (!he) return jsc_string("");
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return jsc_string("");
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    
    struct timeval tv = {2, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return jsc_string("");
    }
    
    char buf[4096];
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    close(sock);
    
    if (n <= 0) return jsc_string("");
    buf[n] = 0;
    return jsc_string(buf);
}

static JscValue *native_hack_headers(JscValue **args, int argc) {
    (void)argc;
    const char *url = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    
    /* Parse simples de http://host/path */
    char host[256] = {0};
    char path[256] = {0};
    int port = 80;
    
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) { p += 8; port = 443; }
    
    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hlen = slash - p;
        if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
        memcpy(host, p, hlen);
        strncpy(path, slash, sizeof(path) - 1);
    } else {
        strncpy(host, p, sizeof(host) - 1);
        strcpy(path, "/");
    }
    
    struct hostent *he = gethostbyname(host);
    if (!he) return jsc_string("");
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return jsc_string("");
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    
    struct timeval tv = {5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return jsc_string("");
    }
    
    char req[1024];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: JSC$+/3.0\r\nConnection: close\r\n\r\n",
             path, host);
    send(sock, req, strlen(req), 0);
    
    /* Le headers */
    size_t cap = 8192, len = 0;
    char *out = (char *)malloc(cap);
    int n;
    while ((n = recv(sock, out + len, cap - len - 1, 0)) > 0) {
        len += n;
        if (len + 1024 >= cap) { cap *= 2; out = (char *)realloc(out, cap); }
        if (strstr(out, "\r\n\r\n") != NULL) break;
    }
    out[len] = 0;
    close(sock);
    
    JscValue *r = jsc_string(out);
    free(out);
    return r;
}

JscValue *make_hack_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, "banner",  jsc_native("banner",  native_hack_banner));
    jsc_map_set(m, "headers", jsc_native("headers", native_hack_headers));
    return m;
}
/* ============================================================
 * Modulo STRING
 * ============================================================ */

static JscValue *native_string_upper(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    char *buf = strdup(s);
    for (char *p = buf; *p; p++) *p = toupper((unsigned char)*p);
    JscValue *r = jsc_string(buf);
    free(buf);
    return r;
}

static JscValue *native_string_lower(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    char *buf = strdup(s);
    for (char *p = buf; *p; p++) *p = tolower((unsigned char)*p);
    JscValue *r = jsc_string(buf);
    free(buf);
    return r;
}

static JscValue *native_string_reverse(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    int len = (int)strlen(s);
    char *buf = (char *)malloc(len + 1);
    for (int i = 0; i < len; i++) buf[i] = s[len - 1 - i];
    buf[len] = 0;
    JscValue *r = jsc_string(buf);
    free(buf);
    return r;
}

static JscValue *native_string_starts_with(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    const char *p = (args[1]->type == JSC_STRING) ? args[1]->as.s : "";
    return jsc_bool(strncmp(s, p, strlen(p)) == 0);
}

static JscValue *native_string_ends_with(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    const char *p = (args[1]->type == JSC_STRING) ? args[1]->as.s : "";
    size_t sl = strlen(s), pl = strlen(p);
    if (pl > sl) return jsc_bool(0);
    return jsc_bool(strcmp(s + sl - pl, p) == 0);
}

static JscValue *native_string_pad_left(JscValue **args, int argc) {
    (void)argc;
    const char *s = (args[0]->type == JSC_STRING) ? args[0]->as.s : "";
    int total = (int)jsc_to_int(args[1]);
    const char *pad = (args[2]->type == JSC_STRING) ? args[2]->as.s : " ";
    int slen = (int)strlen(s);
    if (slen >= total) return jsc_string(s);
    int pad_len = total - slen;
    char *buf = (char *)malloc(total + 1);
    for (int i = 0; i < pad_len; i++) buf[i] = pad[0];
    memcpy(buf + pad_len, s, slen);
    buf[total] = 0;
    JscValue *r = jsc_string(buf);
    free(buf);
    return r;
}

JscValue *make_string_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, "upper",       jsc_native("upper",       native_string_upper));
    jsc_map_set(m, "lower",       jsc_native("lower",       native_string_lower));
    jsc_map_set(m, "reverse",     jsc_native("reverse",     native_string_reverse));
    jsc_map_set(m, "starts_with", jsc_native("starts_with", native_string_starts_with));
    jsc_map_set(m, "ends_with",   jsc_native("ends_with",   native_string_ends_with));
    jsc_map_set(m, "pad_left",    jsc_native("pad_left",    native_string_pad_left));
    return m;
}

/* ============================================================
 * Modulo TIME (substitui o stub)
 * ============================================================ */









/* ============================================================
 * Modulo JSON (basico)
 * ============================================================ */

static JscValue *native_json_stringify(JscValue **args, int argc) {
    (void)argc;
    /* Versao simplificada — usa jsc_to_cstr */
    return jsc_string(jsc_to_cstr(args[0]));
}

JscValue *make_json_module(void) {
    JscValue *m = jsc_map();
    jsc_map_set(m, "stringify", jsc_native("stringify", native_json_stringify));
    return m;
}
/* ============================================================
 * Modulo STRING
 * ============================================================ */















/* ============================================================
 * Modulo TIME (substitui o stub)
 * ============================================================ */









/* ============================================================
 * Modulo JSON (basico)
 * ============================================================ */





/* ============================================================
 * Threads (spawn + join)
 * ============================================================ */

typedef struct {
    JscValue *(*fn)(void *);
    void *arg;
    void *result;
    pthread_t handle;
    int started;
} JscThread;

typedef struct {
    JscValue *(*fn)(JscValue *);
    JscValue *arg;
    JscValue *result;
} ThreadCallData;

static void *thread_call_wrapper(void *data) {
    ThreadCallData *td = (ThreadCallData *)data;
    td->result = td->fn(td->arg);
    return NULL;
}

JscValue *jsc_thread_new(JscValue *(*fn)(void *), void *arg) {
    JscThread *t = (JscThread *)malloc(sizeof(JscThread));
    t->fn = fn;
    t->arg = arg;
    t->result = NULL;
    
    /* pthread_create precisa void*(*)(void*) */
    /* Usa um wrapper interno */
    typedef struct {
        JscValue *(*fn)(void *);
        void *arg;
        JscValue *result;
    } InnerData;
    
    InnerData *inner = (InnerData *)malloc(sizeof(InnerData));
    inner->fn = fn;
    inner->arg = arg;
    inner->result = NULL;
    
    /* wrapper void* */
    void *(*wrapper)(void *) = NULL;
    
    /* Cria a thread chamando uma funcao que ignora */
    pthread_t handle;
    /* Usa thread_call_wrapper ja definida */
    ThreadCallData *td = (ThreadCallData *)malloc(sizeof(ThreadCallData));
    td->fn = (JscValue *(*)(JscValue *))fn;
    td->arg = (JscValue *)arg;
    td->result = NULL;
    
    pthread_create(&handle, NULL, thread_call_wrapper, td);
    
    t->handle = handle;
    t->started = 1;
    t->arg = td;
    
    JscValue *v = (JscValue *)malloc(sizeof(JscValue));
    v->type = JSC_THREAD;
    v->as.ptr = t;
    return v;
}

/* Callback pra thread */


JscValue *jsc_thread_join(JscValue *thread) {
    if (!thread || thread->type != JSC_THREAD) return jsc_vazio();
    JscThread *t = (JscThread *)thread->as.ptr;
    if (t->started) {
        pthread_join(t->handle, NULL);
    }
    /* Retorna o resultado */
    ThreadCallData *td = (ThreadCallData *)t->arg;
    return td->result ? td->result : jsc_vazio();
}

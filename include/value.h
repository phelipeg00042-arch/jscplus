#ifndef JSC_VALUE_H
#define JSC_VALUE_H

#include <stddef.h>
#include "ast.h"

/* Forward declaration */
typedef struct Env Env;

typedef enum {
    VAL_VAZIO = 0,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_BIGINT,
    VAL_STRING,
    VAL_ARRAY,
    VAL_MAP,
    VAL_FUNCTION,
    VAL_CLASS,
    VAL_INSTANCE,    /* instância de classe */
    VAL_THREAD       /* thread */
} JscType;

typedef struct JscValue JscValue;

/* Funcao nativa: usa void* pra evitar dependencia circular de headers */
typedef JscValue *(*NativeFn)(void *call, void *env, void *state);

typedef struct {
    JscValue *key;
    JscValue *value;
} JscMapEntry;

struct JscValue {
    JscType type;
    int     gc_marked;      /* flag do GC: 1 = em uso, 0 = candidato */
    int     gc_reserved;    /* padding/reservado */
    union {
        int      b;
        long long i;
        double   f;
        char    *s;

        struct {
            JscValue **items;
            size_t     count;
            size_t     capacity;
        } array;

        struct {
            JscMapEntry *entries;
            size_t       count;
            size_t       capacity;
        } map;

        struct {
            Node      *body;
            NodeList  *params;
            Env       *closure;
            char      *name;
            NativeFn   native;    /* se != NULL, é função nativa */
        } func;

        struct {
            char      *name;
            JscValue  *parent;       /* classe pai (JscValue VAL_CLASS), ou NULL */
            NodeList  *fields;       /* NodeVarDecl */
            NodeList  *methods;      /* NodeFuncDecl */
            Env       *closure;      /* env onde foi definida */
        } cls;

        struct {
            struct JscClass *klass;  /* classe da instância */
            Env             *fields; /* variáveis de instância (nome -> valor) */
        } inst;

        struct {
            void *thread_ptr;   /* JscThread * (definido em eval.c) */
        } thread;
    } as;
};

/* Construtores */
JscValue *jsc_vazio(void);
JscValue *jsc_bool(int value);
JscValue *jsc_int(long long value);
JscValue *jsc_float(double value);
JscValue *jsc_string(const char *value);
JscValue *jsc_native(const char *name, NativeFn fn);
JscValue *jsc_class(const char *name, NodeList *fields, NodeList *methods, void *closure);
JscValue *jsc_instance(JscValue *klass);
JscValue *jsc_thread(void *thread_ptr);
JscValue *jsc_array(void);
JscValue *jsc_map(void);

/* Cópia profunda — TODO valor devolvido por essa função precisa ser liberado */
JscValue *jsc_value_copy(JscValue *v);

/* Array */
void      jsc_array_push(JscValue *arr, JscValue *item);
JscValue *jsc_array_get(JscValue *arr, size_t index);
size_t    jsc_array_len(JscValue *arr);

/* Map */
void      jsc_map_set(JscValue *map, JscValue *key, JscValue *value);
JscValue *jsc_map_get(JscValue *map, JscValue *key);

/* Impressão */
void jsc_print_value(JscValue *v);
void jsc_print_value_debug(JscValue *v);

/* Comparação */
int jsc_value_equals(JscValue *a, JscValue *b);

/* Truthy */
int jsc_value_is_truthy(JscValue *v);

/* Conversão */
double      jsc_value_to_float(JscValue *v);
long long   jsc_value_to_int(JscValue *v);
const char *jsc_type_name(JscType t);

/* Liberação */
void jsc_value_free(JscValue *v);

/* ============================================================
 * Garbage Collector (GC.1 — rastreamento)
 * ============================================================ */

/* Forward declaration */
typedef struct Env Env;

/* Registra valor na lista do GC — chamado por alloc_value */
void gc_track(JscValue *v);

/* Remove da lista — chamado por jsc_value_free */
void gc_untrack(JscValue *v);

/* Imprime estatísticas (valores vivos, capacidade) */
void gc_stats(void);

/* Define o env raiz (chamado por main/repl) */
void gc_set_roots(Env *global_env);

/* Marca um valor e todos os seus filhos recursivamente */
void gc_mark(JscValue *v);

/* Marca todas as variáveis de um env */
void gc_mark_env(Env *env);

/* Marca a partir das raízes (env global) */
void gc_mark_roots(void);

/* Roda GC se passou do limite. Chamado entre comandos. */
void gc_maybe_collect(void);

/* Sweep e collect (gc.3) */
void gc_sweep(void);
void gc_collect(void);

#endif

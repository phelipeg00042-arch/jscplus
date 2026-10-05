#define _GNU_SOURCE
#include "value.h"
#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

/* Estatisticas do GC (otimizacao) */
static double gc_total_time = 0.0;
static int gc_total_calls = 0;


static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (out) {
        memcpy(out, s, len);
        out[len] = '\0';
    }
    return out;
}

/* ============================================================
 * Garbage Collector — lista global de valores
 * ============================================================ */

static JscValue **gc_list = NULL;
static size_t     gc_count = 0;
static size_t     gc_capacity = 0;

/* Mutex recursivo pra proteger a lista do GC */
static pthread_mutex_t gc_mutex;
static pthread_once_t  gc_mutex_once = PTHREAD_ONCE_INIT;

__attribute__((unused))
static void gc_mutex_init(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&gc_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

/* Spinlock simples — atomico, sem deadlock possivel. */
static volatile int gc_spinlock = 0;

static void gc_lock(void) {
    while (__sync_lock_test_and_set(&gc_spinlock, 1)) {
        /* spin */
    }
}

static void gc_unlock(void) {
    __sync_lock_release(&gc_spinlock);
}

/* Contador de threads ativas. Se > 0, GC pausa tudo. */
extern volatile int gc_active_threads;

void gc_track(JscValue *v) {
    if (!v) return;
    gc_lock();
    if (gc_count >= gc_capacity) {
        size_t new_cap = gc_capacity == 0 ? 1024 : gc_capacity * 2;
        JscValue **new_list = (JscValue **)realloc(gc_list, new_cap * sizeof(JscValue *));
        if (!new_list) {
            fprintf(stderr, "JSC$+ erro: sem memória (gc_track)\n");
            exit(1);
        }
        gc_list = new_list;
        gc_capacity = new_cap;
    }
    gc_list[gc_count++] = v;
    v->gc_marked = 0;
    gc_unlock();
}

void gc_untrack(JscValue *v) {
    if (!v) return;
    gc_lock();
    for (size_t i = 0; i < gc_count; i++) {
        if (gc_list[i] == v) {
            gc_list[i] = gc_list[gc_count - 1];
            gc_count--;
            gc_unlock();
            return;
        }
    }
    gc_unlock();
}

void gc_stats(void) {
    gc_lock();
    fprintf(stderr, "[GC] valores vivos: %zu  (capacidade: %zu)\n",
            gc_count, gc_capacity);
    gc_unlock();
    fprintf(stderr, "[GC] tempo total: %.3f s (%d coletas)\n", gc_total_time, gc_total_calls);
}

/* ============================================================
 * GC.2 — Mark: marca o que tá vivo
 * ============================================================ */

static Env *gc_root_env = NULL;

/* Roots permanentes (otimizacao) */
#define GC_PERM_ROOT_MAX 16
static JscValue *gc_perm_roots[GC_PERM_ROOT_MAX];
static int gc_perm_root_count = 0;

void gc_add_perm_root(JscValue *v) {
    if (gc_perm_root_count < GC_PERM_ROOT_MAX) {
        gc_perm_roots[gc_perm_root_count++] = v;
    }
}

void gc_set_roots(Env *global_env) {
    gc_root_env = global_env;
}

/* Marca recursivamente um valor e seus filhos */
void gc_mark(JscValue *v) {
    if (!v) return;
    if (v->gc_marked) return;  /* já marcado */
    v->gc_marked = 1;

    switch (v->type) {
        case VAL_ARRAY:
            for (size_t i = 0; i < v->as.array.count; i++) {
                gc_mark(v->as.array.items[i]);
            }
            break;

        case VAL_MAP:
            for (size_t i = 0; i < v->as.map.count; i++) {
                gc_mark(v->as.map.entries[i].key);
                gc_mark(v->as.map.entries[i].value);
            }
            break;

        case VAL_FUNCTION:
            /* Marca a closure (env de onde a função veio) */
            if (v->as.func.closure) {
                gc_mark_env(v->as.func.closure);
            }
            /* Body, params são AST — não são JscValue, não precisam marcar */
            break;

        case VAL_CLASS:
            /* Marca a closure da classe + a classe pai */
            if (v->as.cls.closure) {
                gc_mark_env(v->as.cls.closure);
            }
            if (v->as.cls.parent) {
                gc_mark(v->as.cls.parent);
            }
            break;

        case VAL_INSTANCE:
            /* Marca os campos (env) + a classe */
            if (v->as.inst.fields) {
                gc_mark_env(v->as.inst.fields);
            }
            if (v->as.inst.klass) {
                gc_mark((JscValue *)v->as.inst.klass);
            }
            break;

        case VAL_THREAD:
            /* Marca o resultado da thread (se já terminou) */
            {
                extern void gc_mark_thread(void *thread_ptr);
                if (v->as.thread.thread_ptr) {
                    gc_mark_thread(v->as.thread.thread_ptr);
                }
            }
            break;

        default:
            /* VAZIO, BOOL, INT, FLOAT, BIGINT, STRING — não têm filhos */
            break;
    }
}

/* Marca todas as variáveis de um env */
void gc_mark_env(Env *env) {
    if (!env) return;
    /* Marca o env como "já visitado" pra evitar loop infinito.
     * Uso um truque: se o env tem um valor sentinela, pula.
     * Melhor: mantenho um Set de envs visitados. Simplificação:
     * marco só as variáveis e depois pulo pro parent. */
    for (size_t i = 0; i < env->count; i++) {
        gc_mark(env->entries[i].value);
    }
    /* Marca o env pai também (escopo léxico) */
    if (env->parent) {
        gc_mark_env(env->parent);
    }
}

/* Debug: roda mark a partir das raízes e conta quantos ficaram marcados */
void gc_mark_all_and_report(void) {
    /* Zera todas as marcas */
    for (size_t i = 0; i < gc_count; i++) {
        gc_list[i]->gc_marked = 0;
    }
    /* Marca a partir do env raiz */
    gc_mark_roots();
    /* Conta quantos ficaram marcados */
    size_t marcados = 0;
    for (size_t i = 0; i < gc_count; i++) {
        if (gc_list[i]->gc_marked) marcados++;
    }
    fprintf(stderr, "[GC] marcados: %zu de %zu (%.1f%%)\n",
            marcados, gc_count,
            gc_count > 0 ? (100.0 * marcados / gc_count) : 0.0);
}

/* ============================================================
 * GC.3 — Sweep: libera o que não foi marcado
 * ============================================================ */

/* Marca a partir das raízes (env global) */
void gc_mark_roots(void) {
    if (!gc_root_env) return;
    gc_mark_env(gc_root_env);
    
    /* Marca roots permanentes */
    for (int i = 0; i < gc_perm_root_count; i++) {
        if (gc_perm_roots[i]) gc_perm_roots[i]->gc_marked = 1;
    }
}

/* Libera valores não-marcados.
 * CUIDADO: não pode usar jsc_value_free aqui, porque ela chama gc_untrack
 * que bagunça a lista. Então libera direto e reconstrói a lista. */
void gc_sweep(void) {
    if (gc_count == 0) return;

    size_t sobreviventes = 0;
    size_t mortos = 0;

    /* Reconstrói a lista: move vivos pra frente */
    for (size_t i = 0; i < gc_count; i++) {
        JscValue *v = gc_list[i];
        if (v && v->gc_marked) {
            gc_list[sobreviventes++] = v;
        } else {
            mortos++;
        }
    }

    /* Agora 'sobreviventes' é quantos vivos. Os mortos estão espalhados
     * da posição 'sobreviventes' até 'gc_count' (não necessariamente
     * todos, porque a gente compactou). Precisamos liberar os mortos
     * que ficaram "para trás" durante a compactação. */

    /* Estratégia mais simples: percorre a lista original UMA VEZ,
     * libera quem tá marcado != 1, e recompacta.
     * Mas precisa cuidado com jsc_value_free recursivo. */

    /* Vou fazer em duas passadas:
     * 1) Já fiz acima: compactei vivos pra frente.
     * 2) Agora, os mortos foram sobrescritos pelos vivos na compactação.
     *    PROBLEMA: perdemos referência aos mortos pra liberar!
     *
     * Solução correta: guardar ponteiros dos mortos numa lista temporária. */

    /* Refazendo: */
    size_t vivos = 0;
    JscValue **mortos_list = NULL;
    size_t mortos_count = 0;
    size_t mortos_cap = 0;

    for (size_t i = 0; i < gc_count; i++) {
        JscValue *v = gc_list[i];
        if (v && v->gc_marked) {
            gc_list[vivos++] = v;
        } else {
            if (mortos_count >= mortos_cap) {
                size_t nc = mortos_cap == 0 ? 256 : mortos_cap * 2;
                mortos_list = (JscValue **)realloc(mortos_list, nc * sizeof(JscValue *));
                mortos_cap = nc;
            }
            mortos_list[mortos_count++] = v;
        }
    }

    gc_count = vivos;

    /* Agora libera os mortos. CUIDADO: libera os FILHOS antes do pai?
     * Não importa — os filhos também estão na lista de mortos,
     * a gente só libera a MEMÓRIA, sem recursão. */

    for (size_t i = 0; i < mortos_count; i++) {
        JscValue *v = mortos_list[i];
        if (!v) continue;

        /* Libera só os recursos INTERNOS do valor, sem recursão.
         * (os filhos também estão em mortos_list e serão liberados) */
        switch (v->type) {
            case VAL_STRING:
                free(v->as.s);
                break;
            case VAL_ARRAY:
                free(v->as.array.items);
                break;
            case VAL_MAP:
                free(v->as.map.entries);
                break;
            case VAL_FUNCTION:
                if (v->as.func.native == NULL && v->as.func.name) {
                    free(v->as.func.name);
                }
                break;
            case VAL_CLASS:
                /* name e parent são gerenciados pela AST/valores */
                break;
            case VAL_INSTANCE:
                /* fields/klass são gerenciados por outros valores */
                break;
            default:
                break;
        }
        free(v);
    }

    free(mortos_list);

    /* Opcional: imprime stats se pediu */
    if (getenv("JSC_GC_VERBOSE")) {
        fprintf(stderr, "[GC-SWEEP] liberou %zu, sobraram %zu\n",
                mortos, gc_count);
    }
}

/* ============================================================
 * GC.4 — Coleta automática
 * ============================================================ */

static size_t gc_threshold = 10000;

/* Flag: 1 se estamos numa thread filha (definida em value.c) */
volatile int gc_in_thread = 0;

void gc_maybe_collect(void) {
    /* NAO roda GC se tem thread ativa ou estamos em thread */
    if (gc_in_thread) return;
    if (gc_active_threads > 0) return;
    if (gc_count <= gc_threshold) return;
    if (!gc_root_env) return;

    size_t antes = gc_count;
    gc_collect();
    size_t depois = gc_count;

    /* Se liberou mais de 30%, mantém o threshold.
     * Se liberou pouco, o threshold tá apertado — dobra. */
    size_t liberados = antes - depois;
    if (antes > 0 && liberados * 10 / antes < 3) {
        /* liberou menos de 30%, dobra o threshold */
        gc_threshold = gc_threshold * 2;
    } else {
        /* liberou bastante, mantém */
    }
}

/* Executa uma coleta completa: mark + sweep */


void gc_collect(void) {
    if (!gc_root_env) return;
    
    struct timespec _t0, _t1;
    clock_gettime(CLOCK_MONOTONIC, &_t0);

    gc_lock();

    /* Zera todas as marcas */
    for (size_t i = 0; i < gc_count; i++) {
        gc_list[i]->gc_marked = 0;
    }

    /* Marca a partir das raízes */
    gc_mark_roots();

    /* Libera os não marcados */
    gc_sweep();

    gc_unlock();
    
    /* Mede tempo */
    clock_gettime(CLOCK_MONOTONIC, &_t1);
    double _dt = (_t1.tv_sec - _t0.tv_sec) + (_t1.tv_nsec - _t0.tv_nsec) / 1e9;
    gc_total_time += _dt;
    gc_total_calls++;
}

static JscValue *alloc_value(JscType type) {
    JscValue *v = (JscValue *)calloc(1, sizeof(JscValue));
    if (!v) {
        fprintf(stderr, "JSC$+ erro: sem memória (value)\n");
        exit(1);
    }
    v->type = type;
    v->gc_marked = 0;
    gc_track(v);
    return v;
}

JscValue *jsc_vazio(void) {
    return alloc_value(VAL_VAZIO);
}


/* Small bool cache (com root permanente no GC) */
JscValue *bool_cache_true = NULL;
JscValue *bool_cache_false = NULL;
static int bool_cache_init = 0;

static void init_bool_cache(void) {
    if (bool_cache_init) return;
    
    bool_cache_true = alloc_value(VAL_BOOL);
    bool_cache_true->as.b = 1;
    gc_add_perm_root(bool_cache_true);
    
    bool_cache_false = alloc_value(VAL_BOOL);
    bool_cache_false->as.b = 0;
    gc_add_perm_root(bool_cache_false);
    
    bool_cache_init = 1;
}

JscValue *jsc_bool(int b) {
    if (!bool_cache_init) init_bool_cache();
    return b ? bool_cache_true : bool_cache_false;
}

/* Cache de inteiros pequenos (-256 a 256).
 * Inteiros nesse range sao muito comuns (indices, contadores).
 * Reusar o mesmo ponteiro evita alocacao. */
#define SMALL_INT_MIN (-256)
#define SMALL_INT_MAX 256
#define SMALL_INT_COUNT (SMALL_INT_MAX - SMALL_INT_MIN + 1)

static JscValue *small_int_cache[SMALL_INT_COUNT];
static int       small_int_ready = 0;

static JscValue *alloc_static_int(long long value) {
    JscValue *v = (JscValue *)calloc(1, sizeof(JscValue));
    if (!v) {
        fprintf(stderr, "JSC$+ erro: sem memoria (cache)\n");
        exit(1);
    }

    v->type = VAL_INT;
    v->as.i = value;

    return v;
}

static void init_small_int_cache(void) {
    for (int i = 0; i < SMALL_INT_COUNT; i++) {
        small_int_cache[i] = alloc_static_int(SMALL_INT_MIN + i);
        /* Marca como "pinned" pro GC nunca liberar */
        small_int_cache[i]->gc_marked = 1;
    }
    small_int_ready = 1;
}

JscValue *jsc_int(long long value) {
    /* Otimizacao: usa cache de small ints */
    if (value >= SMALL_INT_MIN && value <= SMALL_INT_MAX) {
        if (!small_int_cache[0]) init_small_int_cache();
        return small_int_cache[value - SMALL_INT_MIN];
    }
    
    /* Fallback: aloca novo pra ints grandes */
    JscValue *v = alloc_value(VAL_INT);
    v->as.i = value;
    return v;
}
JscValue *jsc_float(double value) {
    JscValue *v = alloc_value(VAL_FLOAT);
    v->as.f = value;
    return v;
}

JscValue *jsc_string(const char *value) {
    JscValue *v = alloc_value(VAL_STRING);
    v->as.s = dup_str(value ? value : "");
    return v;
}

JscValue *jsc_native(const char *name, NativeFn fn) {
    JscValue *v = alloc_value(VAL_FUNCTION);
    v->as.func.name    = dup_str(name);
    v->as.func.body    = NULL;
    v->as.func.params  = NULL;
    v->as.func.closure = NULL;
    v->as.func.native  = fn;
    return v;
}

JscValue *jsc_class(const char *name, NodeList *fields, NodeList *methods, void *closure) {
    JscValue *v = alloc_value(VAL_CLASS);
    v->as.cls.name    = dup_str(name);
    v->as.cls.parent  = NULL;
    v->as.cls.fields  = fields;
    v->as.cls.methods = methods;
    v->as.cls.closure = (Env *)closure;
    return v;
}

JscValue *jsc_instance(JscValue *klass) {
    JscValue *v = alloc_value(VAL_INSTANCE);
    /* Guarda CÓPIA da classe — a instância é dona */
    v->as.inst.klass  = (struct JscClass *)jsc_value_copy(klass);
    v->as.inst.fields = NULL;
    return v;
}

JscValue *jsc_thread(void *thread_ptr) {
    JscValue *v = alloc_value(VAL_THREAD);
    v->as.thread.thread_ptr = thread_ptr;
    return v;
}

JscValue *jsc_array(void) {
    JscValue *v = alloc_value(VAL_ARRAY);
    v->as.array.items    = NULL;
    v->as.array.count    = 0;
    v->as.array.capacity = 0;
    return v;
}

JscValue *jsc_map(void) {
    JscValue *v = alloc_value(VAL_MAP);
    v->as.map.entries    = NULL;
    v->as.map.count      = 0;
    v->as.map.capacity   = 0;
    return v;
}

/* Cópia profunda */
JscValue *jsc_value_copy(JscValue *v) {
    if (!v) return NULL;

    switch (v->type) {
        case VAL_VAZIO:   return jsc_vazio();
        case VAL_BOOL:    return jsc_bool(v->as.b);
        case VAL_INT:     return jsc_int(v->as.i);
        case VAL_FLOAT:   return jsc_float(v->as.f);
        case VAL_STRING:  return jsc_string(v->as.s);
        case VAL_ARRAY: {
            JscValue *out = jsc_array();
            for (size_t i = 0; i < v->as.array.count; i++) {
                jsc_array_push(out, jsc_value_copy(v->as.array.items[i]));
            }
            return out;
        }
        case VAL_MAP: {
            JscValue *out = jsc_map();
            for (size_t i = 0; i < v->as.map.count; i++) {
                jsc_map_set(out,
                            jsc_value_copy(v->as.map.entries[i].key),
                            jsc_value_copy(v->as.map.entries[i].value));
            }
            return out;
        }
        case VAL_FUNCTION: {
            /* Copia RASA — funcao e imutavel */
            JscValue *out = alloc_value(VAL_FUNCTION);
            out->as.func = v->as.func;
            return out;
        }
        case VAL_CLASS: {
            /* Copia RASA — classes sao compartilhadas (mesma referencia) */
            JscValue *out = alloc_value(VAL_CLASS);
            out->as.cls = v->as.cls;
            return out;
        }
        case VAL_INSTANCE: {
            /* Copia RASA — instancias compartilham o ponteiro da classe
             * e o env de campos. Duplicar daria comportamento errado. */
            JscValue *out = alloc_value(VAL_INSTANCE);
            out->as.inst = v->as.inst;
            return out;
        }
                case VAL_THREAD: {
            JscValue *out = alloc_value(VAL_THREAD);
            out->as.thread = v->as.thread;
            return out;
        }
        default:
            /* BIGINT — devolve vazio por enquanto */
            return jsc_vazio();
    }
}

void jsc_array_push(JscValue *arr, JscValue *item) {
    if (!arr || arr->type != VAL_ARRAY) return;

    if (arr->as.array.count >= arr->as.array.capacity) {
        size_t new_cap = arr->as.array.capacity == 0 ? 4 : arr->as.array.capacity * 2;
        JscValue **new_items = (JscValue **)realloc(arr->as.array.items,
                                                    new_cap * sizeof(JscValue *));
        if (!new_items) {
            fprintf(stderr, "JSC$+ erro: sem memória (array push)\n");
            exit(1);
        }
        arr->as.array.items    = new_items;
        arr->as.array.capacity = new_cap;
    }
    arr->as.array.items[arr->as.array.count++] = item;
}

JscValue *jsc_array_get(JscValue *arr, size_t index) {
    if (!arr || arr->type != VAL_ARRAY) return NULL;
    if (index >= arr->as.array.count) return NULL;
    return arr->as.array.items[index];
}

size_t jsc_array_len(JscValue *arr) {
    if (!arr || arr->type != VAL_ARRAY) return 0;
    return arr->as.array.count;
}

void jsc_map_set(JscValue *map, JscValue *key, JscValue *value) {
    if (!map || map->type != VAL_MAP) return;
    if (!key || !value) return;

    for (size_t i = 0; i < map->as.map.count; i++) {
        if (jsc_value_equals(map->as.map.entries[i].key, key)) {
            jsc_value_free(map->as.map.entries[i].value);
            map->as.map.entries[i].value = value;
            return;
        }
    }

    if (map->as.map.count >= map->as.map.capacity) {
        size_t new_cap = map->as.map.capacity == 0 ? 4 : map->as.map.capacity * 2;
        JscMapEntry *new_entries = (JscMapEntry *)realloc(map->as.map.entries,
                                                          new_cap * sizeof(JscMapEntry));
        if (!new_entries) {
            fprintf(stderr, "JSC$+ erro: sem memória (map set)\n");
            exit(1);
        }
        map->as.map.entries    = new_entries;
        map->as.map.capacity   = new_cap;
    }
    map->as.map.entries[map->as.map.count].key   = key;
    map->as.map.entries[map->as.map.count].value = value;
    map->as.map.count++;
}

JscValue *jsc_map_get(JscValue *map, JscValue *key) {
    if (!map || map->type != VAL_MAP) return NULL;
    for (size_t i = 0; i < map->as.map.count; i++) {
        if (jsc_value_equals(map->as.map.entries[i].key, key)) {
            return map->as.map.entries[i].value;
        }
    }
    return NULL;
}

void jsc_print_value(JscValue *v) {
    if (!v) { printf("vazio"); return; }

    switch (v->type) {
        case VAL_VAZIO:  printf("vazio"); break;
        case VAL_BOOL:   printf("%s", v->as.b ? "true" : "false"); break;
        case VAL_INT:    printf("%lld", v->as.i); break;
        case VAL_FLOAT:
            if (v->as.f == (long long)v->as.f) printf("%lld", (long long)v->as.f);
            else                                printf("%g", v->as.f);
            break;
        case VAL_STRING: printf("%s", v->as.s ? v->as.s : ""); break;
        case VAL_ARRAY:
            printf("[");
            for (size_t i = 0; i < v->as.array.count; i++) {
                if (i > 0) printf(", ");
                jsc_print_value(v->as.array.items[i]);
            }
            printf("]");
            break;
        case VAL_MAP:
            printf("{");
            for (size_t i = 0; i < v->as.map.count; i++) {
                if (i > 0) printf(", ");
                jsc_print_value(v->as.map.entries[i].key);
                printf(": ");
                jsc_print_value(v->as.map.entries[i].value);
            }
            printf("}");
            break;
        case VAL_FUNCTION: printf("<funcao>"); break;
        case VAL_CLASS:    printf("<classe>"); break;
        case VAL_INSTANCE: printf("<instancia>"); break;
        case VAL_THREAD:   printf("<thread>"); break;
        case VAL_BIGINT:   printf("<bigint>"); break;
    }
}

void jsc_print_value_debug(JscValue *v) {
    if (!v) { printf("(JscValue*)NULL"); return; }
    printf("%s(", jsc_type_name(v->type));
    jsc_print_value(v);
    printf(")");
}

int jsc_value_equals(JscValue *a, JscValue *b) {
    if (!a || !b) return a == b;

    if (a->type == VAL_INT && b->type == VAL_FLOAT) return (double)a->as.i == b->as.f;
    if (a->type == VAL_FLOAT && b->type == VAL_INT) return a->as.f == (double)b->as.i;

    if (a->type != b->type) return 0;

    switch (a->type) {
        case VAL_VAZIO:  return 1;
        case VAL_BOOL:   return a->as.b == b->as.b;
        case VAL_INT:    return a->as.i == b->as.i;
        case VAL_FLOAT:  return a->as.f == b->as.f;
        case VAL_STRING: return strcmp(a->as.s ? a->as.s : "",
                                       b->as.s ? b->as.s : "") == 0;
        case VAL_ARRAY: {
            if (a->as.array.count != b->as.array.count) return 0;
            for (size_t i = 0; i < a->as.array.count; i++) {
                if (!jsc_value_equals(a->as.array.items[i], b->as.array.items[i])) return 0;
            }
            return 1;
        }
        case VAL_MAP: {
            if (a->as.map.count != b->as.map.count) return 0;
            for (size_t i = 0; i < a->as.map.count; i++) {
                JscValue *b_val = jsc_map_get(b, a->as.map.entries[i].key);
                if (!b_val) return 0;
                if (!jsc_value_equals(a->as.map.entries[i].value, b_val)) return 0;
            }
            return 1;
        }
        default: return a == b;
    }
}

int jsc_value_is_truthy(JscValue *v) {
    if (!v) return 0;
    switch (v->type) {
        case VAL_VAZIO:  return 0;
        case VAL_BOOL:   return v->as.b != 0;
        case VAL_INT:    return v->as.i != 0;
        case VAL_FLOAT:  return v->as.f != 0.0;
        case VAL_STRING: return v->as.s && v->as.s[0] != '\0';
        case VAL_ARRAY:  return v->as.array.count > 0;
        case VAL_MAP:    return v->as.map.count > 0;
        default:         return 1;
    }
}

double jsc_value_to_float(JscValue *v) {
    if (!v) return 0.0;
    switch (v->type) {
        case VAL_INT:   return (double)v->as.i;
        case VAL_FLOAT: return v->as.f;
        case VAL_BOOL:  return (double)v->as.b;
        default:        return 0.0;
    }
}

long long jsc_value_to_int(JscValue *v) {
    if (!v) return 0;
    switch (v->type) {
        case VAL_INT:   return v->as.i;
        case VAL_FLOAT: return (long long)v->as.f;
        case VAL_BOOL:  return (long long)v->as.b;
        default:        return 0;
    }
}

const char *jsc_type_name(JscType t) {
    switch (t) {
        case VAL_VAZIO:    return "vazio";
        case VAL_BOOL:     return "bool";
        case VAL_INT:      return "int";
        case VAL_FLOAT:    return "float";
        case VAL_BIGINT:   return "bigint";
        case VAL_STRING:   return "string";
        case VAL_ARRAY:    return "array";
        case VAL_MAP:      return "map";
        case VAL_FUNCTION: return "function";
        case VAL_CLASS:    return "class";
        case VAL_INSTANCE: return "instance";
        case VAL_THREAD:   return "thread";
    }
    return "???";
}

static unsigned long long small_int_free_checks = 0;
static unsigned long long small_int_cache_hits = 0;

static int is_small_int_cached(JscValue *v) {
    small_int_free_checks++;

    if (!v) return 0;

    for (int i = 0; i < SMALL_INT_COUNT; i++) {
        if (small_int_cache[i] == v) {
            small_int_cache_hits++;
            return 1;
        }
    }

    return 0;
}

void jsc_value_free(JscValue *v) {
    if (!v) return;
    
    /* NAO libera bools cacheados (otimizacao) */
    extern JscValue *bool_cache_true;
    extern JscValue *bool_cache_false;
    if (v == bool_cache_true || v == bool_cache_false) return;
    
    /* ...resto... */
    if (!v) return;
    if (is_small_int_cached(v)) return;

    /* Remove da lista do GC antes de liberar */
    gc_untrack(v);

    switch (v->type) {
        case VAL_STRING:
            free(v->as.s);
            break;
        case VAL_ARRAY:
            for (size_t i = 0; i < v->as.array.count; i++) {
                jsc_value_free(v->as.array.items[i]);
            }
            free(v->as.array.items);
            break;
        case VAL_MAP:
            for (size_t i = 0; i < v->as.map.count; i++) {
                jsc_value_free(v->as.map.entries[i].key);
                jsc_value_free(v->as.map.entries[i].value);
            }
            free(v->as.map.entries);
            break;
        case VAL_FUNCTION:
            /* NAO libera name/body/params/closure — pertencem a AST */
            break;
        default:
            break;
    }

    free(v);
}

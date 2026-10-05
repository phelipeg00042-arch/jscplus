#include "env.h"
#include "intern.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================
 * JSC$+ — Ambiente (implementação)
 * ============================================================ */

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
 * Pool de Env reutilizavel (otimizacao)
 * Reusa structs Env pra evitar malloc/free em loops
 * ============================================================ */

#define ENV_POOL_SIZE 64
static Env *env_pool[ENV_POOL_SIZE];
static int env_pool_top = 0;

static Env *env_pool_get(void) {
    if (env_pool_top > 0) {
        return env_pool[--env_pool_top];
    }
    return NULL;
}

static void env_pool_put(Env *e) {
    if (env_pool_top < ENV_POOL_SIZE) {
        env_pool[env_pool_top++] = e;
    } else {
        /* Pool cheio: libera de verdade */
        free(e->entries);
        free(e);
    }
}

Env *env_new(Env *parent) {
    /* Tenta pegar do pool */
    Env *e = env_pool_get();
    
    if (e) {
        /* Reusa: limpa entries antigas */
        for (size_t i = 0; i < e->count; i++) {
            free(e->entries[i].name);
            jsc_value_free(e->entries[i].value);
        }
        e->count = 0;
        e->parent = parent;
        e->cache_name = NULL;
        e->cache_value = NULL;
        e->cache_env = NULL;
        return e;
    }
    
    /* Pool vazio: aloca novo */
    e = (Env *)calloc(1, sizeof(Env));
    if (!e) {
        fprintf(stderr, "JSC$+ erro: sem memória (env)\n");
        exit(1);
    }
    e->parent = parent;
    e->cache_name = NULL;
    e->cache_value = NULL;
    e->cache_env = NULL;
    return e;
}


void env_define(Env *env, const char *name, JscValue *value) {
    /* Invalida cache (variavel pode mudar) */
    if (env->cache_name == name || (env->cache_name && strcmp(env->cache_name, name) == 0)) {
        /* Mantem cache, so atualiza o valor */
        env->cache_value = value;
    }
    if (!env || !name) return;

    /* Se já existe no escopo atual, substitui */
    for (size_t i = 0; i < env->count; i++) {
        if (strcmp(env->entries[i].name, name) == 0) {
            jsc_value_free(env->entries[i].value);
            env->entries[i].value = value;
            return;
        }
    }

    /* Se NÃO existe aqui, mas existe num escopo PAI, atualiza lá.
     * Isso faz `soma = soma + x` dentro de um for propagar pro escopo externo. */
    if (env->parent) {
        for (Env *p = env->parent; p != NULL; p = p->parent) {
            for (size_t i = 0; i < p->count; i++) {
                if (strcmp(p->entries[i].name, name) == 0) {
                    jsc_value_free(p->entries[i].value);
                    p->entries[i].value = value;
                    return;
                }
            }
        }
    }

    /* Senão, adiciona novo no escopo atual */
    if (env->count >= env->capacity) {
        size_t new_cap = env->capacity == 0 ? 8 : env->capacity * 2;
        EnvEntry *new_entries = (EnvEntry *)realloc(env->entries,
                                                    new_cap * sizeof(EnvEntry));
        if (!new_entries) {
            fprintf(stderr, "JSC$+ erro: sem memória (env define)\n");
            exit(1);
        }
        env->entries    = new_entries;
        env->capacity   = new_cap;
    }
    env->entries[env->count].name  = intern(name);
    env->entries[env->count].value = value;
    env->count++;
}


JscValue *env_get(Env *env, const char *name) {
    if (!env || !name) return NULL;

    /* Cache hit (otimizacao) */
    if (env->cache_name == name ||
        (env->cache_name && strcmp(env->cache_name, name) == 0)) {
        return env->cache_value;
    }

    for (size_t i = 0; i < env->count; i++) {
        if (env->entries[i].name && strcmp(env->entries[i].name, name) == 0) {
            /* Popula cache */
            env->cache_name = env->entries[i].name;
            env->cache_value = env->entries[i].value;
            env->cache_env = env;
            return env->entries[i].value;
        }
    }

    if (env->parent) {
        JscValue *v = env_get(env->parent, name);
        if (v) {
            /* Popula cache no filho */
            env->cache_name = name;
            env->cache_value = v;
            env->cache_env = env->parent;
        }
        return v;
    }
    return NULL;
}

int env_has(Env *env, const char *name) {
    return env_get(env, name) != NULL;
}

void env_free(Env *env) {
    if (!env) return;

    /* Limpa entries (sempre) */
    for (size_t i = 0; i < env->count; i++) {
        /* NAO libera o name (e interned, gerido pelo pool) */
        jsc_value_free(env->entries[i].value);
    }
    env->count = 0;
    
    /* Devolve ao pool (se houver espaco) */
    if (env_pool_top < ENV_POOL_SIZE) {
        env_pool[env_pool_top++] = env;
    } else {
        /* Pool cheio: libera de verdade */
        free(env->entries);
        free(env);
    }
}

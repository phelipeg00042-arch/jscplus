#ifndef JSC_ENV_H
#define JSC_ENV_H

#include "value.h"

/* ============================================================
 * JSC$+ — Ambiente (escopo de variáveis)
 * ============================================================ */

typedef struct {
    char      *name;
    JscValue  *value;
} EnvEntry;

typedef struct Env {
    EnvEntry    *entries;
    size_t       count;
    size_t       capacity;
    struct Env  *parent;    /* escopo pai (pra closures) */
    
    /* Cache de ultima variavel acessada (otimizacao) */
    const char  *cache_name;
    JscValue    *cache_value;
    struct Env  *cache_env;   /* Env onde a variavel ta */
} Env;

/* Cria um ambiente novo (opcionalmente com pai) */
Env *env_new(Env *parent);

/* Define uma variável (sobrescreve se já existe no escopo atual) */
void env_define(Env *env, const char *name, JscValue *value);

/* Busca uma variável (sobe pro pai se não achar no atual) */
JscValue *env_get(Env *env, const char *name);

/* Verifica se existe */
int env_has(Env *env, const char *name);

/* Libera o ambiente (e todos os valores dentro) */
void env_free(Env *env);

#endif /* JSC_ENV_H */

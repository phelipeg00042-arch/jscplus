#define _POSIX_C_SOURCE 200809L

#include "intern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Pool de strings internadas
 * Hashmap simples com encadeamento
 * ============================================================ */

#define INTERN_BUCKETS 1024
#define INTERN_MAX 4096

typedef struct InternEntry {
    const char *str;
    struct InternEntry *next;
} InternEntry;

static InternEntry *buckets[INTERN_BUCKETS];
static char *strings[INTERN_MAX];
static int str_count = 0;

static unsigned long hash_str(const char *s) {
    unsigned long h = 5381;
    while (*s) {
        h = ((h << 5) + h) + (unsigned char)*s;
        s++;
    }
    return h;
}

const char *intern(const char *s) {
    if (!s) return NULL;

    unsigned long h = hash_str(s) % INTERN_BUCKETS;

    /* Procura na bucket */
    InternEntry *e = buckets[h];
    while (e) {
        if (strcmp(e->str, s) == 0) {
            return e->str;  /* Ja existe, retorna ponteiro existente */
        }
        e = e->next;
    }

    /* Nao existe: adiciona */
    if (str_count >= INTERN_MAX) {
        /* Pool cheio: copia normal */
        return strdup(s);
    }

    char *copia = strdup(s);
    if (!copia) return NULL;

    strings[str_count++] = copia;

    InternEntry *novo = malloc(sizeof(InternEntry));
    novo->str = copia;
    novo->next = buckets[h];
    buckets[h] = novo;

    return copia;
}

void intern_cleanup(void) {
    for (int i = 0; i < INTERN_BUCKETS; i++) {
        InternEntry *e = buckets[i];
        while (e) {
            InternEntry *next = e->next;
            free(e);
            e = next;
        }
        buckets[i] = NULL;
    }
    for (int i = 0; i < str_count; i++) {
        free(strings[i]);
    }
    str_count = 0;
}

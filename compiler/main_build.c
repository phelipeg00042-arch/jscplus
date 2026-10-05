#define _POSIX_C_SOURCE 200809L
#include "transpiler.h"
#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================
 * jsc-build — Transpilador JSC$+ -> C
 * Uso: ./jsc-build programa.jsc
 * Gera: build/programa.c
 * Compila: gcc -o programa build/programa.c runtime/jsc_runtime.c
 * ============================================================ */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    return buf;
}

static char *basename_no_ext(const char *path) {
    const char *p = strrchr(path, '/');
    if (p) p++; else p = path;
    
    char *out = strdup(p);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
    return out;
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);  /* sem buffering */
    if (argc < 2) {
        fprintf(stderr, "Uso: %s programa.jsc [--run]\\n", argv[0]);
        return 1;
    }
    
    const char *input = argv[1];
    int run = 0;
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--run") == 0) run = 1;
    }
    
    /* Le arquivo */
    char *src = read_file(input);
    if (!src) {
        fprintf(stderr, "Erro: nao consegui ler %s\\n", input);
        return 1;
    }
    
    /* Parseia */
    Parser p;
    parser_init(&p, src, input);
    Node *prog = parser_parse_program(&p);
    
    if (p.had_error) {
        fprintf(stderr, "Erro de parse\\n");
        free(src);
        return 1;
    }
    
    /* Transpila */
    Transpiler *t = transpiler_new();
    transpile_program(t, prog);
    
    /* Salva C gerado */
    char *base = basename_no_ext(input);
    char out_c[512];
    snprintf(out_c, sizeof(out_c), "build/%s.c", base);
    
    FILE *f = fopen(out_c, "w");
    if (!f) {
        fprintf(stderr, "Erro: nao consegui criar %s\\n", out_c);
        free(src);
        free(base);
        transpiler_free(t);
        return 1;
    }
    fputs(t->buf, f);
    fclose(f);
    
    printf("[ok] C gerado: %s (%zu bytes)\\n", out_c, t->len);
    
    /* Pega diretorio do projeto (onde esta o jsc-build) */
    char exe_path[512];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) {
        strncpy(exe_path, ".", sizeof(exe_path));
    } else {
        exe_path[len] = 0;
        char *slash = strrchr(exe_path, '/');
        if (slash) *slash = 0;
    }
    
    /* Compila usando caminhos absolutos */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "gcc -Wall -Wextra -std=c11 -O2 -I%s/runtime -o build/%s "
             "build/%s.c %s/runtime/jsc_runtime.c -lcrypto -lm",
             exe_path, base, base, exe_path);
    
    printf("[*] Compilando: %s\\n", base);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "Erro de compilacao\\n");
        free(src);
        free(base);
        transpiler_free(t);
        return 1;
    }
    
    printf("[ok] Binario: build/%s\\n", base);
    
    /* Executa se pedido */
    if (run) {
        printf("[*] Executando:\\n");
        printf("---\\n");
        char run_cmd[512];
        snprintf(run_cmd, sizeof(run_cmd), "./build/%s", base);
        system(run_cmd);
        printf("---\\n");
    }
    
    free(src);
    free(base);
    transpiler_free(t);
    return 0;
}

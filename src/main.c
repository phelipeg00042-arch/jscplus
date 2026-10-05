#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "eval.h"
#include "env.h"
#include "repl.h"
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    /* Sem argumento → REPL */
    if (argc < 2) {
        repl_run();
        return 0;
    }

    /* Com argumento → executa arquivo */
    char *src = read_file(argv[1]);
    if (!src) return 1;

    Parser p;
    parser_init(&p, src, argv[1]);

    Node *prog = parser_parse_program(&p);

    if (p.had_error) {
        fprintf(stderr, "\n[erro durante o parse]\n");
        ast_free(prog);
        free(src);
        return 1;
    }

    Env *global = env_new(NULL);
    natives_init(global);
    gc_set_roots(global);

    EvalState state = {0};
    state.return_value = NULL;
    state.error_message[0] = '\0';
    state.in_try = 0;
    state.has_break = 0;
    state.has_continue = 0;

    eval(prog, global, &state);

    if (state.return_value) {
        jsc_value_free(state.return_value);
    }

    /* Se pedir, roda GC stats + coleta ANTES de liberar */
    if (getenv("JSC_GC_STATS")) {
        gc_stats();
    }
    if (getenv("JSC_GC_VERBOSE")) {
        gc_collect();
        gc_stats();
    }

    env_free(global);
    ast_free(prog);
    free(src);

    return state.had_error ? 1 : 0;
}

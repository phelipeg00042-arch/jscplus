/* ============================================================
 * JSC$+ — REPL (Read-Eval-Print Loop)
 * Interface moderna com cores ANSI
 * ============================================================ */

#include "repl.h"
#include "lexer.h"
#include "parser.h"
#include "eval.h"
#include "env.h"
#include "value.h"
#include "ast.h"
#include "intern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

/* ============================================================
 * Cores ANSI
 * ============================================================ */

#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"
#define ITALIC      "\033[3m"
#define UNDERLINE   "\033[4m"

/* Cores base (preto e vermelho) */
#define RED         "\033[31m"
#define BRIGHT_RED  "\033[91m"
#define BG_RED      "\033[41m"
#define BG_BLACK    "\033[40m"

/* Cores auxiliares */
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"
#define MAGENTA     "\033[35m"
#define CYAN        "\033[36m"
#define WHITE       "\033[37m"
#define GRAY        "\033[90m"
#define BRIGHT_WHITE "\033[97m"

/* ============================================================
 * Banner
 * ============================================================ */

static void print_banner(void) {
    /* Banner minimalista (sem ASCII art) */
    printf("\n");
    printf(BRIGHT_RED BOLD "  JSC$+" RESET "\n");
    printf(RED "  A linguagem da liberdade" RESET "\n");
    printf(GRAY "  ─────────────────────────────────" RESET "\n");
    printf("\n");
    printf("  " GRAY "digite" RESET " " WHITE BOLD "@help" RESET " " GRAY "para ajuda" RESET);
    printf("  " GRAY "•" RESET "  " GRAY "digite" RESET " " WHITE BOLD "@sair" RESET " " GRAY "para sair" RESET "\n");
    printf("\n");
}

/* ============================================================
 * Prompt
 * ============================================================ */

static void print_prompt(void) {
    printf(BRIGHT_RED BOLD "  ❯ " RESET);
    fflush(stdout);
}

/* ============================================================
 * Ajuda
 * ============================================================ */

static void print_help(void) {
    printf("\n");
    printf(BRIGHT_RED BOLD "  ╭─ COMANDOS ───────────────────────────────────╮" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "                                              " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " WHITE BOLD "@help" RESET "        " GRAY "esta ajuda" RESET "                " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " WHITE BOLD "@sair" RESET "        " GRAY "sair do REPL" RESET "             " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " WHITE BOLD "@limpar" RESET "      " GRAY "limpar tela" RESET "              " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " WHITE BOLD "@reset" RESET "       " GRAY "resetar variaveis" RESET "        " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " WHITE BOLD "@sintaxe" RESET "     " GRAY "exemplos de sintaxe" RESET "      " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "                                              " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED BOLD "  ╰──────────────────────────────────────────────╯" RESET "\n");
    printf("\n");
    printf(GRAY "  Dica:" RESET " toda instrucao termina com " BRIGHT_RED BOLD "$+" RESET "\n");
    printf("\n");
}

static void print_sintaxe(void) {
    printf("\n");
    printf(BRIGHT_RED BOLD "  ╭─ SINTAXE RAPIDA ────────────────────────────╮" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "                                            " BRIGHT_RED "│" RESET "\n");

    printf(BRIGHT_RED "  │" RESET "  " CYAN "variavel" RESET "     " WHITE "x = 10" BRIGHT_RED "$+" RESET "                " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " CYAN "printj" RESET "       " WHITE "printj(\"Oi\")" BRIGHT_RED "$+" RESET "           " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " CYAN "condicional" RESET "  " WHITE "x >= 18 >> { ... } << { ... }" BRIGHT_RED "$+" RESET " " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " CYAN "while" RESET "        " WHITE "ENQ x <= 5 { ... }" BRIGHT_RED "$+" RESET "     " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " CYAN "for" RESET "          " WHITE "$> i in 1..10 { ... }" BRIGHT_RED "$+" RESET "   " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " CYAN "funcao" RESET "       " WHITE "fn+ dobro(x) #> x * 2" BRIGHT_RED "$+" RESET " " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " CYAN "retorno" RESET "      " WHITE "GWB valor" BRIGHT_RED "$+" RESET "              " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "  " CYAN "logica" RESET "       " WHITE "a+ (and) o+ (or) n+ (not)" BRIGHT_RED "$+" RESET " " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED "  │" RESET "                                            " BRIGHT_RED "│" RESET "\n");
    printf(BRIGHT_RED BOLD "  ╰────────────────────────────────────────────╯" RESET "\n");
    printf("\n");
}

/* ============================================================
 * Mensagens
 * ============================================================ */

static void print_error(const char *msg) {
    printf("\n  " BRIGHT_RED BOLD "✗" RESET " " RED "%s" RESET "\n\n", msg);
}

static void print_success(void) {
    printf("\n  " GREEN BOLD "✓" RESET " " GRAY "tchau, ate logo!" RESET "\n\n");
}

/* ============================================================
 * Processa linha
 * ============================================================ */

static int process_line(const char *line, Env **global_env) {
    if (!line || !*line) return 1;

    /* ============================================================
     * Comandos @
     * ============================================================ */
    if (line[0] == '@') {
        if (strcmp(line, "@sair") == 0 || strcmp(line, "@exit") == 0 || strcmp(line, "@quit") == 0) {
            print_success();
            return 0;
        }
        if (strcmp(line, "@help") == 0) {
            print_help();
            return 1;
        }
        if (strcmp(line, "@sintaxe") == 0 || strcmp(line, "@syntax") == 0) {
            print_sintaxe();
            return 1;
        }
        if (strcmp(line, "@limpar") == 0 || strcmp(line, "@clear") == 0) {
            printf("\033[2J\033[H");
            print_banner();
            return 1;
        }
        if (strcmp(line, "@reset") == 0) {
            if (*global_env) {
                env_free(*global_env);
            }
            *global_env = env_new(NULL);
            natives_init(*global_env);
            gc_set_roots(*global_env);
            printf("\n  " GREEN BOLD "✓" RESET " " GRAY "variaveis resetadas" RESET "\n\n");
            return 1;
        }
        print_error("comando desconhecido. Tente @help");
        return 1;
    }

    /* ============================================================
     * Remove $+ final se tiver (pra trabalhar com a linha limpa)
     * ============================================================ */
    char linha_limpa[4096];
    strncpy(linha_limpa, line, sizeof(linha_limpa) - 1);
    linha_limpa[sizeof(linha_limpa) - 1] = 0;
    size_t len = strlen(linha_limpa);
    if (len >= 2 && linha_limpa[len-2] == '$' && linha_limpa[len-1] == '+') {
        linha_limpa[len-2] = 0;
    }

    /* ============================================================
     * Detecta se e' uma EXPRESSAO PURA (nao comeca com keyword)
     *
     * Keywords que indicam statement:
     *   x =   (atribuicao com =)
     *   fn+   (funcao)
     *   printj (ja imprime)
     *   >>    (condicional)
     *   ENQ   (while)
     *   $>    (for)
     *   jsc   (import)
     *   base  (classe)
     *   !!    (try)
     *   GWB   (return)
     * ============================================================ */
    int eh_expressao = 1;
    const char *p = linha_limpa;
    while (*p == ' ' || *p == '\t') p++;

    /* Se contem " = " (atribuicao), eh statement */
    if (strstr(linha_limpa, " = ") != NULL) eh_expressao = 0;
    /* Se contem "=" antes de qualquer operador, pode ser atribuicao */
    for (const char *q = linha_limpa; *q; q++) {
        if (*q == '=') {
            /* Exclui ==, >=, <=, != */
            if ((q > linha_limpa && (*(q-1) == '!' || *(q-1) == '<' || *(q-1) == '>' || *(q-1) == '=')) ||
                (*(q+1) == '=')) continue;
            eh_expressao = 0;
            break;
        }
    }
    if (strncmp(p, "printj", 6) == 0) eh_expressao = 0;
    if (strncmp(p, "fn+", 3) == 0) eh_expressao = 0;
    if (strncmp(p, "jsc ", 4) == 0) eh_expressao = 0;
    if (strncmp(p, "base ", 5) == 0) eh_expressao = 0;
    if (strncmp(p, "ENQ ", 4) == 0) eh_expressao = 0;
    if (strncmp(p, "$>", 2) == 0) eh_expressao = 0;
    if (strncmp(p, "!!", 2) == 0) eh_expressao = 0;
    if (strncmp(p, "GWB ", 4) == 0) eh_expressao = 0;
    
    /* Detecta >> ou << (condicional) em qualquer lugar da linha */
    if (strstr(linha_limpa, ">>") != NULL) eh_expressao = 0;
    if (strstr(linha_limpa, "<<") != NULL) eh_expressao = 0;
    /* Detecta } { ... } (block) */
    if (strstr(linha_limpa, "{") != NULL) eh_expressao = 0;

    /* ============================================================
     * CASO 1: EXPRESSAO - envolve em printj()
     * ============================================================ */
    if (eh_expressao) {
        char linha_expr[4096];
        snprintf(linha_expr, sizeof(linha_expr), "printj(%s)$+", linha_limpa);

        Parser p2;
        parser_init(&p2, linha_expr, "<repl>");
        Node *prog2 = parser_parse_program(&p2);

        if (!p2.had_error && prog2) {
            EvalState state2 = {0};
            state2.return_value = NULL;
            eval(prog2, *global_env, &state2);
            /* NAO libera AST (env guarda ponteiro) */
            return 1;
        }
        /* Se falhou como expressao, cai pro statement */
    }

    /* ============================================================
     * CASO 2: STATEMENT
     * ============================================================ */
    Parser parser_stmt;
    parser_init(&parser_stmt, line, "<repl>");
    Node *prog = parser_parse_program(&parser_stmt);

    if (!parser_stmt.had_error && prog) {
        EvalState state = {0};
        state.return_value = NULL;
        eval(prog, *global_env, &state);

        if (state.return_value) {
            printf("  " GRAY "→" RESET " ");
            jsc_print_value(state.return_value);
            printf("\n");
            jsc_value_free(state.return_value);
        }
        /* NAO libera AST */
        return 1;
    }

    print_error("sintaxe invalida");
    return 1;
}

/* ============================================================
 * Loop principal
 * ============================================================ */

void repl_run(void) {
    /* NAO limpa tela (chato) */
    /* printf("\033[2J\033[H"); */

    print_banner();

    Env *global_env = env_new(NULL);
    natives_init(global_env);
    gc_set_roots(global_env);

    char line[4096];

    while (1) {
        print_prompt();

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            print_success();
            break;
        }

        /* Remove \n */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = 0;
        }

        if (!process_line(line, &global_env)) {
            break;
        }
    }

    env_free(global_env);
    intern_cleanup();
}
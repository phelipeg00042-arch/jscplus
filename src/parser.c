#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================
 * JSC$+ — Parser
 * Converte lista de tokens em AST
 *
 * IMPORTANTE: sempre duplicar lexeme antes de chamar parser_advance,
 * porque parser_advance libera o lexeme do token anterior.
 * ============================================================ */

/* ------------------------------------------------------------
 * Infraestrutura básica
 * ------------------------------------------------------------ */

/* Flag: desabilita >> como shift quando estamos testando if */
static int parser_no_shift = 0;

void parser_init(Parser *p, const char *src, const char *filename) {
    lexer_init(&p->lexer, src);
    p->filename  = filename;
    p->had_error = 0;
    p->previous.type   = TOK_EOF;
    p->previous.lexeme = NULL;
    p->current = lexer_next(&p->lexer);
}

void parser_advance(Parser *p) {
    if (p->previous.lexeme) {
        free(p->previous.lexeme);
    }
    p->previous = p->current;
    p->current  = lexer_next(&p->lexer);
}

int parser_check(Parser *p, TokenType type) {
    return p->current.type == type;
}

int parser_match(Parser *p, TokenType type) {
    if (parser_check(p, type)) {
        parser_advance(p);
        return 1;
    }
    return 0;
}

Token parser_expect(Parser *p, TokenType type, const char *msg) {
    if (parser_check(p, type)) {
        Token t = p->current;
        parser_advance(p);
        return t;
    }
    parser_error(p, msg);
    return p->current;
}

void parser_error(Parser *p, const char *msg) {
    fprintf(stderr,
            "JSC$+ erro em %s [%d:%d]: %s (token atual: %s '%s')\n",
            p->filename ? p->filename : "?",
            p->current.line,
            p->current.column,
            msg,
            token_type_name(p->current.type),
            p->current.lexeme ? p->current.lexeme : "");
    p->had_error = 1;
}

/* ------------------------------------------------------------
 * Programa — loop de comandos
 * ------------------------------------------------------------ */

Node *parser_parse_program(Parser *p) {
    Node *prog = ast_program(1, 1);
    NodeProgram *prog_node = (NodeProgram *)prog;

    while (!parser_check(p, TOK_EOF)) {Node *stmt = parser_parse_statement(p);
        if (stmt) {
            nodelist_push(&prog_node->statements, stmt);
        }
        if (p->had_error) break;
    }

    return prog;
}

/* ------------------------------------------------------------
 * Expressão — ponto de entrada
 * ------------------------------------------------------------ */

Node *parser_parse_expression(Parser *p) {
    Node *left = parser_parse_ternary(p);

    /* v3.0: pipe ~> (encadeamento)
     * `a ~> f` vira `f(a)`
     * `a ~> f ~> g` vira `g(f(a))`
     */
    while (parser_match(p, TOK_PIPE)) {
        /* Le o lado direito (deve ser uma chamada de funcao) */
        Node *right = parser_parse_ternary(p);

        /* Se right for NodeCall: insere left como PRIMEIRO argumento */
        if (right->type == NODE_CALL) {
            NodeCall *call = (NodeCall *)right;
            /* Insere left no inicio da lista de args */
            /* Precisa criar lista nova com left primeiro */
            size_t n = call->args.count;
            NodeList novo_args;
            nodelist_init(&novo_args);
            nodelist_push(&novo_args, left);
            for (size_t i = 0; i < n; i++) {
                nodelist_push(&novo_args, call->args.items[i]);
            }
            free(call->args.items);
            call->args = novo_args;
            left = right;
        }
        /* Se right for NodeIdent (funcao sem parenteses), vira chamada f(left) */
        else if (right->type == NODE_IDENT) {
            Node *call = ast_call(right, right->line, right->column);
            NodeCall *cn = (NodeCall *)call;
            nodelist_push(&cn->args, left);
            left = call;
        }
        else {
            parser_error(p, "lado direito do pipe (~>) deve ser funcao");
            ast_free(right);
            return left;
        }
    }

    return left;
}

/* ------------------------------------------------------------
 * Ternário: cond ? a : b
 * ------------------------------------------------------------ */

Node *parser_parse_ternary(Parser *p) {
    Node *cond = parser_parse_or(p);

    if (parser_match(p, TOK_QUESTION)) {
        Node *then_e = parser_parse_expression(p);
        parser_expect(p, TOK_COLON, "esperava ':' no ternário");
        Node *else_e = parser_parse_expression(p);
        return ast_ternary(cond, then_e, else_e, cond->line, cond->column);
    }
    return cond;
}

/* ------------------------------------------------------------
 * Or: a o+ b
 * ------------------------------------------------------------ */

Node *parser_parse_or(Parser *p) {
    Node *left = parser_parse_and(p);

    while (parser_match(p, TOK_OR)) {
        Node *right = parser_parse_and(p);
        left = ast_binary("o+", left, right, left->line, left->column);
    }
    return left;
}

/* ------------------------------------------------------------
 * And: a a+ b
 * ------------------------------------------------------------ */

Node *parser_parse_and(Parser *p) {
    Node *left = parser_parse_equality(p);

    while (parser_match(p, TOK_AND)) {
        Node *right = parser_parse_equality(p);
        left = ast_binary("a+", left, right, left->line, left->column);
    }
    return left;
}

/* ------------------------------------------------------------
 * Igualdade: == !=
 * ------------------------------------------------------------ */

Node *parser_parse_equality(Parser *p) {
    Node *left = parser_parse_comparison(p);

    while (1) {
        if (parser_match(p, TOK_EQ)) {
            Node *right = parser_parse_comparison(p);
            left = ast_binary("==", left, right, left->line, left->column);
        } else if (parser_match(p, TOK_NEQ)) {
            Node *right = parser_parse_comparison(p);
            left = ast_binary("!=", left, right, left->line, left->column);
        } else {
            break;
        }
    }
    return left;
}

/* ------------------------------------------------------------
 * Comparação: < > <= >=
 * ------------------------------------------------------------ */

Node *parser_parse_comparison(Parser *p) {
    Node *left = parser_parse_bit_or(p);

    while (1) {
        if (parser_match(p, TOK_LT)) {
            Node *right = parser_parse_bit_or(p);
            left = ast_binary("<", left, right, left->line, left->column);
        } else if (parser_match(p, TOK_GT)) {
            Node *right = parser_parse_bit_or(p);
            left = ast_binary(">", left, right, left->line, left->column);
        } else if (parser_match(p, TOK_LTE)) {
            Node *right = parser_parse_bit_or(p);
            left = ast_binary("<=", left, right, left->line, left->column);
        } else if (parser_match(p, TOK_GTE)) {
            Node *right = parser_parse_bit_or(p);
            left = ast_binary(">=", left, right, left->line, left->column);
        } else {
            break;
        }
    }
    return left;
}

/* ------------------------------------------------------------
 * Bitwise OR: |
 * ------------------------------------------------------------ */

Node *parser_parse_bit_or(Parser *p) {
    Node *left = parser_parse_bit_xor(p);
    while (parser_match(p, TOK_BIT_OR)) {
        Node *right = parser_parse_bit_xor(p);
        left = ast_binary("|", left, right, left->line, left->column);
    }
    return left;
}

/* ------------------------------------------------------------
 * Bitwise XOR: ^
 * ------------------------------------------------------------ */

Node *parser_parse_bit_xor(Parser *p) {
    Node *left = parser_parse_bit_and(p);
    while (parser_match(p, TOK_BIT_XOR)) {
        Node *right = parser_parse_bit_and(p);
        left = ast_binary("^", left, right, left->line, left->column);
    }
    return left;
}

/* ------------------------------------------------------------
 * Bitwise AND: &
 * ------------------------------------------------------------ */

Node *parser_parse_bit_and(Parser *p) {
    Node *left = parser_parse_shift(p);
    while (parser_match(p, TOK_BIT_AND)) {
        Node *right = parser_parse_shift(p);
        left = ast_binary("&", left, right, left->line, left->column);
    }
    return left;
}

/* ------------------------------------------------------------
 * Shift: << >>
 * ------------------------------------------------------------ */

Node *parser_parse_shift(Parser *p) {
    Node *left = parser_parse_term(p);
    while (1) {
        /* Se estamos testando if, NAO consome >> nem << */
        if (parser_no_shift && (parser_check(p, TOK_SHR) || parser_check(p, TOK_SHL))) {
            break;
        }
        if (parser_match(p, TOK_SHL)) {
            Node *right = parser_parse_term(p);
            left = ast_binary("<<", left, right, left->line, left->column);
        } else if (parser_match(p, TOK_SHR)) {
            Node *right = parser_parse_term(p);
            left = ast_binary(">>", left, right, left->line, left->column);
        } else {
            break;
        }
    }
    return left;
}

/* ------------------------------------------------------------
 * Termo: + -
 * ------------------------------------------------------------ */

Node *parser_parse_term(Parser *p) {
    Node *left = parser_parse_factor(p);

    while (1) {
        if (parser_match(p, TOK_PLUS)) {
            Node *right = parser_parse_factor(p);
            left = ast_binary("+", left, right, left->line, left->column);
        } else if (parser_match(p, TOK_MINUS)) {
            Node *right = parser_parse_factor(p);
            left = ast_binary("-", left, right, left->line, left->column);
        } else {
            break;
        }
    }
    return left;
}

/* ------------------------------------------------------------
 * Fator: * / %
 * ------------------------------------------------------------ */

Node *parser_parse_factor(Parser *p) {
    Node *left = parser_parse_unary(p);

    while (1) {
        if (parser_match(p, TOK_STAR)) {
            Node *right = parser_parse_unary(p);
            left = ast_binary("*", left, right, left->line, left->column);
        } else if (parser_match(p, TOK_SLASH)) {
            Node *right = parser_parse_unary(p);
            left = ast_binary("/", left, right, left->line, left->column);
        } else if (parser_match(p, TOK_PERCENT)) {
            Node *right = parser_parse_unary(p);
            left = ast_binary("%", left, right, left->line, left->column);
        } else {
            break;
        }
    }
    return left;
}

/* ------------------------------------------------------------
 * Unário: - n+ ~
 * ------------------------------------------------------------ */

Node *parser_parse_unary(Parser *p) {
    if (parser_match(p, TOK_MINUS)) {
        int line = p->previous.line;
        int col  = p->previous.column;
        Node *operand = parser_parse_unary(p);
        return ast_unary("-", operand, line, col);
    }
    if (parser_match(p, TOK_NOT)) {
        int line = p->previous.line;
        int col  = p->previous.column;
        Node *operand = parser_parse_unary(p);
        return ast_unary("n+", operand, line, col);
    }
    if (parser_match(p, TOK_BIT_NOT)) {
        int line = p->previous.line;
        int col  = p->previous.column;
        Node *operand = parser_parse_unary(p);
        return ast_unary("~", operand, line, col);
    }
    return parser_parse_power(p);
}

/* ------------------------------------------------------------
 * Potência: ** (direita-associativa)
 * ------------------------------------------------------------ */

Node *parser_parse_power(Parser *p) {
    Node *left = parser_parse_postfix(p);

    if (parser_match(p, TOK_POW)) {
        Node *right = parser_parse_unary(p);
        return ast_binary("**", left, right, left->line, left->column);
    }
    return left;
}

/* ------------------------------------------------------------
 * Postfix: chamada f(x), index a[0], field obj.x
 * ------------------------------------------------------------ */

Node *parser_parse_postfix(Parser *p) {
    Node *expr = parser_parse_primary(p);

    for (;;) {
        if (parser_match(p, TOK_LPAREN)) {
            Node *call = ast_call(expr, expr->line, expr->column);
            NodeCall *c = (NodeCall *)call;

            if (!parser_check(p, TOK_RPAREN)) {
                do {
                    Node *arg = parser_parse_expression(p);
                    nodelist_push(&c->args, arg);
                } while (parser_match(p, TOK_COMMA));
            }
            parser_expect(p, TOK_RPAREN, "esperava ')' após argumentos");
            expr = call;
        } else if (parser_match(p, TOK_LBRACKET)) {
            Node *idx = parser_parse_expression(p);
            parser_expect(p, TOK_RBRACKET, "esperava ']' após índice");
            expr = ast_index(expr, idx, expr->line, expr->column);
        } else if (parser_match(p, TOK_DOT)) {
            Token name = parser_expect(p, TOK_IDENT, "esperava nome de campo após '.'");
            char *field_name = name.lexeme ? strdup(name.lexeme) : strdup("?");
            int   field_line = expr->line;
            int   field_col  = expr->column;
            expr = ast_field(expr, field_name, field_line, field_col);
            free(field_name);
        } else {
            break;
        }
    }
    return expr;
}

/* ------------------------------------------------------------
 * Primary: número, string, bool, vazio, ident, array, map, (expr)
 * ------------------------------------------------------------ */

Node *parser_parse_primary(Parser *p) {
    /* Número */
    if (parser_check(p, TOK_NUMBER)) {
        Token t = p->current;
        double val = t.number;
        int    line = t.line;
        int    col  = t.column;
        parser_advance(p);
        return ast_number(val, line, col);
    }

    /* String */
    if (parser_check(p, TOK_STRING)) {
        Token t = p->current;
        char *val = t.lexeme ? strdup(t.lexeme) : strdup("");
        int   line = t.line;
        int   col  = t.column;
        parser_advance(p);
        Node *n = ast_string(val, line, col);
        free(val);
        return n;
    }

    /* Bool */
    if (parser_match(p, TOK_TRUE)) {
        return ast_bool(1, p->previous.line, p->previous.column);
    }
    if (parser_match(p, TOK_FALSE)) {
        return ast_bool(0, p->previous.line, p->previous.column);
    }

    /* Vazio */
    if (parser_match(p, TOK_VAZIO)) {
        return ast_vazio(p->previous.line, p->previous.column);
    }

    /* this */
    if (parser_match(p, TOK_THIS)) {
        return ast_ident("this", p->previous.line, p->previous.column);
    }

    /* Identificador */
    if (parser_check(p, TOK_IDENT)) {
        Token t = p->current;
        char *name = t.lexeme ? strdup(t.lexeme) : strdup("?");
        int   line = t.line;
        int   col  = t.column;
        parser_advance(p);
        Node *n = ast_ident(name, line, col);
        free(name);
        return n;
    }

    /* Array: [1, 2, 3] */
    if (parser_match(p, TOK_LBRACKET)) {
        int line = p->previous.line;
        int col  = p->previous.column;
        Node *arr = ast_array(line, col);
        NodeArray *a = (NodeArray *)arr;

        if (!parser_check(p, TOK_RBRACKET)) {
            do {
                Node *item = parser_parse_expression(p);
                nodelist_push(&a->items, item);
            } while (parser_match(p, TOK_COMMA));
        }
        parser_expect(p, TOK_RBRACKET, "esperava ']' após array");
        return arr;
    }

    /* Map: {"k": v} */
    if (parser_match(p, TOK_LBRACE)) {
        int line = p->previous.line;
        int col  = p->previous.column;
        Node *map = ast_map(line, col);
        NodeMap *m = (NodeMap *)map;

        if (!parser_check(p, TOK_RBRACE)) {
            do {
                Node *key;
                if (parser_check(p, TOK_STRING)) {
                    Token t = p->current;
                    char *val = t.lexeme ? strdup(t.lexeme) : strdup("");
                    int   line_k = t.line;
                    int   col_k  = t.column;
                    parser_advance(p);
                    key = ast_string(val, line_k, col_k);
                    free(val);
                } else if (parser_check(p, TOK_IDENT)) {
                    Token t = p->current;
                    char *name = t.lexeme ? strdup(t.lexeme) : strdup("?");
                    int   line_k = t.line;
                    int   col_k  = t.column;
                    parser_advance(p);
                    key = ast_ident(name, line_k, col_k);
                    free(name);
                } else {
                    parser_error(p, "esperava chave (string ou ident) no map");
                    return map;
                }

                parser_expect(p, TOK_COLON, "esperava ':' após chave do map");
                Node *value = parser_parse_expression(p);

                nodelist_push(&m->keys, key);
                nodelist_push(&m->values, value);
            } while (parser_match(p, TOK_COMMA));
        }
        parser_expect(p, TOK_RBRACE, "esperava '}' após map");
        return map;
    }

    /* Agrupamento: (expr) */
    if (parser_match(p, TOK_LPAREN)) {
        Node *inner = parser_parse_expression(p);
        parser_expect(p, TOK_RPAREN, "esperava ')' após expressão");
        return inner;
    }

    parser_error(p, "expressão inválida");
    parser_advance(p);
    return ast_vazio(p->current.line, p->current.column);
}

/* ============================================================
 * COMANDOS — Camada 2.3
 * ============================================================ */

/* ============================================================
 * Terminador opcional $+ — obrigatorio em instrucoes,
 * opcional apos }, ] ou ).
 * ============================================================ */
void parser_optional_terminator(Parser *p) {
    if (parser_check(p, TOK_TERMINATOR)) {
        parser_advance(p);
        return;
    }
    /* Verifica se o token anterior foi }, ] ou ) */
    if (p->previous.type == TOK_RBRACE ||
        p->previous.type == TOK_RBRACKET ||
        p->previous.type == TOK_RPAREN) {
        return;  /* OK, terminador opcional */
    }
    parser_error(p, "esperava '$+' no fim da linha");
}

Node *parser_parse_statement(Parser *p) {
    /* Nativos */
    if (parser_check(p, TOK_PRINTJ))    return parser_parse_printj(p);
    if (parser_check(p, TOK_JSC))       return parser_parse_import(p);

    /* Controle de fluxo v3.0 */
    if (parser_check(p, TOK_SHR) || parser_check(p, TOK_GT)) return parser_parse_if(p);
    if (parser_check(p, TOK_ENQ))       return parser_parse_while(p);
    if (parser_check(p, TOK_FOR) || parser_check(p, TOK_FOR_EACH)) return parser_parse_for(p);

    /* Declaracoes v3.0 */
    if (parser_check(p, TOK_BASE))      return parser_parse_class(p);
    if (parser_check(p, TOK_GWB))       return parser_parse_return(p);
    if (parser_check(p, TOK_IDENT) && p->current.lexeme &&
        strcmp(p->current.lexeme, "fn") == 0 &&
        p->lexer.src[p->lexer.pos] == '+') {
        return parser_parse_function(p);
    }

    /* Excecao v3.0 */
    if (parser_check(p, TOK_TRY_OP))    return parser_parse_try(p);

    /* Break / Continue */
    if (parser_check(p, TOK_BREAK)) {
        Token kw = p->current;
        parser_advance(p);
        parser_optional_terminator(p);
        return ast_break(kw.line, kw.column);
    }
    if (parser_check(p, TOK_CONTINUE)) {
        Token kw = p->current;
        parser_advance(p);
        parser_optional_terminator(p);
        return ast_continue(kw.line, kw.column);
    }

    /* Bloco e this */
    if (parser_check(p, TOK_LBRACE))    return parser_parse_block(p);
    if (parser_check(p, TOK_THIS))      return parser_parse_this_statement(p);

    /* Expressoes que comecam com operador unario (n+, -, ~, !) seguidas de >> */
    if (parser_check(p, TOK_NOT) || parser_check(p, TOK_MINUS) ||
        parser_check(p, TOK_BIT_NOT)) {

        /* Scanner: verifica se tem >> antes do fim da linha */
        const char *src = p->lexer.src + p->lexer.pos;
        int tem_shr = 0;
        while (*src && *src != '\n' && *src != '{') {
            if (*src == '>' && src[1] == '>') { tem_shr = 1; break; }
            if (*src == '$' && src[1] == '+') break;
            src++;
        }

        if (tem_shr) {
            int saved = parser_no_shift;
            parser_no_shift = 1;
            Node *expr = parser_parse_expression(p);
            parser_no_shift = saved;

            int line = expr ? expr->line : 1;
            int col  = expr ? expr->column : 1;

            if (parser_check(p, TOK_SHR) || parser_check(p, TOK_GT)) {
                parser_advance(p);
                Node *then_b = parser_parse_block(p);
                Node *else_b = NULL;
                if (parser_check(p, TOK_SHL) || parser_check(p, TOK_LT)) {
                    parser_advance(p);
                    if (parser_check(p, TOK_LBRACE)) {
                        else_b = parser_parse_block(p);
                    } else {
                        Node *else_if = parser_parse_statement(p);
                        else_b = ast_block(else_if ? else_if->line : line,
                                           else_if ? else_if->column : col);
                        if (else_if) {
                            nodelist_push(&((NodeBlock *)else_b)->statements, else_if);
                        }
                    }
                }
                return ast_if(expr, then_b, else_b, line, col);
            }
            ast_free(expr);
        }
    }

    /* Identificador — pode ser var decl, atribuicao OU if v3.0 (expr >> { }) */
    if (parser_check(p, TOK_IDENT)) {
        /* Scanner rapido: olha o source atual ate achar '{' ou '=' ou '$+'.
         * Se achar '>>' antes, e um IF. */
        size_t back = p->current.lexeme ? strlen(p->current.lexeme) : 0;
        const char *src = p->lexer.src + p->lexer.pos - back;
        int tem_shr = 0;

        while (*src) {
            if (*src == '{' || *src == '\n') break;
            if (*src == '$' && src[1] == '+') break;
            if ((*src == '>' || *src == '<' || *src == '=' || *src == '!') && src[1] == '=') {
                src += 2; continue;
            }
            if (*src == '=') break;
            if (*src == '>' && src[1] == '>') { tem_shr = 1; break; }
            src++;
        }

        if (tem_shr) {
            int saved = parser_no_shift;
            parser_no_shift = 1;
            Node *cond = parser_parse_expression(p);
            parser_no_shift = saved;

            int line = cond ? cond->line : 1;
            int col  = cond ? cond->column : 1;

            if (parser_check(p, TOK_SHR) || parser_check(p, TOK_GT)) {
                parser_advance(p);
                Node *then_b = parser_parse_block(p);
                Node *else_b = NULL;

                /* Verifica se tem << */
                if (parser_check(p, TOK_SHL) || parser_check(p, TOK_LT)) {
                    parser_advance(p);  /* consome << */

                    /* Se for { direto, e o else final */
                    if (parser_check(p, TOK_LBRACE)) {
                        else_b = parser_parse_block(p);
                    }
                    /* Senao, e else if: le a condicao e chama parser_parse_if
                     * (que vai ler a condicao, >> e bloco). */
                    else {
                        /* Empacota o else if num bloco */
                        Node *else_if = parser_parse_statement(p);
                        else_b = ast_block(else_if ? else_if->line : line,
                                           else_if ? else_if->column : col);
                        if (else_if) {
                            nodelist_push(&((NodeBlock *)else_b)->statements, else_if);
                        }
                    }
                }

                return ast_if(cond, then_b, else_b, line, col);
            }
            ast_free(cond);
        }

        return parser_parse_var_decl_or_assign(p);
    }

    if (parser_match(p, TOK_TERMINATOR)) return NULL;

    parser_error(p, "comando inválido");
    parser_advance(p);
    return NULL;
}

Node *parser_parse_block(Parser *p) {Token brace = parser_expect(p, TOK_LBRACE, "esperava '{'");
    Node *blk = ast_block(brace.line, brace.column);
    NodeBlock *b = (NodeBlock *)blk;

    while (!parser_check(p, TOK_RBRACE) && !parser_check(p, TOK_EOF)) {
        Node *stmt = parser_parse_statement(p);
        if (stmt) {
            nodelist_push(&b->statements, stmt);
        }
        if (p->had_error) break;
    }parser_expect(p, TOK_RBRACE, "esperava '}' para fechar bloco");
    parser_match(p, TOK_TERMINATOR);return blk;
}

Node *parser_parse_printj(Parser *p) {
    Token kw = parser_expect(p, TOK_PRINTJ, "esperava 'printj'");
    parser_expect(p, TOK_LPAREN, "esperava '(' depois de printj");

    Node *expr = parser_parse_expression(p);

    parser_expect(p, TOK_RPAREN, "esperava ')' depois da expressão");
    parser_optional_terminator(p);

    return ast_printj(expr, kw.line, kw.column);
}

Node *parser_parse_var_decl_or_assign(Parser *p) {
    /* v3.0: pode ser:
     *   - `nome: tipo = valor` (var decl com tipo)
     *   - `nome = valor` (var decl sem tipo)
     *   - `nome[0] = valor` (atribuicao por indice)
     *   - `nome.metodo(args)` (chamada de metodo)
     *   - `funcao(args)` (chamada de funcao)
     *   - `expr >> { }` (if em v3.0)
     *   - `expr << { }` (else em v3.0 — raro)
     */

    Token first = parser_expect(p, TOK_IDENT, "esperava identificador");
    char *first_name = first.lexeme ? strdup(first.lexeme) : strdup("?");
    int   first_line = first.line;
    int   first_col  = first.column;

    /* Caso 1: chamada de funcao solta — funcao(args) */
    if (parser_check(p, TOK_LPAREN)) {
        parser_advance(p);
        Node *callee = ast_ident(first_name, first_line, first_col);
        free(first_name);
        Node *call = ast_call(callee, first_line, first_col);
        NodeCall *cn = (NodeCall *)call;
        if (!parser_check(p, TOK_RPAREN)) {
            do {
                Node *arg = parser_parse_expression(p);
                nodelist_push(&cn->args, arg);
            } while (parser_match(p, TOK_COMMA));
        }
        parser_expect(p, TOK_RPAREN, "esperava ')' apos argumentos");
        parser_optional_terminator(p);
        Node *stmt = ast_block(first_line, first_col);
        nodelist_push(&((NodeBlock *)stmt)->statements, call);
        return stmt;
    }

    /* Caso 2: chamada de metodo solta — nome.metodo(...) */
    if (parser_check(p, TOK_DOT)) {
        Node *target = ast_ident(first_name, first_line, first_col);
        free(first_name);
        while (parser_match(p, TOK_DOT)) {
            Token fname = parser_expect(p, TOK_IDENT, "esperava nome de campo");
            char *f = fname.lexeme ? strdup(fname.lexeme) : strdup("?");
            target = ast_field(target, f, first_line, first_col);
            free(f);
        }
        if (parser_check(p, TOK_LPAREN)) {
            parser_advance(p);
            Node *call = ast_call(target, first_line, first_col);
            NodeCall *cn = (NodeCall *)call;
            if (!parser_check(p, TOK_RPAREN)) {
                do {
                    Node *arg = parser_parse_expression(p);
                    nodelist_push(&cn->args, arg);
                } while (parser_match(p, TOK_COMMA));
            }
            parser_expect(p, TOK_RPAREN, "esperava ')' apos argumentos");
            parser_optional_terminator(p);
            Node *stmt = ast_block(first_line, first_col);
            nodelist_push(&((NodeBlock *)stmt)->statements, call);
            return stmt;
        }
        parser_error(p, "esperava chamada de metodo apos '.'");
        return NULL;
    }

    /* Caso 3: acesso por indice e/ou metodo
     *   nome[0]              (so leitura — expressao solta)
     *   nome[0] = valor      (escrita)
     *   nome[0].metodo(args) (chamada de metodo)
     *   nome[0].campo        (acesso de campo)
     */
    if (parser_check(p, TOK_LBRACKET)) {
        Node *target = ast_ident(first_name, first_line, first_col);
        free(first_name);
        while (parser_match(p, TOK_LBRACKET)) {
            Node *idx2 = parser_parse_expression(p);
            parser_expect(p, TOK_RBRACKET, "esperava ']'");
            target = ast_index(target, idx2, first_line, first_col);
        }

        /* Verifica se tem . depois do index — pode ser metodo ou campo */
        if (parser_check(p, TOK_DOT)) {
            while (parser_match(p, TOK_DOT)) {
                Token fname = parser_expect(p, TOK_IDENT, "esperava nome de campo");
                char *f = fname.lexeme ? strdup(fname.lexeme) : strdup("?");
                target = ast_field(target, f, first_line, first_col);
                free(f);
            }

            /* Chamada de metodo */
            if (parser_check(p, TOK_LPAREN)) {
                parser_advance(p);
                Node *call = ast_call(target, first_line, first_col);
                NodeCall *cn = (NodeCall *)call;
                if (!parser_check(p, TOK_RPAREN)) {
                    do {
                        Node *arg = parser_parse_expression(p);
                        nodelist_push(&cn->args, arg);
                    } while (parser_match(p, TOK_COMMA));
                }
                parser_expect(p, TOK_RPAREN, "esperava ')' apos argumentos");
                parser_optional_terminator(p);
                Node *stmt = ast_block(first_line, first_col);
                nodelist_push(&((NodeBlock *)stmt)->statements, call);
                return stmt;
            }
        }

        /* Caso contrario: e atribuicao por indice */
        const char *op_str = "=";
        if (parser_match(p, TOK_ASSIGN)) op_str = "=";
        else if (parser_match(p, TOK_PLUS_EQ)) op_str = "+=";
        else if (parser_match(p, TOK_MINUS_EQ)) op_str = "-=";
        else if (parser_match(p, TOK_STAR_EQ)) op_str = "*=";
        else if (parser_match(p, TOK_SLASH_EQ)) op_str = "/=";
        else if (parser_match(p, TOK_PERCENT_EQ)) op_str = "%=";
        else {
            parser_error(p, "esperava '=', '.' ou operador depois de index");
            return NULL;
        }
        Node *value = parser_parse_expression(p);
        parser_optional_terminator(p);
        return ast_assign(op_str, target, value, first_line, first_col);
    }

    /* Caso 4: var decl com tipo — nome: tipo = valor  OU  nome: tipo */
    if (parser_match(p, TOK_COLON)) {
        Token tipo = parser_expect(p, TOK_IDENT, "esperava tipo");
        char *type_name = tipo.lexeme ? strdup(tipo.lexeme) : NULL;
        char *var_name = first_name;

        Node *value = NULL;
        if (parser_match(p, TOK_ASSIGN)) {
            value = parser_parse_expression(p);
        }
        parser_optional_terminator(p);
        return ast_var_decl(type_name, var_name, value, first_line, first_col);
    }

    /* Caso 5: var decl sem tipo — nome = valor */
    if (parser_check(p, TOK_ASSIGN)) {
        parser_advance(p);
        Node *value = parser_parse_expression(p);

        /* === DETECCAO DE IF EM v3.0 ===
         * Depois da expressao, se o proximo token eh >>, entao era um if!
         * Ex: `idade >= 18 >> { }`
         * O `idade >= 18` foi lido como expressao, e agora temos `>>`.
         */
        if (parser_check(p, TOK_SHR) || parser_check(p, TOK_GT)) {
            /* Consome o >> (ou >) */
            if (parser_check(p, TOK_SHR)) parser_advance(p);
            else parser_advance(p);

            Node *cond = value;  /* a expressao lida eh a condicao */
            Node *then_b = parser_parse_block(p);

            Node *else_b = NULL;
            if (parser_check(p, TOK_SHL) || parser_check(p, TOK_LT)) {
                if (parser_check(p, TOK_SHL)) parser_advance(p);
                else parser_advance(p);

                /* `<< expr >> { }` ou `<< { }` */
                if (parser_check(p, TOK_LBRACE)) {
                    else_b = parser_parse_block(p);
                } else {
                    /* else if: `<< cond >> { }` — a condicao vem primeiro */int saved_elseif = parser_no_shift;
                    parser_no_shift = 1;
                    Node *else_cond = parser_parse_expression(p);
                    parser_no_shift = saved_elseif;if (parser_check(p, TOK_SHR) || parser_check(p, TOK_GT)) {
                        if (parser_check(p, TOK_SHR)) parser_advance(p);
                        else parser_advance(p);
                        Node *else_then = parser_parse_block(p);

                        Node *else_if = ast_if(else_cond, else_then, NULL,
                                               else_cond->line, else_cond->column);
                        else_b = ast_block(else_if->line, else_if->column);
                        nodelist_push(&((NodeBlock *)else_b)->statements, else_if);
                    }
                }
            }

            return ast_if(cond, then_b, else_b, first_line, first_col);
        }

        parser_optional_terminator(p);
        return ast_var_decl(NULL, first_name, value, first_line, first_col);
    }

    /* Caso 6: outros operadores (+=, etc) */
    if (parser_check(p, TOK_PLUS_EQ) || parser_check(p, TOK_MINUS_EQ) ||
        parser_check(p, TOK_STAR_EQ) || parser_check(p, TOK_SLASH_EQ) ||
        parser_check(p, TOK_PERCENT_EQ)) {
        const char *op_str = "=";
        switch (p->current.type) {
            case TOK_PLUS_EQ:    op_str = "+="; break;
            case TOK_MINUS_EQ:   op_str = "-="; break;
            case TOK_STAR_EQ:    op_str = "*="; break;
            case TOK_SLASH_EQ:   op_str = "/="; break;
            case TOK_PERCENT_EQ: op_str = "%="; break;
            default: break;
        }
        parser_advance(p);
        Node *target = ast_ident(first_name, first_line, first_col);
        Node *value  = parser_parse_expression(p);
        parser_optional_terminator(p);
        return ast_assign(op_str, target, value, first_line, first_col);
    }

    parser_error(p, "esperava '=', ':', '(', '.', '[' ou operador de atribuicao");
    return NULL;
}

Node *parser_parse_if(Parser *p) {
    /* v3.0: `cond >> { ... } << { ... }` ou `cond >> { ... }` */
    Token start = p->current;
    int line = start.line;
    int col  = start.column;

    /* Ja consumimos a condicao? Nao — a condicao vem antes.
     * Mas o dispatcher chamou quando viu TOK_SHR ou TOK_GT.
     * Precisamos voltar. Hmm.
     *
     * Solucao: parser_parse_if espera que a condicao JA tenha sido
     * lida. Mas o parser_parse_statement chama parser_parse_if
     * quando ve TOK_SHR/TOK_GT — o que significa que a condicao
     * ja foi consumida.
     *
     * Vou refazer: o dispatcher NAO deve chamar parser_parse_if
     * baseado em TOK_SHR. Deve haver um caso especial.
     *
     * Simplificacao: parser_parse_if so lida com o BLOCO. A
     * condicao eh passada como argumento implicito.
     */

    parser_error(p, "parser_parse_if precisa ser reescrito (ver comentario)");
    return NULL;
}

Node *parser_parse_while(Parser *p) {
    Token kw = parser_expect(p, TOK_ENQ, "esperava 'ENQ'");
    Node *cond = parser_parse_expression(p);
    Node *body = parser_parse_block(p);
    return ast_while(cond, body, kw.line, kw.column);
}

Node *parser_parse_for(Parser *p) {
    /* v3.0: $> i in a..b { }  (range)  OU  $>| x in lista { }  (foreach) */

    int is_each = 0;
    Token kw;

    if (parser_check(p, TOK_FOR_EACH)) {
        kw = parser_expect(p, TOK_FOR_EACH, "esperava '$>|'");
        is_each = 1;
    } else {
        kw = parser_expect(p, TOK_FOR, "esperava '$>'");
    }

    Token var = parser_expect(p, TOK_IDENT, "esperava nome de variavel no for");
    char *var_name = var.lexeme ? strdup(var.lexeme) : strdup("?");
    int   for_line = kw.line;
    int   for_col  = kw.column;

    parser_expect(p, TOK_IN, "esperava 'in' no for");

    if (is_each) {
        /* For each: $>| x in expr { } */
        Node *iter = parser_parse_expression(p);
        Node *body = parser_parse_block(p);
        Node *result = ast_for_each(var_name, iter, body, for_line, for_col);
        free(var_name);
        return result;
    }

    /* For range: $> i in a..b { } */
    Node *first = parser_parse_expression(p);
    parser_expect(p, TOK_RANGE, "esperava '..' no for");
    Node *end = parser_parse_expression(p);
    Node *body = parser_parse_block(p);
    Node *result = ast_for(var_name, first, end, body, for_line, for_col);
    free(var_name);
    return result;
}

Node *parser_parse_return(Parser *p) {
    Token kw = parser_expect(p, TOK_GWB, "esperava 'GWB'");

    Node *expr = NULL;
    if (!parser_check(p, TOK_TERMINATOR) &&
        !parser_check(p, TOK_RBRACE)) {
        expr = parser_parse_expression(p);
    }
    parser_optional_terminator(p);

    return ast_return(expr, kw.line, kw.column);
}

Node *parser_parse_function(Parser *p) {
    /* v3.0: fn+ nome(args) { } */
    Token fn_token = parser_expect(p, TOK_IDENT, "esperava 'fn'");
    if (!fn_token.lexeme || strcmp(fn_token.lexeme, "fn") != 0) {
        parser_error(p, "esperava 'fn'");
        return NULL;
    }
    parser_expect(p, TOK_PLUS, "esperava '+' apos fn");
    int fn_line = fn_token.line;
    int fn_col  = fn_token.column;

    Token name = parser_expect(p, TOK_IDENT, "esperava nome da funcao");
    char *fname = name.lexeme ? strdup(name.lexeme) : strdup("?");

    parser_expect(p, TOK_LPAREN, "esperava '(' apos nome da funcao");

    Node *fn = ast_func_decl(fname, fn_line, fn_col);
    free(fname);
    NodeFuncDecl *f = (NodeFuncDecl *)fn;

    /* Parametros */
    if (!parser_check(p, TOK_RPAREN)) {
        do {
            Token param = parser_expect(p, TOK_IDENT, "esperava nome de parametro");
            char *pname = param.lexeme ? strdup(param.lexeme) : strdup("?");
            int   pline = param.line;
            int   pcol  = param.column;

            /* v3.0: aceita `nome: tipo` (tipo declarado) */
            char *ptype = NULL;
            if (parser_match(p, TOK_COLON)) {
                Token tipo = parser_expect(p, TOK_IDENT, "esperava tipo do parametro");
                ptype = tipo.lexeme ? strdup(tipo.lexeme) : NULL;
            }

            /* Cria no como ident (tipo nao usado ainda) */
            Node *param_node = ast_ident(pname, pline, pcol);
            (void)ptype;
            nodelist_push(&f->params, param_node);
            free(pname);
            if (ptype) free(ptype);
        } while (parser_match(p, TOK_COMMA));
    }
    parser_expect(p, TOK_RPAREN, "esperava ')' apos parametros");

    /* v3.0: retorno curto com #> ou bloco normal */
    if (parser_check(p, TOK_RETURN_SHORT)) {
        parser_advance(p);  /* consome #> */
        Node *expr = parser_parse_expression(p);
        Node *body = ast_block(fn_line, fn_col);
        nodelist_push(&((NodeBlock *)body)->statements,
                      ast_return(expr, fn_line, fn_col));
        f->body = body;
        parser_optional_terminator(p);
        return fn;
    }

    f->body = parser_parse_block(p);
    return fn;
}

Node *parser_parse_class(Parser *p) {
    Token kw = parser_expect(p, TOK_BASE, "esperava 'base'");
    Token name = parser_expect(p, TOK_IDENT, "esperava nome da classe");
    char *cls_name = name.lexeme ? strdup(name.lexeme) : strdup("?");
    int   cls_line = kw.line;
    int   cls_col  = kw.column;

    char *parent = NULL;
    if (parser_match(p, TOK_EXTENDS)) {
        Token pname = parser_expect(p, TOK_IDENT, "esperava nome da classe pai");
        parent = pname.lexeme ? strdup(pname.lexeme) : NULL;
    }

    Node *cls = ast_class_decl(cls_name, parent, cls_line, cls_col);
    free(cls_name);
    free(parent);
    NodeClassDecl *c = (NodeClassDecl *)cls;

    parser_expect(p, TOK_LBRACE, "esperava '{' apos classe");

    while (!parser_check(p, TOK_RBRACE) && !parser_check(p, TOK_EOF)) {
        /* v3.0: metodo fn+ nome(args) { } */
        if (parser_check(p, TOK_IDENT) && p->current.lexeme &&
            strcmp(p->current.lexeme, "fn") == 0) {
            Node *method = parser_parse_function(p);
            nodelist_push(&c->methods, method);
        }
        /* Campo com tipo: `nome: tipo` */
        else if (parser_check(p, TOK_IDENT)) {
            char *first = p->current.lexeme ? strdup(p->current.lexeme) : strdup("?");
            int   fline = p->current.line;
            int   fcol  = p->current.column;
            parser_advance(p);

            char *field_name = NULL;
            char *field_type = NULL;

            if (parser_match(p, TOK_COLON)) {
                Token tipo = parser_expect(p, TOK_IDENT, "esperava tipo do campo");
                field_name = first;
                field_type = tipo.lexeme ? strdup(tipo.lexeme) : NULL;
            } else if (parser_check(p, TOK_IDENT)) {
                /* Estilo antigo: tipo nome */
                field_type = first;
                Token second = p->current;
                field_name = second.lexeme ? strdup(second.lexeme) : strdup("?");
                parser_advance(p);
            } else {
                field_name = first;
            }

            parser_optional_terminator(p);

            Node *field_decl = ast_var_decl(field_type, field_name, NULL, fline, fcol);
            nodelist_push(&c->fields, field_decl);

            if (field_name) free(field_name);
            if (field_type) free(field_type);
        }
        else {
            break;
        }
    }

    parser_expect(p, TOK_RBRACE, "esperava '}' apos classe");
    parser_optional_terminator(p);
    return cls;
}

Node *parser_parse_import(Parser *p) {
    Token kw = parser_expect(p, TOK_JSC, "esperava 'jsc'");
    Token module = parser_expect(p, TOK_IDENT, "esperava nome do módulo");
    char *mod_name = module.lexeme ? strdup(module.lexeme) : strdup("?");
    int   imp_line = kw.line;
    int   imp_col  = kw.column;

    char *item = NULL;
    if (parser_check(p, TOK_IDENT)) {
        Token item_tok = p->current;
        item = item_tok.lexeme ? strdup(item_tok.lexeme) : NULL;
        parser_advance(p);
    }

    parser_expect(p, TOK_TERMINATOR, "esperava '$+' no fim do jsc");

    Node *result = ast_import(mod_name, item, imp_line, imp_col);
    free(mod_name);
    free(item);
    return result;
}

/* ============================================================
 * this.campo = valor
 * ============================================================ */

Node *parser_parse_this_statement(Parser *p) {
    Token kw = parser_expect(p, TOK_THIS, "esperava 'this'");
    int line = kw.line;
    int col  = kw.column;

    parser_expect(p, TOK_DOT, "esperava '.' apos 'this'");

    Token field = parser_expect(p, TOK_IDENT, "esperava nome de campo apos 'this.'");
    char *fname = field.lexeme ? strdup(field.lexeme) : strdup("?");

    Node *this_ident = ast_ident("this", line, col);
    Node *field_node = ast_field(this_ident, fname, line, col);
    free(fname);

    /* v3.0: this.campo >> { } (if) */
    if (parser_check(p, TOK_SHR) || parser_check(p, TOK_GT)) {
        parser_advance(p);
        Node *then_b = parser_parse_block(p);
        Node *else_b = NULL;
        if (parser_check(p, TOK_SHL) || parser_check(p, TOK_LT)) {
            parser_advance(p);
            if (parser_check(p, TOK_LBRACE)) {
                else_b = parser_parse_block(p);
            } else {
                Node *else_if = parser_parse_statement(p);
                else_b = ast_block(else_if ? else_if->line : line,
                                   else_if ? else_if->column : col);
                if (else_if) {
                    nodelist_push(&((NodeBlock *)else_b)->statements, else_if);
                }
            }
        }
        return ast_if(field_node, then_b, else_b, line, col);
    }

    /* this.metodo(args) */
    if (parser_check(p, TOK_LPAREN)) {
        parser_advance(p);
        Node *call = ast_call(field_node, line, col);
        NodeCall *cn = (NodeCall *)call;

        if (!parser_check(p, TOK_RPAREN)) {
            do {
                Node *arg = parser_parse_expression(p);
                nodelist_push(&cn->args, arg);
            } while (parser_match(p, TOK_COMMA));
        }
        parser_expect(p, TOK_RPAREN, "esperava ')' apos argumentos");
        parser_optional_terminator(p);

        Node *stmt = ast_block(line, col);
        nodelist_push(&((NodeBlock *)stmt)->statements, call);
        return stmt;
    }

    /* this.campo = valor */
    const char *op_str = "=";
    if (parser_match(p, TOK_ASSIGN)) op_str = "=";
    else if (parser_match(p, TOK_PLUS_EQ)) op_str = "+=";
    else if (parser_match(p, TOK_MINUS_EQ)) op_str = "-=";
    else if (parser_match(p, TOK_STAR_EQ)) op_str = "*=";
    else if (parser_match(p, TOK_SLASH_EQ)) op_str = "/=";
    else if (parser_match(p, TOK_PERCENT_EQ)) op_str = "%=";
    else {
        parser_error(p, "esperava '=', '>>', '(' ou operador apos 'this.campo'");
        return NULL;
    }

    Node *value = parser_parse_expression(p);
    parser_optional_terminator(p);

    return ast_assign(op_str, field_node, value, line, col);
}

/* ============================================================
 * try { } catch e { } finally { }
 * ============================================================ */

Node *parser_parse_try(Parser *p) {
    Token kw = parser_expect(p, TOK_TRY_OP, "esperava '!!'");
    int line = kw.line;
    int col  = kw.column;

    Node *try_block = parser_parse_block(p);

    char *catch_var = NULL;
    Node *catch_block = NULL;

    if (parser_match(p, TOK_CATCH_OP)) {
        Token var = parser_expect(p, TOK_IDENT, "esperava nome de variavel no ??");
        catch_var = var.lexeme ? strdup(var.lexeme) : strdup("e");
        catch_block = parser_parse_block(p);
    }

    Node *finally_block = NULL;
    if (parser_match(p, TOK_FINALLY_OP)) {
        finally_block = parser_parse_block(p);
    }

    return ast_try(try_block, catch_var, catch_block, finally_block, line, col);
}


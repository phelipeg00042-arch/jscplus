#include "transpiler.h"
#include <stdarg.h>
#include <string.h>

/* ============================================================
 * JSC$+ Transpilador - implementacao
 * ============================================================ */

Transpiler *transpiler_new(void) {
    Transpiler *t = (Transpiler *)calloc(1, sizeof(Transpiler));
    t->cap = 4096;
    t->buf = (char *)malloc(t->cap);
    t->buf[0] = 0;
    t->len = 0;
    t->indent = 0;
    t->tmp_id = 0;
    t->strings = NULL;
    t->n_strings = 0;
    t->n_params = 0;
    t->n_classes = 0;
    nodelist_init(&t->declared_vars);
    return t;
}

void transpiler_free(Transpiler *t) {
    if (!t) return;
    free(t->buf);
    for (int i = 0; i < t->n_strings; i++) free(t->strings[i]);
    free(t->strings);
    free(t);
}

/* ---- Buffer helpers ---- */

void tp_append(Transpiler *t, const char *s) {
    size_t slen = strlen(s);
    while (t->len + slen + 1 > t->cap) {
        t->cap *= 2;
        t->buf = (char *)realloc(t->buf, t->cap);
    }
    memcpy(t->buf + t->len, s, slen);
    t->len += slen;
    t->buf[t->len] = 0;
}

void tp_appendf(Transpiler *t, const char *fmt, ...) {
    char tmp[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    tp_append(t, tmp);
}

void tp_indent(Transpiler *t) {
    for (int i = 0; i < t->indent; i++) tp_append(t, "    ");
}

void tp_newline(Transpiler *t) {
    tp_append(t, "\n");
}

/* ---- Escape de string ---- */

static void tp_escape_string(Transpiler *t, const char *s) {
    tp_append(t, "\"");
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  tp_append(t, "\\\""); break;
            case '\\': tp_append(t, "\\\\"); break;
            case '\n': tp_append(t, "\\n"); break;
            case '\t': tp_append(t, "\\t"); break;
            case '\r': tp_append(t, "\\r"); break;
            default: {
                char tmp[2] = { *p, 0 };
                tp_append(t, tmp);
            }
        }
    }
    tp_append(t, "\"");
}

/* ---- Transpile EXPRESSAO ---- */

void transpile_expression(Transpiler *t, Node *node) {
    if (!node) {
        tp_append(t, "jsc_vazio()");
        return;
    }
    
    switch (node->type) {
        case NODE_NUMBER: {
            NodeNumber *n = (NodeNumber *)node;
            double v = n->value;
            if (v == (long long)v) {
                tp_appendf(t, "jsc_int(%lld)", (long long)v);
            } else {
                tp_appendf(t, "jsc_float(%g)", v);
            }
            break;
        }
        
        case NODE_STRING: {
            NodeString *s = (NodeString *)node;
            tp_append(t, "jsc_string(");
            tp_escape_string(t, s->value);
            tp_append(t, ")");
            break;
        }
        
        case NODE_BOOL: {
            NodeBool *b = (NodeBool *)node;
            tp_appendf(t, "jsc_bool(%d)", b->value);
            break;
        }
        
        case NODE_VAZIO: {
            tp_append(t, "jsc_vazio()");
            break;
        }
        
        case NODE_IDENT: {
            NodeIdent *id = (NodeIdent *)node;
            /* "this" → variavel local C "this" */
            if (strcmp(id->name, "this") == 0) {
                tp_append(t, "this");
            }
            /* "true" / "false" / "vazio" → literais */
            else if (strcmp(id->name, "true") == 0) {
                tp_append(t, "jsc_bool(1)");
            }
            else if (strcmp(id->name, "false") == 0) {
                tp_append(t, "jsc_bool(0)");
            }
            else if (strcmp(id->name, "vazio") == 0) {
                tp_append(t, "jsc_vazio()");
            }
            /* Checa se e' parametro ativo */
            else {
                int eh_param = 0;
                for (int pi = 0; pi < t->n_params; pi++) {
                    if (t->params[pi] && strcmp(t->params[pi], id->name) == 0) {
                        /* Em METODO: parametro e' "X" direto */
                        /* Em FUNCAO: parametro e' "p_X" */
                        if (t->em_metodo) {
                            tp_appendf(t, "%s", id->name);
                        } else {
                            tp_appendf(t, "p_%s", id->name);
                        }
                        eh_param = 1;
                        break;
                    }
                }
                /* Senao, e' variavel global */
                if (!eh_param) {
                    tp_appendf(t, "v_%s", id->name);
                }
            }
            break;
        }
        
        case NODE_BINARY: {
            NodeBinary *b = (NodeBinary *)node;
            const char *fn = "jsc_add";
            
            if (b->op_code == OP_SUB) fn = "jsc_sub";
            else if (b->op_code == OP_MUL) fn = "jsc_mul";
            else if (b->op_code == OP_DIV) fn = "jsc_div";
            else if (b->op_code == OP_MOD) fn = "jsc_mod";
            else if (b->op_code == OP_EQ)  fn = "jsc_eq";
            else if (b->op_code == OP_NEQ) fn = "jsc_neq";
            else if (b->op_code == OP_LT)  fn = "jsc_lt";
            else if (b->op_code == OP_GT)  fn = "jsc_gt";
            else if (b->op_code == OP_LTE) fn = "jsc_lte";
            else if (b->op_code == OP_GTE) fn = "jsc_gte";
            else if (b->op_code == OP_AND) fn = "jsc_and";
            else if (b->op_code == OP_OR)  fn = "jsc_or";
            
            tp_appendf(t, "%s(", fn);
            transpile_expression(t, b->left);
            tp_append(t, ", ");
            transpile_expression(t, b->right);
            tp_append(t, ")");
            break;
        }
        
        case NODE_UNARY: {
            NodeUnary *u = (NodeUnary *)node;
            const char *fn = "jsc_neg";
            if (u->op_code == OP_NOT) fn = "jsc_not";
            
            tp_appendf(t, "%s(", fn);
            transpile_expression(t, u->operand);
            tp_append(t, ")");
            break;
        }
        
        case NODE_CALL: {
            NodeCall *call = (NodeCall *)node;
            
            /* Caso 1: obj.metodo(args) → jsc_method_call(obj, "metodo", argc, ...) */
            if (call->callee && call->callee->type == NODE_FIELD) {
                NodeField *f = (NodeField *)call->callee;
                
                /* Detecta join de thread */
                if (strcmp(f->field, "join") == 0) {
                    tp_append(t, "jsc_thread_join(");
                    transpile_expression(t, f->object);
                    tp_append(t, ")");
                    break;
                }
                
                /* Detecta se obj é modulo nativo (math, time, etc) */
                int eh_modulo = 0;
                if (f->object && f->object->type == NODE_IDENT) {
                    NodeIdent *id = (NodeIdent *)f->object;
                    const char *mods[] = {"math", "time", "string", "crypto",
                                          "fs", "os", "proc", "net", "json",
                                          "hack", NULL};
                    for (int i = 0; mods[i]; i++) {
                        if (strcmp(id->name, mods[i]) == 0) {
                            eh_modulo = 1;
                            break;
                        }
                    }
                }
                
                if (eh_modulo) {
                    /* Chama funcao do modulo: jsc_map_get + jsc_call_native */
                    tp_append(t, "jsc_call_native(jsc_map_get(");
                    transpile_expression(t, f->object);
                    tp_appendf(t, ", \"%s\"), ", f->field);
                    
                    /* Cria array de args temporario */
                    tp_append(t, "(JscValue*[]){");
                    for (size_t i = 0; i < call->args.count; i++) {
                        if (i > 0) tp_append(t, ", ");
                        transpile_expression(t, call->args.items[i]);
                    }
                    if (call->args.count == 0) tp_append(t, "NULL");
                    tp_appendf(t, "}, %zu)", call->args.count);
                } else {
                    /* Metodo normal de objeto */
                    tp_append(t, "jsc_method_call(");
                    transpile_expression(t, f->object);
                    tp_appendf(t, ", \"%s\", %zu", f->field, call->args.count);
                    for (size_t i = 0; i < call->args.count; i++) {
                        tp_append(t, ", ");
                        transpile_expression(t, call->args.items[i]);
                    }
                    tp_append(t, ")");
                }
                break;
            }
            
            /* Caso 2: chamada normal */
            const char *nome = "?";
            if (call->callee && call->callee->type == NODE_IDENT) {
                NodeIdent *id = (NodeIdent *)call->callee;
                nome = id->name;
            }
            
            /* Checa se e' classe */
            int eh_classe = 0;
            for (int ci = 0; ci < t->n_classes; ci++) {
                if (t->classes[ci] && strcmp(t->classes[ci], nome) == 0) {
                    eh_classe = 1;
                    break;
                }
            }
            
            if (eh_classe) {
                /* Chamada de classe: cls_Nome_new(args) */
                tp_appendf(t, "cls_%s_new(", nome);
                for (size_t i = 0; i < call->args.count; i++) {
                    if (i > 0) tp_append(t, ", ");
                    transpile_expression(t, call->args.items[i]);
                }
                tp_append(t, ")");
                break;
            }
            
            /* spawn(fn, arg) → thread com wrapper */
            if (strcmp(nome, "spawn") == 0) {
                /* Encontra o indice do wrapper */
                if (call->args.count >= 1 && call->args.items[0]->type == NODE_IDENT) {
                    NodeIdent *fn_id = (NodeIdent *)call->args.items[0];
                    /* Procura wrapper correspondente */
                    int idx = -1;
                    for (size_t i = 0; i < t->prog_statements.count; i++) {
                        Node *s = t->prog_statements.items[i];
                        if (s->type != NODE_VAR_DECL && s->type != NODE_ASSIGN) continue;
                        Node *val = (s->type == NODE_VAR_DECL)
                            ? ((NodeVarDecl *)s)->value
                            : ((NodeAssign *)s)->value;
                        if (!val || val->type != NODE_CALL) continue;
                        NodeCall *c2 = (NodeCall *)val;
                        if (!c2->callee || c2->callee->type != NODE_IDENT) continue;
                        if (strcmp(((NodeIdent *)c2->callee)->name, "spawn") != 0) continue;
                        if (c2->args.count < 1) continue;
                        Node *f2 = c2->args.items[0];
                        if (f2->type != NODE_IDENT) continue;
                        if (strcmp(((NodeIdent *)f2)->name, fn_id->name) == 0) {
                            idx++;
                            break;
                        }
                        idx++;
                    }
                    if (idx >= 0) {
                        tp_appendf(t, "jsc_thread_new(spawn_wrap_%d, ", idx);
                        if (call->args.count >= 2) {
                            transpile_expression(t, call->args.items[1]);
                        } else {
                            tp_append(t, "NULL");
                        }
                        tp_append(t, ")");
                    } else {
                        tp_append(t, "jsc_vazio() /* spawn sem wrapper */");
                    }
                } else {
                    tp_append(t, "jsc_vazio() /* spawn invalido */");
                }
            }
            /* import_c(path) → jsc_import_c("path") */
            else if (strcmp(nome, "import_c") == 0) {
                tp_append(t, "jsc_import_c(");
                if (call->args.count > 0) {
                    Node *a = call->args.items[0];
                    if (a->type == NODE_STRING) {
                        NodeString *ns = (NodeString *)a;
                        tp_appendf(t, "\"%s\"", ns->value);
                    } else {
                        tp_append(t, "\"\"");
                    }
                }
                tp_append(t, ")");
            }
            /* call_c(lib, "nome", "tipo", args...) → jsc_call_c(lib, "nome", "tipo", argc, args...) */
            else if (strcmp(nome, "call_c") == 0) {
                tp_append(t, "jsc_call_c(");
                /* Arg 0: lib */
                if (call->args.count >= 1) {
                    transpile_expression(t, call->args.items[0]);
                }
                /* Arg 1: nome (string C) */
                if (call->args.count >= 2) {
                    tp_append(t, ", ");
                    Node *a = call->args.items[1];
                    if (a->type == NODE_STRING) {
                        NodeString *ns = (NodeString *)a;
                        tp_appendf(t, "\"%s\"", ns->value);
                    } else {
                        tp_append(t, "\"\"");
                    }
                }
                /* Arg 2: tipo (string C) */
                if (call->args.count >= 3) {
                    tp_append(t, ", ");
                    Node *a = call->args.items[2];
                    if (a->type == NODE_STRING) {
                        NodeString *ns = (NodeString *)a;
                        tp_appendf(t, "\"%s\"", ns->value);
                    } else {
                        tp_append(t, "\"\"");
                    }
                }
                /* Arg 3: argc */
                tp_appendf(t, ", %d", (int)call->args.count - 3);
                /* Args extras */
                for (size_t i = 3; i < call->args.count; i++) {
                    tp_append(t, ", ");
                    transpile_expression(t, call->args.items[i]);
                }
                tp_append(t, ")");
            }
            /* printj(expr) → jsc_print(expr) */
            else if (strcmp(nome, "printj") == 0) {
                tp_append(t, "jsc_print(");
                if (call->args.count > 0) {
                    transpile_expression(t, call->args.items[0]);
                } else {
                    tp_append(t, "jsc_string(\"\")");
                }
                tp_append(t, ")");
            } else {
                /* Funcoes user-defined: fn_nome(args) */
                tp_appendf(t, "fn_%s(", nome);
                for (size_t i = 0; i < call->args.count; i++) {
                    if (i > 0) tp_append(t, ", ");
                    transpile_expression(t, call->args.items[i]);
                }
                tp_append(t, ")");
            }
            break;
        }
        
        case NODE_ARRAY: {
            NodeArray *a = (NodeArray *)node;
            tp_appendf(t, "jsc_array_from(%zu", a->items.count);
            for (size_t i = 0; i < a->items.count; i++) {
                tp_append(t, ", ");
                transpile_expression(t, a->items.items[i]);
            }
            tp_append(t, ")");
            break;
        }
        
        case NODE_MAP: {
            NodeMap *m = (NodeMap *)node;
            tp_appendf(t, "jsc_map_from(%zu", m->keys.count);
            for (size_t i = 0; i < m->keys.count; i++) {
                Node *k = m->keys.items[i];
                Node *v = m->values.items[i];
                tp_append(t, ", ");
                if (k && k->type == NODE_STRING) {
                    NodeString *ks = (NodeString *)k;
                    tp_appendf(t, "\"%s\"", ks->value);
                } else {
                    tp_append(t, "\"\"");
                }
                tp_append(t, ", ");
                transpile_expression(t, v);
            }
            tp_append(t, ")");
            break;
        }
        
        case NODE_INDEX: {
            NodeIndex *n = (NodeIndex *)node;
            tp_append(t, "jsc_index(");
            transpile_expression(t, n->object);
            tp_append(t, ", ");
            transpile_expression(t, n->index);
            tp_append(t, ")");
            break;
        }
        
        case NODE_FIELD: {
            NodeField *n = (NodeField *)node;
            /* obj.campo → jsc_map_get(obj, "campo") */
            tp_append(t, "jsc_map_get(");
            transpile_expression(t, n->object);
            tp_appendf(t, ", \"%s\")", n->field);
            break;
        }
        
        default:
            tp_append(t, "jsc_vazio() /* TODO */");
            break;
    }
}

/* ---- Transpile STATEMENT ---- */

void transpile_block(Transpiler *t, Node *block) {
    if (!block) return;
    if (block->type == NODE_BLOCK) {
        NodeBlock *b = (NodeBlock *)block;
        for (size_t i = 0; i < b->statements.count; i++) {
            transpile_statement(t, b->statements.items[i]);
        }
    } else {
        transpile_statement(t, block);
    }
}

void transpile_statement(Transpiler *t, Node *node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_PRINTJ: {
            NodePrintj *p = (NodePrintj *)node;
            tp_indent(t);
            tp_append(t, "jsc_print(");
            transpile_expression(t, p->expr);
            tp_append(t, ");\n");
            break;
        }
        case NODE_VAR_DECL: {
            NodeVarDecl *vd = (NodeVarDecl *)node;
            tp_indent(t);
            tp_appendf(t, "v_%s = ", vd->var_name);
            transpile_expression(t, vd->value);
            tp_append(t, ";\n");
            break;
        }
        case NODE_ASSIGN: {
            NodeAssign *a = (NodeAssign *)node;
            tp_indent(t);
            /* this.campo = valor → jsc_map_set(this, "campo", valor) */
            if (a->target && a->target->type == NODE_FIELD) {
                NodeField *f = (NodeField *)a->target;
                tp_append(t, "jsc_map_set(");
                transpile_expression(t, f->object);
                tp_appendf(t, ", \"%s\", ", f->field);
                transpile_expression(t, a->value);
                tp_append(t, ");\n");
            }
            /* x = valor → v_x = valor */
            else if (a->target && a->target->type == NODE_IDENT) {
                NodeIdent *id = (NodeIdent *)a->target;
                tp_appendf(t, "v_%s = ", id->name);
                transpile_expression(t, a->value);
                tp_append(t, ";\n");
            }
            break;
        }
        
        case NODE_CALL: {
            /* Chamada de funcao/metodo como statement */
            tp_indent(t);
            transpile_expression(t, node);
            tp_append(t, ";\n");
            break;
        }
        
        case NODE_IMPORT: {
            /* jsc math$+ → v_math = jsc_import_module("math"); */
            NodeImport *imp = (NodeImport *)node;
            tp_indent(t);
            tp_appendf(t, "v_%s = jsc_import_module(\"%s\");\n",
                       imp->module, imp->module);
            break;
        }
        case NODE_IF: {
            NodeIf *n = (NodeIf *)node;
            tp_indent(t);
            tp_append(t, "if (jsc_truthy(");
            transpile_expression(t, n->cond);
            tp_append(t, ")) {\n");
            t->indent++;
            transpile_block(t, n->then_block);
            t->indent--;
            tp_indent(t);
            tp_append(t, "}");
            if (n->else_block) {
                tp_append(t, " else {\n");
                t->indent++;
                transpile_block(t, n->else_block);
                t->indent--;
                tp_indent(t);
                tp_append(t, "}");
            }
            tp_append(t, "\n");
            break;
        }
        case NODE_WHILE: {
            NodeWhile *n = (NodeWhile *)node;
            tp_indent(t);
            tp_append(t, "while (jsc_truthy(");
            transpile_expression(t, n->cond);
            tp_append(t, ")) {\n");
            t->indent++;
            transpile_block(t, n->body);
            t->indent--;
            tp_indent(t);
            tp_append(t, "}\n");
            break;
        }
        case NODE_BLOCK: {
            NodeBlock *n = (NodeBlock *)node;
            for (size_t i = 0; i < n->statements.count; i++) {
                transpile_statement(t, n->statements.items[i]);
            }
            break;
        }
        case NODE_FOR: {
            /* $> i in start..end { body } */
            NodeFor *n = (NodeFor *)node;
            tp_indent(t);
            tp_appendf(t, "for (long long _for_%s = jsc_to_int(", n->var_name);
            transpile_expression(t, n->start);
            tp_appendf(t, "); _for_%s <= jsc_to_int(", n->var_name);
            transpile_expression(t, n->end);
            tp_appendf(t, "); _for_%s++) {\n", n->var_name);
            t->indent++;
            tp_indent(t);
            tp_appendf(t, "v_%s = jsc_int(_for_%s);\n", n->var_name, n->var_name);
            transpile_block(t, n->body);
            t->indent--;
            tp_indent(t);
            tp_append(t, "}\n");
            break;
        }
        
        case NODE_FOR_EACH: {
            /* $>| x in arr { body } */
            NodeForEach *n = (NodeForEach *)node;
            tp_indent(t);
            tp_append(t, "{ /* for each */\n");
            t->indent++;
            tp_indent(t);
            tp_append(t, "JscValue *_arr = ");
            transpile_expression(t, n->iterable);
            tp_append(t, ";\n");
            tp_indent(t);
            tp_append(t, "long long _len = jsc_method_call(_arr, \"len\", 0)->as.i;\n");
            tp_indent(t);
            tp_append(t, "for (long long _i = 0; _i < _len; _i++) {\n");
            t->indent++;
            tp_indent(t);
            tp_appendf(t, "JscValue *v_%s = jsc_index(_arr, jsc_int(_i));\n", n->var_name);
            transpile_block(t, n->body);
            t->indent--;
            tp_indent(t);
            tp_append(t, "}\n");
            t->indent--;
            tp_indent(t);
            tp_append(t, "}\n");
            break;
        }
        
        case NODE_RETURN: {
            NodeReturn *r = (NodeReturn *)node;
            tp_indent(t);
            tp_append(t, "return ");
            if (r->expr) {
                transpile_expression(t, r->expr);
            } else {
                tp_append(t, "jsc_vazio()");
            }
            tp_append(t, ";\n");
            break;
        }
        case NODE_CLASS_DECL:
            /* Classes ja foram emitidas antes do main */
            break;
        
        case NODE_BREAK:
            tp_indent(t);
            tp_append(t, "break;\n");
            break;
        
        case NODE_CONTINUE:
            tp_indent(t);
            tp_append(t, "continue;\n");
            break;
        
        default:
            tp_indent(t);
            tp_append(t, "/* TODO */\n");
            break;
    }
}




/* ---- Transpile FUNCTION DECL ---- */

void transpile_func_decl(Transpiler *t, Node *node) {
    if (!node || node->type != NODE_FUNC_DECL) return;
    NodeFuncDecl *fn = (NodeFuncDecl *)node;
    
    /* Assinatura: JscValue *fn_nome(JscValue *p1, JscValue *p2, ...) */
    tp_appendf(t, "JscValue *fn_%s(", fn->name);
    
    if (fn->params.count == 0) {
        tp_append(t, "void");
    } else {
        for (size_t i = 0; i < fn->params.count; i++) {
            Node *p = fn->params.items[i];
            const char *pname = "arg";
            if (p && p->type == NODE_IDENT) {
                pname = ((NodeIdent *)p)->name;
            }
            if (i > 0) tp_append(t, ", ");
            tp_appendf(t, "JscValue *p_%s", pname);
        }
    }
    
    tp_append(t, ") {\n");
    t->indent++;
    
    /* Registra parametros */
    int params_antigos = t->n_params;
    for (size_t i = 0; i < fn->params.count && t->n_params < 32; i++) {
        Node *p = fn->params.items[i];
        if (p && p->type == NODE_IDENT) {
            NodeIdent *id = (NodeIdent *)p;
            t->params[t->n_params++] = id->name;
        }
    }
    
    /* Corpo */
    transpile_block(t, fn->body);
    
    /* Restaura parametros */
    t->n_params = params_antigos;
    
    /* Return padrao se nao houver */
    tp_indent(t);
    tp_append(t, "return jsc_vazio();\n");
    
    t->indent--;
    tp_append(t, "}\n\n");
}


/* ---- ETAPA 1: Transpile CLASS DECL (ainda nao usada) ---- */

void transpile_class_decl(Transpiler *t, Node *node) {
    if (!node || node->type != NODE_CLASS_DECL) return;
    NodeClassDecl *cls = (NodeClassDecl *)node;
    
    /* Procura init proprio */
    NodeFuncDecl *init_fn = NULL;
    for (size_t i = 0; i < cls->methods.count; i++) {
        Node *m = cls->methods.items[i];
        if (m->type == NODE_FUNC_DECL) {
            NodeFuncDecl *fn = (NodeFuncDecl *)m;
            if (strcmp(fn->name, "init") == 0) {
                init_fn = fn;
                break;
            }
        }
    }
    
    /* Se nao tem init proprio E tem pai, procura init do pai */
    NodeFuncDecl *init_usar = init_fn;
    const char *init_dono = cls->name;  /* quem define o init */
    
    if (!init_usar && cls->parent) {
        for (size_t i = 0; i < t->prog_statements.count; i++) {
            Node *s = t->prog_statements.items[i];
            if (s->type != NODE_CLASS_DECL) continue;
            NodeClassDecl *pai = (NodeClassDecl *)s;
            if (!pai->name || strcmp(pai->name, cls->parent) != 0) continue;
            /* Achou o pai. Procura init. */
            for (size_t k = 0; k < pai->methods.count; k++) {
                Node *m = pai->methods.items[k];
                if (m->type != NODE_FUNC_DECL) continue;
                NodeFuncDecl *f2 = (NodeFuncDecl *)m;
                if (strcmp(f2->name, "init") == 0) {
                    init_usar = f2;
                    init_dono = pai->name;
                    break;
                }
            }
            break;
        }
    }
    
    /* 1) Metodos */
    for (size_t i = 0; i < cls->methods.count; i++) {
        Node *m = cls->methods.items[i];
        if (m->type != NODE_FUNC_DECL) continue;
        NodeFuncDecl *fn = (NodeFuncDecl *)m;
        
        tp_appendf(t, "JscValue *cls_%s_%s(JscValue *this", cls->name, fn->name);
        for (size_t j = 0; j < fn->params.count; j++) {
            Node *p = fn->params.items[j];
            if (p->type == NODE_IDENT) {
                NodeIdent *id = (NodeIdent *)p;
                tp_appendf(t, ", JscValue *%s", id->name);
            }
        }
        tp_append(t, ") {\n");
        t->indent++;
        
        /* Registra parametros */
        int params_antigos = t->n_params;
        for (size_t j = 0; j < fn->params.count && t->n_params < 32; j++) {
            Node *p = fn->params.items[j];
            if (p->type == NODE_IDENT) {
                NodeIdent *id = (NodeIdent *)p;
                t->params[t->n_params++] = id->name;
            }
        }
        
        t->em_metodo = 1;
        transpile_block(t, fn->body);
        t->em_metodo = 0;
        
        t->n_params = params_antigos;
        
        tp_indent(t);
        tp_append(t, "return jsc_vazio();\n");
        t->indent--;
        tp_append(t, "}\n\n");
    }
    
    /* 2) Construtor */
    tp_appendf(t, "JscValue *cls_%s_new(", cls->name);
    if (init_usar && init_usar->params.count > 0) {
        for (size_t j = 0; j < init_usar->params.count; j++) {
            Node *p = init_usar->params.items[j];
            if (j > 0) tp_append(t, ", ");
            if (p->type == NODE_IDENT) {
                NodeIdent *id = (NodeIdent *)p;
                tp_appendf(t, "JscValue *%s", id->name);
            }
        }
    } else {
        tp_append(t, "void");
    }
    tp_append(t, ") {\n");
    t->indent++;
    
    tp_indent(t);
    tp_append(t, "JscValue *this = jsc_map();\n");
    tp_indent(t);
    tp_appendf(t, "jsc_map_set(this, \"__class__\", jsc_string(\"%s\"));\n", cls->name);
    
    if (init_usar) {
        tp_indent(t);
        tp_appendf(t, "cls_%s_init(this", init_dono);
        for (size_t j = 0; j < init_usar->params.count; j++) {
            Node *p = init_usar->params.items[j];
            if (p->type == NODE_IDENT) {
                NodeIdent *id = (NodeIdent *)p;
                tp_appendf(t, ", %s", id->name);
            }
        }
        tp_append(t, ");\n");
    }
    
    tp_indent(t);
    tp_append(t, "return this;\n");
    t->indent--;
    tp_append(t, "}\n\n");
}


/* ---- Wrappers de thread ---- */

static void transpile_thread_wrappers(Transpiler *t, NodeProgram *p) {
    /* Procura chamadas spawn() e gera wrappers */
    int idx = 0;
    for (size_t i = 0; i < p->statements.count; i++) {
        Node *s = p->statements.items[i];
        /* Procura spawn em qualquer lugar — simplificado */
        /* Aqui só faz wrapper pra top-level */
        if (s->type != NODE_VAR_DECL && s->type != NODE_ASSIGN) continue;
        Node *val = (s->type == NODE_VAR_DECL)
            ? ((NodeVarDecl *)s)->value
            : ((NodeAssign *)s)->value;
        if (!val || val->type != NODE_CALL) continue;
        NodeCall *c = (NodeCall *)val;
        if (!c->callee || c->callee->type != NODE_IDENT) continue;
        NodeIdent *id = (NodeIdent *)c->callee;
        if (strcmp(id->name, "spawn") != 0) continue;
        if (c->args.count < 1) continue;
        Node *fn_node = c->args.items[0];
        if (fn_node->type != NODE_IDENT) continue;
        NodeIdent *fn_id = (NodeIdent *)fn_node;
        
        tp_appendf(t, "static JscValue *spawn_wrap_%d(void *arg) {\n", idx);
        tp_appendf(t, "    return fn_%s((JscValue *)arg);\n", fn_id->name);
        tp_append(t, "}\n\n");
        idx++;
    }
}


/* Coleta variaveis (NODE_ASSIGN com target IDENT) recursivamente */
static void collect_vars(Transpiler *t, Node *node) {
    if (!node) return;
    
    /* NODE_VAR_DECL: declara a variavel */
    if (node->type == NODE_VAR_DECL) {
        NodeVarDecl *vd = (NodeVarDecl *)node;
        if (vd->var_name) {
            int ja = 0;
            for (size_t i = 0; i < t->declared_vars.count; i++) {
                NodeIdent *d = (NodeIdent *)t->declared_vars.items[i];
                if (strcmp(d->name, vd->var_name) == 0) { ja = 1; break; }
            }
            if (!ja) {
                tp_appendf(t, "static JscValue *v_%s = NULL;\n", vd->var_name);
                NodeIdent *fake = (NodeIdent *)calloc(1, sizeof(NodeIdent));
                fake->base.type = NODE_IDENT;
                fake->name = vd->var_name;
                nodelist_push(&t->declared_vars, (Node *)fake);
                fprintf(stderr, "[COLLECT_VARDECL] declarou v_%s\n", vd->var_name);
            }
        }
        collect_vars(t, vd->value);
    }
    
    if (node->type == NODE_ASSIGN) {
        NodeAssign *a = (NodeAssign *)node;
        fprintf(stderr, "[COLLECT_ASSIGN] encontrado!\n");
        if (a->target && a->target->type == NODE_IDENT) {
            NodeIdent *id = (NodeIdent *)a->target;
            fprintf(stderr, "[COLLECT_ASSIGN] target=%s\n", id->name);
            int ja = 0;
            for (size_t i = 0; i < t->declared_vars.count; i++) {
                NodeIdent *d = (NodeIdent *)t->declared_vars.items[i];
                if (strcmp(d->name, id->name) == 0) { ja = 1; break; }
            }
            if (!ja) {
                tp_appendf(t, "static JscValue *v_%s = NULL;\n", id->name);
                NodeIdent *fake = (NodeIdent *)calloc(1, sizeof(NodeIdent));
                fake->base.type = NODE_IDENT;
                fake->name = id->name;
                nodelist_push(&t->declared_vars, (Node *)fake);
            }
        }
        collect_vars(t, a->value);
    }
    
    switch (node->type) {
        case NODE_PROGRAM: {
            NodeProgram *p = (NodeProgram *)node;
            for (size_t i = 0; i < p->statements.count; i++)
                collect_vars(t, p->statements.items[i]);
            break;
        }
        case NODE_BLOCK: {
            NodeBlock *b = (NodeBlock *)node;
            fprintf(stderr, "[COLLECT_BLOCK] block com %zu stmts\n", b->statements.count);
            for (size_t i = 0; i < b->statements.count; i++) {
                fprintf(stderr, "[COLLECT_BLOCK]   stmt[%zu] type=%d\n", i, b->statements.items[i]->type);
                collect_vars(t, b->statements.items[i]);
            }
            break;
        }
        case NODE_IF: {
            NodeIf *n = (NodeIf *)node;
            collect_vars(t, n->cond);
            collect_vars(t, n->then_block);
            collect_vars(t, n->else_block);
            break;
        }
        case NODE_WHILE: {
            NodeWhile *n = (NodeWhile *)node;
            collect_vars(t, n->cond);
            collect_vars(t, n->body);
            break;
        }
        case NODE_FOR: {
            NodeFor *n = (NodeFor *)node;
            collect_vars(t, n->start);
            collect_vars(t, n->end);
            collect_vars(t, n->body);
            break;
        }
        case NODE_FOR_EACH: {
            NodeForEach *n = (NodeForEach *)node;
            fprintf(stderr, "[DEBUG_FE] for_each encontrado! body type=%d\n",
                    n->body ? n->body->type : -1);
            collect_vars(t, n->iterable);
            collect_vars(t, n->body);
            break;
        }
        case NODE_PRINTJ: {
            NodePrintj *n = (NodePrintj *)node;
            collect_vars(t, n->expr);
            break;
        }
        case NODE_CALL: {
            NodeCall *n = (NodeCall *)node;
            for (size_t i = 0; i < n->args.count; i++)
                collect_vars(t, n->args.items[i]);
            break;
        }
        case NODE_BINARY: {
            NodeBinary *n = (NodeBinary *)node;
            collect_vars(t, n->left);
            collect_vars(t, n->right);
            break;
        }
        case NODE_UNARY: {
            NodeUnary *n = (NodeUnary *)node;
            collect_vars(t, n->operand);
            break;
        }
        case NODE_RETURN: {
            NodeReturn *n = (NodeReturn *)node;
            collect_vars(t, n->expr);
            break;
        }
        case NODE_VAR_DECL: {
            NodeVarDecl *n = (NodeVarDecl *)node;
            collect_vars(t, n->value);
            break;
        }
        case NODE_FIELD: {
            NodeField *n = (NodeField *)node;
            collect_vars(t, n->object);
            break;
        }
        case NODE_INDEX: {
            NodeIndex *n = (NodeIndex *)node;
            collect_vars(t, n->object);
            collect_vars(t, n->index);
            break;
        }
        case NODE_ARRAY: {
            NodeArray *n = (NodeArray *)node;
            for (size_t i = 0; i < n->items.count; i++)
                collect_vars(t, n->items.items[i]);
            break;
        }
        case NODE_FUNC_DECL: {
            NodeFuncDecl *n = (NodeFuncDecl *)node;
            collect_vars(t, n->body);
            break;
        }
        case NODE_CLASS_DECL: {
            NodeClassDecl *n = (NodeClassDecl *)node;
            for (size_t i = 0; i < n->methods.count; i++)
                collect_vars(t, n->methods.items[i]);
            break;
        }
    }
}

/* ---- Transpile PROGRAMA ---- */

void transpile_program(Transpiler *t, Node *prog) {
    if (!prog || prog->type != NODE_PROGRAM) return;
    
    NodeProgram *dbg_p = (NodeProgram *)prog;
    fprintf(stderr, "[DEBUG] transpile_program: %zu statements\n", dbg_p->statements.count);
    for (size_t dbg_i = 0; dbg_i < dbg_p->statements.count; dbg_i++) {
        Node *dbg_s = dbg_p->statements.items[dbg_i];
        fprintf(stderr, "[DEBUG]   stmt %zu: type=%d", dbg_i, dbg_s->type);
        if (dbg_s->type == NODE_FUNC_DECL) {
            fprintf(stderr, " (FUNC_DECL)");
        } else if (dbg_s->type == NODE_CLASS_DECL) {
            fprintf(stderr, " (CLASS_DECL)");
        } else if (dbg_s->type == NODE_VAR_DECL) {
            fprintf(stderr, " (VAR_DECL)");
        } else if (dbg_s->type == NODE_ASSIGN) {
            fprintf(stderr, " (ASSIGN)");
        }
        fprintf(stderr, "\n");
    }
    
    
    NodeProgram *p = (NodeProgram *)prog;
    t->prog_statements = p->statements;
    
    /* Header */
    tp_append(t, "#define _POSIX_C_SOURCE 200809L\n");
    tp_append(t, "#include \"jsc_runtime.h\"\n");
    tp_append(t, "\n");
    
    /* Declara variaveis globais (todas as atribuicoes) */
    tp_append(t, "/* Variaveis globais */\n");
    for (size_t i = 0; i < p->statements.count; i++) {
        Node *stmt = p->statements.items[i];
        if (stmt->type == NODE_VAR_DECL) {
            NodeVarDecl *vd = (NodeVarDecl *)stmt;
            tp_appendf(t, "static JscValue *v_%s = NULL;\n", vd->var_name);
            /* Marca como declarada pra collect_vars nao duplicar */
            NodeIdent *fake = (NodeIdent *)calloc(1, sizeof(NodeIdent));
            fake->base.type = NODE_IDENT;
            fake->name = vd->var_name;
            nodelist_push(&t->declared_vars, (Node *)fake);
        } else if (stmt->type == NODE_ASSIGN) {
            NodeAssign *a = (NodeAssign *)stmt;
            if (a->target && a->target->type == NODE_IDENT) {
                NodeIdent *id = (NodeIdent *)a->target;
                tp_appendf(t, "static JscValue *v_%s = NULL;\n", id->name);
                NodeIdent *fake = (NodeIdent *)calloc(1, sizeof(NodeIdent));
                fake->base.type = NODE_IDENT;
                fake->name = id->name;
                nodelist_push(&t->declared_vars, (Node *)fake);
            }
        } else if (stmt->type == NODE_FOR) {
            NodeFor *f = (NodeFor *)stmt;
            tp_appendf(t, "static JscValue *v_%s = NULL;\n", f->var_name);
        } else if (stmt->type == NODE_FOR_EACH) {
            NodeForEach *fe = (NodeForEach *)stmt;
            tp_appendf(t, "static JscValue *v_%s = NULL;\n", fe->var_name);
            NodeIdent *fake = (NodeIdent *)calloc(1, sizeof(NodeIdent));
            fake->base.type = NODE_IDENT;
            fake->name = fe->var_name;
            nodelist_push(&t->declared_vars, (Node *)fake);
        } else if (stmt->type == NODE_IMPORT) {
            NodeImport *imp = (NodeImport *)stmt;
            tp_appendf(t, "static JscValue *v_%s = NULL;\n", imp->module);
            NodeIdent *fake = (NodeIdent *)calloc(1, sizeof(NodeIdent));
            fake->base.type = NODE_IDENT;
            fake->name = imp->module;
            nodelist_push(&t->declared_vars, (Node *)fake);
        } else if (stmt->type == NODE_FOR_EACH) {
            /* Variaveis DENTRO do for_each */
            NodeForEach *fe = (NodeForEach *)stmt;
            /* Procura atribuicoes no body */
            Node *body = fe->body;
            if (body && body->type == NODE_BLOCK) {
                NodeBlock *b = (NodeBlock *)body;
                for (size_t j = 0; j < b->statements.count; j++) {
                    Node *inner = b->statements.items[j];
                    if (inner->type == NODE_ASSIGN) {
                        NodeAssign *a = (NodeAssign *)inner;
                        if (a->target && a->target->type == NODE_IDENT) {
                            NodeIdent *id = (NodeIdent *)a->target;
                            /* Se nao e' o var do for, declara */
                            if (strcmp(id->name, fe->var_name) != 0) {
                                tp_appendf(t, "static JscValue *v_%s = NULL;\n", id->name);
                            }
                        }
                    }
                }
            }
        }
    }
    tp_append(t, "\n");
    
    /* Coleta variaveis recursivamente (dentro de for_each, if, etc) */
    
    /* Coleta classes conhecidas (pra detectar chamadas) */
    for (size_t i = 0; i < p->statements.count; i++) {
        Node *stmt = p->statements.items[i];
        if (stmt->type == NODE_CLASS_DECL) {
            NodeClassDecl *cls = (NodeClassDecl *)stmt;
            if (t->n_classes < 64) {
                t->classes[t->n_classes] = cls->name;
                t->pais[t->n_classes] = cls->parent;
                t->n_classes++;
            }
        }
    }
    
    /* Classes e funcoes user-defined (ANTES do main) */
    int tem_decls = 0;
    for (size_t i = 0; i < p->statements.count; i++) {
        Node *stmt = p->statements.items[i];
        if (stmt->type == NODE_CLASS_DECL) {
            fprintf(stderr, "[DEBUG] emitindo classe %zu\n", i);
            transpile_class_decl(t, stmt);
            tem_decls = 1;
        }
    }
    for (size_t i = 0; i < p->statements.count; i++) {
        Node *stmt = p->statements.items[i];
        if (stmt->type == NODE_FUNC_DECL) {
            transpile_func_decl(t, stmt);
            tem_decls = 1;
        }
    }
    if (tem_decls) tp_append(t, "\n");
    
    /* No main, registra os metodos */
    /* Vai ser feito no inicio do main */
    
    /* Threads: gera wrappers */
    transpile_thread_wrappers(t, p);
    
    /* Coleta TODAS as variaveis recursivamente */
    collect_vars(t, prog);
    
    /* Main */
    /* COLLECT: forcar coleta de todas as vars em TODOS os statements */
    {
        /* Percorre TODOS os statements DIRETOS */
        for (size_t _i = 0; _i < p->statements.count; _i++) {
            Node *_s = p->statements.items[_i];
            /* Recursao simples: pega vars de for_each e if */
            if (_s->type == NODE_FOR_EACH) {
                NodeForEach *_fe = (NodeForEach *)_s;
                if (_fe->body && _fe->body->type == NODE_BLOCK) {
                    NodeBlock *_b = (NodeBlock *)_fe->body;
                    for (size_t _j = 0; _j < _b->statements.count; _j++) {
                        Node *_inner = _b->statements.items[_j];
                        if (_inner->type == NODE_ASSIGN) {
                            NodeAssign *_a = (NodeAssign *)_inner;
                            if (_a->target && _a->target->type == NODE_IDENT) {
                                NodeIdent *_id = (NodeIdent *)_a->target;
                                tp_appendf(t, "static JscValue *v_%s = NULL;\n", _id->name);
                            }
                        }
                    }
                }
            }
        }
    }
    
    tp_append(t, "int main(void) {\n");
    t->indent = 1;
    tp_indent(t);
    tp_append(t, "jsc_init();\n\n");
    
    /* Registra metodos das classes (incluindo herdados) */
    for (size_t i = 0; i < p->statements.count; i++) {
        Node *stmt = p->statements.items[i];
        if (stmt->type != NODE_CLASS_DECL) continue;
        NodeClassDecl *cls = (NodeClassDecl *)stmt;
        
        /* Registra metodos proprios */
        for (size_t j = 0; j < cls->methods.count; j++) {
            Node *m = cls->methods.items[j];
            if (m->type != NODE_FUNC_DECL) continue;
            NodeFuncDecl *fn = (NodeFuncDecl *)m;
            tp_indent(t);
            tp_appendf(t, "jsc_register_method(\"%s\", \"%s\", (void*)cls_%s_%s);\n",
                       cls->name, fn->name, cls->name, fn->name);
        }
        
        /* Registra metodos herdados (do pai) */
        if (cls->parent) {
            for (size_t k = 0; k < p->statements.count; k++) {
                Node *s2 = p->statements.items[k];
                if (s2->type != NODE_CLASS_DECL) continue;
                NodeClassDecl *pai = (NodeClassDecl *)s2;
                if (!pai->name || strcmp(pai->name, cls->parent) != 0) continue;
                
                /* Registra metodos do pai TAMBEM no filho */
                for (size_t j = 0; j < pai->methods.count; j++) {
                    Node *m = pai->methods.items[j];
                    if (m->type != NODE_FUNC_DECL) continue;
                    NodeFuncDecl *fn = (NodeFuncDecl *)m;
                    /* So registra se filho NAO tem o mesmo metodo */
                    int filho_tem = 0;
                    for (size_t fi = 0; fi < cls->methods.count; fi++) {
                        Node *fm = cls->methods.items[fi];
                        if (fm->type == NODE_FUNC_DECL) {
                            NodeFuncDecl *ff = (NodeFuncDecl *)fm;
                            if (strcmp(ff->name, fn->name) == 0) {
                                filho_tem = 1;
                                break;
                            }
                        }
                    }
                    if (!filho_tem) {
                        tp_indent(t);
                        tp_appendf(t, "jsc_register_method(\"%s\", \"%s\", (void*)cls_%s_%s);\n",
                                   cls->name, fn->name, pai->name, fn->name);
                    }
                }
                break;
            }
        }
    }
    tp_append(t, "\n");
    
    /* Statements */
    for (size_t i = 0; i < p->statements.count; i++) {
        Node *stmt = p->statements.items[i];
        /* Pula declarations de classe e funcao (ja emitidas antes) */
        if (stmt->type == NODE_FUNC_DECL) continue;
        if (stmt->type == NODE_CLASS_DECL) continue;
        transpile_statement(t, stmt);
    }
    
    tp_append(t, "\n");
    tp_indent(t);
    tp_append(t, "jsc_cleanup();\n");
    tp_indent(t);
    tp_append(t, "return 0;\n");
    tp_append(t, "}\n");
}

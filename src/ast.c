#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================
 * JSC$+ — AST (implementação)
 * ============================================================ */

/* Copia string com segurança */
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

/* Aloca um nó do tamanho pedido e preenche o header */
static Node *alloc_node(size_t size, NodeType type, int line, int col) {
    Node *n = (Node *)calloc(1, size);
    if (!n) {
        fprintf(stderr, "JSC$+ erro: sem memória\n");
        exit(1);
    }
    n->type   = type;
    n->line   = line;
    n->column = col;
    return n;
}

/* ============================================================
 * Lista de nós
 * ============================================================ */

void nodelist_init(NodeList *list) {
    list->items    = NULL;
    list->count    = 0;
    list->capacity = 0;
}

void nodelist_push(NodeList *list, Node *node) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity == 0 ? 8 : list->capacity * 2;
        Node **new_items = (Node **)realloc(list->items, new_cap * sizeof(Node *));
        if (!new_items) {
            fprintf(stderr, "JSC$+ erro: sem memória (lista)\n");
            exit(1);
        }
        list->items    = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = node;
}

void nodelist_free(NodeList *list) {
    for (size_t i = 0; i < list->count; i++) {
        ast_free(list->items[i]);
    }
    free(list->items);
    list->items    = NULL;
    list->count    = 0;
    list->capacity = 0;
}

/* ============================================================
 * Construtores de nós
 * ============================================================ */

Node *ast_number(double value, int line, int col) {
    NodeNumber *n = (NodeNumber *)alloc_node(sizeof(NodeNumber), NODE_NUMBER, line, col);
    n->value = value;
    return (Node *)n;
}

Node *ast_string(const char *value, int line, int col) {
    NodeString *n = (NodeString *)alloc_node(sizeof(NodeString), NODE_STRING, line, col);
    n->value = dup_str(value);
    return (Node *)n;
}

Node *ast_bool(int value, int line, int col) {
    NodeBool *n = (NodeBool *)alloc_node(sizeof(NodeBool), NODE_BOOL, line, col);
    n->value = value;
    return (Node *)n;
}

Node *ast_vazio(int line, int col) {
    return alloc_node(sizeof(Node), NODE_VAZIO, line, col);
}

Node *ast_ident(const char *name, int line, int col) {
    NodeIdent *n = (NodeIdent *)alloc_node(sizeof(NodeIdent), NODE_IDENT, line, col);
    n->name = dup_str(name);
    return (Node *)n;
}


/* Converte string de operador em codigo numerico */
static OpCode opcode_from_string(const char *op) {
    if (!op) return OP_NONE;
    if (strcmp(op, "+") == 0) return OP_ADD;
    if (strcmp(op, "-") == 0) return OP_SUB;
    if (strcmp(op, "*") == 0) return OP_MUL;
    if (strcmp(op, "/") == 0) return OP_DIV;
    if (strcmp(op, "%") == 0) return OP_MOD;
    if (strcmp(op, "**") == 0) return OP_POW;
    if (strcmp(op, "==") == 0) return OP_EQ;
    if (strcmp(op, "!=") == 0) return OP_NEQ;
    if (strcmp(op, "<") == 0) return OP_LT;
    if (strcmp(op, ">") == 0) return OP_GT;
    if (strcmp(op, "<=") == 0) return OP_LTE;
    if (strcmp(op, ">=") == 0) return OP_GTE;
    if (strcmp(op, "a+") == 0) return OP_AND;
    if (strcmp(op, "o+") == 0) return OP_OR;
    if (strcmp(op, "n+") == 0) return OP_NOT;
    if (strcmp(op, "&") == 0) return OP_BIT_AND;
    if (strcmp(op, "|") == 0) return OP_BIT_OR;
    if (strcmp(op, "^") == 0) return OP_BIT_XOR;
    if (strcmp(op, "~") == 0) return OP_BIT_NOT;
    if (strcmp(op, "<<") == 0) return OP_SHL;
    if (strcmp(op, ">>") == 0) return OP_SHR;
    return OP_NONE;
}

Node *ast_binary(const char *op, Node *left, Node *right, int line, int col) {
    NodeBinary *n = (NodeBinary *)alloc_node(sizeof(NodeBinary), NODE_BINARY, line, col);
    n->op      = dup_str(op);
    n->op_code = opcode_from_string(op);
    n->left    = left;
    n->right   = right;
    return (Node *)n;
}

Node *ast_unary(const char *op, Node *operand, int line, int col) {
    NodeUnary *n = (NodeUnary *)alloc_node(sizeof(NodeUnary), NODE_UNARY, line, col);
    n->op      = dup_str(op);
    n->op_code = opcode_from_string(op);
    n->operand = operand;
    return (Node *)n;
}

Node *ast_ternary(Node *cond, Node *then_e, Node *else_e, int line, int col) {
    NodeTernary *n = (NodeTernary *)alloc_node(sizeof(NodeTernary), NODE_TERNARY, line, col);
    n->cond      = cond;
    n->then_expr = then_e;
    n->else_expr = else_e;
    return (Node *)n;
}

Node *ast_call(Node *callee, int line, int col) {
    NodeCall *n = (NodeCall *)alloc_node(sizeof(NodeCall), NODE_CALL, line, col);
    n->callee = callee;
    nodelist_init(&n->args);
    return (Node *)n;
}

Node *ast_index(Node *object, Node *index, int line, int col) {
    NodeIndex *n = (NodeIndex *)alloc_node(sizeof(NodeIndex), NODE_INDEX, line, col);
    n->object = object;
    n->index  = index;
    return (Node *)n;
}

Node *ast_field(Node *object, const char *field, int line, int col) {
    NodeField *n = (NodeField *)alloc_node(sizeof(NodeField), NODE_FIELD, line, col);
    n->object = object;
    n->field  = dup_str(field);
    return (Node *)n;
}

Node *ast_assign(const char *op, Node *target, Node *value, int line, int col) {
    NodeAssign *n = (NodeAssign *)alloc_node(sizeof(NodeAssign), NODE_ASSIGN, line, col);
    n->op     = dup_str(op);
    n->target = target;
    n->value  = value;
    return (Node *)n;
}

Node *ast_array(int line, int col) {
    NodeArray *n = (NodeArray *)alloc_node(sizeof(NodeArray), NODE_ARRAY, line, col);
    nodelist_init(&n->items);
    return (Node *)n;
}

Node *ast_map(int line, int col) {
    NodeMap *n = (NodeMap *)alloc_node(sizeof(NodeMap), NODE_MAP, line, col);
    nodelist_init(&n->keys);
    nodelist_init(&n->values);
    return (Node *)n;
}

Node *ast_program(int line, int col) {
    NodeProgram *n = (NodeProgram *)alloc_node(sizeof(NodeProgram), NODE_PROGRAM, line, col);
    nodelist_init(&n->statements);
    return (Node *)n;
}

Node *ast_block(int line, int col) {
    NodeBlock *n = (NodeBlock *)alloc_node(sizeof(NodeBlock), NODE_BLOCK, line, col);
    nodelist_init(&n->statements);
    return (Node *)n;
}

Node *ast_printj(Node *expr, int line, int col) {
    NodePrintj *n = (NodePrintj *)alloc_node(sizeof(NodePrintj), NODE_PRINTJ, line, col);
    n->expr = expr;
    return (Node *)n;
}

Node *ast_var_decl(const char *type_name, const char *var_name, Node *value, int line, int col) {
    NodeVarDecl *n = (NodeVarDecl *)alloc_node(sizeof(NodeVarDecl), NODE_VAR_DECL, line, col);
    n->type_name = dup_str(type_name);
    n->var_name  = dup_str(var_name);
    n->value     = value;
    return (Node *)n;
}

Node *ast_if(Node *cond, Node *then_b, Node *else_b, int line, int col) {
    NodeIf *n = (NodeIf *)alloc_node(sizeof(NodeIf), NODE_IF, line, col);
    n->cond       = cond;
    n->then_block = then_b;
    n->else_block = else_b;
    return (Node *)n;
}

Node *ast_while(Node *cond, Node *body, int line, int col) {
    NodeWhile *n = (NodeWhile *)alloc_node(sizeof(NodeWhile), NODE_WHILE, line, col);
    n->cond = cond;
    n->body = body;
    return (Node *)n;
}

Node *ast_for(const char *var, Node *start, Node *end, Node *body, int line, int col) {
    NodeFor *n = (NodeFor *)alloc_node(sizeof(NodeFor), NODE_FOR, line, col);
    n->var_name = dup_str(var);
    n->start    = start;
    n->end      = end;
    n->body     = body;
    return (Node *)n;
}

Node *ast_for_each(const char *var, Node *iterable, Node *body, int line, int col) {
    NodeForEach *n = (NodeForEach *)alloc_node(sizeof(NodeForEach), NODE_FOR_EACH, line, col);
    n->var_name = dup_str(var);
    n->iterable = iterable;
    n->body     = body;
    return (Node *)n;
}

Node *ast_return(Node *expr, int line, int col) {
    NodeReturn *n = (NodeReturn *)alloc_node(sizeof(NodeReturn), NODE_RETURN, line, col);
    n->expr = expr;
    return (Node *)n;
}

Node *ast_func_decl(const char *name, int line, int col) {
    NodeFuncDecl *n = (NodeFuncDecl *)alloc_node(sizeof(NodeFuncDecl), NODE_FUNC_DECL, line, col);
    n->name = dup_str(name);
    nodelist_init(&n->params);
    n->body = NULL;
    return (Node *)n;
}

Node *ast_class_decl(const char *name, const char *parent, int line, int col) {
    NodeClassDecl *n = (NodeClassDecl *)alloc_node(sizeof(NodeClassDecl), NODE_CLASS_DECL, line, col);
    n->name   = dup_str(name);
    n->parent = dup_str(parent);
    nodelist_init(&n->fields);
    nodelist_init(&n->methods);
    return (Node *)n;
}

Node *ast_import(const char *module, const char *item, int line, int col) {
    NodeImport *n = (NodeImport *)alloc_node(sizeof(NodeImport), NODE_IMPORT, line, col);
    n->module = dup_str(module);
    n->item   = dup_str(item);
    return (Node *)n;
}

Node *ast_try(Node *try_b, const char *catch_var, Node *catch_b, Node *finally_b, int line, int col) {
    NodeTry *n = (NodeTry *)alloc_node(sizeof(NodeTry), NODE_TRY, line, col);
    n->try_block     = try_b;
    n->catch_var     = dup_str(catch_var);
    n->catch_block   = catch_b;
    n->finally_block = finally_b;
    return (Node *)n;
}

Node *ast_break(int line, int col) {
    return alloc_node(sizeof(Node), NODE_BREAK, line, col);
}

Node *ast_continue(int line, int col) {
    return alloc_node(sizeof(Node), NODE_CONTINUE, line, col);
}

/* ============================================================
 * Liberação de memória
 * ============================================================ */

void ast_free(Node *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_NUMBER:
        case NODE_BOOL:
        case NODE_VAZIO:
        case NODE_BREAK:
        case NODE_CONTINUE:
            break;

        case NODE_STRING: {
            NodeString *n = (NodeString *)node;
            free(n->value);
            break;
        }
        case NODE_IDENT: {
            NodeIdent *n = (NodeIdent *)node;
            free(n->name);
            break;
        }
        case NODE_BINARY: {
            NodeBinary *n = (NodeBinary *)node;
            free(n->op);
            ast_free(n->left);
            ast_free(n->right);
            break;
        }
        case NODE_UNARY: {
            NodeUnary *n = (NodeUnary *)node;
            free(n->op);
            ast_free(n->operand);
            break;
        }
        case NODE_TERNARY: {
            NodeTernary *n = (NodeTernary *)node;
            ast_free(n->cond);
            ast_free(n->then_expr);
            ast_free(n->else_expr);
            break;
        }
        case NODE_CALL: {
            NodeCall *n = (NodeCall *)node;
            ast_free(n->callee);
            nodelist_free(&n->args);
            break;
        }
        case NODE_INDEX: {
            NodeIndex *n = (NodeIndex *)node;
            ast_free(n->object);
            ast_free(n->index);
            break;
        }
        case NODE_FIELD: {
            NodeField *n = (NodeField *)node;
            ast_free(n->object);
            free(n->field);
            break;
        }
        case NODE_ASSIGN: {
            NodeAssign *n = (NodeAssign *)node;
            free(n->op);
            ast_free(n->target);
            ast_free(n->value);
            break;
        }
        case NODE_ARRAY: {
            NodeArray *n = (NodeArray *)node;
            nodelist_free(&n->items);
            break;
        }
        case NODE_MAP: {
            NodeMap *n = (NodeMap *)node;
            nodelist_free(&n->keys);
            nodelist_free(&n->values);
            break;
        }
        case NODE_PROGRAM: {
            NodeProgram *n = (NodeProgram *)node;
            nodelist_free(&n->statements);
            break;
        }
        case NODE_BLOCK: {
            NodeBlock *n = (NodeBlock *)node;
            nodelist_free(&n->statements);
            break;
        }
        case NODE_PRINTJ: {
            NodePrintj *n = (NodePrintj *)node;
            ast_free(n->expr);
            break;
        }
        case NODE_VAR_DECL: {
            NodeVarDecl *n = (NodeVarDecl *)node;
            free(n->type_name);
            free(n->var_name);
            ast_free(n->value);
            break;
        }
        case NODE_IF: {
            NodeIf *n = (NodeIf *)node;
            ast_free(n->cond);
            ast_free(n->then_block);
            ast_free(n->else_block);
            break;
        }
        case NODE_WHILE: {
            NodeWhile *n = (NodeWhile *)node;
            ast_free(n->cond);
            ast_free(n->body);
            break;
        }
        case NODE_FOR: {
            NodeFor *n = (NodeFor *)node;
            free(n->var_name);
            ast_free(n->start);
            ast_free(n->end);
            ast_free(n->body);
            break;
        }
        case NODE_FOR_EACH: {
            NodeForEach *n = (NodeForEach *)node;
            free(n->var_name);
            ast_free(n->iterable);
            ast_free(n->body);
            break;
        }
        case NODE_RETURN: {
            NodeReturn *n = (NodeReturn *)node;
            ast_free(n->expr);
            break;
        }
        case NODE_FUNC_DECL: {
            NodeFuncDecl *n = (NodeFuncDecl *)node;
            free(n->name);
            nodelist_free(&n->params);
            ast_free(n->body);
            break;
        }
        case NODE_CLASS_DECL: {
            NodeClassDecl *n = (NodeClassDecl *)node;
            free(n->name);
            free(n->parent);
            nodelist_free(&n->fields);
            nodelist_free(&n->methods);
            break;
        }
        case NODE_IMPORT: {
            NodeImport *n = (NodeImport *)node;
            free(n->module);
            free(n->item);
            break;
        }
        case NODE_TRY: {
            NodeTry *n = (NodeTry *)node;
            ast_free(n->try_block);
            free(n->catch_var);
            ast_free(n->catch_block);
            ast_free(n->finally_block);
            break;
        }
    }

    free(node);
}

/* ============================================================
 * Debug — imprime a árvore
 * ============================================================ */

static void indent(int n) {
    for (int i = 0; i < n; i++) printf("  ");
}

void ast_dump(Node *node, int level) {
    if (!node) {
        indent(level);
        printf("(null)\n");
        return;
    }

    indent(level);
    printf("%s", node_type_name(node->type));

    switch (node->type) {
        case NODE_NUMBER: {
            NodeNumber *n = (NodeNumber *)node;
            printf("  %.4f", n->value);
            break;
        }
        case NODE_STRING: {
            NodeString *n = (NodeString *)node;
            printf("  \"%s\"", n->value);
            break;
        }
        case NODE_BOOL: {
            NodeBool *n = (NodeBool *)node;
            printf("  %s", n->value ? "true" : "false");
            break;
        }
        case NODE_IDENT: {
            NodeIdent *n = (NodeIdent *)node;
            printf("  %s", n->name);
            break;
        }
        case NODE_BINARY: {
            NodeBinary *n = (NodeBinary *)node;
            printf("  op='%s'", n->op);
            break;
        }
        case NODE_UNARY: {
            NodeUnary *n = (NodeUnary *)node;
            printf("  op='%s'", n->op);
            break;
        }
        case NODE_ASSIGN: {
            NodeAssign *n = (NodeAssign *)node;
            printf("  op='%s'", n->op);
            break;
        }
        case NODE_VAR_DECL: {
            NodeVarDecl *n = (NodeVarDecl *)node;
            if (n->type_name) printf("  tipo='%s'", n->type_name);
            printf("  nome='%s'", n->var_name);
            break;
        }
        case NODE_FOR: {
            NodeFor *n = (NodeFor *)node;
            printf("  var='%s'", n->var_name);
            break;
        }
        case NODE_FUNC_DECL: {
            NodeFuncDecl *n = (NodeFuncDecl *)node;
            printf("  nome='%s'", n->name);
            break;
        }
        case NODE_CLASS_DECL: {
            NodeClassDecl *n = (NodeClassDecl *)node;
            printf("  nome='%s'", n->name);
            if (n->parent) printf("  extends='%s'", n->parent);
            break;
        }
        case NODE_IMPORT: {
            NodeImport *n = (NodeImport *)node;
            printf("  modulo='%s'", n->module);
            if (n->item) printf("  item='%s'", n->item);
            break;
        }
        case NODE_FIELD: {
            NodeField *n = (NodeField *)node;
            printf("  campo='%s'", n->field);
            break;
        }
        default:
            break;
    }

    printf("  [%d:%d]\n", node->line, node->column);

    /* Filhos */
    switch (node->type) {
        case NODE_BINARY: {
            NodeBinary *n = (NodeBinary *)node;
            ast_dump(n->left, level + 1);
            ast_dump(n->right, level + 1);
            break;
        }
        case NODE_UNARY: {
            NodeUnary *n = (NodeUnary *)node;
            ast_dump(n->operand, level + 1);
            break;
        }
        case NODE_TERNARY: {
            NodeTernary *n = (NodeTernary *)node;
            ast_dump(n->cond, level + 1);
            ast_dump(n->then_expr, level + 1);
            ast_dump(n->else_expr, level + 1);
            break;
        }
        case NODE_CALL: {
            NodeCall *n = (NodeCall *)node;
            ast_dump(n->callee, level + 1);
            for (size_t i = 0; i < n->args.count; i++) {
                ast_dump(n->args.items[i], level + 1);
            }
            break;
        }
        case NODE_INDEX: {
            NodeIndex *n = (NodeIndex *)node;
            ast_dump(n->object, level + 1);
            ast_dump(n->index, level + 1);
            break;
        }
        case NODE_FIELD: {
            NodeField *n = (NodeField *)node;
            ast_dump(n->object, level + 1);
            break;
        }
        case NODE_ASSIGN: {
            NodeAssign *n = (NodeAssign *)node;
            ast_dump(n->target, level + 1);
            ast_dump(n->value, level + 1);
            break;
        }
        case NODE_ARRAY: {
            NodeArray *n = (NodeArray *)node;
            for (size_t i = 0; i < n->items.count; i++)
                ast_dump(n->items.items[i], level + 1);
            break;
        }
        case NODE_MAP: {
            NodeMap *n = (NodeMap *)node;
            for (size_t i = 0; i < n->keys.count; i++) {
                ast_dump(n->keys.items[i], level + 1);
                ast_dump(n->values.items[i], level + 1);
            }
            break;
        }
        case NODE_PROGRAM: {
            NodeProgram *n = (NodeProgram *)node;
            for (size_t i = 0; i < n->statements.count; i++)
                ast_dump(n->statements.items[i], level + 1);
            break;
        }
        case NODE_BLOCK: {
            NodeBlock *n = (NodeBlock *)node;
            for (size_t i = 0; i < n->statements.count; i++)
                ast_dump(n->statements.items[i], level + 1);
            break;
        }
        case NODE_PRINTJ: {
            NodePrintj *n = (NodePrintj *)node;
            ast_dump(n->expr, level + 1);
            break;
        }
        case NODE_VAR_DECL: {
            NodeVarDecl *n = (NodeVarDecl *)node;
            ast_dump(n->value, level + 1);
            break;
        }
        case NODE_IF: {
            NodeIf *n = (NodeIf *)node;
            ast_dump(n->cond, level + 1);
            ast_dump(n->then_block, level + 1);
            if (n->else_block) ast_dump(n->else_block, level + 1);
            break;
        }
        case NODE_WHILE: {
            NodeWhile *n = (NodeWhile *)node;
            ast_dump(n->cond, level + 1);
            ast_dump(n->body, level + 1);
            break;
        }
        case NODE_FOR: {
            NodeFor *n = (NodeFor *)node;
            ast_dump(n->start, level + 1);
            ast_dump(n->end, level + 1);
            ast_dump(n->body, level + 1);
            break;
        }
        case NODE_FOR_EACH: {
            NodeForEach *n = (NodeForEach *)node;
            ast_dump(n->iterable, level + 1);
            ast_dump(n->body, level + 1);
            break;
        }
        case NODE_RETURN: {
            NodeReturn *n = (NodeReturn *)node;
            if (n->expr) ast_dump(n->expr, level + 1);
            break;
        }
        case NODE_FUNC_DECL: {
            NodeFuncDecl *n = (NodeFuncDecl *)node;
            for (size_t i = 0; i < n->params.count; i++)
                ast_dump(n->params.items[i], level + 1);
            ast_dump(n->body, level + 1);
            break;
        }
        case NODE_CLASS_DECL: {
            NodeClassDecl *n = (NodeClassDecl *)node;
            for (size_t i = 0; i < n->methods.count; i++)
                ast_dump(n->methods.items[i], level + 1);
            break;
        }
        case NODE_TRY: {
            NodeTry *n = (NodeTry *)node;
            ast_dump(n->try_block, level + 1);
            ast_dump(n->catch_block, level + 1);
            if (n->finally_block) ast_dump(n->finally_block, level + 1);
            break;
        }
        default:
            break;
    }
}

const char *node_type_name(NodeType t) {
    switch (t) {
        case NODE_NUMBER:     return "Number";
        case NODE_STRING:     return "String";
        case NODE_BOOL:       return "Bool";
        case NODE_VAZIO:      return "Vazio";
        case NODE_IDENT:      return "Ident";
        case NODE_BINARY:     return "Binary";
        case NODE_UNARY:      return "Unary";
        case NODE_TERNARY:    return "Ternary";
        case NODE_CALL:       return "Call";
       	case NODE_INDEX:      return "Index";
        case NODE_FIELD:      return "Field";
        case NODE_ASSIGN:     return "Assign";
        case NODE_ARRAY:      return "Array";
        case NODE_MAP:        return "Map";
        case NODE_PROGRAM:    return "Program";
        case NODE_BLOCK:      return "Block";
        case NODE_PRINTJ:     return "Printj";
        case NODE_VAR_DECL:   return "VarDecl";
        case NODE_IF:         return "If";
        case NODE_WHILE:      return "While";
        case NODE_FOR:        return "For";
        case NODE_FOR_EACH:   return "ForEach";
        case NODE_RETURN:     return "Return";
        case NODE_FUNC_DECL:  return "FuncDecl";
        case NODE_CLASS_DECL: return "ClassDecl";
        case NODE_IMPORT:     return "Import";
        case NODE_TRY:        return "Try";
        case NODE_BREAK:      return "Break";
        case NODE_CONTINUE:   return "Continue";
    }
    return "???";
}

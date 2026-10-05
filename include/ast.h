#ifndef JSC_AST_H
#define JSC_AST_H

#include <stddef.h>

/* ============================================================
 * JSC$+ — AST (Abstract Syntax Tree)
 * Definição dos nós da árvore sintática
 * ============================================================ */

/* Codigos de operador — mais rapido que strcmp */
typedef enum {
    OP_NONE = 0,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LTE, OP_GTE,
    OP_AND, OP_OR, OP_NOT,
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_NOT,
    OP_SHL, OP_SHR,
    OP_NEG,
} OpCode;

typedef enum {
    /* Expressões */
    NODE_NUMBER,        /* 42, 3.14 */
    NODE_STRING,        /* "texto" */
    NODE_BOOL,          /* true / false */
    NODE_VAZIO,         /* vazio (null) */
    NODE_IDENT,         /* nome, idade, x */
    NODE_BINARY,        /* a + b, a == b, a a+ b */
    NODE_UNARY,         /* -x, n+ x, ~x */
    NODE_TERNARY,       /* cond ? a : b */
    NODE_CALL,          /* printj(x), somar(2,3) */
    NODE_INDEX,         /* array[0], map["key"] */
    NODE_FIELD,         /* obj.nome, this.x */
    NODE_ASSIGN,        /* x = 5, x += 3 */
    NODE_ARRAY,         /* [1, 2, 3] */
    NODE_MAP,           /* {"k": v} */

    /* Comandos */
    NODE_PROGRAM,       /* nó raiz — contém uma lista de comandos */
    NODE_BLOCK,         /* { ... } — lista de comandos */
    NODE_PRINTJ,        /* printj(expr) */
    NODE_VAR_DECL,      /* idade = 19, int x = 5 */
    NODE_IF,            /* if cond { } else { } */
    NODE_WHILE,         /* while cond { } */
    NODE_FOR,           /* for i in 1..10 { } */
    NODE_FOR_EACH,      /* for x in lista { } */
    NODE_RETURN,        /* return expr */
    NODE_FUNC_DECL,     /* function nome(args) { } */
    NODE_CLASS_DECL,    /* class Nome { } */
    NODE_IMPORT,        /* jsc math$+ */
    NODE_TRY,           /* try { } catch e { } finally { } */
    NODE_BREAK,         /* break */
    NODE_CONTINUE       /* continue */
} NodeType;

/* Nó genérico — todo nó da AST começa com type */
typedef struct Node Node;

/* Lista de nós (usada em programa, bloco, argumentos, etc) */
typedef struct {
    Node **items;
    size_t count;
    size_t capacity;
} NodeList;

/* Estrutura base — todo nó tem tipo + linha + coluna */
struct Node {
    NodeType type;
    int      line;
    int      column;
};

/* -------- Expressões -------- */

typedef struct {
    Node   base;
    double value;
} NodeNumber;

typedef struct {
    Node  base;
    char *value;
} NodeString;

typedef struct {
    Node base;
    int  value;   /* 0 = false, 1 = true */
} NodeBool;

typedef struct {
    Node  base;
    char *name;
} NodeIdent;

typedef struct {
    Node  base;
    char *op;       /* "+", "-", "==", "a+", etc */
    OpCode op_code; /* codigo numerico — mais rapido */
    Node *left;
    Node *right;
} NodeBinary;

typedef struct {
    Node  base;
    char *op;       /* "-", "n+", "~" */
    OpCode op_code;
    Node *operand;
} NodeUnary;

typedef struct {
    Node  base;
    Node *cond;
    Node *then_expr;
    Node *else_expr;
} NodeTernary;

typedef struct {
    Node      base;
    Node     *callee;      /* o que está sendo chamado (ident, field) */
    NodeList  args;
} NodeCall;

typedef struct {
    Node  base;
    Node *object;      /* array ou map */
    Node *index;       /* índice */
} NodeIndex;

typedef struct {
    Node  base;
    Node *object;      /* obj */
    char *field;       /* nome do campo */
} NodeField;

typedef struct {
    Node  base;
    char *op;          /* "=", "+=", "-=", etc */
    Node *target;      /* o que recebe (ident, index, field) */
    Node *value;       /* o valor */
} NodeAssign;

typedef struct {
    Node     base;
    NodeList items;    /* elementos do array */
} NodeArray;

typedef struct {
    Node     base;
    NodeList keys;     /* chaves */
    NodeList values;   /* valores */
} NodeMap;

/* -------- Comandos -------- */

typedef struct {
    Node     base;
    NodeList statements;
} NodeProgram;

typedef struct {
    Node     base;
    NodeList statements;
} NodeBlock;

typedef struct {
    Node  base;
    Node *expr;      /* expressão a imprimir */
} NodePrintj;

typedef struct {
    Node  base;
    char *type_name;  /* "int", "string", ou NULL se sem tipo */
    char *var_name;
    Node *value;      /* pode ser NULL */
} NodeVarDecl;

typedef struct {
    Node  base;
    Node *cond;
    Node *then_block;
    Node *else_block;  /* pode ser NULL */
} NodeIf;

typedef struct {
    Node  base;
    Node *cond;
    Node *body;
} NodeWhile;

typedef struct {
    Node  base;
    char *var_name;
    Node *start;
    Node *end;
    Node *body;
} NodeFor;

typedef struct {
    Node  base;
    char *var_name;
    Node *iterable;
    Node *body;
} NodeForEach;

typedef struct {
    Node  base;
    Node *expr;      /* pode ser NULL (return vazio) */
} NodeReturn;

typedef struct {
    Node     base;
    char    *name;
    NodeList params;      /* lista de NodeIdent */
    Node    *body;        /* NodeBlock */
} NodeFuncDecl;

typedef struct {
    Node     base;
    char    *name;
    char    *parent;      /* nome da classe pai, ou NULL */
    NodeList fields;      /* NodeVarDecl (sem value) */
    NodeList methods;     /* NodeFuncDecl */
} NodeClassDecl;

typedef struct {
    Node  base;
    char *module;      /* "math", "time", "net" */
    char *item;        /* "add", "now", ou NULL */
} NodeImport;

typedef struct {
    Node  base;
    Node *try_block;
    char *catch_var;
    Node *catch_block;
    Node *finally_block;   /* pode ser NULL */
} NodeTry;

/* ============================================================
 * API da AST
 * ============================================================ */

/* Cria nós — cada função aloca e devolve o nó genérico */
Node *ast_number(double value, int line, int col);
Node *ast_string(const char *value, int line, int col);
Node *ast_bool(int value, int line, int col);
Node *ast_vazio(int line, int col);
Node *ast_ident(const char *name, int line, int col);
Node *ast_binary(const char *op, Node *left, Node *right, int line, int col);
Node *ast_unary(const char *op, Node *operand, int line, int col);
Node *ast_ternary(Node *cond, Node *then_e, Node *else_e, int line, int col);
Node *ast_call(Node *callee, int line, int col);
Node *ast_index(Node *object, Node *index, int line, int col);
Node *ast_field(Node *object, const char *field, int line, int col);
Node *ast_assign(const char *op, Node *target, Node *value, int line, int col);
Node *ast_array(int line, int col);
Node *ast_map(int line, int col);

Node *ast_program(int line, int col);
Node *ast_block(int line, int col);
Node *ast_printj(Node *expr, int line, int col);
Node *ast_var_decl(const char *type_name, const char *var_name, Node *value, int line, int col);
Node *ast_if(Node *cond, Node *then_b, Node *else_b, int line, int col);
Node *ast_while(Node *cond, Node *body, int line, int col);
Node *ast_for(const char *var, Node *start, Node *end, Node *body, int line, int col);
Node *ast_for_each(const char *var, Node *iterable, Node *body, int line, int col);
Node *ast_return(Node *expr, int line, int col);
Node *ast_func_decl(const char *name, int line, int col);
Node *ast_class_decl(const char *name, const char *parent, int line, int col);
Node *ast_import(const char *module, const char *item, int line, int col);
Node *ast_try(Node *try_b, const char *catch_var, Node *catch_b, Node *finally_b, int line, int col);
Node *ast_break(int line, int col);
Node *ast_continue(int line, int col);

/* Manipulação de listas de nós */
void  nodelist_init(NodeList *list);
void  nodelist_push(NodeList *list, Node *node);
void  nodelist_free(NodeList *list);

/* Libera toda a árvore */
void  ast_free(Node *node);

/* Imprime a árvore (debug) */
void  ast_dump(Node *node, int indent);

/* Nome legível do tipo de nó */
const char *node_type_name(NodeType t);

#endif /* JSC_AST_H */

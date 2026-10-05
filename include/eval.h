#ifndef JSC_EVAL_H
#define JSC_EVAL_H

#include "ast.h"
#include "env.h"
#include "value.h"

typedef struct {
    int       had_error;
    int       has_return;
    int       has_break;
    int       has_continue;
    int       in_try;              /* dentro de um bloco try? */
    JscValue *return_value;
    char      error_message[256];  /* mensagem do último erro */
} EvalState;

JscValue *eval(Node *node, Env *env, EvalState *state);

/* Registra funcoes nativas no env global (input, etc) */
void natives_init(Env *global);

/* Expressoes */
JscValue *eval_literal(Node *node, Env *env, EvalState *state);
JscValue *eval_ident(Node *node, Env *env, EvalState *state);
JscValue *eval_binary(Node *node, Env *env, EvalState *state);
JscValue *eval_unary(Node *node, Env *env, EvalState *state);
JscValue *eval_ternary(Node *node, Env *env, EvalState *state);

/* Comandos */
JscValue *eval_program(Node *node, Env *env, EvalState *state);
JscValue *eval_block(Node *node, Env *env, EvalState *state);
JscValue *eval_printj(Node *node, Env *env, EvalState *state);
JscValue *eval_var_decl(Node *node, Env *env, EvalState *state);

/* Controle de fluxo */
JscValue *eval_if(Node *node, Env *env, EvalState *state);
JscValue *eval_while(Node *node, Env *env, EvalState *state);
JscValue *eval_for(Node *node, Env *env, EvalState *state);
JscValue *eval_for_each(Node *node, Env *env, EvalState *state);
JscValue *eval_import(Node *node, Env *env, EvalState *state);

/* Funcoes (4.d) */
JscValue *eval_func_decl(Node *node, Env *env, EvalState *state);
JscValue *eval_call(Node *node, Env *env, EvalState *state);
JscValue *eval_return(Node *node, Env *env, EvalState *state);
JscValue *eval_class_decl(Node *node, Env *env, EvalState *state);

/* Arrays e Maps (B.1) */
JscValue *eval_array(Node *node, Env *env, EvalState *state);
JscValue *eval_map(Node *node, Env *env, EvalState *state);

/* Acesso por indice (B.2) */
JscValue *eval_index(Node *node, Env *env, EvalState *state);
JscValue *eval_field(Node *node, Env *env, EvalState *state);
JscValue *eval_assign(Node *node, Env *env, EvalState *state);

/* Metodos (B.3) */
JscValue *eval_method_call(NodeCall *n, Env *env, EvalState *state);


/* Tratamento de erro */
JscValue *eval_try(Node *node, Env *env, EvalState *state);

/* Break/Continue */
JscValue *eval_break(Node *node, Env *env, EvalState *state);
JscValue *eval_continue(Node *node, Env *env, EvalState *state);

/* Threads */
JscValue *native_spawn(void *call, void *env, void *state);

JscValue *make_tensor_module(void);

JscValue *make_embedding_module(void);

JscValue *make_attention_module(void);

JscValue *make_multihead_module(void);

JscValue *make_block_module(void);

JscValue *make_model_module(void);

JscValue *make_transformer_module(void);

JscValue *make_loss_module(void);

JscValue *make_backprop_module(void);

JscValue *make_backprop_ff_module(void);

JscValue *make_backprop_ln_module(void);

JscValue *make_backprop_attn_module(void);

JscValue *make_optimizer_module(void);

JscValue *make_forward_cache_module(void);

JscValue *make_backprop_layer_module(void);

#endif /* JSC_EVAL_H */

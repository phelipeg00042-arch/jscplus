" ============================================================
" JSC$+ Syntax Highlighting para Vim
" ============================================================

if exists("b:current_syntax")
    finish
endif

" Terminador $+
syntax match jscTerminator /\(\$\+\)/
hi def link jscTerminator Special

" Comentário @ até fim da linha
syntax match jscComment /@.*$/
hi def link jscComment Comment

" Palavras-chave
syntax keyword jscKeyword if else for in while function return
syntax keyword jscKeyword class extends this init try catch finally
syntax keyword jscKeyword break continue jsc import new spawn join
syntax keyword jscKeyword async await
hi def link jscKeyword Statement

" Funções nativas
syntax keyword jscNative printj
hi def link jscNative Function

" Operadores lógicos JSC$+
syntax match jscLogic /\(\a\+\|o\+\|n\+\)\>/
hi def link jscLogic Special

" Booleanos e vazio
syntax keyword jscBool true false vazio
hi def link jscBool Boolean

" Números
syntax match jscNumber /\<\d\+\(\.\d\+\)\?\>/
hi def link jscNumber Number

" Strings com aspas duplas
syntax region jscString start=/"/ skip=/\\"/ end=/"/
" Strings com aspas simples
syntax region jscString start=/'/ skip=/\\'/ end=/'/
hi def link jscString String

" Operadores
syntax match jscOperator /==\|!=\|<=\|>=\|<<\|>>\|\*\*\|->\|=>\|\.\./
syntax match jscOperator /[+\-*\/%=<>!&|^~?:]/
hi def link jscOperator Operator

" Pontuação
syntax match jscPunct /[(){}\[\],;.]/
hi def link jscPunct Delimiter

let b:current_syntax = "jscplus"

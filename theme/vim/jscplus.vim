" ============================================================
" JSC$+ Theme — "JusSec Dark"
" Tema oficial da linguagem JSC$+
" ============================================================

hi clear
if exists("syntax_on")
    syntax reset
endif

set background=dark
let g:colors_name = "jscplus"

" Fundo
hi Normal          guifg=#e8d8c8 guibg=#1a1a1a ctermfg=230 ctermbg=234
hi LineNr          guifg=#6a6560 guibg=#1a1a1a ctermfg=242 ctermbg=234
hi CursorLine      guibg=#2a2a2a ctermbg=236
hi CursorLineNr    guifg=#ff4d6d guibg=#2a2a2a ctermfg=204 ctermbg=236
hi Cursor          guifg=#1a1a1a guibg=#ff0044 ctermfg=234 ctermbg=197
hi Visual          guibg=#3a1a1a ctermbg=52
hi MatchParen      guifg=#ff0044 guibg=#3a1a1a ctermfg=197 ctermbg=52
hi StatusLine      guifg=#e8d8c8 guibg=#262626 ctermfg=230 ctermbg=235
hi StatusLineNC    guifg=#6a6560 guibg=#1a1a1a ctermfg=242 ctermbg=234
hi VertSplit       guifg=#262626 guibg=#262626 ctermfg=235 ctermbg=235
hi Pmenu           guifg=#e8d8c8 guibg=#262626 ctermfg=230 ctermbg=235
hi PmenuSel        guifg=#1a1a1a guibg=#ff4d6d ctermfg=234 ctermbg=204

" Comentário
hi Comment         guifg=#6a6560 ctermfg=242

" Terminador $+ — A MARCA DA JSC$+
hi Special         guifg=#ff0044 ctermfg=197 gui=bold cterm=bold

" Palavras-chave
hi Statement       guifg=#ff4d6d ctermfg=204
hi Keyword         guifg=#ff4d6d ctermfg=204

" Strings
hi String          guifg=#ffb86c ctermfg=215

" Números
hi Number          guifg=#7ec8e3 ctermfg=117
hi Float           guifg=#7ec8e3 ctermfg=117

" Booleanos
hi Boolean         guifg=#ff9f1c ctermfg=214

" Funções nativas (printj)
hi Function        guifg=#c9a86a ctermfg=179

" Identificadores
hi Identifier      guifg=#e8d8c8 ctermfg=230

" Operadores
hi Operator        guifg=#e8d8c8 ctermfg=230

" Tipos
hi Type            guifg=#ff4d6d ctermfg=204

" Erros
hi Error           guifg=#ff2e2e guibg=#1a1a1a ctermfg=196 ctermbg=234
hi ErrorMsg        guifg=#ff2e2e guibg=#262626 ctermfg=196 ctermbg=235

" Busca
hi Search          guifg=#1a1a1a guibg=#ff9f1c ctermfg=234 ctermbg=214
hi IncSearch       guifg=#1a1a1a guibg=#ff0044 ctermfg=234 ctermbg=197

" TODO
hi Todo            guifg=#ff9f1c guibg=#1a1a1a ctermfg=214 ctermbg=234 gui=bold cterm=bold

" ============================================================
" JSC$+ Theme: Ocean
" ============================================================

hi clear
if exists("syntax_on")
    syntax reset
endif

set background=dark
let g:colors_name = "ocean"

" Base
hi Normal          guifg=#c8e0f0 guibg=#0a1420
hi LineNr          guifg=#4a6a80 guibg=#0a1420
hi CursorLine      guibg=#122a3e
hi CursorLineNr    guifg=#4fc3f7 guibg=#122a3e
hi Cursor          guifg=#0a1420 guibg=#00bcd4
hi Visual          guibg=#0f1e2e
hi MatchParen      guifg=#00bcd4 guibg=#0f1e2e
hi StatusLine      guifg=#c8e0f0 guibg=#0f1e2e
hi StatusLineNC    guifg=#4a6a80 guibg=#0a1420
hi VertSplit       guifg=#0f1e2e guibg=#0f1e2e
hi Pmenu           guifg=#c8e0f0 guibg=#0f1e2e
hi PmenuSel        guifg=#0a1420 guibg=#4fc3f7

" Sintaxe
hi Comment         guifg=#4a6a80
hi Special         guifg=#00bcd4 gui=bold
hi Statement       guifg=#4fc3f7
hi Keyword         guifg=#4fc3f7
hi String          guifg=#ffb86c
hi Number          guifg=#a8e6cf
hi Float           guifg=#a8e6cf
hi Boolean         guifg=#ffa726
hi Function        guifg=#81d4fa
hi Identifier      guifg=#c8e0f0
hi Operator        guifg=#c8e0f0
hi Type            guifg=#4fc3f7
hi Error           guifg=#ef5350 guibg=#0a1420
hi ErrorMsg        guifg=#ef5350 guibg=#0f1e2e
hi Search          guifg=#0a1420 guibg=#ffa726
hi IncSearch       guifg=#0a1420 guibg=#00bcd4
hi Todo            guifg=#ffa726 guibg=#0a1420 gui=bold

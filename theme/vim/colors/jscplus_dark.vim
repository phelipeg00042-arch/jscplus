" ============================================================
" JSC$+ Theme: JusSec Dark
" ============================================================

hi clear
if exists("syntax_on")
    syntax reset
endif

set background=dark
let g:colors_name = "jscplus_dark"

" Base
hi Normal          guifg=#e8d8c8 guibg=#1a1a1a
hi LineNr          guifg=#6a6560 guibg=#1a1a1a
hi CursorLine      guibg=#2a2a2a
hi CursorLineNr    guifg=#ff4d6d guibg=#2a2a2a
hi Cursor          guifg=#1a1a1a guibg=#ff0044
hi Visual          guibg=#262626
hi MatchParen      guifg=#ff0044 guibg=#262626
hi StatusLine      guifg=#e8d8c8 guibg=#262626
hi StatusLineNC    guifg=#6a6560 guibg=#1a1a1a
hi VertSplit       guifg=#262626 guibg=#262626
hi Pmenu           guifg=#e8d8c8 guibg=#262626
hi PmenuSel        guifg=#1a1a1a guibg=#ff4d6d

" Sintaxe
hi Comment         guifg=#6a6560
hi Special         guifg=#ff0044 gui=bold
hi Statement       guifg=#ff4d6d
hi Keyword         guifg=#ff4d6d
hi String          guifg=#ffb86c
hi Number          guifg=#7ec8e3
hi Float           guifg=#7ec8e3
hi Boolean         guifg=#ff9f1c
hi Function        guifg=#c9a86a
hi Identifier      guifg=#e8d8c8
hi Operator        guifg=#e8d8c8
hi Type            guifg=#ff4d6d
hi Error           guifg=#ff2e2e guibg=#1a1a1a
hi ErrorMsg        guifg=#ff2e2e guibg=#262626
hi Search          guifg=#1a1a1a guibg=#ff9f1c
hi IncSearch       guifg=#1a1a1a guibg=#ff0044
hi Todo            guifg=#ff9f1c guibg=#1a1a1a gui=bold

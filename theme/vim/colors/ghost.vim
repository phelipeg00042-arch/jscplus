" ============================================================
" JSC$+ Theme: Ghost
" ============================================================

hi clear
if exists("syntax_on")
    syntax reset
endif

set background=light
let g:colors_name = "ghost"

" Base
hi Normal          guifg=#2a2a2a guibg=#f0f0f0
hi LineNr          guifg=#808080 guibg=#f0f0f0
hi CursorLine      guibg=#e8e8e8
hi CursorLineNr    guifg=#4a4a8a guibg=#e8e8e8
hi Cursor          guifg=#f0f0f0 guibg=#8a4a4a
hi Visual          guibg=#e0e0e0
hi MatchParen      guifg=#8a4a4a guibg=#e0e0e0
hi StatusLine      guifg=#2a2a2a guibg=#e0e0e0
hi StatusLineNC    guifg=#808080 guibg=#f0f0f0
hi VertSplit       guifg=#e0e0e0 guibg=#e0e0e0
hi Pmenu           guifg=#2a2a2a guibg=#e0e0e0
hi PmenuSel        guifg=#f0f0f0 guibg=#4a4a8a

" Sintaxe
hi Comment         guifg=#808080
hi Special         guifg=#8a4a4a gui=bold
hi Statement       guifg=#4a4a8a
hi Keyword         guifg=#4a4a8a
hi String          guifg=#8a6a4a
hi Number          guifg=#4a8a8a
hi Float           guifg=#4a8a8a
hi Boolean         guifg=#8a6a4a
hi Function        guifg=#4a6a8a
hi Identifier      guifg=#2a2a2a
hi Operator        guifg=#2a2a2a
hi Type            guifg=#4a4a8a
hi Error           guifg=#c03030 guibg=#f0f0f0
hi ErrorMsg        guifg=#c03030 guibg=#e0e0e0
hi Search          guifg=#f0f0f0 guibg=#8a6a4a
hi IncSearch       guifg=#f0f0f0 guibg=#8a4a4a
hi Todo            guifg=#8a6a4a guibg=#f0f0f0 gui=bold

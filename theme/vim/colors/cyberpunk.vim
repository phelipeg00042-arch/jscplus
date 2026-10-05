" ============================================================
" JSC$+ Theme: Cyberpunk
" ============================================================

hi clear
if exists("syntax_on")
    syntax reset
endif

set background=dark
let g:colors_name = "cyberpunk"

" Base
hi Normal          guifg=#e0e0f0 guibg=#0a0a12
hi LineNr          guifg=#5a5a7a guibg=#0a0a12
hi CursorLine      guibg=#1a1a2e
hi CursorLineNr    guifg=#ff006e guibg=#1a1a2e
hi Cursor          guifg=#0a0a12 guibg=#ff006e
hi Visual          guibg=#12121e
hi MatchParen      guifg=#ff006e guibg=#12121e
hi StatusLine      guifg=#e0e0f0 guibg=#12121e
hi StatusLineNC    guifg=#5a5a7a guibg=#0a0a12
hi VertSplit       guifg=#12121e guibg=#12121e
hi Pmenu           guifg=#e0e0f0 guibg=#12121e
hi PmenuSel        guifg=#0a0a12 guibg=#ff006e

" Sintaxe
hi Comment         guifg=#5a5a7a
hi Special         guifg=#ff006e gui=bold
hi Statement       guifg=#ff006e
hi Keyword         guifg=#ff006e
hi String          guifg=#ffbe0b
hi Number          guifg=#00f5ff
hi Float           guifg=#00f5ff
hi Boolean         guifg=#8338ec
hi Function        guifg=#3a86ff
hi Identifier      guifg=#e0e0f0
hi Operator        guifg=#e0e0f0
hi Type            guifg=#ff006e
hi Error           guifg=#ff006e guibg=#0a0a12
hi ErrorMsg        guifg=#ff006e guibg=#12121e
hi Search          guifg=#0a0a12 guibg=#8338ec
hi IncSearch       guifg=#0a0a12 guibg=#ff006e
hi Todo            guifg=#8338ec guibg=#0a0a12 gui=bold

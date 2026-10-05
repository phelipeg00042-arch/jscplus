# Tema JSC$+ para Vim — "JusSec Dark"

Esquema de cores oficial da linguagem JSC$+.

## Paleta

| Elemento | Cor | Hex |
|----------|-----|-----|
| Fundo | cinza escuro | #1a1a1a |
| Texto | creme | #e8d8c8 |
| Terminador `$+` | vermelho neon | #ff0044 |
| Palavra-chave | rosa-vermelho | #ff4d6d |
| String | laranja quente | #ffb86c |
| Número | azul claro | #7ec8e3 |
| Lógica (`a+`,`o+`,`n+`) | âmbar | #ff9f1c |
| Função nativa (`printj`) | dourado | #c9a86a |
| Comentário (`@`) | cinza apagado | #6a6560 |
| Erro | vermelho sangue | #ff2e2e |

## Instalação

```bash
mkdir -p ~/.vim/colors ~/.vim/syntax ~/.vim/ftdetect
cp jscplus.vim ~/.vim/colors/
cp syntax/jscplus.vim ~/.vim/syntax/
cp ftdetect/jscplus.vim ~/.vim/ftdetect/

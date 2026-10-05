# JSC$+

**A linguagem da liberdade.**

Livre, versátil e autêntica.

JSC$+ (lê-se "J-S-C mais" ou "Jussec") é uma linguagem de programação
imperativa, funcional e orientada a objetos, com foco em hacking,
automação e criptografia.

Integração nativa com C e Python. Dois modos de execução: interpretado
e compilado.

## Características

- **Sintaxe única** — `$+` como terminador, `>>`/`<<` como condicional, `a+`/`o+`/`n+` como lógica
- **Dois modos** — interpretado + compilado (transpilador C nativo)
- **10 módulos nativos** — math, crypto, fs, os, proc, net, hack, string, time, json
- **FFI com C** — chame qualquer função de bibliotecas C nativas
- **Python embutido** — `py.exec()`, `py.eval()`, `py.version()`
- **Threads nativas** — `spawn()`, `.join()`
- **OOP completa** — `base`, `extends`, métodos, herança
- **Syntax highlighting** — Vim + VSCode com 4 temas

## Instalação

### Pré-requisitos

- GCC
- Make
- Bibliotecas: libm, libpthread, zlib, libdl

### Debian / Ubuntu / Termux (proot)

    apt-get install -y gcc make zlib1g-dev libc6-dev libpthread-stubs0-dev

### Compilar

    git clone https://github.com/PhelipeMendes/jscplus.git
    cd jscplus
    make

Gera dois binários:

- `jsc` — interpretador
- `jsc-build` — transpilador

### Instalar (opcional)

    sudo make install

## Uso

### REPL

    ./jsc

Comandos do REPL:

| Comando | Função |
|---|---|
| `@help` | Ajuda |
| `@sintaxe` | Sintaxe |
| `@limpar` | Limpa tela |
| `@reset` | Reinicia estado |
| `@sair` | Sai |

### Executar arquivo

    ./jsc programa.jsc

### Compilar

    ./jsc-build programa.jsc

## Sintaxe básica

### Terminador

Toda instrução termina com `$+`.

    x = 10$+
    printj("oi")$+

### Tipos

    idade = 17$+             @ int
    pi = 3.14$+              @ float
    nome = "Phelipe"$+       @ string
    ativo = true$+           @ bool
    lista = [1, 2, 3]$+      @ array
    pessoa = {"nome": "P"}$+ @ map

### Operadores

    10 + 5$+                 @ aritmética
    10 > 5$+                 @ comparação
    true a+ false$+          @ AND
    true o+ false$+          @ OR
    n+ true$+                @ NOT

### Condicional

    idade >= 18 >> {
        printj("maior")$+
    } << {
        printj("menor")$+
    }$+

### Loops

    @ While
    ENQ x < 10 {
        x = x + 1$+
    }$+

    @ For range
    $> i in 1..10 {
        printj(i)$+
    }$+

    @ For each
    $>| item in lista {
        printj(item)$+
    }$+

### Funções

    @ Normal
    fn+ soma(a, b) {
        GWB a + b$+
    }$+

    @ Curto (uma linha)
    fn+ dobro(n) #> n * 2$+

### Classes

    base Pessoa {
        fn+ init(nome, idade) {
            self.nome = nome$+
            self.idade = idade$+
        }$+
    }$+

    base Aluno extends Pessoa {
        fn+ estudar() {
            printj(self.nome + " ta estudando")$+
        }$+
    }$+

## Módulos nativos

| Módulo | Função |
|---|---|
| math | Operações matemáticas |
| crypto | MD5, SHA-256, base64, UUID, XOR |
| fs | Sistema de arquivos |
| os | Sistema operacional |
| proc | Processos |
| net | Rede |
| hack | Ferramentas de hacking |
| string | Strings |
| time | Tempo e data |
| json | JSON |

## FFI com C

    libc = import_c("libc.so.6")$+
    resultado = call_c(libc, "abs", "i", -42)$+
    printj(resultado)$+

## Python embutido

    py.exec("print('ola do Python')")$+
    resultado = py.eval("2 ** 10")$+
    printj(resultado)$+

## Threads

    fn+ tarefa(x) {
        printj("thread " + x.to_str())$+
    }$+

    t1 = spawn(tarefa, 10)$+
    t1.join()$+

## Modo compilado

    ./jsc-build programa.jsc

Fluxo: lê .jsc, transpila pra C, compila com gcc, gera binário nativo.

Benchmark Fibonacci(20):

| Modo | Tempo |
|---|---|
| Interpretado | 11.981 ms |
| Compilado | 33 ms |

## Estrutura

    jscplus/
    ├── src/           @ código do interpretador
    ├── include/       @ headers
    ├── runtime/       @ runtime C
    ├── compiler/      @ transpilador
    ├── examples/      @ exemplos
    ├── theme/         @ syntax highlighting
    ├── Makefile
    ├── README.md
    ├── LICENSE
    └── .gitignore

## Como contribuir

1. Fork
2. Branch: `git checkout -b feature/minha-feature`
3. Commit: `git commit -m 'Adiciona feature X'`
4. Push: `git push origin feature/minha-feature`
5. Abra um Pull Request

## Roadmap

Curto prazo:

- [ ] Tratamento de erro no modo compilado
- [ ] `py.exec`/`py.eval` no compilado
- [ ] Histórico readline no REPL

Médio prazo:

- [ ] Async/await
- [ ] Módulos sqlite, http, websocket
- [ ] Suite de testes

Longo prazo:

- [ ] Publicação em gerenciadores de pacote
- [ ] Documentação completa
- [ ] Livro

## Licença

MIT License. Veja [LICENSE](LICENSE).

## Autor

**Phelipe Gabriel Mendes Silva**

- 17 anos
- Criador da JSC$+
- Criada em 2026
- Brasil

## Filosofia

> Livre, versátil e autêntica.

JSC$+ não tenta ser a linguagem mais rápida, nem a mais popular,
nem a mais bonita. Ela tenta ser livre — pra você fazer o que
quiser, do jeito que quiser.

Feita com café, ódio e amor.
Sem framework. Sem IDE. Sem desculpa.

---

**JSC$+ — A linguagem da liberdade.**

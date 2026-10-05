# ============================================================
# JSC$+ - Makefile
# ============================================================
# Linguagem de programação livre, versátil e autêntica
# Autor: Phelipe Gabriel Mendes Silva
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -Iinclude -g -O2
LDFLAGS = -lm -lpthread -lz -ldl

# Arquivos do interpretador
SRC = src/main.c \
      src/lexer.c \
      src/ast.c \
      src/parser.c \
      src/value.c \
      src/env.c \
      src/intern.c \
      src/eval.c \
      src/repl.c \
      src/mod_tensor.c \
      src/mod_embedding.c \
      src/mod_attention.c \
      src/mod_multihead.c \
      src/mod_block.c \
      src/mod_model.c \
      src/mod_transformer.c \
      src/mod_loss.c \
      src/mod_backprop.c \
      src/mod_backprop_ff.c \
      src/mod_backprop_ln.c \
      src/mod_backprop_attn.c \
      src/mod_optimizer.c \
      src/mod_forward_cache.c \
      src/mod_backprop_layer.c

# Arquivos do transpilador
SRC_BUILD = compiler/main_build.c \
            compiler/transpiler.c \
            src/lexer.c \
            src/parser.c \
            src/ast.c

OUT       = jsc
OUT_BUILD = jsc-build

# ============================================================
# TARGETS
# ============================================================

.PHONY: all clean install uninstall run help

all: $(OUT) $(OUT_BUILD)

# --- Interpretador ---
$(OUT): $(SRC)
	@echo "[CC] Compilando interpretador JSC\$+ ..."
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)
	@echo "[OK] $(OUT) pronto."

# --- Transpilador ---
$(OUT_BUILD): $(SRC_BUILD)
	@echo "[CC] Compilando transpilador ..."
	$(CC) $(CFLAGS) -Icompiler -Iruntime -o $(OUT_BUILD) $(SRC_BUILD) -lm
	@echo "[OK] $(OUT_BUILD) pronto."

# --- Instalação ---
install: all
	@echo "[INSTALL] Instalando em /usr/local/bin ..."
	cp $(OUT) /usr/local/bin/jsc
	cp $(OUT_BUILD) /usr/local/bin/jsc-build
	cp $(OUT) "/usr/local/bin/jsc\$+"
	@echo "[OK] Instalado."

uninstall:
	rm -f /usr/local/bin/jsc
	rm -f /usr/local/bin/jsc-build
	rm -f "/usr/local/bin/jsc\$+"
	@echo "[OK] Desinstalado."

# --- Utilitários ---
clean:
	@echo "[CLEAN] Removendo binários ..."
	rm -f $(OUT) $(OUT_BUILD)
	rm -f *.o
	@echo "[OK] Limpo."

run: $(OUT)
	@if [ -z "$(FILE)" ]; then \
		echo "Uso: make run FILE=exemplos/hello.jsc"; \
	else \
		./$(OUT) $(FILE); \
	fi

help:
	@echo ""
	@echo "JSC\$+ - Makefile"
	@echo "=================="
	@echo ""
	@echo "Targets disponíveis:"
	@echo "  make              Compila tudo (jsc + jsc-build)"
	@echo "  make clean        Remove binários"
	@echo "  make install      Instala em /usr/local/bin"
	@echo "  make uninstall    Remove instalação"
	@echo "  make run FILE=x   Roda arquivo .jsc"
	@echo "  make help         Mostra esta ajuda"
	@echo ""

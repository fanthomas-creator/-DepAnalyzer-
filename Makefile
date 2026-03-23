# ============================================================
# depanalyzer - Makefile
# Linux/macOS: make
# Windows (MinGW): make CC=gcc
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2 -Isrc/core -Isrc/parsers
TARGET  = depanalyzer
COREDIR = src/core
PARDIR  = src/parsers
OBJDIR  = build

CORE_SRCS = \
    $(COREDIR)/main.c      \
    $(COREDIR)/scanner.c   \
    $(COREDIR)/parser.c    \
    $(COREDIR)/resolver.c  \
    $(COREDIR)/analyzer.c  \
    $(COREDIR)/exporter.c

PARSER_SRCS = \
    $(PARDIR)/parser_py.c     \
    $(PARDIR)/parser_js.c     \
    $(PARDIR)/parser_ts.c     \
    $(PARDIR)/parser_c.c      \
    $(PARDIR)/parser_html.c   \
    $(PARDIR)/parser_css.c    \
    $(PARDIR)/parser_json.c   \
    $(PARDIR)/parser_php.c    \
    $(PARDIR)/parser_rb.c     \
    $(PARDIR)/parser_go.c     \
    $(PARDIR)/parser_rs.c     \
    $(PARDIR)/parser_java.c   \
    $(PARDIR)/parser_kt.c     \
    $(PARDIR)/parser_swift.c  \
    $(PARDIR)/parser_md.c     \
    $(PARDIR)/parser_yaml.c   \
    $(PARDIR)/parser_sh.c     \
    $(PARDIR)/parser_sql.c     \
    $(PARDIR)/parser_dart.c    \
    $(PARDIR)/parser_lua.c     \
    $(PARDIR)/parser_r.c       \
    $(PARDIR)/parser_scala.c   \
    $(PARDIR)/parser_cs.c      \
    $(PARDIR)/parser_vue.c     \
    $(PARDIR)/parser_svelte.c  \
    $(PARDIR)/parser_hs.c      \
    $(PARDIR)/parser_ex.c      \
    $(PARDIR)/parser_toml.c    \
    $(PARDIR)/parser_make.c    \
    $(PARDIR)/parser_docker.c  \
    $(PARDIR)/parser_gql.c     \
    $(PARDIR)/parser_proto.c   \
    $(PARDIR)/parser_tf.c      \
    $(PARDIR)/parser_nginx.c  \
    $(PARDIR)/parser_perl.c   \
    $(PARDIR)/parser_asm.c    \
    $(PARDIR)/parser_ocaml.c  \
    $(PARDIR)/parser_fsharp.c  \
    $(PARDIR)/parser_julia.c   \
    $(PARDIR)/parser_zig.c     \
    $(PARDIR)/parser_cr.c      \
    $(PARDIR)/parser_nim.c     \
    $(PARDIR)/parser_v.c       \
    $(PARDIR)/parser_groovy.c  \
    $(PARDIR)/parser_ps.c      \
    $(PARDIR)/parser_cmake.c   \
    $(PARDIR)/parser_bazel.c   \
    $(PARDIR)/parser_nix.c     \
    $(PARDIR)/parser_sol.c     \
    $(PARDIR)/parser_glsl.c    \
    $(PARDIR)/parser_erl.c     \
    $(PARDIR)/parser_clj.c     \
    $(PARDIR)/parser_cob.c     \
    $(PARDIR)/parser_f90.c

SRCS = $(CORE_SRCS) $(PARSER_SRCS)
OBJS = $(patsubst src/%.c, $(OBJDIR)/%.o, $(SRCS))

# ---- Windows detection -----------------------------------------
ifeq ($(OS),Windows_NT)
    TARGET  := $(TARGET).exe
    RM      := del /Q
    MKDIR   := mkdir
else
    RM      := rm -f
    MKDIR   := mkdir -p
endif

# ---- Targets ---------------------------------------------------

all: $(OBJDIR)/core $(OBJDIR)/parsers $(TARGET)

$(OBJDIR)/core:
	$(MKDIR) $(OBJDIR)/core

$(OBJDIR)/parsers:
	$(MKDIR) $(OBJDIR)/parsers

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Build successful: $(TARGET)"

$(OBJDIR)/core/%.o: $(COREDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/parsers/%.o: $(PARDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(OBJS) $(TARGET)
	@echo "Cleaned."

test: all
	@echo "--- Running test on ./tests/sample_project ---"
	./$(TARGET) tests/sample_project --stats --output tests/output.json
	@echo "--- Output: tests/output.json ---"

.PHONY: all clean test

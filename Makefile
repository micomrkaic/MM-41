# --- portability knobs (Linux + macOS) ---
SHELL      := /bin/sh
APP        ?= mm41

# Build mode: release | asan | ubsan | harden
MODE       ?= release

# Default to user-local install to avoid sudo (/usr/local still available via install-system)
PREFIX     ?= $(HOME)/.local
BINDIR     ?= $(PREFIX)/bin
MANDIR     ?= $(PREFIX)/share/man/man1
DOCDIR     ?= $(PREFIX)/share/doc/mm41
DOCS        = README.md MANUAL.md COMMANDS.md CHANGES.md LICENSE doc/mm41-manual.pdf

# Respect XDG on Linux; fall back to ~/.config on macOS/Linux
CONFIG_HOME  ?= $(if $(XDG_CONFIG_HOME),$(XDG_CONFIG_HOME),$(HOME)/.config)
APP_CONF_DIR := $(CONFIG_HOME)/$(APP)

INSTALL    ?= install
MKDIR_P    ?= mkdir -p

# Config files live in data/
DATA_DIR := data
CONFIG_FILES := config.txt
CONFIG_SRC  := $(addprefix $(DATA_DIR)/,$(CONFIG_FILES))
WORD_MODULES := $(wildcard $(DATA_DIR)/modules/*.mm)

# Compiler and base flags
CC       ?= gcc
CFLAGS   ?= -g -std=c17 -Wall -Wextra -Werror -Wpedantic -Iinclude -fno-common
LDFLAGS  ?= -rdynamic   # export symbols to shared-object modules
LDLIBS   ?= -lgsl -lgslcblas -lreadline -lm -ldl

# Auto-deps: generate .d files per source (write alongside .o)
DEPFLAGS = -MMD -MP -MF $(@:.o=.d) -MT $@

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  CFLAGS  += -I/opt/homebrew/opt/gsl/include
  LDFLAGS += -L/opt/homebrew/opt/gsl/lib
endif

# Directories (mode-specific outputs)
SRC_DIR = src
INC_DIR = include
BIN_DIR = bin/$(MODE)
OBJ_DIR = build/$(MODE)

# --- Git version header (reliable + rebuilds when git state changes) ---
GIT_HEADER := $(OBJ_DIR)/git_version.h
GIT_HASH   := $(shell git rev-parse --short HEAD 2>/dev/null)
GIT_DIRTY  := $(shell git diff-index --quiet HEAD -- 2>/dev/null || echo -dirty)
GIT_VERSION:= $(GIT_HASH)$(GIT_DIRTY)

# Force-include the generated header in every compile
CPPFLAGS += -include $(GIT_HEADER)


# Executable (mode-specific)
TARGET = $(BIN_DIR)/$(APP)

# -------- Modules --------
# Each built-in module is a directory modules/<name>/ exporting one `Module`
# struct (see include/module.h). MODULES chooses which are compiled in;
# the registry table is generated from the list. The core is always built.
MODULES ?= complex special matrices numerics strings registers dates programs
MODULE_SRCS := $(foreach m,$(MODULES),$(wildcard modules/$(m)/*.c))
MODULE_TABLE := $(OBJ_DIR)/module_table.c
CFLAGS += $(foreach m,$(MODULES),-Imodules/$(m))

# Sources / objects / deps
SRCS := $(wildcard $(SRC_DIR)/*.c) $(MODULE_SRCS)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(wildcard $(SRC_DIR)/*.c)) \
        $(patsubst modules/%.c,$(OBJ_DIR)/modules/%.o,$(MODULE_SRCS)) \
        $(OBJ_DIR)/module_table.o
DEPS := $(OBJS:.o=.d)

# -------- Mode flags (applied only when MODE matches) --------
ifeq ($(MODE),asan)
  CFLAGS  += -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined
  LDFLAGS += -fsanitize=address,undefined
endif

ifeq ($(MODE),ubsan)
  CFLAGS  += -O1 -g3 -fno-omit-frame-pointer -fsanitize=undefined
  LDFLAGS += -fsanitize=undefined
endif

ifeq ($(MODE),harden)
  CFLAGS  += -O1 -g3 -D_FORTIFY_SOURCE=2 -fno-omit-frame-pointer \
             -Wformat=2 -Wformat-truncation -Wformat-overflow \
             -Wstringop-overflow=2 -Wstringop-truncation \
             -Warray-bounds=2 -Wshadow -Wcast-qual -Wwrite-strings \
             -Wstrict-prototypes -Wmissing-prototypes -Wvla
endif

# -------- Debug/safety convenience targets --------
.PHONY: asan ubsan harden scan-unsafe test-asan

.PHONY: default
default: MODE=release
default: all

asan:
	$(MAKE) MODE=asan all

ubsan:
	$(MAKE) MODE=ubsan all

harden:
	$(MAKE) MODE=harden all

# Quick triage: grep for unsafe string APIs (ripgrep required)
scan-unsafe:
	@echo "Scanning for high-risk string calls..."
	@rg -n '\b(strcpy|strcat|sprintf|vsprintf|gets)\b' $(SRC_DIR) $(INC_DIR) || true
	@echo
	@echo "Scanning for medium-risk calls (audit each use)..."
	@rg -n '\b(strncpy|strncat|snprintf|vsnprintf|memcpy|memmove)\b' $(SRC_DIR) $(INC_DIR) || true

# Abuse tests under ASan build
test-asan: asan
	@echo "Running ASan abuse tests..."
	@python3 -c 'import random,string; \
	[print("".join(random.choice(string.printable) for _ in range(random.randint(0,4000)))) for __ in range(2000)]' \
	| "bin/asan/$(APP)" >/dev/null || true

	@python3 -c 'import random,string; [print("".join(random.choice(string.printable) for _ in range(random.randint(0,4000)))) for __ in range(2000)]' \
	| "bin/asan/$(APP)" >/dev/null || true

	@echo "Done. If ASan found anything, you already saw the stack trace."

# -------- Functional test harness --------
# `make test` builds the ASan binary and runs tests/run_tests.sh against it,
# so every functional test also gets leak/UB checking for free. Exit status
# is nonzero if any case fails, so this is CI-usable.
.PHONY: test
test: asan examples/modules/hello/hello.so
	@bash tests/run_tests.sh
	@ASAN_OPTIONS=detect_leaks=0 sh tests/run_golden.sh bin/asan/mm41

golden: all examples/modules/hello/hello.so
	@sh tests/run_golden.sh $(TARGET)

examples/modules/hello/hello.so: examples/modules/hello/hello.c include/module.h include/ops.h
	$(MAKE) --no-print-directory shared-module DIR=examples/modules/hello

# -------- Rules --------
.PHONY: all install install-system uninstall clean doc docs manual

# Regenerate git version header when HEAD or index changes
# Rebuild when HEAD changes (new commit) or refs change (branch moves / packed-refs updates)
$(GIT_HEADER): .git/HEAD .git/packed-refs
	@$(MKDIR_P) "$(@D)"
	@ver=$$(git rev-parse --short HEAD 2>/dev/null || echo unknown); \
	  dirty=$$(git diff --no-ext-diff --quiet --ignore-submodules -- 2>/dev/null || echo -dirty); \
	  tmp="$@.tmp"; \
	  { printf '%s\n' '#pragma once' "#define VERSION \"$$ver$$dirty\""; } > "$$tmp"; \
	  if [ ! -f "$@" ] || ! cmp -s "$$tmp" "$@"; then mv "$$tmp" "$@"; else rm -f "$$tmp"; fi

# $(GIT_HEADER): .git/HEAD .git/index
# 	@$(MKDIR_P) "$(@D)"
# 	@printf '%s\n' \
# 	  '#pragma once' \
# 	  '#define VERSION "$(GIT_VERSION)"' \
# 	  > "$@"

all: $(TARGET)

# Link
$(TARGET): $(OBJS)
	@$(MKDIR_P) "$(@D)"
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Compile (+ auto header deps)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(GIT_HEADER)
	@$(MKDIR_P) "$(@D)"
	$(CC) $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/modules/%.o: modules/%.c $(GIT_HEADER)
	@$(MKDIR_P) "$(@D)"
	$(CC) $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS) -c $< -o $@

# The registry table: the core first, then the modules in MODULES order.
# Regenerated on every run but only touched when the list changes.
.PHONY: FORCE
$(MODULE_TABLE): FORCE
	@$(MKDIR_P) "$(@D)"
	@{ echo '/* generated by make from MODULES="$(MODULES)" */'; \
	   echo '#include "module.h"'; \
	   echo 'extern const Module mm41_mod_core;'; \
	   for m in $(MODULES); do echo "extern const Module mm41_mod_$$m;"; done; \
	   echo 'const Module *const mm41_builtin_modules[] = {'; \
	   echo '  &mm41_mod_core,'; \
	   for m in $(MODULES); do echo "  &mm41_mod_$$m,"; done; \
	   echo '  NULL'; echo '};'; } > $@.tmp; \
	if [ ! -f "$@" ] || ! cmp -s "$@.tmp" "$@"; then mv "$@.tmp" "$@"; else rm -f "$@.tmp"; fi

$(OBJ_DIR)/module_table.o: $(MODULE_TABLE) $(GIT_HEADER)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS) -c $< -o $@

# Pull in auto-generated header dependencies (safe if missing)
-include $(DEPS)

# --- User-local install (default) ---
# Default installs release to avoid accidentally installing ASan builds.
install: MODE=release
install: $(TARGET)
	@echo "Installing $(APP) ($(MODE)) binary to $(DESTDIR)$(BINDIR)"
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(MANDIR)" "$(DESTDIR)$(DOCDIR)"
	$(INSTALL) -m 755 "$(TARGET)" "$(DESTDIR)$(BINDIR)/$(APP)"
	$(INSTALL) -m 644 mm41.1 "$(DESTDIR)$(MANDIR)/mm41.1"
	$(INSTALL) -m 644 $(DOCS) "$(DESTDIR)$(DOCDIR)/"
	@case ":$$PATH:" in *":$(BINDIR):"*) ;; *) echo "note: $(BINDIR) is not in your PATH" ;; esac

	@echo "Installing config to $(APP_CONF_DIR)"
	$(INSTALL) -d "$(APP_CONF_DIR)"
	@for f in $(CONFIG_SRC); do \
	  base=$$(basename "$$f"); \
	  if [ ! -f "$(APP_CONF_DIR)/$$base" ]; then \
	    echo "  -> $$base (new)"; \
	    $(INSTALL) -m 644 "$$f" "$(APP_CONF_DIR)/$$base"; \
	  else \
	    echo "  -> $$base exists; keeping user's copy"; \
	  fi; \
	done
	@echo "Installing word modules to $(APP_CONF_DIR)/modules (shipped modules are replaced)"
	$(INSTALL) -d "$(APP_CONF_DIR)/modules"
	@for f in $(WORD_MODULES); do \
	  echo "  -> modules/$$(basename "$$f")"; \
	  $(INSTALL) -m 644 "$$f" "$(APP_CONF_DIR)/modules/"; \
	done
	@if [ -f "$(APP_CONF_DIR)/predefined_macros.txt" ]; then \
	  echo "  note: $(APP_CONF_DIR)/predefined_macros.txt is no longer read; its words are now modules/std.mm"; \
	fi

# --- System-wide install (optional): `make install-system` ---
install-system: PREFIX=/usr/local
install-system: BINDIR=$(PREFIX)/bin
install-system: install

uninstall:
	@echo "Removing $(DESTDIR)$(BINDIR)/$(APP), the man page and $(DESTDIR)$(DOCDIR)"
	@rm -f "$(DESTDIR)$(BINDIR)/$(APP)" "$(DESTDIR)$(MANDIR)/mm41.1"
	@rm -rf "$(DESTDIR)$(DOCDIR)"
	@echo "Left $(APP_CONF_DIR) in place (user data)."

# Regenerate the command reference from the dispatch table.
docs: $(TARGET)
	./$(TARGET) --commands > COMMANDS.md
	./$(TARGET) --commands-tex > doc/commands.tex

# Build a compiled module as a shared object: make shared-module DIR=path/to/module
# (all .c files in DIR -> DIR/<basename>.so). Load it with "<path>.so" load, or
# drop it into $(APP_CONF_DIR)/modules/ to have it loaded at startup.
.PHONY: shared-module
shared-module:
	@test -n "$(DIR)" || { echo "usage: make shared-module DIR=path/to/module"; exit 2; }
	$(CC) -std=c17 -Wall -Wextra -O2 -fPIC -shared -Iinclude -o "$(DIR)/$$(basename "$(DIR)").so" $(DIR)/*.c -lm

# The user manual: doc/manual.tex + the generated command tables -> doc/mm41-manual.pdf.
# Needs LuaLaTeX (or XeLaTeX: LATEX=xelatex) with fontspec, booktabs, longtable, tcolorbox.
LATEX ?= lualatex
manual: docs
	cd doc && latexmk -$(LATEX) -interaction=nonstopmode -halt-on-error manual.tex
	cp doc/manual.pdf doc/mm41-manual.pdf
	cd doc && latexmk -c manual.tex >/dev/null 2>&1 || true

clean:
	rm -rf build bin

doc:
	doxygen Doxyfile

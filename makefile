# ===== Config =====
APP ?= tetris           # output name (change if you like)
SRC ?= tetris.c         # your C source file (e.g., iceburger_tetris.c)

CC ?= gcc
CSTD ?= -std=c11
WARN ?= -Wall -Wextra
OPT ?= -O2

PKGCONF ?= pkg-config
PKGS := sdl2 SDL2_ttf

# Try pkg-config first
PKG_CFLAGS := $(shell $(PKGCONF) --cflags $(PKGS) 2>/dev/null)
PKG_LIBS   := $(shell $(PKGCONF) --libs   $(PKGS) 2>/dev/null)

# Fallback include/lib paths if pkg-config not found or not configured
# (Homebrew/Intel macs commonly use /usr/local; Apple Silicon uses /opt/homebrew)
FALLBACK_INC := -I/opt/homebrew/include -I/usr/local/include
FALLBACK_LIB := -L/opt/homebrew/lib -L/usr/local/lib -lSDL2 -lSDL2_ttf

CFLAGS ?= $(OPT) $(WARN) $(CSTD)
CFLAGS += $(PKG_CFLAGS)
LDFLAGS +=
LIBS := $(if $(strip $(PKG_LIBS)),$(PKG_LIBS),$(FALLBACK_LIB))

UNAME_S := $(shell uname -s)

# Linux needs -lm for sinf/cosf/etc. (macOS links libm via libSystem)
ifeq ($(UNAME_S),Linux)
  LIBS += -lm
endif

# ===== Targets =====
.PHONY: all run clean debug sanitize

all: $(APP)

$(APP): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

run: $(APP)
	./$(APP)

clean:
	$(RM) $(APP) *.o

# Debug build (symbols, no optimizations)
debug: CFLAGS := -g -O0 $(WARN) $(CSTD) $(PKG_CFLAGS)
debug: $(APP)

# Address/UB sanitizer build (development only)
sanitize: CFLAGS := -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) $(CSTD) $(PKG_CFLAGS)
sanitize: $(APP)

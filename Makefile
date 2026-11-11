CC ?= gcc
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ARCH_FLAGS :=
ifeq ($(filter x86_64 amd64,$(UNAME_M)),$(UNAME_M))
  AVX2_OK := $(shell $(CC) -mavx2 -x c -E /dev/null >/dev/null 2>&1 && echo 1)
  ifeq ($(AVX2_OK),1)
    ARCH_FLAGS += -mavx2
  endif
endif

CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -DAKS_RELEASE_BUILD -DNDEBUG -Iinclude $(ARCH_FLAGS) -fPIC
DEPFLAGS = -MMD -MP
LIBS = -lm -lpthread -ldl

ifeq ($(UNAME_S),Darwin)
  SHLIB_EXT = dylib
  SHLIB_FLAG = -dynamiclib -Wl,-install_name,@rpath/libaks.dylib
else
  SHLIB_EXT = so
  SHLIB_FLAG = -shared
endif

COMMON_SRCS := \
	src/prelude.c \
	src/bind.c \
	src/dap.c \
	src/aot.c \
	src/ast.c \
	src/compiler.c \
	src/cross.c \
	src/crypto_custom.c \
	src/jitllvm.c \
	src/jitqbe.c \
	src/jitx64.c \
	src/jitarm64.c \
	src/learning.c \
	src/lexer.c \
	src/parser.c \
	src/pkg.c \
	src/value.c \
	src/vm.c \
	src/vm_sanitizer.c \
	src/stdlib_core.c \
	src/stdlib_collections.c \
	src/stdlib_math_simd.c \
	src/stdlib_crypto_db.c \
	src/stdlib_autodiff.c \
	src/stdlib_os_net.c \
	src/typer.c \
	src/staticcodegen.c \
	src/wasmcodegen.c \
	src/nostdcodegen.c \
	src/bench.c \
	src/direct_linker.c \
	src/diagnostic.c

COMMON_OBJS := $(patsubst src/%.c,obj/%.o,$(COMMON_SRCS))
DEPS := $(COMMON_OBJS:.o=.d)

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
LIBDIR = $(PREFIX)/lib

.PHONY: all clean test install

all: aks aksls aksfmt akslint libaks.$(SHLIB_EXT) tests/TestProtocols

obj/%.o: src/%.c
	@mkdir -p obj tests
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

aks: src/main.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) src/main.c $(COMMON_OBJS) $(LIBS) -o $@

aksls: src/lsp.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) src/lsp.c $(COMMON_OBJS) $(LIBS) -o $@

aksfmt: src/aksfmt.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@

akslint: src/akslint.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@

libaks.$(SHLIB_EXT): src/aks.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $(SHLIB_FLAG) -DAKS_BUILD_DLL src/aks.c $(COMMON_OBJS) $(LIBS) -o $@

tests/TestProtocols: tests/TestProtocols.c
	@mkdir -p tests
	$(CC) $(CFLAGS) $< $(LIBS) -o $@

test: all
	bash test.sh

install: all
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR)
	install -m 755 aks aksls aksfmt akslint $(DESTDIR)$(BINDIR)/
	install -m 755 libaks.$(SHLIB_EXT) $(DESTDIR)$(LIBDIR)/

clean:
	rm -rf obj aks aksls aksfmt akslint libaks.so libaks.dylib tests/TestProtocols

-include $(DEPS)


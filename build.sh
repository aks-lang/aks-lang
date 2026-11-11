#!/usr/bin/env bash
# Cross-platform POSIX build script for AKS Programming Language (Linux & macOS)
set -euo pipefail

CC="${CC:-gcc}"
UNAME_S="$(uname -s)"
UNAME_M="$(uname -m)"

ARCH_FLAGS=""
if [ "$UNAME_M" = "x86_64" ] || [ "$UNAME_M" = "amd64" ]; then
    if $CC -mavx2 -x c -E /dev/null >/dev/null 2>&1; then
        ARCH_FLAGS="-mavx2"
    fi
fi

CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -DAKS_RELEASE_BUILD -DNDEBUG -Iinclude $ARCH_FLAGS -fPIC"
LIBS="-lm -lpthread -ldl"
SHLIB_EXT="so"
SHLIB_FLAG="-shared"
if [ "$UNAME_S" = "Darwin" ]; then
    SHLIB_EXT="dylib"
    SHLIB_FLAG="-dynamiclib -Wl,-install_name,@rpath/libaks.dylib"
fi

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
mkdir -p obj tests

COMMON_SRCS=(
    src/prelude.c
    src/bind.c
    src/dap.c
    src/aot.c
    src/ast.c
    src/compiler.c
    src/cross.c
    src/crypto_custom.c
    src/jitllvm.c
    src/jitqbe.c
    src/jitx64.c
    src/jitarm64.c
    src/learning.c
    src/lexer.c
    src/parser.c
    src/pkg.c
    src/value.c
    src/vm.c
    src/vm_sanitizer.c
    src/stdlib_core.c
    src/stdlib_collections.c
    src/stdlib_math_simd.c
    src/stdlib_crypto_db.c
    src/stdlib_autodiff.c
    src/stdlib_os_net.c
    src/typer.c
    src/staticcodegen.c
    src/wasmcodegen.c
    src/nostdcodegen.c
    src/bench.c
    src/direct_linker.c
    src/diagnostic.c
)

COMMON_OBJS=()
PIDS=()
echo "Compiling shared runtime objects ($CC, $UNAME_S/$UNAME_M, -j$JOBS)..."
for src in "${COMMON_SRCS[@]}"; do
    base="$(basename "$src" .c)"
    obj="obj/${base}.o"
    $CC $CFLAGS -c "$src" -o "$obj" &
    PIDS+=($!)
    COMMON_OBJS+=("$obj")
    if [ "${#PIDS[@]}" -ge "$JOBS" ]; then
        for pid in "${PIDS[@]}"; do
            wait "$pid"
        done
        PIDS=()
    fi
done
for pid in "${PIDS[@]}"; do
    wait "$pid"
done

echo "Linking aks..."
$CC $CFLAGS src/main.c "${COMMON_OBJS[@]}" $LIBS -o aks

echo "Linking aksls..."
$CC $CFLAGS src/lsp.c "${COMMON_OBJS[@]}" $LIBS -o aksls

echo "Building aksfmt..."
$CC $CFLAGS src/aksfmt.c $LIBS -o aksfmt

echo "Building akslint..."
$CC $CFLAGS src/akslint.c $LIBS -o akslint

echo "Linking libaks.${SHLIB_EXT}..."
$CC $CFLAGS $SHLIB_FLAG -DAKS_BUILD_DLL src/aks.c "${COMMON_OBJS[@]}" $LIBS -o "libaks.${SHLIB_EXT}"

echo "Building test helpers..."
$CC $CFLAGS tests/TestProtocols.c $LIBS -o tests/TestProtocols

echo "POSIX build completed successfully."


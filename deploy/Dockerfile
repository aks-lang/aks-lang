# AKS Interactive Playground Dockerfile
# Production image for play.aks-lang.org

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    clang \
    git \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN gcc -O3 -w \
    src/main.c src/prelude.c src/bind.c src/dap.c src/aot.c src/ast.c \
    src/compiler.c src/cross.c src/crypto_custom.c src/jitllvm.c src/jitqbe.c \
    src/jitx64.c src/jitarm64.c src/learning.c src/lexer.c src/parser.c \
    src/pkg.c src/value.c src/vm.c src/typer.c src/staticcodegen.c \
    src/wasmcodegen.c src/nostdcodegen.c src/bench.c src/direct_linker.c \
    src/diagnostic.c \
    -Iinclude -lm -lpthread -ldl \
    -o /build/aks

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash aksuser
USER aksuser
WORKDIR /home/aksuser

COPY --from=builder /build/aks /usr/local/bin/aks

EXPOSE 8000

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
  CMD wget --no-verbose --tries=1 --spider http://localhost:8000/health || exit 1

ENTRYPOINT ["aks", "playground", "8000"]


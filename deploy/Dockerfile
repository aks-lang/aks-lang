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

RUN bash build.sh && cp /build/aks /build/aks_bin && mv /build/aks_bin /build/aks

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


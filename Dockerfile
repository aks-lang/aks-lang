# Dockerfile for AKS Runtime (aksruntime:latest)
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN bash build.sh

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    curl \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/aks /usr/local/bin/aks
COPY --from=builder /src/aksls /usr/local/bin/aksls
COPY --from=builder /src/aksfmt /usr/local/bin/aksfmt
COPY --from=builder /src/akslint /usr/local/bin/akslint
COPY --from=builder /src/libaks.so /usr/local/lib/libaks.so
RUN ldconfig

WORKDIR /workspace

ENTRYPOINT ["aks"]

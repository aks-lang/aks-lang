# Dockerfile for AKS Runtime (aksruntime:latest)
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    clang \
    curl \
    git \
    && rm -rf /var/lib/apt/lists/*

# Install AKS compiler and runtime binary
COPY aks /usr/local/bin/aks
COPY aksls /usr/local/bin/aksls

WORKDIR /workspace

ENTRYPOINT ["aks"]

#!/usr/bin/env bash
# Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
# Standalone POSIX Installer for AKS Programming Language

set -euo pipefail

BOLD="\033[1m"
GREEN="\033[1;32m"
CYAN="\033[1;36m"
YELLOW="\033[1;33m"
RED="\033[1;31m"
RESET="\033[0m"

REPO_OWNER="aks-lang"
REPO_NAME="aks-lang"
if [ -f "$(dirname "$0")/VERSION" ]; then
    DEFAULT_TAG="v$(cat "$(dirname "$0")/VERSION" | tr -d '[:space:]')"
else
    DEFAULT_TAG="${AKS_VERSION:-v1.2.3}"
fi
VERSION="${AKS_VERSION:-$DEFAULT_TAG}"

INSTALL_DIR="${AKS_INSTALL_DIR:-$HOME/.aks}"
BIN_DIR="$INSTALL_DIR/bin"
TARGET_EXE="$BIN_DIR/aks"

print_header() {
    printf "${CYAN}${BOLD}"
    cat << EOF
    █████╗ ██╗  ██╗███████╗
   ██╔══██╗██║ ██╔╝██╔════╝   AKS Programming Language
   ███████║█████╔╝ ███████╗   High-Performance Systems Language
   ██╔══██║██╔═██╗ ╚════██║   Version ${VERSION#v} (POSIX One-Line Installer)
   ██║  ██║██║  ██╗███████║   https://github.com/aks-lang/aks-lang
   ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝
EOF
    printf "${RESET}\n"
}

detect_platform() {
    OS="$(uname -s)"
    ARCH="$(uname -m)"

    case "$OS" in
        Linux*)  PLATFORM="linux" ;;
        Darwin*) PLATFORM="darwin" ;;
        *)       PLATFORM="unknown" ;;
    esac

    case "$ARCH" in
        x86_64|amd64)   ARCH_NAME="x64" ;;
        aarch64|arm64)  ARCH_NAME="arm64" ;;
        *)              ARCH_NAME="unknown" ;;
    esac
}

download_file() {
    local url="$1"
    local dest="$2"

    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$dest"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$dest" "$url"
    else
        printf "${RED}Error: Neither curl nor wget was found in PATH.${RESET}\n" >&2
        exit 1
    fi
}

main() {
    print_header
    detect_platform

    printf "${BOLD}Installing AKS Programming Language (${VERSION})...${RESET}\n"
    printf "  Detected OS:   ${GREEN}%s${RESET}\n" "$OS"
    printf "  Architecture:  ${GREEN}%s${RESET}\n" "$ARCH"
    printf "  Install path:  ${CYAN}%s${RESET}\n\n" "$INSTALL_DIR"

    mkdir -p "$BIN_DIR"

    # Attempt release binary download from GitHub
    ASSET_NAME="aks-${PLATFORM}-${ARCH_NAME}"
    DOWNLOAD_URL="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download/${VERSION}/${ASSET_NAME}"

    printf "${CYAN}--> Fetching runtime binary...${RESET}\n"
    
    DOWNLOAD_SUCCESS=0
    if download_file "$DOWNLOAD_URL" "$TARGET_EXE" 2>/dev/null; then
        DOWNLOAD_SUCCESS=1
    fi

    # Fallback to local source compilation if binary asset not on release yet
    if [ "$DOWNLOAD_SUCCESS" -ne 1 ]; then
        if [ -f "./src/main.c" ] && command -v gcc >/dev/null 2>&1; then
            printf "${YELLOW}Pre-built binary asset not reached. Compiling directly from local workspace...${RESET}\n"
            if [ -f "./build.sh" ]; then
                bash ./build.sh
                if [ -f "./aks" ]; then
                    cp ./aks "$TARGET_EXE"
                    [ -f "./aksls" ] && cp ./aksls "$BIN_DIR/aksls" && chmod +x "$BIN_DIR/aksls"
                    [ -f "./aksfmt" ] && cp ./aksfmt "$BIN_DIR/aksfmt" && chmod +x "$BIN_DIR/aksfmt"
                    [ -f "./akslint" ] && cp ./akslint "$BIN_DIR/akslint" && chmod +x "$BIN_DIR/akslint"
                    DOWNLOAD_SUCCESS=1
                fi
            elif [ -f "./Makefile" ] && command -v make >/dev/null 2>&1; then
                make -j aks
                if [ -f "./aks" ]; then
                    cp ./aks "$TARGET_EXE"
                    DOWNLOAD_SUCCESS=1
                fi
            fi
        fi
    fi

    if [ ! -f "$TARGET_EXE" ]; then
        printf "${RED}Error: Could not obtain or compile aks binary for %s-%s.${RESET}\n" "$PLATFORM" "$ARCH_NAME" >&2
        printf "Please check GitHub release assets at: https://github.com/%s/%s/releases/tag/%s\n" "$REPO_OWNER" "$REPO_NAME" "$VERSION" >&2
        exit 1
    fi

    chmod +x "$TARGET_EXE"
    printf "  ${GREEN}[OK]${RESET} Installed binary to: ${BOLD}%s${RESET}\n\n" "$TARGET_EXE"

    # Configure Shell PATH environment
    SHELL_PROFILE=""
    if [ -n "${ZSH_VERSION:-}" ] || [ "$(basename "${SHELL:-}")" = "zsh" ]; then
        SHELL_PROFILE="$HOME/.zshrc"
    elif [ -n "${BASH_VERSION:-}" ] || [ "$(basename "${SHELL:-}")" = "bash" ]; then
        SHELL_PROFILE="$HOME/.bashrc"
    else
        SHELL_PROFILE="$HOME/.profile"
    fi

    PATH_EXPORT="export PATH=\"$BIN_DIR:\$PATH\""
    if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
        if [ -n "$SHELL_PROFILE" ] && [ -f "$SHELL_PROFILE" ]; then
            if ! grep -q "$BIN_DIR" "$SHELL_PROFILE" 2>/dev/null; then
                printf "\n# AKS Programming Language Toolchain\n%s\n" "$PATH_EXPORT" >> "$SHELL_PROFILE"
                printf "  ${GREEN}[OK]${RESET} Added PATH configuration to: ${BOLD}%s${RESET}\n" "$SHELL_PROFILE"
            fi
        elif [ -n "$SHELL_PROFILE" ]; then
            printf "# AKS Programming Language Toolchain\n%s\n" "$PATH_EXPORT" > "$SHELL_PROFILE"
            printf "  ${GREEN}[OK]${RESET} Created PATH configuration in: ${BOLD}%s${RESET}\n" "$SHELL_PROFILE"
        fi
    fi

    printf "\n${GREEN}${BOLD}Installation Completed Successfully!${RESET}\n"
    printf "AKS Programming Language is ready to use from your terminal.\n\n"
    printf "${BOLD}Quick Start Commands:${RESET}\n"
    printf "  ${CYAN}aks --version${RESET}       Verify compiler and JIT runtime installation\n"
    printf "  ${CYAN}aks -e 'print(1+2)'${RESET} Run quick expression directly\n"
    printf "  ${CYAN}aks tour${RESET}            Launch interactive hands-on terminal walkthrough\n"
    printf "  ${CYAN}aks run <file>${RESET}      Compile and execute an AKS program\n\n"
    printf "To reload your current shell environment, run:\n"
    printf "  ${YELLOW}export PATH=\"%s:\$PATH\"${RESET}\n\n" "$BIN_DIR"
}

main "$@"


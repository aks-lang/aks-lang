# AKS for Neovim

Full-featured Neovim plugin for the [AKS Programming Language](https://github.com/aks-lang/aks-lang).

## Features

- **Filetype Detection**: Automatically associates `*.aks` and `*.aksb` files
- **Syntax Highlighting**: Comprehensive syntax support for all language keywords, contracts, effects, and concurrency primitives
- **Language Server (LSP)**: First-class integration with `aksls`
- **Commands**: Built-in `:AksRun`, `:AksTest`, `:AksFmt`, and `:AksLint`
- **Health Check**: Run `:checkhealth aks` to diagnose toolchain availability

## Installation

### lazy.nvim

```lua
{
    "aks-lang/aks-lang",
    ft = { "aks" },
    config = function()
        require("aks").setup({
            settings = {
                aks = {
                    formatOnSave = false,
                    borrowChecker = true,
                },
            },
        })
    end,
}
```

### packer.nvim

```lua
use {
    "aks-lang/aks-lang",
    rtp = "editors/neovim",
    config = function()
        require("aks").setup()
    end,
}
```

## Commands

- `:AksRun [file]` - Run current file or specified script
- `:AksTest [file|dir]` - Execute test suite with `aks test`
- `:AksFmt [file]` - Format source code using `aksfmt`
- `:AksLint [file]` - Lint source code using `akslint`
- `:checkhealth aks` - Verify compiler and language server setup

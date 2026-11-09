# tree-sitter-aks

Tree-sitter grammar and syntax highlighter for the [AKS Programming Language](https://github.com/aks-lang/aks-lang).

## Features

- Full coverage of AKS language syntax:
  - Functions, closures, trailing lambdas, decorators (`@decorator`)
  - Parametric polymorphism and generics (`fn Identity<T>(x: T) -> T`, `struct Box<T>`)
  - Traits and interfaces (`trait Printable { fn print(); }`, `dyn Printable`)
  - Pattern matching (`match (v) { (a, b) => ..., { x, y } => ... }`)
  - Parameter pattern destructuring (`fn foo((a, b), { x, y })`)
  - Concurrency primitives (`async`, `await`, `spawn`)
  - Built-in type system (primitives, tuples, arrays, sum types/enums)
  - Comptime type-level metaprogramming blocks (`comptime { ... }`)
- Syntax highlighting queries (`queries/highlights.scm`) for Neovim, Helix, and Zed.

## Development & Testing

Ensure `tree-sitter-cli` and `node` are installed:

```bash
npm install
npm run build
npm test
```

## Editor Integration

### Neovim (nvim-treesitter)

Add to your Neovim `init.lua`:

```lua
local parser_config = require("nvim-treesitter.parsers").get_parser_configs()
parser_config.aks = {
  install_info = {
    url = "https://github.com/aks-lang/aks-lang",
    files = {"tree-sitter-aks/src/parser.c"},
    branch = "main",
    generate_requires_npm = false,
    requires_generate_from_grammar = false,
  },
  filetype = "aks",
}
```

### Helix

Add to `languages.toml`:

```toml
[[language]]
name = "aks"
scope = "source.aks"
injection-regex = "aks"
file-types = ["aks"]
roots = ["aks.json"]
comment-token = "//"
indent = { tab-width = 4, unit = "    " }

[[grammar]]
name = "aks"
source = { git = "https://github.com/aks-lang/aks-lang", subpath = "tree-sitter-aks" }
```

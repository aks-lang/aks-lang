# Appendix C: Compiler Flags, CLI Options & Diagnostics Reference

This appendix provides a comprehensive reference for the AKS command-line driver (`aks.exe`), compilation flags, diagnostic error codes, and runtime environment options.

---

## C.1 Command-Line Interface (CLI) Syntax

```text
Usage:
  aks <command> [options] [arguments]

Commands:
  run <file>         Executes an AKS source script using the virtual machine.
  repl               Launches the interactive Read-Eval-Print Loop.
  test [filter]      Executes the automated regression test suite.
  bench <file>       Runs execution benchmarks with microsecond hardware timing.
  fmt <file>         Formats AKS source code in-place according to style standards.
  lint <file>        Performs static analysis to detect bugs and code smells.
  emit-ir <file>     Emits unboxed LLVM Intermediate Representation (.ll).
  emit-wat <file>    Emits WebAssembly Text representation (.wat).
  pkg <subcommand>   Manages packages and dependencies (init, install, pack).
  debug <file>       Launches script under the DAP interactive debugger.
  help [topic]       Displays built-in documentation across 12 systems topics.
  examples [action]  Browses and copies from 13 curated real-world system examples.
  tutorial [action]  Runs 8 progressive interactive lessons with automated verification.
  tour [step]        Guided 7-step interactive language architecture tour.
  explain <code>     Explains borrow checker diagnostics (E0382..E0596) and errors.
  cheatsheet [out]   Generates the master markdown syntax and standard library cheatsheet.
  playground [port]  Starts local dark-mode interactive web IDE and execution console.
  doc <in> [out]     Generates API markdown documentation from doc-comments (///).
  highlight <in>     Syntax highlights AKS code in ANSI terminal or standalone HTML.
  migrate <file>     Upgrades legacy syntax patterns automatically.
```

---

## C.2 Compiler & Runtime Flags

| Flag | Category | Description |
| :--- | :--- | :--- |
| `--strict-types` | Type Safety | Activates Hindley-Milner type inference, rejecting ill-typed code at compile time. |
| `--borrow-check` | Memory Safety | Activates affine borrow checker, validating ownership and reference lifetimes. |
| `--static` | AOT Compilation | Enables strict static mode, monomorphization, and direct unboxed LLVM IR generation. |
| `--trace-mem` | Memory Diagnostics | Enables allocation tracking, verifying clean reference deallocation and 0 memory leaks on exit. |
| `--debug` | Debugger | Automatically breaks into the DAP debugger upon encountering an unhandled exception or panic. |
| `--watch` | Hot Reload | Watches target file and imported dependencies, re-executing automatically on file change. |
| `--immutable-by-default`| Immutability | Enforces that all `let` variable bindings are immutable constants unless explicitly marked `mut`. |

---

## C.3 Compile-Time Borrow Checker Error Codes

When `--borrow-check` or `--static` is enabled, the compiler performs dataflow lifetime analysis and emits diagnostic error codes:

### 1. `error[E0382]`: Use of Moved Value

Occurs when a non-`Copy` value is accessed after its ownership has been transferred to another variable binding or function parameter:

```text
Compiler error on line 4: error[E0382]: Use of moved value `buffer`
  --> src/main.aks:4:11
   |
 2 | let new_owner = buffer;
   |     --------- value moved here
 4 | print(buffer);
   |       ^^^^^^ value used here after move
   |
note: `buffer` was moved because it does not implement the `Copy` trait.
suggestion: Borrow the value using `&buffer` instead of transferring ownership.
```

### 2. `error[E0499]`: Multiple Simultaneous Mutable Borrows

Occurs when attempting to create a second mutable reference (`&mut`) to a variable while an existing mutable borrow is active:

```text
Compiler error on line 3: error[E0499]: Cannot borrow `data` mutably while it is already mutably borrowed
  --> src/main.aks:3:14
   |
 1 | let r1 = &mut data;
   |     -- first mutable borrow occurs here
 3 | let r2 = &mut data;
   |     ^^^^^^^^ second mutable borrow occurs here
```

### 3. `error[E0502]`: Simultaneous Mutable & Immutable Borrows

Violates the fundamental rule: *Aliasing XOR Mutability*. A variable cannot be mutably borrowed while immutable read references exist:

```text
Compiler error on line 3: error[E0502]: Cannot borrow `state` mutably while it is immutably borrowed
  --> src/main.aks:3:14
   |
 1 | let r1 = &state;
   |     -- immutable borrow occurs here
 3 | let r2 = &mut state;
   |     ^^^^^^^^^ mutable borrow occurs here
 5 | print(r1);
   |       -- immutable borrow later used here
```

### 4. `error[E0515]`: Returning Reference to Local Stack Variable

Prevents dangling pointer corruption by ensuring references never outlive the data they point to:

```text
Compiler error on line 4: LifetimeError: cannot return reference to local variable 'temp_array'
  --> src/main.aks:4:12
   |
 2 | let temp_array = [1, 2, 3];
 4 | return &temp_array;
   |        ^^^^^^^^^^^ reference escapes local stack frame
```

### 5. `error[E0596]`: Mutation Through Immutable Reference

Occurs when attempting to modify a data structure through a read-only borrow (`&T`):

```text
Compiler error on line 3: error[E0596]: Cannot mutate `config` while it is immutably borrowed
  --> src/main.aks:3:5
   |
 1 | let r = &config;
 3 | config.port = 9000;
   | ^^^^^^^^^^^ assignment to borrowed variable occurs here
```

---

## C.4 Process Exit Codes

| Exit Code | Meaning | Cause |
| :--- | :--- | :--- |
| `0` | Success | Normal termination without errors; all memory references cleaned with 0 leaks. |
| `1` | Runtime Error | Unhandled exception, standard library assertion failure, or type error in dynamic mode. |
| `2` | Syntax / Lexer Error | Source file failed lexical tokenization or recursive-descent parsing. |
| `3` | Type Error | Unification failure in strict static mode (`--strict-types` or `--static`). |
| `101` | Panic Exit | Explicit `panic()` invocation with backtrace dump. |

---

## C.5 C Public API Result Codes (`AKSResult`)

When embedding AKS via the native C interface (`include/aks.h`), all public execution APIs return standardized `AKSResult` status codes:

| Enum Constant | Value | Description |
| :--- | :--- | :--- |
| `AKS_OK` | `0` | Execution completed successfully without error. |
| `AKS_PARSE_ERROR` | `1` | Lexical tokenization or syntax parsing failure. |
| `AKS_COMPILE_ERROR` | `2` | Semantic analysis, type checking, or bytecode generation failure. |
| `AKS_RUNTIME_ERROR` | `3` | Uncaught runtime exception, fiber panic, or assertion failure. |
| `AKS_MEMORY_ERROR` | `4` | Heap memory allocation failure / out of memory. |
| `AKS_FATAL` | `-1` | Unrecoverable engine error. |


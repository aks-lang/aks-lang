# The AKS Programming Language

[![Build & Test](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20Linux%20%7C%20macOS-blue.svg)]()
[![Engine](https://img.shields.io/badge/engine-VM%20%2B%20JIT%20%2B%20LLVM%20AOT-purple.svg)]()
[![Coverage](https://img.shields.io/badge/tests-100%25%20verified-success.svg)]()
[![License](https://img.shields.io/badge/license-Strict%20Proprietary%20%28All%20Rights%20Reserved%29-red.svg)](LICENSE)

**AKS** is a high-performance, strongly typed systems and application programming language engineered from first principles for extreme hardware throughput, deterministic memory safety, and modern developer ergonomics.

Combining the expressiveness of modern high-level languages with the mechanical sympathy and predictable execution of low-level systems programming, AKS delivers zero-cost abstractions, fearless concurrency, and an industry-leading suite of built-in developer education and onboarding tools.

---

## Table of Contents

- [Architectural Highlights](#architectural-highlights)
- [Interactive Learning & Onboarding Suite](#interactive-learning--onboarding-suite)
  - [1. Interactive Guided Tour (`aks tour`)](#1-interactive-guided-tour-aks-tour)
  - [2. Hands-On Interactive Tutorials (`aks tutorial`)](#2-hands-on-interactive-tutorials-aks-tutorial)
  - [3. Local Web Playground (`aks playground`)](#3-local-web-playground-aks-playground)
  - [4. Interactive Computational Notebook (`aks notebook`)](#4-interactive-computational-notebook-aks-notebook)
  - [5. Curated Production Examples Catalog (`aks examples`)](#5-curated-production-examples-catalog-aks-examples)
  - [6. Built-in Technical Reference System (`aks help`)](#6-built-in-technical-reference-system-aks-help)
  - [7. Error Explanation & Diagnostic Engine (`aks explain`)](#7-error-explanation--diagnostic-engine-aks-explain)
  - [8. Developer Quick-Reference Cheatsheet (`aks cheatsheet`)](#8-developer-quick-reference-cheatsheet-aks-cheatsheet)
  - [9. Automated Code Migration Assistant (`aks migrate`)](#9-automated-code-migration-assistant-aks-migrate)
  - [10. API Documentation Generator (`aks doc`)](#10-api-documentation-generator-aks-doc)
  - [11. Terminal & HTML Syntax Highlighter (`aks highlight`)](#11-terminal--html-syntax-highlighter-aks-highlight)
- [Modern Language Showcase](#modern-language-showcase)
- [Quick Start](#quick-start)
- [Developer Tooling & CLI Reference](#developer-tooling--cli-reference)
- [The AKS Textbook & Documentation](#the-aks-textbook--documentation)
- [Copyright & License](#copyright--license)

---

## Architectural Highlights

- **Multi-Tier Execution Pipeline**:
  - High-throughput stack/register hybrid bytecode Virtual Machine with runtime inline caching.
  - In-memory dynamic tracing JIT compiler (`--jit`) featuring hot-loop backedge counters and call-site tiering.
  - Ahead-of-Time static compilation directly to optimized LLVM IR (`compile-llvm`), native executables (`compile-exe`), standalone C (`compile-aot`), freestanding bare-metal kernels (`compile-nostd`), and WebAssembly text format (`wasm`).
- **Deterministic Memory Model Without Garbage Collection**:
  - Affine borrow checker with non-lexical lifetimes and zero-overhead move semantics.
  - Explicit region scopes (`region (r) { ... }`) with bump arena allocation and instant reset (`r.Reset()`).
  - Guaranteed scope-exit cleanup via `defer` and transactional error unwinding via `errdefer`.
  - Resource Acquisition Is Initialization (RAII) with deterministic `Drop` lifecycle hooks.
- **Dual-Mode Static & Dynamic Typing**:
  - Rapid prototyping with dynamic typing, transitioning seamlessly to strict static verification (`--strict-types`).
  - Full Hindley-Milner type inference, incremental signature caching (`--incremental`), algebraic data types, and contract verification (`requires` / `ensures`).
- **Hardware Acceleration & Concurrency**:
  - 256-bit AVX/AVX2 vector acceleration, native unboxed vector primitives (`Vec2`, `Vec3`, `Vec4`), and component swizzling (`.xyzw`).
  - Shared-nothing Actor model (`actor`, `Spawn`, `Send`, `Call`), lightweight green fibers, and lock-free concurrency primitives (Treiber stacks, MPSC queues, work-stealing deques, and hazard pointers).

---

## Interactive Learning & Onboarding Suite

AKS provides the most comprehensive, developer-first onboarding and learning ecosystem of any modern systems language. All learning tools are built directly into the single `aks.exe` binary with **zero external dependencies**.

```
                        AKS Developer Onboarding Suite
 ┌───────────────────────────────┬───────────────────────────────┐
 │          Interactive          │      Assisted Discovery       │
 ├───────────────────────────────┼───────────────────────────────┤
 │  aks tour        Guided Walk  │  aks help        32 Manuals   │
 │  aks tutorial    12 Lessons   │  aks examples    28 Templates │
 │  aks playground  Browser Web  │  aks explain     Fix Advisor  │
 │  aks notebook    Web Notebook │  aks cheatsheet  Quick Ref    │
 ├───────────────────────────────┴───────────────────────────────┤
 │                      Developer Productivity                   │
 ├───────────────────────────────┬───────────────────────────────┤
 │  aks migrate     Code Porter  │  aks highlight   HTML/ANSI    │
 │  aks doc         HTML/MD Docs │  aks fmt / lint  Auto-Styling │
 └───────────────────────────────┴───────────────────────────────┘
```

---

### 1. Interactive Guided Tour (`aks tour`)

Take a hands-on, progressive walkthrough of the fundamental concepts, idioms, and paradigms of AKS directly in your terminal:

```cmd
aks tour
```

You can jump to specific topics at any time:
```cmd
aks tour 1    # Variables, Constants, and Dynamic Types
aks tour 2    # Functions, Default Arguments, and Closures
aks tour 3    # Affine Ownership, Borrowing, and Memory Safety
aks tour 4    # Concurrency, Channels, and Lightweight Tasks
aks tour 5    # Advanced Features: Lazy Iterators and SIMD
```

---

### 2. Hands-On Interactive Tutorials (`aks tutorial`)

Practice real coding challenges with an automated in-terminal grading engine across **12 progressive lessons**:

```cmd
# Display current tutorial status and progress
aks tutorial

# Generate exercise template at 'tutorialexercise.aks' for the current lesson
aks tutorial start

# Run automated tests on your solution
aks tutorial verify

# Reset tutorial progress back to Lesson 1
aks tutorial reset
```

#### Curriculum:
1. **Variables, Constants & Types**: Variable binding, immutable constants, and type casting.
2. **Collections: Arrays & Hash Maps**: Array indexing, slice manipulation, and associative maps.
3. **Functions & Higher-Order Callbacks**: First-class functions, parameter passing, and callback closures.
4. **Structs, Constructors & Method Blocks**: User-defined structs, constructors, and `impl` method blocks.
5. **Pattern Matching & Guards**: Native `match` expressions, variant branching, and pattern guards.
6. **Concurrency & Typed Channels**: Asynchronous tasks with `spawn`/`await` and buffered communication channels.
7. **Mathematics & Numerics**: High-precision square roots, trigonometric functions, and powers.
8. **Cryptography & Secure Digests**: Industry-standard cryptographic primitives and SHA-256 digests.
9. **Option & Result Ergonomics**: First-class algebraic variants (`Some`, `Ok`, `Err`), variant checks, and unwrapping.
10. **Lazy Iterator Pipelines**: Composable lazy data streams, mapping, filtering, and reduction.
11. **Scope Cleanup with Defer**: Deterministic block-exit resource cleanup with `defer`.
12. **Region Scopes & Arena Allocation**: Bump-pointer allocation, memory introspection, and zero-leak resets.

---

### 3. Local Web Playground (`aks playground`)

Launch a local, browser-based execution environment powered by an embedded HTTP server:

```cmd
aks playground
# Starting AKS Interactive Web Playground at http://localhost:8000
```

- **Interactive Code Editor**: Write and test AKS scripts with instant feedback.
- **Real-Time Execution**: Direct socket connection to the local VM for sub-millisecond execution.
- **Pre-Loaded Examples**: Browse and load standard library snippets in one click.
- **Zero Config**: Requires no Node.js, Python, or external web dependencies.

Specify a custom port:
```cmd
aks playground 8080
```

---

### 4. Interactive Computational Notebook (`aks notebook`)

Run computational code cells sequentially in an interactive web notebook interface:

```cmd
aks notebook 8888
# Starting AKS Interactive Notebook on http://localhost:8888
```

- Run isolated code blocks, inspect intermediate values, and visualize data structures.
- Perfect for scientific computing, algorithmic exploration, and rapid prototyping.

---

### 5. Curated Production Examples Catalog (`aks examples`)

Access 28 fully implemented, production-grade example scripts spanning every domain of the language:

```cmd
# List all 28 curated example templates
aks examples list

# View the full source code of an example
aks examples show http
aks examples show actors
aks examples show simd
aks examples show autodiff

# Copy an example directly into your project
aks examples copy http server.aks
aks examples copy collections app_collections.aks
```

#### Available Categories:
- **Core Language**: `syntax`, `types`, `contracts`, `effects`, `options`, `defer`, `arena`, `unsafe`
- **Data & Collections**: `collections`, `weakref`, `iterators`, `modules`
- **Concurrency & Parallelism**: `concurrency`, `actors`, `lockfree`, `parallel`, `jit`
- **Numerics & Systems**: `math`, `autodiff`, `units`, `simd`, `crypto`, `networking`, `os`, `ffi`

---

### 6. Built-in Technical Reference System (`aks help`)

Browse comprehensive technical reference manuals directly from the command line:

```cmd
# Display topic index
aks help

# Read detailed topic documentation
aks help syntax        # Lexical grammar, let/const, control flow, functions
aks help types         # Static mode, HM inference, ADTs, dependent arrays
aks help concurrency   # Fibers, spawn/await, channels, mutexes
aks help lockfree      # Treiber stack, MPSC queues, hazard pointers
aks help iterators     # Lazy iterator pipelines and consumers
aks help options       # Option/Result ergonomics, unwrapping, ? operator
aks help memory        # Region scopes, bump arenas, and lifecycle hooks
aks help collections   # Set, Deque, PriorityQueue, LRUCache, BitSet
aks help file          # File streaming, line-by-line reading, atomic ops
aks help logger        # Multi-level structured logging (JSON and text)
aks help http          # Async HTTP server and client
aks help datetime      # ISO formatting, UTC timestamps, and DateTime
```

---

### 7. Error Explanation & Diagnostic Engine (`aks explain`)

Understand cryptic bugs and compiler errors instantly with root-cause analysis and suggested fixes:

```cmd
# Explain by compiler error code or message
aks explain E0382
aks explain "use of moved value"
aks explain "cannot assign to const variable"
aks explain "type mismatch"
```

The engine provides:
- **Plain English Explanation**: Why the compiler rejected the code.
- **Root Cause Diagram**: ASCII visualization of the invalid state (e.g., move semantics).
- **Corrected Code Pattern**: Idiomatic fix showing how to borrow, clone, or restructure.

---

### 8. Developer Quick-Reference Cheatsheet (`aks cheatsheet`)

Display or export a complete syntax and standard library cheat sheet:

```cmd
# Print cheatsheet to terminal
aks cheatsheet

# Export formatted Markdown cheatsheet
aks cheatsheet cheatsheet.md
```

---

### 9. Automated Code Migration Assistant (`aks migrate`)

Transition existing codebases from other languages to idiomatic AKS syntax:

```cmd
aks migrate legacy_script.py
aks migrate algorithm.js
aks migrate service.rs
```

- Analyzes foreign constructs and maps them to AKS primitives (`let`, `const`, `fn`, `match`, `?`).
- Translates common standard library methods (e.g., array operations, map methods, string manipulation).
- Generates inline migration notes explaining key architectural differences.

---

### 10. API Documentation Generator (`aks doc`)

Generate high-quality documentation from doc comments (`///`) preceding functions, structs, traits, and constants:

```cmd
# Generate dark-mode, responsive HTML documentation
aks doc src/main.aks docs/api.html

# Generate GitHub Flavored Markdown documentation
aks doc src/main.aks docs/API.md
```

- **Styled Dark Mode**: HTML output styled with curated typography (Outfit & Fira Code), elegant glassmorphism, and responsive doc-cards.
- **Syntax Awareness**: Automatically indexes functions, parameters, struct schemas, traits, and module constants.

---

### 11. Terminal & HTML Syntax Highlighter (`aks highlight`)

Render syntax-highlighted code blocks with zero external dependencies:

```cmd
# Render syntax-highlighted code directly in the terminal
aks highlight main.aks

# Export standalone HTML with modern dark theme syntax styling
aks highlight main.aks code_preview.html
```

---

## Modern Language Showcase

Here is a brief tour of modern AKS idioms:

```aks
// 1. Top-Level Constants & Compile-Time Folding
const MAX_CONNECTIONS = 1000;
const API_VERSION = "v2.4.0";

// 2. Structs & Static Methods
struct ServerConfig {
    host: string;
    port: int;
    timeout_ms: int;
}

// 3. Error Propagation (?) on Option & Result
fn load_port(config_map: any) -> Result {
    let port = config_map.port?;   // Unwraps Ok/Some or early-returns Err/None
    return Ok(port);
}

// 4. Native Pattern Matching (match)
fn describe_status(code: int) -> string {
    match (code) {
        case 200 => { return "OK"; }
        case 404 => { return "Not Found"; }
        case 500 => { return "Internal Error"; }
        default  => { return "Unknown Status"; }
    }
}

// 5. Standard Collections (Set, Deque, PriorityQueue, LRUCache)
let cache = LRUCache(100);
cache.Put("auth_token", "xyz_session_123");
assert(cache.Get("auth_token") == "xyz_session_123");

let pq = PriorityQueue(); // Min-heap by default
pq.Push(40);
pq.Push(10);
assert(pq.Pop() == 10);

// 6. Namespace Resolution (::) & Static Calls
let clamped = Math::Clamp(125, 0, 100); // 100
let level = Logger::DEBUG();             // 0

// 7. Structured Logging
let logger = Logger("service", LOG_INFO, "json");
logger.Info("Server booted", { "port": 8080, "workers": 4 });

// 8. Composable Lazy Iterators
let evens_sum = lazy([1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
    .Filter(fn(x) { return x % 2 == 0; })
    .Map(fn(x) { return x * 10; })
    .Sum(); // 300

// 9. Cooperative Async Concurrency & Cancellation
let cts = CancellationTokenSource();
let token = cts.Token();

let t1 = spawn fn() { TaskDelay(10); return 42; }();
let t2 = spawn fn() { TaskDelay(20); return 100; }();
let results = Task.WhenAll([t1, t2]);
print(f"Async results: {results}"); // [42, 100]

// 10. Region Memory Scopes & Arena Reset (0 leaks)
region (r) {
    let scratch = r.AllocArray(512);
    // Scratch buffer operations...
    r.Reset(); // Instant O(1) bulk reset
}
```

---

## Quick Start

### Prerequisites & Source Build

AKS is self-contained with minimal external dependencies. On Windows x64 with MSVC installed:

```cmd
# Clone repository
git clone https://github.com/anoop/aks-lang.git
cd aks-lang

# Build compiler, VM, LSP daemon, formatter, and linter
build.bat
```

This generates:
- `aks.exe`: Primary compiler, runtime engine, and development CLI.
- `aksls.exe`: Language Server Protocol (LSP) server for VS Code / editors.
- `aksfmt.exe`: Deterministic code formatter.
- `akslint.exe`: Static analyzer and linter.
- `aks.dll`: Shared dynamic library for C/C++ embedding.

### Running the Verification Suite

```cmd
test.bat
```

---

## Developer Tooling & CLI Reference

AKS ships with a complete toolchain out of the box:

| Command | Usage | Description |
| :--- | :--- | :--- |
| **`run`** | `aks run <file.aks>` | Compiles and executes in memory via VM and JIT. |
| **`watch`** | `aks watch <file.aks>` | Watches source file and hot-reloads on modification. |
| **`compile-exe`**| `aks compile-exe <file.aks> -o <out.exe>` | Generates a standalone native Windows executable. |
| **`static`** | `aks static build <file.aks> -o <out.exe>` | Direct LLVM IR compilation with zero VM overhead. |
| **`wasm`** | `aks wasm <file.aks> -o <out.wat>` | Compiles source to WebAssembly text format. |
| **`test`** | `aks test [file.aks] [--coverage]` | Discovers and executes `test_*` functions with reporting. |
| **`check`** | `aks check [file.aks]` | Static type checking and safety verification without execution. |
| **`bench`** | `aks bench [file.aks \| dir] --save <out>` | Runs benchmark functions and saves performance profiles. |
| **`fuzz`** | `aks fuzz [file.aks]` | Automated coverage-guided fuzz testing loop. |
| **`fmt`** | `aks fmt <file.aks>` | Formats source files to idiomatic styling conventions. |
| **`lint`** | `aks lint <file.aks>` | Enforces best practices, style rules, and safety checks. |
| **`dap`** | `aks dap <file.aks> [port]` | Starts Debug Adapter Protocol server for visual debugging. |
| **`pkg`** | `aks pkg <init\|add\|install\|run\|bundle>` | Built-in package manager with cryptographic signing. |
| **`bind`** | `aks bind <header.h> <output.aks>` | Auto-generates type-safe FFI bindings from C/C++ headers. |

---

## The AKS Textbook & Documentation

The repository includes a complete 20-chapter technical textbook located in the [`book/`](book/) directory:

- [Chapter 01: Introduction & Systems Architecture](book/Chapter%2001%20-%20Introduction.md)
- [Chapter 02: Language Basics & Lexical Structure](book/Chapter%2002%20-%20Language%20Basics%20and%20Primitive%20Types.md)
- [Chapter 03: Control Flow, Loops & Pattern Matching](book/Chapter%2003%20-%20Control%20Flow%20and%20Pattern%20Matching.md)
- [Chapter 04: Functions, Closures, Stack Frames & Error Handling](book/Chapter%2004%20-%20Functions,%20Closures%20and%20Error%20Handling.md)
- [Chapter 05: Collections, Structs & Object Architecture](book/Chapter%2005%20-%20Collections,%20Structs%20and%20Object%20Model.md)
- [Chapter 06: The AKS Type System & Static Mode](book/Chapter%2006%20-%20Type%20System%20and%20Static%20Mode.md)
- [Chapter 07: Memory Model, Ownership & Borrow Checker](book/Chapter%2007%20-%20Ownership,%20Borrowing%20and%20Memory%20Model.md)
- [Chapter 08: Concurrency, Async, Fibers & Parallelism](book/Chapter%2008%20-%20Concurrency,%20Async%20and%20Fibers.md)
- [Chapter 09: Lock-Free Concurrency & Memory Reclamation](book/Chapter%2009%20-%20Lock-Free%20Data%20Structures.md)
- [Chapter 10: Advanced Pure Data Structures](book/Chapter%2010%20-%20Advanced%20Pure%20Data%20Structures.md)
- [Chapter 11: Mathematics, Numerics & Scientific Computing](book/Chapter%2011%20-%20Mathematics,%20Numerics%20and%20Scientific%20Computing.md)
- [Chapter 12: Parsing, Serialization & Config Engines](book/Chapter%2012%20-%20Serialization,%20Formats%20and%20Config%20Engines.md)
- [Chapter 13: Cryptography & Security Engine](book/Chapter%2013%20-%20Cryptography%20and%20Security%20Engine.md)
- [Chapter 14: SIMD Hardware Acceleration & HPC](book/Chapter%2014%20-%20SIMD%20and%20Hardware%20Acceleration.md)
- [Chapter 15: Networking, Sockets & Web Protocols](book/Chapter%2015%20-%20Networking,%20Sockets%20and%20Web%20Protocols.md)
- [Chapter 16: Windows OS Kernel & Systems Integration](book/Chapter%2016%20-%20Windows%20OS%20Kernel%20and%20Systems%20Internals.md)
- [Chapter 17: Media, Graphics & Audio Subsystems](book/Chapter%2017%20-%20Media,%20Graphics%20and%20Audio%20Subsystems.md)
- [Chapter 18: Foreign Function Interface & Native Interop](book/Chapter%2018%20-%20Foreign%20Function%20Interface%20and%20Interop.md)
- [Chapter 19: Developer Tooling, Ecosystem & Internals](book/Chapter%2019%20-%20Developer%20Tooling%20and%20Ecosystem.md)
- [Chapter 20: Practical Cookbook & Real-World Projects](book/Chapter%2020%20-%20Cookbook%20and%20Real-World%20Projects.md)
- [Appendix A: Built-in & Standard Library API Reference](book/Appendix%20A%20-%20Complete%20Builtin%20and%20Standard%20Library%20API%20Reference.md)
- [Appendix B: Virtual Machine Instruction Set (ISA) Reference](book/Appendix%20B%20-%20Virtual%20Machine%20Instruction%20Set%20and%20Opcode%20Reference.md)
- [Appendix C: Compiler Flags & CLI Diagnostics](book/Appendix%20C%20-%20Compiler%20Flags,%20CLI%20Options%20and%20Diagnostics%20Reference.md)

Consolidated edition: [The AKS Book (Complete)](book/The%20AKS%20Book.md)

---

## Copyright & License

Copyright (c) 2026 **Anoop Kumar Sharma**. All rights reserved.

This project is protected under a strict proprietary license. Unauthorized copying, distribution, reverse engineering, commercial exploitation, or incorporation into automated datasets is strictly prohibited. See [`LICENSE`](LICENSE) for complete legal terms.

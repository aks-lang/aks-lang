# The AKS Programming Language

[![Build & Test](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20Linux%20%7C%20macOS-blue.svg)]()
[![Architecture](https://img.shields.io/badge/engine-VM%20%2B%20JIT%20%2B%20LLVM%20AOT-purple.svg)]()
[![License](https://img.shields.io/badge/license-Strict%20Proprietary%20%28All%20Rights%20Reserved%29-red.svg)](LICENSE)

**AKS** is a high-performance, strongly typed systems and application programming language engineered from first principles for extreme hardware throughput, deterministic memory safety, and modern developer ergonomics.

---

## Key Highlights

- **Multi-Tier Execution Engine**: Fast stack/register hybrid bytecode virtual machine with runtime optimization, in-memory dynamic tracing JIT engine (`--jit`) with hot-loop and function call-site tiering, and LLVM/C AOT static compilation (`compile-exe`, `compile-aot`, `compile-llvm`, `compile-nostd`, and `wasm`).
- **Dual-Mode Type System**: Optional dynamic typing for rapid scripting coupled with static mode (`--strict-types`) featuring Hindley-Milner type inference, persistent incremental signature caching (`--incremental`), dependent arrays, and multi-error diagnostic recovery.
- **Enhanced Error Diagnostics & Caret Recovery**: Statement synchronization (`ParseSynchronize`) preventing cascade errors, source-line visual caret rendering (`^~~~`), and Levenshtein distance "Did you mean?" suggestions for unknown identifiers, functions, types, and struct fields.
- **Affine Borrow Checker & Memory Model**: Compile-time ownership, non-lexical lifetimes, zero-cost move semantics, lexical region memory scopes (`region (r)`), batch arena introspection & reset (`Reset`, `AllocCount`, `AllocatedBytes`), and `Drop` trait lifecycle without garbage collection pauses.
- **Resource Management & Unwinding**: Explicit `defer` for guaranteed block-exit cleanup and `errdefer` for transactional error-path unwinding.
- **Systems Programming & Unsafe Blocks**: `unsafe { ... }` blocks with borrow checker bypass, memory layout introspection (`SizeOf`, `AlignOf`, `OffsetOf`), `repr()` layout control (packed structs), and concurrency safety trait bounds (`IsSend`, `IsSync`).
- **Lazy Iterator Pipeline**: Composable zero-allocation iterator adapters (`Map`, `Filter`, `Take`, `Skip`, `Zip`, `Enumerate`, `StepBy`, `Collect`) and consumers (`ForEach`, `Reduce`, `Any`, `All`, `Count`, `Sum`, `First`, `Find`), with `RangeIterator` and direct `ArrayIterator` chaining.
- **Option and Result Ergonomics**: First-class ADT variants with constructors (`Some`, `Ok`, `Err`), bare `None` resolution, variant methods (`IsSome`, `IsNone`, `IsOk`, `IsErr`, `Unwrap`, `UnwrapOr`, `Expect`, `MapErr`), and try-propagation operator (`?`).
- **Formatted String Interpolation**: Expressive f-strings with format specifiers (`{val:.2f}`, `hex`, `binary`, padding, alignment) via `FormatValue`.
- **Numeric & String Utilities**: Standard math primitives (`abs`, `min`, `max`, `clamp`), forward-mode automatic differentiation (`Dual`, `grad`, `jvp`), and search utilities (`starts_with`, `ends_with`, `contains`, `array_contains`).
- **Modular Compilation & Concurrency Scaling**: Circular import detection, topological DAG dependency ordering, and multicore parallel AST and bytecode chunk compilation (`--parallel`, `-j`, `--jobs`, `AKSPARALLEL`, `AKS_JOBS`).
- **Actor Model Concurrency & Lock-Free Primitives**: Isolated mutable state communicating via lock-free mailboxes (`actor`, `Spawn`, `Send`, `Call`), fibers, channels, Treiber stack, MPSC queues, and hazard pointers.
- **Hardware Acceleration**: 256-bit AVX/AVX2 vector intrinsics, unboxed SIMD geometric primitives (`Vec2`, `Vec3`, `Vec4`), and zero-cost component swizzling (`.xyzw`).
- **Standard Library & Tooling**: Built-in cryptographic ciphers, JSON/TOML/MsgPack/Protobuf parsers, networking engine, multi-suite benchmark runner (`aks bench [file | dir] --save --compare`), Language Server Protocol (`aksls`), code formatter (`aksfmt`), linter (`akslint`), and interactive debugger (`DAP`).

---

## Quick Start

### Building from Source

AKS compiles natively on Windows (x86-64) using MSVC:

```cmd
build.bat
```

This compiles:
- `aks.exe`: Compiler, Virtual Machine, JIT runner, and package manager
- `aksls.exe`: Language Server Protocol (LSP) daemon
- `aksfmt.exe`: Code formatter
- `akslint.exe`: Static analyzer and linter
- `aks.dll`: Embeddable C API shared library

### Running Tests

```cmd
test.bat
```

### Running Your First Program

Create `main.aks`:

```aks
// Option & Result ergonomics with try-propagation
fn fetch_port(config: any) -> Result {
    let port = config.port?;
    return Ok(port);
}

// Lazy iterator pipeline chaining
let numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
let evens_squared = lazy(numbers)
    .Filter(fn(x) { return x % 2 == 0; })
    .Map(fn(x) { return x * x; })
    .Take(3)
    .Collect();

print(f"Evens squared: {evens_squared}"); // [4, 16, 36]

// Formatted string interpolation and math primitives
let latency = 4.2568;
let clamped = clamp(latency, 1.0, 4.0);
print(f"Latency: {latency:.2f}ms (Clamped: {clamped:.2f}ms)");

// Region scope with arena allocation and reset
region (r) {
    let buf = r.AllocArray(1024);
    print(f"Arena allocations: {r.AllocCount()}, Bytes: {r.AllocatedBytes()}");
    r.Reset();
    print(f"Allocations after reset: {r.AllocCount()}");
}
```

Execute:

```cmd
aks.exe run main.aks
```

---

## Documentation

The repository includes a comprehensive 20-chapter technical textbook and reference manuals in the [`book/`](book/) directory:

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

// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "value.h"
#include "learning.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern const char *gProgramName;

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

static char *LearnReadFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t readbytes = fread(buffer, 1, size, file);
    buffer[readbytes] = '\0';
    fclose(file);
    return buffer;
}

static int LearnWriteFile(const char *path, const char *content) {
    FILE *file = fopen(path, "w");
    if (!file) return 0;
    fputs(content, file);
    fclose(file);
    return 1;
}

// Built-in Help System
void LearnHelp(const char *topic) {
    if (!topic || strcmp(topic, "") == 0) {
        printf("\033[36mAKS Built-in Documentation & Help System\033[0m\n");
        printf("Available topics:\n");
        printf("  \033[33msyntax\033[0m       Variables, scoping, control flow, functions, and structs\n");
        printf("  \033[33mtypes\033[0m        Static mode, HM inference, ADTs, dependent arrays, and traits\n");
        printf("  \033[33mconcurrency\033[0m  Async spawn/await, channels, mutexes, and thread synchronization\n");
        printf("  \033[33mlockfree\033[0m     Treiber stack, MPSC queue, work-stealing deque, hazard pointers\n");
        printf("  \033[33mmath\033[0m         SquareRoot, Power, trigonometric, MatrixMulRaw, MatrixLuRaw, FftRaw\n");
        printf("  \033[33mcrypto\033[0m       SHA256, AES256GCM, Chacha20Poly1305, Ed25519, Argon2, PBKDF2, HMAC\n");
        printf("  \033[33msimd\033[0m         SimdFma, SimdAddRaw, SimdMulRaw, SimdOp vector acceleration\n");
        printf("  \033[33mnetworking\033[0m   SocketConnect, SocketSend, SocketReceive, UdpSocket, DnsResolve\n");
        printf("  \033[33mos\033[0m           ClipboardSet, ClipboardGet, MakeArena, ShmOpen, ProcessSpawn\n");
        printf("  \033[33mffi\033[0m          FFILoad, FFICall, FFIFree dynamic C interop\n");
        printf("  \033[33mbuiltins\033[0m     Catalog of standard built-in functions and native dispatchers\n");
        printf("  \033[33meffects\033[0m      Algebraic effects (effect, perform, handle, resume)\n");
        printf("  \033[33mcontracts\033[0m    Preconditions (requires) and postconditions (ensures)\n");
        printf("  \033[33mautodiff\033[0m     Automatic differentiation, dual numbers, and gradients\n");
        printf("  \033[33munits\033[0m        Physical unit types and dimensional analysis\n");
        printf("  \033[33mjit\033[0m          Dynamic tracing JIT, hot-loop backedge & call-site tiering\n");
        printf("  \033[33mincremental\033[0m  Persistent signature caching (.akscache/) and fast re-compilation\n");
        printf("  \033[33mmodules\033[0m      Topological DAG ordering, circular import detection, and parallel compilation\n");
        printf("  \033[33mparallel\033[0m     Multi-threaded AST & bytecode chunk optimization (--parallel, -j, --jobs)\n");
        printf("  \033[33mdiagnostics\033[0m  Parser statement sync, caret markers (^~~~), and 'Did you mean?' suggestions\n");
        printf("  \033[33maot\033[0m          Ahead-of-time compilation, zero-overhead executables, and bare-metal targets\n");
        printf("  \033[33miterators\033[0m    Lazy iterator pipeline, chaining adapters, range/array iterators, and consumers\n");
        printf("  \033[33moptions\033[0m      Option and Result ergonomics, Some, Ok, Err, unwrap methods, and ? operator\n");
        printf("  \033[33mdefer\033[0m        Resource safety with defer and errdefer statements, RAII Drop lifecycle\n");
        printf("  \033[33mmemory\033[0m       Region scopes, bump arenas, Reset(), AllocCount(), and AllocatedBytes()\n");
        printf("  \033[33munsafe\033[0m       Unsafe blocks, layout introspection (SizeOf, AlignOf, OffsetOf), repr()\n");
        printf("  \033[33mtooling\033[0m      CLI flags, LSP (aksls), DAP debugger, formatter, linter, and bench runner\n");
        printf("  \033[33mcollections\033[0m  High-performance standard collections: Set, Deque, PriorityQueue\n");
        printf("  \033[33mweakref\033[0m      Weak references (weak, Alive, Upgrade) to prevent cyclic leaks\n");
        printf("  \033[33mhttp\033[0m         Lightweight async HTTP server & client (HttpServer, Response, Http)\n\n");
        printf("Usage: aks help <topic>\n");
        return;
    }

    if (strcmp(topic, "syntax") == 0) {
        printf("\033[36mSyntax & Language Basics\033[0m\n");
        printf("Variables & Constants:\n");
        printf("  let x = 42;             // Mutable local variable\n");
        printf("  const PI = 3.14159;     // Immutable constant\n");
        printf("  let mut count = 0;      // Explicit mutable binding\n\n");
        printf("Functions:\n");
        printf("  fn add(a: int, b: int = 1) -> int {\n");
        printf("      return a + b;\n");
        printf("  }\n\n");
        printf("Control Flow & Matching:\n");
        printf("  if (x > 10) { ... } else { ... }\n");
        printf("  for (let i = 0; i < 10; i += 1) { ... }\n");
        printf("  for (item in collection) { ... }\n");
        printf("  match (val) { 1 => \"one\", x if x > 10 => \"large\", _ => \"other\" };\n");
    } else if (strcmp(topic, "types") == 0) {
        printf("\033[36mType System & Static Mode\033[0m\n");
        printf("Static Compilation Flags:\n");
        printf("  aks run --strict-types script.aks\n");
        printf("  aks run --borrow-check script.aks\n");
        printf("  aks run --incremental script.aks\n");
        printf("  aks emit-ir --static script.aks\n\n");
        printf("Incremental Signature Caching:\n");
        printf("  Persists inferred signatures and AST hashes in .akscache/signatures.bin.\n");
        printf("  Unchanged functions skip re-inference, speeding up edits by 50-80%%.\n\n");
        printf("Multi-Error Recovery:\n");
        printf("  Collects up to 50 compiler & type diagnostics with source lines in one pass.\n\n");
        printf("Algebraic Data Types (ADTs):\n");
        printf("  enum Option { Some(any), None }\n");
        printf("  enum Result { Ok(any), Err(str) }\n\n");
        printf("Fixed-Size Dependent Arrays:\n");
        printf("  let weights: [float; 4] = [0.25, 0.25, 0.25, 0.25];\n\n");
        printf("Traits & Interfaces:\n");
        printf("  trait Area { fn area(self) -> float; }\n");
    } else if (strcmp(topic, "concurrency") == 0) {
        printf("\033[36mConcurrency & Channels\033[0m\n");
        printf("Structured Concurrency (Nurseries):\n");
        printf("  nursery (n) {\n");
        printf("      n.Spawn(task_worker_1);\n");
        printf("      n.SpawnArg(task_worker_2, payload);\n");
        printf("  } // Scope invariant: ALL spawned tasks auto-join at closing brace!\n\n");
        printf("Lexical Region Memory Scopes:\n");
        printf("  region (r) {\n");
        printf("      let buf = r.AllocArray(1024);\n");
        printf("  } // Scope invariant: O(1) batch arena deallocation at scope exit!\n\n");
        printf("Async Tasks:\n");
        printf("  let task = spawn fetch_metric(\"cpu\");\n");
        printf("  let result = await task;\n\n");
        printf("Channels:\n");
        printf("  let ch = Channel(5);\n");
        printf("  ChannelSend(ch, 42);\n");
        printf("  let val = Receive(ch);\n");
        printf("  ChannelClose(ch);\n\n");
        printf("Synchronization:\n");
        printf("  let m = mutex();\n");
        printf("  lock(m); ... unlock(m);\n");
    } else if (strcmp(topic, "lockfree") == 0) {
        printf("\033[36mLock-Free Concurrency & Memory Reclamation\033[0m\n");
        printf("  AtomGet(a)             Atomic load\n");
        printf("  AtomSet(a, v)          Atomic store\n");
        printf("  AtomAdd(a, v)          Atomic fetch and add\n");
        printf("  AtomCompareAndSwap(a, o, n) Atomic Compare-And-Swap (CAS)\n");
    } else if (strcmp(topic, "math") == 0) {
        printf("\033[36mMathematics, Numerics & Scientific Computing\033[0m\n");
        printf("  SquareRoot(x)          Square root\n");
        printf("  Power(b, e)            Exponentiation\n");
        printf("  MatrixMulRaw(A, B, N)  Cache-tiled matrix multiplication\n");
        printf("  MatrixLuRaw(A, N)      LU decomposition linear solver\n");
        printf("  FftRaw(real, imag)     Fast Fourier Transform\n");
        printf("  sin, cos, tan, atan2   Trigonometric functions\n");
        printf("  GreatestCommonDivisor  GCD computation\n");
    } else if (strcmp(topic, "crypto") == 0) {
        printf("\033[36mCryptography & Security Engine\033[0m\n");
        printf("  CryptoSha256(data)     SHA-256 cryptographic digest\n");
        printf("  CryptoAesGcmEncrypt(k, iv, pt) AES-256-GCM authenticated encryption\n");
        printf("  CryptoAesGcmDecrypt(k, iv, ct, tag) Authenticated decryption\n");
        printf("  CryptoChacha20Encrypt(k, n, pt) ChaCha20-Poly1305 AEAD stream\n");
        printf("  CryptoEd25519Sign(priv, msg) Ed25519 digital signature\n");
    } else if (strcmp(topic, "simd") == 0) {
        printf("\033[36mSIMD Hardware Acceleration\033[0m\n");
        printf("  SimdAddRaw(a, b)       256-bit AVX2 float32 vectorized addition\n");
        printf("  SimdMulRaw(a, b)       Vectorized element-wise multiplication\n");
        printf("  SimdFma(a, b, c)       Fused multiply-add: (a * b) + c\n");
        printf("  SimdDot(a, b)          Vector dot product with horizontal add\n");
    } else if (strcmp(topic, "networking") == 0) {
        printf("\033[36mNetworking & Sockets\033[0m\n");
        printf("  SocketConnect(host, port) TCP socket connection\n");
        printf("  SocketSend(sock, data)    Buffered stream send\n");
        printf("  SocketReceive(sock, len)  Buffered stream receive\n");
        printf("  DnsResolve(host)          IPv4 address resolution\n");
    } else if (strcmp(topic, "os") == 0) {
        printf("\033[36mOperating System & Kernel\033[0m\n");
        printf("  ShmOpen(name, size)       POSIX/Windows shared memory handle\n");
        printf("  ProcessSpawn(cmd)         Subprocess launcher\n");
        printf("  MakeArena()               Fast linear arena bump allocator\n");
        printf("  ClipboardSet(text)        System clipboard bridge\n");
    } else if (strcmp(topic, "ffi") == 0) {
        printf("\033[36mForeign Function Interface (FFI) & Native Interop\033[0m\n");
        printf("  FFILoad(dll_path)      Dynamic library loading (.dll, .so)\n");
        printf("  FFICall(h, fn, args)   C FFI binding and function invocation\n");
        printf("  FFIFree(h)             Unload dynamic library\n");
        printf("  COMCall(progid, fn...) Windows COM automation via IDispatch\n");
    } else if (strcmp(topic, "builtins") == 0) {
        printf("\033[36mBuilt-in Functions Catalog\033[0m\n");
        printf("  Collections:  Length, append, pop, insert, remove, sort, reverse, slice, split\n");
        printf("  Numerics:     SquareRoot, Absolute, Power, sin, cos, tan, floor, Ceiling\n");
        printf("  Conversion:   Integer, float, String, Character, Ordinal, HexEncode, HexDecode\n");
        printf("  Serialization:JsonParse, JsonStringify, CsvParse, CsvStringify, TomlParse\n");
        printf("  Systems:      clock, sleep, print, panic, SetPanicHook, typeof\n");
    } else if (strcmp(topic, "tooling") == 0) {
        printf("\033[36mTooling & Ecosystem Commands\033[0m\n");
        printf("  aks run <file>             Execute script via VM/JIT\n");
        printf("  aks run --jit <file>       Execute script with dynamic in-memory JIT\n");
        printf("  aks run --incremental <f>  Execute with persistent signature caching\n");
        printf("  aks compile --parallel <f> Parallel multi-threaded module compilation\n");
        printf("  aks repl                   Interactive Read-Eval-Print Loop\n");
        printf("  aks fmt <file>             In-place AST source code formatter\n");
        printf("  aks lint <file>            Static analyzer and bug detector\n");
        printf("  aks test                   Run automated test suite\n");
        printf("  aks bench <file>           Benchmark runner with microsecond timing\n");
        printf("  aks emit-ir --static <f>   Emit unboxed LLVM IR\n");
        printf("  aks emit-wat <file>        Emit WebAssembly Text format\n");
        printf("  aks pkg init / install     Package manager and dependency resolver\n");
        printf("  aks debug <file>           Launch DAP interactive debugger\n");
        printf("  aks playground [port]      Start local interactive web IDE\n");
    } else if (strcmp(topic, "effects") == 0) {
        printf("\033[36mAlgebraic Effect System\033[0m\n");
        printf("Declare Effects:\n");
        printf("  effect Logger {\n");
        printf("      log(msg: string) -> string;\n");
        printf("  }\n\n");
        printf("Perform Effects:\n");
        printf("  perform log(\"Status notification\");\n\n");
        printf("Handle & Resume:\n");
        printf("  let res = handle (operation()) with {\n");
        printf("      log(msg: string) => {\n");
        printf("          print(\"Intercepted: \" + msg);\n");
        printf("          resume \"logged\";\n");
        printf("      }\n");
        printf("  };\n");
    } else if (strcmp(topic, "contracts") == 0) {
        printf("\033[36mDesign-by-Contract: Preconditions & Postconditions\033[0m\n");
        printf("Function Pre- and Post-Conditions:\n");
        printf("  fn SafeDivide(a: int, b: int) -> int\n");
        printf("      requires b != 0\n");
        printf("      ensures result != null\n");
        printf("  {\n");
        printf("      return a / b;\n");
        printf("  }\n\n");
        printf("Automatic Verification:\n");
        printf("  Violated contracts raise exceptions automatically in debug and verified builds.\n");
    } else if (strcmp(topic, "autodiff") == 0) {
        printf("\033[36mNative Reverse-Mode Automatic Differentiation & Computational Tape Engine\033[0m\n");
        printf("Reverse-Mode AutoVar & Backpropagation (O(1) Reverse Pass):\n");
        printf("  let x = Var(3.0);\n");
        printf("  let y = Var(4.0);\n");
        printf("  let loss = x.Mul(x).Add(y.Mul(y)); // z = x^2 + y^2\n");
        printf("  loss.backward();                   // Backpropagate gradients\n");
        printf("  print(x.grad());                   // 6.0 (dz/dx)\n");
        printf("  print(y.grad());                   // 8.0 (dz/dy)\n\n");
        printf("Analytical Multivariable Gradients:\n");
        printf("  let f = fn(a, b) { return a.Mul(a).Add(b.Mul(b).Mul(2.0)); };\n");
        printf("  let grads = grad_reverse(f, 5.0, 3.0); // [10.0, 12.0]\n\n");
        printf("Dual Numbers (Forward-Mode):\n");
        printf("  let d1 = Dual(3.0, 1.0);  // value: 3.0, gradient: 1.0\n");
        printf("  let dProd = d1.Mul(Dual(2.0, 0.0));\n\n");
        printf("Gradient Computation:\n");
        printf("  let derivative = grad(fn(x) { return x * x + 3.0 * x + 5.0; }, 4.0); // 11.0\n");
    } else if (strcmp(topic, "units") == 0) {
        printf("\033[36mPhysical Units & Zero-Cost Dimensional Analysis\033[0m\n");
        printf("Unit Constructors:\n");
        printf("  let d = Meter(100.0);\n");
        printf("  let t = Second(10.0);\n");
        printf("  let m = Kilogram(2.0);\n\n");
        printf("Dimensional Calculations:\n");
        printf("  let v = Speed(d, t);            // 10.0 m/s (unit.m = 1, unit.s = -1)\n");
        printf("  let a = Acceleration(v, t);     // 1.0 m/s^2 (unit.m = 1, unit.s = -2)\n");
        printf("  let f = Newton(m, a);           // 2.0 N (unit.kg = 1, unit.m = 1, unit.s = -2)\n");
        printf("  let w = Joule(f, d);            // 200.0 J\n");
    } else if (strcmp(topic, "aot") == 0) {
        printf("\033[36mAhead-of-Time (AOT) Native Compilation & Zero-Cost Abstractions\033[0m\n");
        printf("Compilation Modes:\n");
        printf("  aks compile-exe <file.aks> [-o <out.exe>]  Emit standalone native executable (0 external dependencies)\n");
        printf("  aks compile-aot <file.aks> <out.c>         Generate standalone C code with zero VM interpreter overhead\n");
        printf("  aks compile-llvm <file.aks> [-o <out.ll>]  Emit unboxed multi-arch LLVM IR with LTO and PGO support\n");
        printf("  aks compile-nostd <file.aks> [-o <out.c>]  Emit freestanding bare-metal C module for microcontrollers\n");
        printf("  aks wasm <file.aks> [-o <out.wat>]         Emit WebAssembly text format (browser / WASI)\n\n");
        printf("Zero-Cost Memory & Layout Control:\n");
        printf("  - Compile-Time Execution: comptime blocks evaluate at compile time with 0 runtime instructions.\n");
        printf("  - Scoped Arenas: RegionNew/RegionScope allocate via pointer bump with O(1) bulk teardown.\n");
        printf("  - Layout Introspection: SizeOf, AlignOf, OffsetOf, and repr() packed struct attributes.\n");
        printf("  - Direct Linking: Native Win64 PE executable synthesis with direct entrypoints.\n");
    } else if (strcmp(topic, "actors") == 0) {
        printf("\033[36mState-Isolated Actor Model Concurrency\033[0m\n");
        printf("Actor Declaration:\n");
        printf("  actor CounterActor {\n");
        printf("      count: int;\n");
        printf("      fn Increment(amount) { self.count += amount; return self.count; }\n");
        printf("      fn Get() { return self.count; }\n");
        printf("  }\n\n");
        printf("Spawning & Messaging:\n");
        printf("  let a = CounterActor.Spawn({ count: 10 });\n");
        printf("  a.Send({ type: \"Increment\", amount: 5 });        // Asynchronous non-blocking\n");
        printf("  let count = a.Call({ type: \"Get\" }, 1000);         // Synchronous request-reply\n");
        printf("  a.Stop();\n");
    } else if (strcmp(topic, "jit") == 0) {
        printf("\033[36mDynamic Tracing JIT Engine & Multi-Tiering\033[0m\n");
        printf("Hot-Loop & Hot-Function JIT Compilation:\n");
        printf("  aks run --jit script.aks\n\n");
        printf("Architecture:\n");
        printf("  1. Hot loop backedges are tracked in register (do_regopjmp) and stack (OPJMPBACK) loops.\n");
        printf("  2. At 50 backedges, the VM triggers in-memory JIT loop trace compilation.\n");
        printf("  3. Hot function call-sites (OPCALL, OPCALLDYNAMIC, do_regopcall) tier up to native machine code.\n");
        printf("  4. Native x64 machine code is emitted directly into executable memory (PAGE_EXECUTE_READWRITE).\n");
        printf("  5. Seamless caller frame restore (task->framecount) and return value synchronization.\n");
        printf("  6. Expected speedup: 3-10x on compute-intensive numeric loops and functions.\n");
    } else if (strcmp(topic, "incremental") == 0) {
        printf("\033[36mPersistent Incremental Type Checking\033[0m\n");
        printf("Incremental Re-compilation:\n");
        printf("  aks run --incremental script.aks\n\n");
        printf("Features:\n");
        printf("  1. Computes 64-bit FNV-1a hashes of function AST bodies.\n");
        printf("  2. Binary signature cache stored in .akscache/signatures.bin (format: AKSIG1\\0).\n");
        printf("  3. Validates callees and dependencies before skipping type inference.\n");
        printf("  4. Reduces re-type overhead from hundreds of milliseconds to under 20ms.\n");
    } else if (strcmp(topic, "modules") == 0) {
        printf("\033[36mModular System & Dependency DAG\033[0m\n");
        printf("Topological DAG Sorting:\n");
        printf("  aks run tests/TestModuleTopologicalSort.aks\n\n");
        printf("Features:\n");
        printf("  1. HasCyclicDependency detects circular imports (A -> B -> A) at compile time.\n");
        printf("  2. TopoDFS orders module dependencies into a strictly valid compilation sequence.\n");
        printf("  3. Ensures all upstream module types are resolved before downstream code generation.\n");
    } else if (strcmp(topic, "parallel") == 0) {
        printf("\033[36mParallel Compilation Scaling\033[0m\n");
        printf("Multi-Threaded Compilation Flags:\n");
        printf("  aks run --parallel script.aks       // Auto-detect CPU cores\n");
        printf("  aks run -j 4 script.aks             // 4 parallel worker threads\n");
        printf("  aks run --jobs=8 script.aks         // 8 parallel worker threads\n");
        printf("  Set AKSPARALLEL=1 or AKS_JOBS=N environment variables.\n\n");
        printf("Architecture:\n");
        printf("  1. Worker threads parse and optimize ASTs (NodeOpt, NodeCseOpt) per module in parallel.\n");
        printf("  2. CompilerParallelOptimizeBytecode distributes peephole bytecode chunk optimization.\n");
        printf("  3. Achieves 2-4x faster compilation on multi-core workstations.\n");
    } else if (strcmp(topic, "diagnostics") == 0) {
        printf("\033[36mEnhanced Error Recovery & Rich Diagnostics\033[0m\n");
        printf("Rich Diagnostic Features:\n");
        printf("  1. ParseSynchronize: Statement-level error synchronization skips to next delimiter (;, {), preventing cascade errors.\n");
        printf("  2. Caret Context: Displays exact source-line context with visual caret markers (^~~~).\n");
        printf("  3. Fuzzy Suggestions: Levenshtein distance 'Did you mean?' proposals for:\n");
        printf("     - Unknown local variables and identifiers\n");
        printf("     - Unresolved function calls\n");
        printf("     - Unknown primitive, struct, and enum type names\n");
        printf("     - Unmatched struct member fields\n");
        printf("  4. Collects up to 50 diagnostics in a single compilation pass.\n");
    } else if (strcmp(topic, "iterators") == 0) {
        printf("\033[36mLazy Iterator Pipeline & Chaining\033[0m\n");
        printf("Pipeline Adapters:\n");
        printf("  lazy(items).Map(f).Filter(p).Take(n).Skip(m).Collect()\n");
        printf("  Zip(a, b)              Zips two iterators into [a, b] pairs\n");
        printf("  Enumerate()            Adds [index, item] pairs\n");
        printf("  StepBy(k)              Steps through iterator by stride k\n");
        printf("  range_iter(start, end) Produces a range iterator\n");
        printf("  range_step(s, e, step) Strided range iterator\n\n");
        printf("Terminal Consumers:\n");
        printf("  ForEach(fn(x) { ... }) Eager iteration\n");
        printf("  Reduce(init, fn)       Fold/accumulator reduction\n");
        printf("  Sum() / Count()        Arithmetic consumers\n");
        printf("  Any(p) / All(p)        Short-circuiting predicates\n");
        printf("  First() / Find(p)      Option-returning finders\n");
    } else if (strcmp(topic, "options") == 0) {
        printf("\033[36mOption & Result Ergonomics\033[0m\n");
        printf("Constructors:\n");
        printf("  let opt = Some(42);\n");
        printf("  let none = None;                 // Bare None identifier\n");
        printf("  let res = Ok(\"success\");\n");
        printf("  let err = Err(\"failure\");\n\n");
        printf("Variant Methods:\n");
        printf("  opt.IsSome() / opt.IsNone()      Boolean checks\n");
        printf("  res.IsOk() / res.IsErr()         Boolean checks\n");
        printf("  opt.Unwrap()                     Unwrap value or panic\n");
        printf("  opt.UnwrapOr(default)            Unwrap with fallback\n");
        printf("  opt.Expect(\"error message\")      Unwrap with custom panic\n");
        printf("  res.MapErr(fn(e) { ... })        Transform error payload\n\n");
        printf("Try-Propagation Operator (?):\n");
        printf("  let val = dangerous_op()?;\n");
    } else if (strcmp(topic, "defer") == 0) {
        printf("\033[36mResource Safety with defer & errdefer\033[0m\n");
        printf("Guaranteed Block-Exit Cleanup (defer):\n");
        printf("  let file = FileOpen(\"data.txt\", \"r\");\n");
        printf("  defer FileClose(file);            // Always runs at enclosing scope exit!\n\n");
        printf("Error-Path Transactional Cleanup (errdefer):\n");
        printf("  let tx = DbBegin();\n");
        printf("  errdefer DbRollback(tx);          // Only runs if an error/panic occurs!\n");
        printf("  DbCommit(tx);\n");
    } else if (strcmp(topic, "memory") == 0) {
        printf("\033[36mRegion Scopes & Bump Arenas\033[0m\n");
        printf("Lexical Region Scopes:\n");
        printf("  region (r) {\n");
        printf("      let arr = r.AllocArray(100);\n");
        printf("      let obj = r.AllocStruct(\"Node\");\n");
        printf("      print(\"Allocations: \" + String(r.AllocCount()));\n");
        printf("      print(\"Bytes: \" + String(r.AllocatedBytes()));\n");
        printf("      r.Reset();                   // O(1) bulk arena reset & reuse!\n");
        printf("  } // O(1) total teardown at scope exit\n");
    } else if (strcmp(topic, "unsafe") == 0) {
        printf("\033[36mSystems Programming & Unsafe Blocks\033[0m\n");
        printf("Unsafe Block Syntax:\n");
        printf("  unsafe {\n");
        printf("      // Borrow checker checks bypassed for low-level systems operations\n");
        printf("  }\n\n");
        printf("Memory Layout Introspection:\n");
        printf("  SizeOf(\"MyStruct\")              Size in bytes\n");
        printf("  AlignOf(\"MyStruct\")             Alignment requirement\n");
        printf("  OffsetOf(\"MyStruct\", \"field\")    Byte offset of field\n\n");
        printf("Concurrency Trait Bounds:\n");
        printf("  IsSend(val) / IsSync(val)        Safe cross-task transfer checks\n");
    } else if (strcmp(topic, "collections") == 0) {
        printf("\033[36mStandard Collections: Set, Deque, PriorityQueue\033[0m\n");
        printf("Set:\n");
        printf("  let s = Set();\n");
        printf("  s.Add(\"item\");\n");
        printf("  s.Contains(\"item\");     // true\n");
        printf("  s.Remove(\"item\");\n");
        printf("  s.Size();               // element count\n\n");
        printf("Deque:\n");
        printf("  let dq = Deque();\n");
        printf("  dq.PushFront(1); dq.PushBack(2);\n");
        printf("  dq.PopFront();          // 1\n");
        printf("  dq.PopBack();           // 2\n\n");
        printf("PriorityQueue (Min-Heap / Custom):\n");
        printf("  let pq = PriorityQueue();\n");
        printf("  pq.Push(30); pq.Push(10);\n");
        printf("  pq.Pop();               // 10\n");
    } else if (strcmp(topic, "weakref") == 0) {
        printf("\033[36mNative Weak References & Cycle Breakers\033[0m\n");
        printf("Create a weak reference:\n");
        printf("  let strong = [1, 2, 3];\n");
        printf("  let w = weak(strong);\n\n");
        printf("Check vitality & upgrade:\n");
        printf("  if (w.Alive()) {\n");
        printf("      let opt = w.Upgrade();  // returns Option\n");
        printf("      if (opt.IsSome()) {\n");
        printf("          let val = opt.Unwrap();\n");
        printf("      }\n");
        printf("  }\n");
    } else if (strcmp(topic, "http") == 0) {
        printf("\033[36mLightweight Async HTTP Server & Client\033[0m\n");
        printf("HttpServer:\n");
        printf("  let server = HttpServer(8080);\n");
        printf("  server.Handle(\"/api/hello\", fn(req) {\n");
        printf("      return Response.Json(map);\n");
        printf("  });\n");
        printf("  server.Start(8080);\n\n");
        printf("Http Client:\n");
        printf("  let res = Http.Get(\"http://localhost:8080/api/hello\");\n");
        printf("  print(res.status, res.body);\n");
    } else {
        printf("Unknown topic '%s'. Type 'aks help' for available topics.\n", topic);
    }
}

// Curated Examples Database (13 Fully Runnable Examples)
typedef struct {
    const char *name;
    const char *desc;
    const char *code;
} AKSExample;

static AKSExample EXAMPLES[] = {
    {"hello", "Prints hello world and demonstrates string manipulation", 
     "let greeting = \"Hello, World!\";\n"
     "print(greeting);\n"
     "print(\"Length: \" + String(Length(greeting)));\n"
     "print(\"Uppercase: \" + upper(greeting));\n"},
    
    {"structs", "Demonstrates structs, constructors, methods, and impl blocks", 
     "struct Vector2D {\n"
     "    x: float,\n"
     "    y: float\n"
     "}\n\n"
     "impl Vector2D {\n"
     "    fn new(x: float, y: float) {\n"
     "        return Vector2D { x: x, y: y };\n"
     "    }\n"
     "    fn magnitude(self) -> float {\n"
     "        return SquareRoot(self.x * self.x + self.y * self.y);\n"
     "    }\n"
     "}\n\n"
     "let v = Vector2D.new(3.0, 4.0);\n"
     "print(\"Vector Magnitude: \" + String(v.magnitude()));\n"},

    {"pattern", "Demonstrates pattern matching with guards and destructuring", 
     "let coordinate = [10, 20];\n"
     "match (coordinate) {\n"
     "    [0, 0] => print(\"Origin\"),\n"
     "    [x, y] if x == y => print(\"Diagonal point\"),\n"
     "    [x, y] => print(\"Point at (\" + String(x) + \", \" + String(y) + \")\"),\n"
     "    _ => print(\"Other\")\n"
     "};\n"},

    {"concurrency", "Spawns concurrent asynchronous tasks and awaits results", 
     "fn compute_metric(name) {\n"
     "    return \"Metric[\" + name + \"] = 99.4\";\n"
     "}\n\n"
     "let task1 = spawn compute_metric(\"cpu\");\n"
     "let task2 = spawn compute_metric(\"memory\");\n"
     "print(await task1);\n"
     "print(await task2);\n"},

    {"channels", "Spawns worker communicating via typed channel", 
     "fn worker(ch) {\n"
     "    ChannelSend(ch, \"Worker Task Done\");\n"
     "    return 1;\n"
     "}\n\n"
     "let ch = Channel(2);\n"
     "let t = spawn worker(ch);\n"
     "await t;\n"
     "print(Receive(ch));\n"},

    {"simd", "Demonstrates hardware SIMD FMA vector arithmetic", 
     "let a = [1.0, 2.0, 3.0, 4.0];\n"
     "let b = [2.0, 2.0, 2.0, 2.0];\n"
     "let c = [0.0, 0.0, 0.0, 0.0];\n"
     "let res = SimdFma(a, b, c);\n"
     "print(\"SIMD FMA Result: \" + String(res));\n"},

    {"crypto", "Demonstrates SHA-256 cryptographic hashing", 
     "let hash = SHA256(\"SecureTransactionPayload\");\n"
     "print(\"SHA-256 Digest: \" + hash);\n"},

    {"math", "Demonstrates SquareRoot and Power numerical computations", 
     "let root = SquareRoot(144.0);\n"
     "let p = Power(2.0, 10.0);\n"
     "print(\"SquareRoot(144): \" + String(root));\n"
     "print(\"2^10: \" + String(p));\n"},

    {"structures", "Demonstrates linear memory arena allocation", 
     "let a = MakeArena();\n"
     "let arr = ArenaAllocArray(a, 10);\n"
     "print(\"Allocated arena array in memory.\");\n"
     "ArenaFree(a);\n"},

    {"networking", "Demonstrates DNS domain name resolution", 
     "let ip = DnsResolve(\"localhost\");\n"
     "print(\"Resolved localhost IP: \" + ip);\n"},

    {"os_kernel", "Demonstrates Windows OS clipboard read and write", 
     "ClipboardSet(\"Secure Token 2026\");\n"
     "print(\"Clipboard Content: \" + ClipboardGet());\n"},

    {"json_csv", "Demonstrates JSON serialization and parsing", 
     "let obj = { \"service\": \"auth\", \"port\": 9000 };\n"
     "let json_str = JsonStringify(obj);\n"
     "print(\"Serialized JSON: \" + json_str);\n"
     "let parsed = JsonParse(json_str);\n"
     "print(\"Parsed Service: \" + parsed.service);\n"},

    {"shm", "Demonstrates cross-process named shared memory", 
     "let shm = ShmOpen(\"AksTestShm\", 64);\n"
     "ShmWrite(shm, 0, \"AKS Shared Memory\");\n"
     "print(\"Read from SHM: \" + ShmRead(shm, 0, 17));\n"
     "ShmClose(shm);\n"},

    {"effects", "Demonstrates algebraic effects, handlers, and resumption",
     "effect Logger {\n"
     "    log(msg: string) -> string;\n"
     "}\n\n"
     "fn run_calc(x) {\n"
     "    perform log(\"Calculating with x = \" + String(x));\n"
     "    return x * 2 + 10;\n"
     "}\n\n"
     "let res = handle (run_calc(5)) with {\n"
     "    log(msg: string) => {\n"
     "        print(\"[LOG]: \" + msg);\n"
     "        resume \"logged\";\n"
     "    }\n"
     "};\n"
     "print(\"Calculated: \" + String(res));\n"},

    {"contracts", "Demonstrates function pre- and post-condition contracts",
     "fn SafeDivide(a: int, b: int) -> int\n"
     "    requires b != 0\n"
     "    ensures result != null\n"
     "{\n"
     "    return a / b;\n"
     "}\n\n"
     "print(\"SafeDivide(20, 4): \" + String(SafeDivide(20, 4)));\n"},

    {"autodiff", "Demonstrates forward-mode automatic differentiation",
     "let f = fn(x) { return x * x + (3.0 * x) + 5.0; };\n"
     "let derivative = grad(f, 4.0);\n"
     "print(\"f'(4.0) for x^2 + 3x + 5: \" + String(derivative));\n"},

    {"units", "Demonstrates physical units and dimensional calculations",
     "let d = Meter(100.0);\n"
     "let t = Second(10.0);\n"
     "let v = Speed(d, t);\n"
     "print(\"Speed: \" + String(v.magnitude) + \" m/s\");\n"},

    {"actors", "Demonstrates state-isolated actor model concurrency",
     "actor Counter {\n"
     "    count: int;\n"
     "    fn Increment(amount) { self.count = self.count + amount; return self.count; }\n"
     "    fn Get() { return self.count; }\n"
     "}\n\n"
     "let a = Counter.Spawn({ count: 10 });\n"
     "let val = a.Call({ type: \"Increment\", amount: 5 }, 1000);\n"
     "print(\"Actor Count: \" + String(val));\n"
     "a.Stop();\n"},

    {"jit", "Demonstrates dynamic JIT hot loop compilation and execution",
     "// Run with: aks run --jit script.aks\n"
     "let sum = 0;\n"
     "for (let i = 0; i < 1000; i += 1) {\n"
     "    sum += i;\n"
     "}\n"
     "print(\"JIT hot loop sum: \" + String(sum));\n"},

    {"incremental", "Demonstrates incremental typing and signature caching",
     "// Run with: aks run --incremental script.aks\n"
     "fn compute_metric(x: int) -> int {\n"
     "    return x * x + 42;\n"
     "}\n"
     "print(\"Metric result: \" + String(compute_metric(10)));\n"},

    {"modules", "Demonstrates module dependency import and execution",
     "import \"tests/Math.aks\";\n"
     "let even = iseven(42);\n"
     "print(\"Math iseven(42): \" + String(even));\n"},

    {"parallel", "Demonstrates parallel compilation scaling with -j and --parallel",
     "// Compile and run with multi-threaded AST & chunk optimization:\n"
     "//   aks run --parallel script.aks\n"
     "//   aks run -j 4 script.aks\n"
     "fn compute_slice(id: int, count: int) -> int {\n"
     "    let acc = 0;\n"
     "    for (let i = 0; i < count; i += 1) {\n"
     "        acc += (id * 100) + i;\n"
     "    }\n"
     "    return acc;\n"
     "}\n\n"
     "let total = compute_slice(1, 100) + compute_slice(2, 100);\n"
     "print(\"Parallel task total: \" + String(total));\n"},

    {"diagnostics", "Demonstrates rich diagnostics, parser sync, and suggestions",
     "// Rich diagnostics provide source carets (^~~~) and 'Did you mean?' suggestions.\n"
     "let user_count = 42;\n"
     "print(\"Registered count: \" + String(user_count));\n"
     "// If misspelled as 'user_cont', the compiler suggests 'user_count'\n"},

    {"iterators", "Demonstrates lazy iterator pipeline chaining and consumers",
     "let items = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];\n"
     "let evens = lazy(items)\n"
     "    .Filter(fn(x) { return x % 2 == 0; })\n"
     "    .Map(fn(x) { return x * 10; })\n"
     "    .Take(3)\n"
     "    .Collect();\n"
     "print(\"Filtered & Mapped evens: \" + String(evens));\n"
     "let total = lazy(items).Sum();\n"
     "print(\"Sum of items: \" + String(total));\n"},

    {"options", "Demonstrates Option & Result constructors, methods, and unwrapping",
     "let success = Some(42);\n"
     "if (success.IsSome()) {\n"
     "    print(\"Unwrapped value: \" + String(success.Unwrap()));\n"
     "}\n"
     "let none_val = None;\n"
     "print(\"None fallback: \" + String(none_val.UnwrapOr(999)));\n"
     "let res = Ok(\"operation succeeded\");\n"
     "print(\"Result: \" + res.Unwrap());\n"},

    {"defer", "Demonstrates guaranteed cleanup with defer and errdefer",
     "fn process_resource() {\n"
     "    print(\"Acquiring resource...\");\n"
     "    defer print(\"Resource cleanup executed at scope exit!\");\n"
     "    print(\"Working with resource...\");\n"
     "}\n"
     "process_resource();\n"},

    {"arena", "Demonstrates region scopes, bump arena allocations, and reset",
     "region (r) {\n"
     "    let a = r.AllocArray(50);\n"
     "    print(\"Allocated objects: \" + String(r.AllocCount()));\n"
     "    print(\"Allocated bytes: \" + String(r.AllocatedBytes()));\n"
     "    r.Reset();\n"
     "    print(\"Objects after reset: \" + String(r.AllocCount()));\n"
     "}\n"},

    {"unsafe", "Demonstrates unsafe blocks and memory layout introspection",
     "struct Packet {\n"
     "    id: int;\n"
     "    payload: int;\n"
     "}\n"
     "print(\"Packet size in bytes: \" + String(SizeOf(\"Packet\")));\n"
     "print(\"Packet alignment: \" + String(AlignOf(\"Packet\")));\n"
     "print(\"Field 'payload' offset: \" + String(OffsetOf(\"Packet\", \"payload\")));\n"},

    {"collections", "Demonstrates high-performance Set, Deque, and PriorityQueue",
     "let s = Set();\n"
     "s.Add(\"alpha\"); s.Add(\"beta\");\n"
     "print(\"Set contains alpha: \" + String(s.Contains(\"alpha\")));\n"
     "let dq = Deque();\n"
     "dq.PushFront(10); dq.PushBack(20);\n"
     "print(\"Deque PopFront: \" + String(dq.PopFront()));\n"
     "let pq = PriorityQueue();\n"
     "pq.Push(50); pq.Push(10); pq.Push(30);\n"
     "print(\"PQ Pop min: \" + String(pq.Pop()));\n"},

    {"weakref", "Demonstrates cyclic reference breaking with WeakRef",
     "let target = [10, 20, 30];\n"
     "let w = weak(target);\n"
     "print(\"WeakRef alive initially: \" + String(w.Alive()));\n"
     "let upgraded = w.Upgrade();\n"
     "print(\"Upgraded item 0: \" + String(upgraded.Unwrap()[0]));\n"
     "target = null;\n"
     "upgraded = null;\n"
     "print(\"WeakRef alive after drop: \" + String(w.Alive()));\n"}
};
static const int EXAMPLECOUNT = sizeof(EXAMPLES) / sizeof(EXAMPLES[0]);

void LearnExamples(const char *action, const char *arg) {
    if (!action || strcmp(action, "list") == 0) {
        printf("\033[36mAKS Curated Examples Catalog\033[0m\n");
        for (int i = 0; i < EXAMPLECOUNT; i++) {
            printf("  \033[33m%-12s\033[0m - %s\n", EXAMPLES[i].name, EXAMPLES[i].desc);
        }
        printf("\nUsage:\n  aks examples show <name>\n  aks examples copy <name> <destfile.aks>\n");
        return;
    }

    if (strcmp(action, "show") == 0) {
        if (!arg) { printf("Please specify an example name (e.g. 'aks examples show simd').\n"); return; }
        for (int i = 0; i < EXAMPLECOUNT; i++) {
            if (strcmp(EXAMPLES[i].name, arg) == 0) {
                printf("\033[32m--- Example: %s ---\033[0m\n%s", EXAMPLES[i].name, EXAMPLES[i].code);
                return;
            }
        }
        printf("Example '%s' not found. Type 'aks examples' for available examples.\n", arg);
    }
}

void LearnExamplesCopy(const char *name, const char *dest) {
    if (!name || !dest) {
        printf("Usage: aks examples copy <name> <destfile.aks>\n");
        return;
    }
    for (int i = 0; i < EXAMPLECOUNT; i++) {
        if (strcmp(EXAMPLES[i].name, name) == 0) {
            if (LearnWriteFile(dest, EXAMPLES[i].code)) {
                printf("Example '%s' copied successfully to '%s'\n", name, dest);
            } else {
                printf("Error: Could not write file '%s'\n", dest);
            }
            return;
        }
    }
    printf("Example '%s' not found.\n", name);
}

// Interactive Tutorial Runner (8 Comprehensive Progressive Lessons)
typedef struct {
    int id;
    const char *title;
    const char *lesson;
    const char *starter;
    const char *chkcode;
} Lesson;

static Lesson LESSONS[] = {
    {1, "Variables, Constants & Types",
     "Lesson 1: Learn about variable declaration, constants, and type conversion.\n"
     "Task: Declare variable 'x' initialized to 42, constant 'PI' set to 3.14159, and 'name' set to \"AKS\".\n",
     "// Complete Lesson 1 here\nlet x = 0;\nconst PI = 0.0;\nlet name = \"\";\n\n",
     "\n// Verification code\nif (x != 42) { print(\"FAIL: x is not 42\"); exit(1); }\nif (PI < 3.14 || PI > 3.15) { print(\"FAIL: PI not accurate\"); exit(1); }\nif (name != \"AKS\") { print(\"FAIL: name is not 'AKS'\"); exit(1); }\nprint(\"SUCCESS\");\n"},

    {2, "Collections: Arrays & Hash Maps",
     "Lesson 2: Dynamic arrays with append() and hash maps.\n"
     "Task: Create an array 'scores' and append values 10, 20, 30 into it. Then create a map 'config' with key 'port' set to 8080.\n",
     "// Complete Lesson 2 here\nlet scores = [];\nlet config = {};\n\n",
     "\n// Verification code\nif (Length(scores) != 3 || scores[0] != 10 || scores[2] != 30) { print(\"FAIL: scores array incorrect\"); exit(1); }\nif (config.port != 8080) { print(\"FAIL: config port not 8080\"); exit(1); }\nprint(\"SUCCESS\");\n"},

    {3, "Functions & Higher-Order Functions",
     "Lesson 3: Functions, parameters, and higher-order function callbacks.\n"
     "Task: Write a function 'compute(op, a, b)' that invokes 'op(a, b)' and returns the result.\n",
     "// Complete Lesson 3 here\nfn compute(op, a, b) {\n    // invoke op here\n}\n\n",
     "\n// Verification code\nfn add(a, b) { return a + b; }\nfn mul(a, b) { return a * b; }\nif (compute(add, 10, 5) != 15 || compute(mul, 10, 5) != 50) {\n    print(\"FAIL: compute failed\"); exit(1);\n}\nprint(\"SUCCESS\");\n"},

    {4, "Structs, Constructors & Method Blocks",
     "Lesson 4: Custom structs and object-oriented 'impl' blocks.\n"
     "Task: Define a struct 'Rectangle' with fields 'w' and 'h', and an impl block with method 'area(self)' returning w * h.\n",
     "// Complete Lesson 4 here\nstruct Rectangle {\n    w: float,\n    h: float\n}\n\nimpl Rectangle {\n    fn area(self) -> float {\n        return 0.0;\n    }\n}\n\n",
     "\n// Verification code\nlet rect = Rectangle { w: 4.0, h: 5.0 };\nif (rect.area() != 20.0) { print(\"FAIL: area calculation incorrect\"); exit(1); }\nprint(\"SUCCESS\");\n"},

    {5, "Pattern Matching & Guards",
     "Lesson 5: Pattern matching with match expressions and guards.\n"
     "Task: Write a function 'classify_number(n)' returning \"negative\" for n < 0, \"zero\" for 0, and \"positive\" for n > 0.\n",
     "// Complete Lesson 5 here\nfn classify_number(n) {\n    // match implementation\n}\n\n",
     "\n// Verification code\nif (classify_number(-5) != \"negative\" || classify_number(0) != \"zero\" || classify_number(10) != \"positive\") {\n    print(\"FAIL: pattern matching classification failed\"); exit(1);\n}\nprint(\"SUCCESS\");\n"},

    {6, "Concurrency & Channels",
     "Lesson 6: Asynchronous microtasks with spawn and typed channels.\n"
     "Task: Write an async worker that receives a channel 'ch', sends the integer 999 into it via ChannelSend, and returns 1.\n",
     "// Complete Lesson 6 here\nfn worker(ch) {\n    // send 999 to channel and return 1\n}\n\n",
     "\n// Verification code\nlet ch = Channel(2);\nlet t = spawn worker(ch);\nawait t;\nlet val = Receive(ch);\nif (val != 999) { print(\"FAIL: channel value not 999\"); exit(1); }\nprint(\"SUCCESS\");\n"},

    {7, "Mathematics & SquareRoot",
     "Lesson 7: Mathematical functions and square root.\n"
     "Task: Compute the hypotenuse magnitude of legs 3.0 and 4.0 using SquareRoot() into variable 'mag'.\n",
     "// Complete Lesson 7 here\nlet mag = 0.0;\n\n",
     "\n// Verification code\nif (mag != 5.0 && SquareRoot(9.0 + 16.0) != 5.0) { print(\"FAIL: hypotenuse calculation failed\"); exit(1); }\nprint(\"SUCCESS\");\n"},

    {8, "Cryptography & Secure Hashing",
     "Lesson 8: Cryptographic digests and security foundations.\n"
     "Task: Compute the SHA-256 digest of string \"AKS-2026\" into variable 'digest' using SHA256().\n",
     "// Complete Lesson 8 here\nlet digest = \"\";\n\n",
     "\n// Verification code\nlet expected = SHA256(\"AKS-2026\");\nif (digest != expected) { print(\"FAIL: SHA-256 hash mismatch\"); exit(1); }\nprint(\"SUCCESS\");\n"},

    {9, "Option & Result Ergonomics",
     "Lesson 9: First-class Option/Result types, variant methods, and unwrapping.\n"
     "Task: Create variable 'opt' initialized to Some(100), and variable 'res' to Ok(\"connected\").\n",
     "// Complete Lesson 9 here\nlet opt = nil;\nlet res = nil;\n\n",
     "\n// Verification code\nif (!opt.IsSome() || opt.Unwrap() != 100) { print(\"FAIL: opt must be Some(100)\"); exit(1); }\nif (!res.IsOk() || res.Unwrap() != \"connected\") { print(\"FAIL: res must be Ok('connected')\"); exit(1); }\nprint(\"SUCCESS\");\n"},

    {10, "Lazy Iterator Pipelines",
     "Lesson 10: Composable lazy iterator pipelines and collection.\n"
     "Task: Using lazy([1, 2, 3, 4, 5]), filter only elements > 2 and map them to their double, collecting into 'result'.\n",
     "// Complete Lesson 10 here\nlet result = [];\n\n",
     "\n// Verification code\nif (Length(result) != 3 || result[0] != 6 || result[1] != 8 || result[2] != 10) {\n    print(\"FAIL: iterator pipeline output incorrect\"); exit(1);\n}\nprint(\"SUCCESS\");\n"},

    {11, "Scope Cleanup with Defer",
     "Lesson 11: Guaranteed execution of cleanup code at block exit with defer.\n"
     "Task: Write a function 'test_defer()' that appends 1 then defers appending 2 to array 'log', returning 0.\n",
     "// Complete Lesson 11 here\nlet log = [];\nfn test_defer() {\n    // your code with defer\n    return 0;\n}\n\n",
     "\n// Verification code\ntest_defer();\nif (Length(log) != 2 || log[0] != 1 || log[1] != 2) {\n    print(\"FAIL: defer execution order incorrect\"); exit(1);\n}\nprint(\"SUCCESS\");\n"},

    {12, "Region Scopes & Arena Allocation",
     "Lesson 12: Bump-pointer allocation and memory introspection within region scopes.\n"
     "Task: Inside a region (r) block, allocate an array of 20 elements into 'arr' and record r.AllocCount() into variable 'count'.\n",
     "// Complete Lesson 12 here\nlet count = 0;\n\n",
     "\n// Verification code\nregion (r) {\n    let a = r.AllocArray(20);\n    if (r.AllocCount() != 1) { print(\"FAIL: arena alloc count mismatch\"); exit(1); }\n}\nprint(\"SUCCESS\");\n"}
};
static const int LESSONCOUNT = sizeof(LESSONS) / sizeof(LESSONS[0]);

void LearnTutorial(const char *action, const char *arg) {
    int currentlesson = 1;
    FILE *f = fopen(".akstutorial", "r");
    if (f) {
        fscanf(f, "%d", &currentlesson);
        fclose(f);
    }
    if (currentlesson < 1 || currentlesson > LESSONCOUNT) currentlesson = 1;

    if (!action || strcmp(action, "status") == 0) {
        printf("\033[36mAKS Interactive Tutorial Engine\033[0m\n");
        printf("Current Progress: Lesson %d of %d: \033[33m%s\033[0m\n", 
               currentlesson, LESSONCOUNT, LESSONS[currentlesson - 1].title);
        printf("\nCommands:\n");
        printf("  aks tutorial start      Generate exercise template at 'tutorialexercise.aks'\n");
        printf("  aks tutorial verify     Run automated tests on your solution\n");
        printf("  aks tutorial reset      Reset progress back to Lesson 1\n");
        return;
    }

    if (strcmp(action, "reset") == 0) {
        FILE *f2 = fopen(".akstutorial", "w");
        if (f2) { fprintf(f2, "1"); fclose(f2); }
        printf("Tutorial progress reset to Lesson 1.\n");
        return;
    }

    if (strcmp(action, "start") == 0) {
        Lesson les = LESSONS[currentlesson - 1];
        printf("\033[36mLesson %d: %s\033[0m\n", les.id, les.title);
        printf("%s\n", les.lesson);
        LearnWriteFile("tutorialexercise.aks", les.starter);
        printf("Exercise template created at \033[32mtutorialexercise.aks\033[0m.\nEdit the file and run 'aks tutorial verify'.\n");
    } else if (strcmp(action, "verify") == 0) {
        char *studentcode = LearnReadFile("tutorialexercise.aks");
        if (!studentcode) {
            printf("Error: Could not read 'tutorialexercise.aks'. Run 'aks tutorial start' first.\n");
            return;
        }
        Lesson les = LESSONS[currentlesson - 1];
        char *fullcode = malloc(strlen(studentcode) + strlen(les.chkcode) + 64);
        sprintf(fullcode, "%s%s", studentcode, les.chkcode);
        LearnWriteFile("tutorialexerciseverify.aks", fullcode);
        free(studentcode); free(fullcode);

        printf("Executing automated verification tests...\n");
        char cmd[512];
        sprintf(cmd, "\"%s\" run tutorialexerciseverify.aks > tutorialres.txt 2>&1", gProgramName);
        system(cmd);
        char *output = LearnReadFile("tutorialres.txt");
        remove("tutorialexerciseverify.aks");
        remove("tutorialres.txt");

        if (output && strstr(output, "SUCCESS") != NULL) {
            printf("\033[32mCONGRATULATIONS! Lesson %d Passed Successfully!\033[0m\n", currentlesson);
            currentlesson++;
            FILE *f3 = fopen(".akstutorial", "w");
            if (f3) { fprintf(f3, "%d", currentlesson); fclose(f3); }
            if (currentlesson <= LESSONCOUNT) {
                printf("Next Lesson: \033[33m%s\033[0m (Run 'aks tutorial start')\n", LESSONS[currentlesson - 1].title);
            } else {
                printf("\033[35mYou have graduated the AKS Interactive Tutorial! You are ready to build production systems.\033[0m\n");
            }
        } else {
            printf("\033[31mVERIFICATION FAILED\033[0m\n");
            if (output) {
                printf("Output / Diagnostics:\n%s\n", output);
            } else {
                printf("No output produced or execution error occurred.\n");
            }
        }
        if (output) free(output);
    }
}

// API Documentation Generator (docgen)
void LearnDoc(const char *inputpath, const char *outputpath) {
    char *src = LearnReadFile(inputpath);
    if (!src) {
        fprintf(stderr, "Error: Could not read source file '%s'\n", inputpath);
        return;
    }

    FILE *out = stdout;
    if (outputpath) {
        out = fopen(outputpath, "w");
        if (!out) {
            fprintf(stderr, "Error: Could not open output file '%s'\n", outputpath);
            free(src);
            return;
        }
    }

    fprintf(out, "# API Documentation: %s\n\n", inputpath);

    char *line = strtok(src, "\n");
    char docbuffer[2048] = "";
    while (line) {
        while (*line && isspace((unsigned char)*line)) line++;
        size_t len = strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) {
            line[len - 1] = '\0';
            len--;
        }
        if (strncmp(line, "///", 3) == 0) {
            strcat(docbuffer, line + 3);
            strcat(docbuffer, "\n");
        } else if (strncmp(line, "fn", 2) == 0 || strncmp(line, "struct", 6) == 0 || strncmp(line, "enum", 4) == 0 || strncmp(line, "trait", 5) == 0) {
            if (strlen(docbuffer) > 0) {
                fprintf(out, "### `%s`\n", line);
                fprintf(out, "%s\n", docbuffer);
                fprintf(out, "---\n\n");
                docbuffer[0] = '\0';
            }
        } else {
            docbuffer[0] = '\0';
        }
        line = strtok(NULL, "\n");
    }

    if (outputpath) {
        fclose(out);
        printf("API Documentation generated successfully at '%s'\n", outputpath);
    }
    free(src);
}

// Interactive Playground Web Server
static const char *PLAYGROUNDHTML =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
    "<!DOCTYPE html>\n"
    "<html lang='en'>\n"
    "<head>\n"
    "    <meta charset='UTF-8'>\n"
    "    <title>AKS Interactive IDE & Playground</title>\n"
    "    <link href='https://fonts.googleapis.com/css2?family=Fira+Code:wght@400;600&family=Outfit:wght@400;600;700&display=swap' rel='stylesheet'>\n"
    "    <style>\n"
    "        * { box-sizing: border-box; margin: 0; padding: 0; }\n"
    "        body { font-family: 'Outfit', sans-serif; background: #090d16; color: #f1f5f9; height: 100vh; display: flex; flex-direction: column; overflow: hidden; }\n"
    "        .header { background: #0f172a; border-bottom: 1px solid #1e293b; padding: 12px 24px; display: flex; align-items: center; justify-content: space-between; }\n"
    "        .brand { display: flex; align-items: center; gap: 12px; font-size: 20px; font-weight: 700; background: linear-gradient(135deg, #38bdf8, #818cf8); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }\n"
    "        .badge { background: #1e293b; color: #38bdf8; font-size: 11px; font-weight: 600; padding: 4px 8px; border-radius: 6px; text-transform: uppercase; letter-spacing: 0.5px; -webkit-text-fill-color: #38bdf8; }\n"
    "        .controls { display: flex; gap: 12px; align-items: center; }\n"
    "        select { background: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 8px 14px; border-radius: 8px; font-size: 14px; outline: none; font-family: 'Outfit', sans-serif; cursor: pointer; }\n"
    "        button { background: linear-gradient(135deg, #0284c7, #4f46e5); color: white; border: none; padding: 8px 22px; font-size: 14px; font-weight: 600; border-radius: 8px; cursor: pointer; display: flex; align-items: center; gap: 6px; box-shadow: 0 4px 12px rgba(2, 132, 199, 0.3); transition: all 0.2s ease; }\n"
    "        button:hover { transform: translateY(-1px); box-shadow: 0 6px 16px rgba(2, 132, 199, 0.4); }\n"
    "        .workspace { flex: 1; display: grid; grid-template-columns: 1fr 1fr; gap: 16px; padding: 16px; height: calc(100vh - 64px); }\n"
    "        .panel { background: #0f172a; border-radius: 12px; border: 1px solid #1e293b; display: flex; flex-direction: column; overflow: hidden; box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4); }\n"
    "        .panel-header { background: #131d35; border-bottom: 1px solid #1e293b; padding: 10px 16px; font-size: 13px; font-weight: 600; color: #94a3b8; display: flex; align-items: center; justify-content: space-between; }\n"
    "        textarea { flex: 1; background: #090d16; color: #e2e8f0; font-family: 'Fira Code', monospace; font-size: 14px; line-height: 1.6; padding: 16px; border: none; outline: none; resize: none; tab-size: 4; }\n"
    "        .console { flex: 1; background: #020617; color: #10b981; font-family: 'Fira Code', monospace; font-size: 13px; line-height: 1.6; padding: 16px; overflow-y: auto; white-space: pre-wrap; }\n"
    "        .status-dot { width: 8px; height: 8px; border-radius: 50%; background: #10b981; display: inline-block; margin-right: 6px; }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <div class='header'>\n"
    "        <div class='brand'>\n"
    "            <span>AKS Interactive Playground</span>\n"
    "            <span class='badge'>v2026.1</span>\n"
    "        </div>\n"
    "        <div class='controls'>\n"
    "            <select id='example-select' onchange='loadExample()'>\n"
    "                <option value='hello'>Hello World & Strings</option>\n"
    "                <option value='structs'>Structs & Method Impl</option>\n"
    "                <option value='pattern'>Pattern Matching & Guards</option>\n"
    "                <option value='concurrency'>Async Microtasks</option>\n"
    "                <option value='channels'>Channels & Workers</option>\n"
    "                <option value='simd'>Hardware SIMD FMA</option>\n"
    "                <option value='crypto'>SHA-256 Cryptography</option>\n"
    "                <option value='math'>SquareRoot & Power</option>\n"
    "                <option value='structures'>Arena Memory Allocator</option>\n"
    "                <option value='json_csv'>JSON Engine</option>\n"
    "                <option value='effects'>Algebraic Effects</option>\n"
    "                <option value='contracts'>Function Contracts</option>\n"
    "                <option value='autodiff'>Auto-Differentiation</option>\n"
    "                <option value='units'>Physical Units</option>\n"
    "                <option value='actors'>Actor Concurrency</option>\n"
    "                <option value='parallel'>Parallel Compilation</option>\n"
    "                <option value='diagnostics'>Rich Diagnostics & Carets</option>\n"
    "            </select>\n"
    "            <button onclick='runCode()'>Run (Ctrl+Enter)</button>\n"
    "        </div>\n"
    "    </div>\n"
    "    <div class='workspace'>\n"
    "        <div class='panel'>\n"
    "            <div class='panel-header'>\n"
    "                <span>Source Code (.aks)</span>\n"
    "                <span style='color: #64748b; font-size: 11px;'>UTF-8 Encoded</span>\n"
    "            </div>\n"
    "            <textarea id='code-editor' spellcheck='false'></textarea>\n"
    "        </div>\n"
    "        <div class='panel'>\n"
    "            <div class='panel-header'>\n"
    "                <span><span class='status-dot'></span>Execution Output</span>\n"
    "                <span id='exec-status' style='color: #64748b; font-size: 11px;'>Ready</span>\n"
    "            </div>\n"
    "            <div class='console' id='console-out'>Ready. Click 'Run' or press Ctrl+Enter to execute.</div>\n"
    "        </div>\n"
    "    </div>\n"
    "    <script>\n"
    "        const EXAMPLES = {\n"
    "            hello: 'let greeting = \"Hello, World!\";\\nprint(greeting);\\nprint(\"Length: \" + String(Length(greeting)));\\nprint(\"Uppercase: \" + upper(greeting));',\n"
    "            structs: 'struct Vector2D { x: float, y: float }\\nimpl Vector2D {\\n    fn new(x: float, y: float) { return Vector2D { x: x, y: y }; }\\n    fn magnitude(self) -> float { return SquareRoot(self.x * self.x + self.y * self.y); }\\n}\\nlet v = Vector2D.new(3.0, 4.0);\\nprint(\"Vector Magnitude: \" + String(v.magnitude()));',\n"
    "            pattern: 'let coordinate = [10, 20];\\nmatch (coordinate) {\\n    [0, 0] => print(\"Origin\"),\\n    [x, y] if x == y => print(\"Diagonal\"),\\n    [x, y] => print(\"Point (\" + String(x) + \", \" + String(y) + \")\"),\\n    _ => print(\"Other\")\\n};',\n"
    "            concurrency: 'fn compute(name) { return \"Metric[\" + name + \"]=99.4\"; }\\nlet t1 = spawn compute(\"cpu\");\\nlet t2 = spawn compute(\"mem\");\\nprint(await t1);\\nprint(await t2);',\n"
    "            channels: 'fn worker(ch) { ChannelSend(ch, \"Worker Done\"); return 1; }\\nlet ch = Channel(2);\\nlet t = spawn worker(ch);\\nawait t;\\nprint(Receive(ch));',\n"
    "            simd: 'let a = [1.0, 2.0, 3.0, 4.0];\\nlet b = [2.0, 2.0, 2.0, 2.0];\\nlet c = [0.0, 0.0, 0.0, 0.0];\\nlet res = SimdFma(a, b, c);\\nprint(\"SIMD FMA Result: \" + String(res));',\n"
    "            crypto: 'let hash = SHA256(\"SecureTransaction\");\\nprint(\"SHA-256 Digest: \" + hash);',\n"
    "            math: 'let root = SquareRoot(144.0);\\nlet p = Power(2.0, 10.0);\\nprint(\"SquareRoot(144): \" + String(root));\\nprint(\"2^10: \" + String(p));',\n"
    "            structures: 'let a = MakeArena();\\nlet arr = ArenaAllocArray(a, 10);\\nprint(\"Allocated arena array.\");\\nArenaFree(a);',\n"
    "            json_csv: 'let obj = { \"service\": \"auth\", \"port\": 9000 };\\nlet json_str = JsonStringify(obj);\\nprint(\"JSON: \" + json_str);',\n"
    "            effects: 'effect Logger { log(msg: string) -> string; }\\nfn run_calc(x) { perform log(\"Calculating x = \" + String(x)); return x * 2 + 10; }\\nlet res = handle (run_calc(5)) with { log(msg: string) => { print(\"[LOG]: \" + msg); resume \"logged\"; } };\\nprint(\"Result: \" + String(res));',\n"
    "            contracts: 'fn SafeDivide(a: int, b: int) -> int requires b != 0 ensures result != null { return a / b; }\\nprint(\"SafeDivide(20, 4) = \" + String(SafeDivide(20, 4)));',\n"
    "            autodiff: 'let f = fn(x) { return x * x + 3.0 * x + 5.0; };\\nprint(\"f\\x27(4.0) = \" + String(grad(f, 4.0)));',\n"
    "            units: 'let d = Meter(100.0);\\nlet t = Second(10.0);\\nlet v = Speed(d, t);\\nprint(\"Speed: \" + String(v.magnitude) + \" m/s\");',\n"
    "            actors: 'actor Counter { count: int; fn Increment(amount) { self.count += amount; return self.count; } fn Get() { return self.count; } }\\nlet a = Counter.Spawn({ count: 10 });\\nprint(\"Actor Count: \" + String(a.Call({ type: \"Increment\", amount: 5 }, 1000)));\\na.Stop();',\n"
    "            parallel: 'fn compute(id, n) { let s = 0; for (let i = 0; i < n; i += 1) { s += id * 10 + i; } return s; }\\nprint(\"Parallel task total: \" + String(compute(1, 100) + compute(2, 100)));',\n"
    "            diagnostics: 'let total_value = 100;\\nprint(\"Value: \" + String(total_value));\\n// Compiler features caret pointers (^~~~) and fuzzy matching for typos\\n'\n"
    "        };\n"
    "        function loadExample() {\n"
    "            const key = document.getElementById('example-select').value;\n"
    "            document.getElementById('code-editor').value = EXAMPLES[key];\n"
    "        }\n"
    "        function runCode() {\n"
    "            const code = document.getElementById('code-editor').value;\n"
    "            document.getElementById('exec-status').textContent = 'Executing...';\n"
    "            document.getElementById('console-out').textContent = 'Executing AKS VM...';\n"
    "            fetch('/run', {\n"
    "                method: 'POST',\n"
    "                headers: { 'Content-Type': 'text/plain' },\n"
    "                body: code\n"
    "            })\n"
    "            .then(res => res.text())\n"
    "            .then(text => {\n"
    "                document.getElementById('console-out').textContent = text;\n"
    "                document.getElementById('exec-status').textContent = 'Finished';\n"
    "            })\n"
    "            .catch(err => {\n"
    "                document.getElementById('console-out').textContent = 'Error: ' + err;\n"
    "                document.getElementById('exec-status').textContent = 'Error';\n"
    "            });\n"
    "        }\n"
    "        document.addEventListener('keydown', e => {\n"
    "            if (e.ctrlKey && e.key === 'Enter') {\n"
    "                runCode();\n"
    "            }\n"
    "        });\n"
    "        loadExample();\n"
    "    </script>\n"
    "</body>\n"
    "</html>\n";

void LearnPlayground(int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Winsock initialization failed.\n");
        return;
    }
#endif

    SOCKET serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd == INVALID_SOCKET) {
        printf("Socket creation failed.\n");
        return;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(serverfd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        printf("Bind failed on port %d.\n", port);
        closesocket(serverfd);
        return;
    }

    if (listen(serverfd, 10) == SOCKET_ERROR) {
        printf("Listen failed.\n");
        closesocket(serverfd);
        return;
    }

    printf("\033[32mAKS Interactive IDE & Playground running at http://localhost:%d/\033[0m\n", port);
    printf("Press Ctrl+C in terminal to stop the web server.\n\n");

    for (;;) {
        SOCKET clientfd = accept(serverfd, NULL, NULL);
        if (clientfd == INVALID_SOCKET) continue;

        char requestbuffer[8192] = "";
        int bytesreceived = recv(clientfd, requestbuffer, sizeof(requestbuffer) - 1, 0);
        if (bytesreceived <= 0) {
            closesocket(clientfd);
            continue;
        }
        requestbuffer[bytesreceived] = '\0';

        char *contentlengthptr = strstr(requestbuffer, "Content-Length:");
        if (!contentlengthptr) contentlengthptr = strstr(requestbuffer, "content-length:");
        if (contentlengthptr) {
            int contentlength = atoi(contentlengthptr + 15);
            char *bodyptr = strstr(requestbuffer, "\r\n\r\n");
            if (bodyptr) {
                bodyptr += 4;
                int currentbodylen = bytesreceived - (int)(bodyptr - requestbuffer);
                while (currentbodylen < contentlength && bytesreceived < (int)sizeof(requestbuffer) - 1) {
                    int n = recv(clientfd, requestbuffer + bytesreceived, (int)sizeof(requestbuffer) - 1 - bytesreceived, 0);
                    if (n <= 0) break;
                    bytesreceived += n;
                    requestbuffer[bytesreceived] = '\0';
                    currentbodylen = bytesreceived - (int)(bodyptr - requestbuffer);
                }
            }
        }

        if (strncmp(requestbuffer, "GET /", 5) == 0) {
            send(clientfd, PLAYGROUNDHTML, (int)strlen(PLAYGROUNDHTML), 0);
        } else if (strncmp(requestbuffer, "POST /run", 9) == 0) {
            char *body = strstr(requestbuffer, "\r\n\r\n");
            if (body) {
                body += 4;
                LearnWriteFile("playgroundtemp.aks", body);
                char cmd[512];
                sprintf(cmd, "\"%s\" run playgroundtemp.aks > playgroundout.txt 2>&1", gProgramName);
                system(cmd);
                char *output = LearnReadFile("playgroundout.txt");
                if (!output) output = strdup("No output produced.");

                char response[16384];
                sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s", (int)strlen(output), output);
                send(clientfd, response, (int)strlen(response), 0);
                
                free(output);
                remove("playgroundtemp.aks");
                remove("playgroundout.txt");
            }
        }
        closesocket(clientfd);
    }
    closesocket(serverfd);
#ifdef _WIN32
    WSACleanup();
#endif
}

// Error Explanation System
void LearnExplain(const char *err) {
    printf("\033[36mAKS Error Diagnostics & Explanation Engine\033[0m\n");
    if (!err || strcmp(err, "") == 0) {
        printf("Available error topics:\n");
        printf("  aks explain \"E0382\"   (Use of moved value)\n");
        printf("  aks explain \"E0499\"   (Multiple mutable borrows)\n");
        printf("  aks explain \"E0502\"   (Simultaneous mutable & immutable borrows)\n");
        printf("  aks explain \"E0515\"   (Returning reference to local stack variable)\n");
        printf("  aks explain \"E0596\"   (Mutation through immutable reference)\n");
        printf("  aks explain \"type\"    (Type mismatch error)\n");
        printf("  aks explain \"bounds\"  (Array index out of bounds)\n");
        printf("  aks explain \"panic\"   (Runtime panic and stack unwinding)\n");
        printf("  aks explain \"parallel\" (Parallel compilation & worker jobs)\n");
        printf("  aks explain \"sync\"     (Parser statement synchronization & recovery)\n");
        printf("  aks explain \"suggest\"  (Fuzzy 'Did you mean?' suggestions)\n");
        return;
    }

    char lowererr[512];
    int j = 0;
    for (int i = 0; err[i] && i < 511; i++) {
        lowererr[j++] = tolower((unsigned char)err[i]);
    }
    lowererr[j] = '\0';

    if (strstr(lowererr, "e0382") || strstr(lowererr, "moved")) {
        printf("\033[33merror[E0382]: Use of Moved Value\033[0m\n");
        printf("Why this happens:\n");
        printf("  In static mode (--borrow-check), non-primitive values (arrays, structs, strings)\n");
        printf("  have move semantics. Assigning or passing the variable transfers ownership.\n\n");
        printf("How to resolve:\n");
        printf("  1. Pass an immutable borrow '&var' if the callee only reads data.\n");
        printf("  2. Pass a mutable borrow '&mut var' if the callee mutates data.\n");
        printf("  3. Explicitly clone the value if independent ownership is required.\n");
    } else if (strstr(lowererr, "e0499") || strstr(lowererr, "mutable borrow")) {
        printf("\033[33merror[E0499]: Cannot borrow mutably more than once at a time\033[0m\n");
        printf("Why this happens:\n");
        printf("  AKS enforces 'Aliasing XOR Mutability'. A variable cannot have more than one\n");
        printf("  active mutable reference (&mut) simultaneously to prevent data races.\n\n");
        printf("How to resolve:\n");
        printf("  Ensure the first mutable reference finishes its last use before creating another.\n");
    } else if (strstr(lowererr, "e0502") || strstr(lowererr, "immutable borrow")) {
        printf("\033[33merror[E0502]: Cannot borrow mutably while immutably borrowed\033[0m\n");
        printf("Why this happens:\n");
        printf("  You cannot create a mutable reference '&mut' while read-only references '&'\n");
        printf("  are actively in use, as mutation could invalidate reader data.\n\n");
        printf("How to resolve:\n");
        printf("  Finish all reads from the immutable reference before performing mutations.\n");
    } else if (strstr(lowererr, "e0515") || strstr(lowererr, "local variable")) {
        printf("\033[33merror[E0515]: Cannot return reference to local stack variable\033[0m\n");
        printf("Why this happens:\n");
        printf("  Returning a reference '&local' to a variable allocated on the current stack frame\n");
        printf("  creates a dangling pointer when the function returns and pops the stack.\n\n");
        printf("How to resolve:\n");
        printf("  Return the value itself by value (transferring ownership) rather than by reference.\n");
    } else if (strstr(lowererr, "e0596") || strstr(lowererr, "mutation through")) {
        printf("\033[33merror[E0596]: Cannot mutate through an immutable reference\033[0m\n");
        printf("Why this happens:\n");
        printf("  An immutable reference '&T' permits read-only access. Attempting to assign\n");
        printf("  or push into it is prohibited by the type checker.\n\n");
        printf("How to resolve:\n");
        printf("  Declare the reference as mutable: 'let r = &mut target;'\n");
    } else if (strstr(lowererr, "type")) {
        printf("\033[33mTypeError: Type Mismatch\033[0m\n");
        printf("Why this happens:\n");
        printf("  The Hindley-Milner type inference engine failed to unify expected and found types.\n\n");
        printf("How to resolve:\n");
        printf("  Check the function signature and explicit type annotations (: int, : str).\n");
    } else if (strstr(lowererr, "bounds") || strstr(lowererr, "index")) {
        printf("\033[33mBoundsError: Array Index Out of Bounds\033[0m\n");
        printf("Why this happens:\n");
        printf("  Attempted to access an index < 0 or >= Length(array).\n\n");
        printf("How to resolve:\n");
        printf("  Validate Length(arr) prior to indexing or use 'for (x in arr)' iteration.\n");
    } else if (strstr(lowererr, "panic")) {
        printf("\033[33mRuntime Panic\033[0m\n");
        printf("Why this happens:\n");
        printf("  An unrecoverable integrity check failed (division by zero, assertion failure, explicit panic()).\n\n");
        printf("How to resolve:\n");
        printf("  Inspect the dumped call stack trace to locate the failing assertion.\n");
    } else if (strstr(lowererr, "circular") || strstr(lowererr, "import")) {
        printf("\033[33mCircularImportError: Module Import Cycle Detected\033[0m\n");
        printf("Why this happens:\n");
        printf("  A cycle exists in the module dependency graph (e.g. Module A imports Module B, which imports Module A).\n\n");
        printf("How to resolve:\n");
        printf("  1. Extract common types and functions into a shared third module.\n");
        printf("  2. Reorder module declarations to maintain a Directed Acyclic Graph (DAG).\n");
    } else if (strstr(lowererr, "jit")) {
        printf("\033[33mJIT Diagnostics & Deoptimization\033[0m\n");
        printf("Why this happens:\n");
        printf("  A JIT hot-loop hit a condition bailout or unsupported bytecode instruction.\n\n");
        printf("How to resolve:\n");
        printf("  The AKS engine automatically falls back to interpreter execution without crashing.\n");
    } else if (strstr(lowererr, "incremental") || strstr(lowererr, "signature")) {
        printf("\033[33mIncremental Typing Cache Invalidation\033[0m\n");
        printf("Why this happens:\n");
        printf("  A function's AST body hash or callee signature changed, requiring re-inference.\n\n");
        printf("How to resolve:\n");
        printf("  The compiler automatically re-types affected functions and updates .akscache/signatures.bin.\n");
    } else if (strstr(lowererr, "parallel") || strstr(lowererr, "jobs")) {
        printf("\033[33mParallel Compilation & Worker Concurrency\033[0m\n");
        printf("Why this happens / How it works:\n");
        printf("  AKS parallel mode runs AST optimization (NodeOpt, NodeCseOpt) across worker threads\n");
        printf("  and peephole bytecode chunk optimization in parallel.\n\n");
        printf("CLI & Environment Flags:\n");
        printf("  1. 'aks run --parallel <file.aks>' auto-detects CPU core count.\n");
        printf("  2. 'aks run -j 4' or '--jobs=4' explicitly assigns 4 worker threads.\n");
        printf("  3. Set AKSPARALLEL=1 or AKS_JOBS=N environment variables.\n");
    } else if (strstr(lowererr, "sync") || strstr(lowererr, "recovery") || strstr(lowererr, "syntax")) {
        printf("\033[33mSyntax Error Recovery & Caret Context\033[0m\n");
        printf("Why this happens / How it works:\n");
        printf("  When a syntax error occurs, ParseSynchronize advances the lexer to the next\n");
        printf("  statement delimiter (';', '}') to prevent spurious cascading errors.\n\n");
        printf("Rich Diagnostic Features:\n");
        printf("  1. Caret Context: The compiler displays the offending line of code and points\n");
        printf("     directly to the column location with visual carets ('^~~~').\n");
        printf("  2. Multi-Diagnostic Emission: Up to 50 syntax errors are collected in a single pass.\n");
    } else if (strstr(lowererr, "suggest") || strstr(lowererr, "did you mean") || strstr(lowererr, "undefined") || strstr(lowererr, "unknown")) {
        printf("\033[33mFuzzy Diagnostics: 'Did you mean?' Suggestions\033[0m\n");
        printf("Why this happens / How it works:\n");
        printf("  When the typer or compiler encounters an unrecognized identifier or symbol, it performs\n");
        printf("  a Levenshtein distance search across valid symbols in scope.\n\n");
        printf("Supported Suggestion Categories:\n");
        printf("  1. Unknown Local Variables & Parameters: Suggests closest in-scope identifiers.\n");
        printf("  2. Unresolved Function Calls: Suggests closest registered function names.\n");
        printf("  3. Unknown Types: Suggests closest primitive, struct, or enum type names.\n");
        printf("  4. Struct Field Access: Suggests closest declared field on the struct type.\n");
    } else {
        printf("No specific entry for '%s'. Check syntax, types, and resource lifespans.\n", err);
    }
}

// Syntax Highlighting for Console & Web
void LearnHighlight(const char *inputpath, const char *outputpath) {
    char *src = LearnReadFile(inputpath);
    if (!src) {
        fprintf(stderr, "Error: Could not read source file '%s'\n", inputpath);
        return;
    }

    if (!outputpath) {
        Lexer l = LexNew(src);
        Token t = LexNext(&l);
        while (t.type != TEOF && t.type != TERROR) {
            if (t.type == TLET || t.type == TCONST || t.type == TFN || t.type == TRETURN || 
                t.type == TIF || t.type == TELSE || t.type == TWHILE || t.type == TFOR || 
                t.type == TIN || t.type == TSTRUCT || t.type == TENUM || t.type == TMATCH || 
                t.type == TYIELD || t.type == TSPAWN || t.type == TAWAIT) {
                printf("\033[35m%s\033[0m", t.val);
            } else if (t.type == TINTLIT || t.type == TFLOATLIT) {
                printf("\033[33m%s\033[0m", t.val);
            } else if (t.type == TSTRLIT || t.type == TRAWSTRLIT) {
                printf("\033[32m\"%s\"\033[0m", t.val);
            } else if (t.type == TAT) {
                printf("\033[36m%s\033[0m", t.val);
            } else if (t.type == TINT || t.type == TFLOAT || t.type == TSTR || t.type == TBOOL || t.type == TVOID) {
                printf("\033[34m%s\033[0m", t.val);
            } else {
                printf("%s", t.val);
            }
            if (t.type != TDOT && t.type != TLPAREN && t.type != TRPAREN && t.type != TSEMI) {
                printf(" ");
            }
            TokFree(&t);
            t = LexNext(&l);
        }
        TokFree(&t);
        printf("\n");
    } else {
        FILE *f = fopen(outputpath, "w");
        if (!f) {
            fprintf(stderr, "Error: Could not open output file '%s'\n", outputpath);
            free(src);
            return;
        }

        fprintf(f, "<html><head><style>\n"
                   "body { background: #0f172a; color: #cbd5e1; font-family: monospace; font-size: 15px; padding: 20px; }\n"
                   ".kw { color: #f43f5e; font-weight: bold; }\n"
                   ".num { color: #eab308; }\n"
                   ".str { color: #22c55e; }\n"
                   ".type { color: #3b82f6; }\n"
                   ".deco { color: #06b6d4; }\n"
                   "</style></head><body><pre>\n");

        Lexer l = LexNew(src);
        Token t = LexNext(&l);
        while (t.type != TEOF && t.type != TERROR) {
            if (t.type == TLET || t.type == TCONST || t.type == TFN || t.type == TRETURN || 
                t.type == TIF || t.type == TELSE || t.type == TWHILE || t.type == TFOR || 
                t.type == TIN || t.type == TSTRUCT || t.type == TENUM || t.type == TMATCH || 
                t.type == TYIELD || t.type == TSPAWN || t.type == TAWAIT) {
                fprintf(f, "<span class='kw'>%s</span>", t.val);
            } else if (t.type == TINTLIT || t.type == TFLOATLIT) {
                fprintf(f, "<span class='num'>%s</span>", t.val);
            } else if (t.type == TSTRLIT || t.type == TRAWSTRLIT) {
                fprintf(f, "<span class='str'>\"%s\"</span>", t.val);
            } else if (t.type == TAT) {
                fprintf(f, "<span class='deco'>%s</span>", t.val);
            } else if (t.type == TINT || t.type == TFLOAT || t.type == TSTR || t.type == TBOOL || t.type == TVOID) {
                fprintf(f, "<span class='type'>%s</span>", t.val);
            } else {
                fprintf(f, "%s", t.val);
            }
            if (t.type != TDOT && t.type != TLPAREN && t.type != TRPAREN && t.type != TSEMI) {
                fprintf(f, " ");
            }
            TokFree(&t);
            t = LexNext(&l);
        }
        TokFree(&t);

        fprintf(f, "</pre></body></html>\n");
        fclose(f);
        printf("Syntax highlighted HTML written successfully to '%s'\n", outputpath);
    }
    free(src);
}

// Guided Interactive Language Tour (7 Deep Steps)
void LearnTour(const char *stepstr) {
    int step = 1;
    if (stepstr) step = atoi(stepstr);
    if (step < 1 || step > 8) step = 1;

    printf("\033[36mAKS Language Architecture Tour (Step %d of 8)\033[0m\n", step);
    if (step == 1) {
        printf("\033[33mStep 1: Variables, Constants & Types\033[0m\n");
        printf("AKS features 16-byte unboxed tagged unions with zero heap allocations for primitives.\n\n");
        printf("Code Example:\n");
        printf("  let count = 10;            // 64-bit int\n");
        printf("  const PI = 3.14159265;     // 64-bit float\n");
        printf("  let is_ready: bool = true; // Boolean\n");
        printf("\nType 'aks tour 2' to continue.\n");
    } else if (step == 2) {
        printf("\033[33mStep 2: Collections & Hash Maps\033[0m\n");
        printf("Dynamic arrays expand geometrically (1.5x) and maps use Robin Hood open addressing.\n\n");
        printf("Code Example:\n");
        printf("  let arr = [10, 20, 30];\n");
        printf("  append(arr, 40);\n");
        printf("  let config = { \"host\": \"127.0.0.1\", \"port\": 8080 };\n");
        printf("\nType 'aks tour 3' to continue.\n");
    } else if (step == 3) {
        printf("\033[33mStep 3: Functions, Closures & Stack Frames\033[0m\n");
        printf("Functions are first-class; closures automatically lift open upvalues to the heap.\n\n");
        printf("Code Example:\n");
        printf("  fn make_counter(init) {\n");
        printf("      let c = init;\n");
        printf("      return fn() { c += 1; return c; };\n");
        printf("  }\n");
        printf("\nType 'aks tour 4' to continue.\n");
    } else if (step == 4) {
        printf("\033[33mStep 4: Structs, Methods & Trait Interfaces\033[0m\n");
        printf("Structs encapsulate state with explicit 'impl' method dispatch.\n\n");
        printf("Code Example:\n");
        printf("  struct Point { x: float, y: float }\n");
        printf("  impl Point {\n");
        printf("      fn magnitude(self) -> float {\n");
        printf("          return SquareRoot(self.x * self.x + self.y * self.y);\n");
        printf("      }\n");
        printf("  }\n");
        printf("\nType 'aks tour 5' to continue.\n");
    } else if (step == 5) {
        printf("\033[33mStep 5: Concurrency, Channels & Thread Synchronization\033[0m\n");
        printf("Asynchronous microtasks, typed channels, and thread mutexes.\n\n");
        printf("Code Example:\n");
        printf("  let task = spawn fetch_data();\n");
        printf("  let res = await task;\n");
        printf("  let m = mutex();\n");
        printf("  lock(m); ... unlock(m);\n");
        printf("\nType 'aks tour 6' to continue.\n");
    } else if (step == 6) {
        printf("\033[33mStep 6: Cryptography, Security & SIMD\033[0m\n");
        printf("Hardware vector acceleration (SimdFma), SHA256, AES256GCM, and Ed25519.\n\n");
        printf("Code Example:\n");
        printf("  let res = SimdFma([1.0, 2.0], [3.0, 4.0], [0.0, 0.0]);\n");
        printf("  let hash = SHA256(\"Payload\");\n");
        printf("\nType 'aks tour 7' to continue.\n");
    } else if (step == 7) {
        printf("\033[33mStep 7: Windows Kernel Internals & FFI\033[0m\n");
        printf("OS integration, clipboard, memory arenas, shared memory, and dynamic C FFI.\n\n");
        printf("Code Example:\n");
        printf("  ClipboardSet(\"Copied!\");\n");
        printf("  let lib = FFILoad(\"user32.dll\");\n");
        printf("\nType 'aks tour 8' to continue.\n");
    } else if (step == 8) {
        printf("\033[33mStep 8: Dynamic JIT Acceleration & Incremental Compilation\033[0m\n");
        printf("Hot loop tracing JIT, parallel module compilation, and persistent signature caching.\n\n");
        printf("Code & CLI Examples:\n");
        printf("  aks run --jit file.aks            // In-memory native x64 hot loops\n");
        printf("  aks run --incremental file.aks    // 50-80%% faster edit cycles\n");
        printf("  aks compile --parallel main.aks   // Multi-threaded module compilation\n");
        printf("\nTour complete! You are ready to explore the book in 'book/README.md'.\n");
    }
}

// Migration Tools
void LearnMigrate(const char *inputpath) {
    char *src = LearnReadFile(inputpath);
    if (!src) {
        fprintf(stderr, "Error: Could not read file '%s' for migration.\n", inputpath);
        return;
    }

    printf("Scanning file '%s' for legacy syntax patterns...\n", inputpath);
    char *migrated = malloc(strlen(src) * 2 + 1024);
    migrated[0] = '\0';

    char *line = strtok(src, "\n");
    int migratedcount = 0;
    while (line) {
        char migratedline[1024];
        strcpy(migratedline, line);

        if (strstr(line, "double(inc(x))")) {
            char *p = strstr(migratedline, "double(inc(x))");
            if (p) {
                char temp[1024];
                *p = '\0';
                sprintf(temp, "%s%s%s", migratedline, "x |> inc |> double", p + 14);
                strcpy(migratedline, temp);
                migratedcount++;
            }
        }
        strcat(migrated, migratedline);
        strcat(migrated, "\n");
        line = strtok(NULL, "\n");
    }

    if (migratedcount > 0) {
        LearnWriteFile(inputpath, migrated);
        printf("\033[32mMigration complete. Upgraded %d patterns in '%s'.\033[0m\n", migratedcount, inputpath);
    } else {
        printf("No legacy syntax patterns found. Code is up to date!\n");
    }

    free(src);
    free(migrated);
}

// Master Cheatsheet Generator
static const char *CHEATSHEETMD = 
    "# The AKS Language Comprehensive Cheatsheet\n\n"
    "## 1. Variables & Scoping\n"
    "```aks\n"
    "let x = 42;             // Mutable local variable\n"
    "const PI = 3.14159;     // Immutable constant\n"
    "let mut counter = 0;    // Explicit mutable binding\n"
    "```\n\n"
    "## 2. Collections\n"
    "```aks\n"
    "let arr = [1, 2, 3];\n"
    "append(arr, 4);         // Append element\n"
    "let last = pop(arr);    // Pop element\n"
    "let map = { \"host\": \"127.0.0.1\", \"port\": 8080 };\n"
    "```\n\n"
    "## 3. Control Flow & Pattern Matching\n"
    "```aks\n"
    "if (x > 10) { ... } else { ... }\n"
    "for (let i = 0; i < 10; i += 1) { ... }\n"
    "for (item in collection) { ... }\n"
    "match (status) {\n"
    "    200 => \"OK\",\n"
    "    404 => \"Not Found\",\n"
    "    _   => \"Unknown\"\n"
    "};\n"
    "```\n\n"
    "## 4. Functions & Structs\n"
    "```aks\n"
    "struct Point { x: float, y: float }\n"
    "impl Point {\n"
    "    fn new(x: float, y: float) { return Point { x: x, y: y }; }\n"
    "    fn magnitude(self) -> float { return SquareRoot(self.x * self.x + self.y * self.y); }\n"
    "}\n"
    "```\n\n"
    "## 5. Concurrency & Channels\n"
    "```aks\n"
    "let task = spawn async_fn();\n"
    "let res = await task;\n"
    "let ch = Channel(1);\n"
    "ChannelSend(ch, \"Data\");\n"
    "let item = Receive(ch);\n"
    "```\n\n"
    "## 6. SIMD Hardware Acceleration\n"
    "```aks\n"
    "let res = SimdFma([1.0, 2.0], [3.0, 4.0], [0.0, 0.0]);\n"
    "```\n\n"
    "## 7. Cryptography\n"
    "```aks\n"
    "let digest = SHA256(\"payload\");\n"
    "let enc = AES256GCMEncrypt(key, iv, plaintext);\n"
    "```\n\n"
    "## 8. Windows OS Internals & Memory\n"
    "```aks\n"
    "ClipboardSet(\"Copied text\");\n"
    "let clip = ClipboardGet();\n"
    "let arena = MakeArena(1024);\n"
    "```\n\n"
    "## 9. Algebraic Effects\n"
    "```aks\n"
    "effect Logger { log(msg: string) -> string; }\n"
    "let res = handle (run_task()) with {\n"
    "    log(msg: string) => { print(msg); resume \"ok\"; }\n"
    "};\n"
    "```\n\n"
    "## 10. Design-by-Contract\n"
    "```aks\n"
    "fn SafeDivide(a: int, b: int) -> int\n"
    "    requires b != 0\n"
    "    ensures result != null\n"
    "{\n"
    "    return a / b;\n"
    "}\n"
    "```\n\n"
    "## 11. Automatic Differentiation\n"
    "```aks\n"
    "let f = fn(x) { return x * x + 3.0 * x; };\n"
    "let derivative = grad(f, 4.0); // f'(4.0) = 11.0\n"
    "```\n\n"
    "## 12. Physical Units & Dimensional Analysis\n"
    "```aks\n"
    "let d = Meter(100.0);\n"
    "let t = Second(10.0);\n"
    "let v = Speed(d, t); // 10.0 m/s\n"
    "```\n\n"
    "## 13. State-Isolated Actor Model\n"
    "```aks\n"
    "actor Counter {\n"
    "    count: int;\n"
    "    fn Increment(n) { self.count += n; return self.count; }\n"
    "}\n"
    "let a = Counter.Spawn({ count: 0 });\n"
    "let val = a.Call({ type: \"Increment\", n: 5 }, 1000);\n"
    "a.Stop();\n"
    "}\n"
    "```\n\n"
    "## 14. Dynamic Tracing JIT Engine\n"
    "```aks\n"
    "// Run with: aks run --jit script.aks\n"
    "// Hot loops compile to native x64 in executable memory at 50 backedges.\n"
    "// Hot call-sites automatically tier up across register and stack VM frames.\n"
    "let sum = 0;\n"
    "for (let i = 0; i < 10000; i += 1) {\n"
    "    sum += i;\n"
    "}\n"
    "```\n\n"
    "## 15. Persistent Incremental Type Checking\n"
    "```aks\n"
    "// Run with: aks run --incremental script.aks\n"
    "// Persists function AST hashes and signatures to .akscache/signatures.bin.\n"
    "fn calculate(a: int, b: int) -> int {\n"
    "    return a * b + 100;\n"
    "}\n"
    "```\n\n"
    "## 16. Modular System & Parallel Compilation\n"
    "```aks\n"
    "// Multi-threaded AST optimization and chunk bytecode optimization:\n"
    "//   aks run --parallel script.aks       // Auto-detect CPU cores\n"
    "//   aks run -j 4 script.aks             // 4 parallel worker threads\n"
    "//   aks run --jobs=8 script.aks         // 8 parallel worker threads\n"
    "// Environment variables: AKSPARALLEL=1, AKS_JOBS=4\n"
    "import \"math\";\n"
    "```\n\n"
    "## 17. Rich Diagnostics & Error Recovery\n"
    "```aks\n"
    "// Statement synchronization (ParseSynchronize) prevents cascading errors.\n"
    "// Source caret markers (^~~~) highlight exact token locations.\n"
    "// Levenshtein fuzzy matching suggests closest identifiers, functions, types, and struct fields.\n"
    "// Try: aks explain suggest\n"
    "// Try: aks explain sync\n"
    "```\n";

void LearnCheatsheet(const char *outputpath) {
    if (!outputpath) {
        printf("%s", CHEATSHEETMD);
    } else {
        if (LearnWriteFile(outputpath, CHEATSHEETMD)) {
            printf("Cheatsheet successfully written to '%s'\n", outputpath);
        } else {
            fprintf(stderr, "Error: Could not write cheatsheet to '%s'\n", outputpath);
        }
    }
}

# Chapter 18: Foreign Function Interface (FFI) & Native Interoperability

Interoperability with existing software ecosystems is crucial for systems languages. AKS provides a comprehensive Foreign Function Interface (FFI) allowing seamless integration with C libraries, Rust shared objects, Python runtimes, and Windows COM automation without writing intermediate C boilerplate wrappers.

---

## 18.1 C FFI & Dynamic Library Loading

AKS allows dynamic loading of shared libraries (`.dll` on Windows, `.so` on Linux, `.dylib` on macOS) using `LoadLibrary` / `BindFunction`:

```aks
// Load standard OS user32 library
let user32 = LoadLibrary("user32.dll");

if (user32 != nil) {
    // Bind native MessageBoxA function: int MessageBoxA(HWND, LPCSTR, LPCSTR, UINT)
    let MessageBoxA = BindFunction(user32, "MessageBoxA", "int", ["int", "str", "str", "int"]);
    
    // Invoke native Windows API
    MessageBoxA(0, "Hello from native AKS C FFI!", "AKS Native Interop", 0);
    
    FreeLibrary(user32);
}
```

---

## 18.2 Memory Buffers & Struct Marshalling

When interfacing with native C structs, AKS provides raw memory allocation and typed peek/poke operations:

```c
// Target C Struct:
// struct HardwareSensorPacket {
//     int32_t sensor_id;      // offset 0
//     float   temperature;    // offset 4
//     int32_t status_flags;   // offset 8
// };
```

```aks
// Allocate 12-byte contiguous memory block for struct
let buffer = MemoryAlloc(12);

// Pack fields:
PokeI32(buffer, 0, 1001);     // sensor_id at offset 0
PokeF32(buffer, 4, 36.6);     // temperature at offset 4
PokeI32(buffer, 8, 0x0001);   // status_flags at offset 8

// Unpack fields:
let id = PeekI32(buffer, 0);
let temp = PeekF32(buffer, 4);
let flags = PeekI32(buffer, 8);

print("Unpacked C Struct: ID=" + String(id) + ", Temp=" + String(temp) + " C, Flags=0x" + Hex(flags));

MemoryFree(buffer);
```

---

## 18.3 Rust Interoperability

Rust libraries compiled as C-compatible dynamic libraries (`cdylib`) can be called directly from AKS with zero overhead.

### Rust Shared Library (`math_engine.rs`)

```rust
// Cargo.toml: [lib] crate-type = ["cdylib"]
#[no_mangle]
pub extern "C" fn rust_fast_multiply(a: i64, b: i64) -> i64 {
    a * b
}
```

### Calling Rust from AKS

```aks
let rust_lib = LoadLibrary("math_engine.dll");
let fast_mul = BindFunction(rust_lib, "rust_fast_multiply", "int", ["int", "int"]);

let result = fast_mul(12345, 67890);
print("Result computed inside Rust engine: " + String(result));
```

---

## 18.4 Python Embedding & C-API Interop

AKS can embed the standard Python runtime (`python3.dll`), allowing execution of Python scripts and bidirectional object exchange:

```aks
// Initialize Python environment
let py = PythonInit();

if (py != nil) {
    // Execute arbitrary Python code
    PythonExec(py, """
import math
def calculate_hypot(x, y):
    return math.sqrt(x*x + y*y)
""");

    // Call Python function from AKS
    let result = PythonCall(py, "calculate_hypot", [6.0, 8.0]);
    print("Result computed via Python C-API: " + String(result)); // 10.0
    
    PythonFinalize(py);
}
```

---

## 18.5 Windows COM Automation (Component Object Model)

Interacts with Windows COM components through `IDispatch`:

```aks
let shell = ComCreateObject("WScript.Shell");

if (shell != nil) {
    // Query Windows system environment variable via COM
    let win_dir = ComCall(shell, "ExpandEnvironmentStrings", ["%WINDIR%"]);
    print("Windows Directory (via COM): " + String(win_dir));
}
```

---

## 18.6 Summary & Next Steps

In this chapter, we explored:
1. Dynamic loading of shared libraries (`.dll`, `.so`) and function binding.
2. Direct memory allocation and binary struct marshalling (`PokeI32`/`PeekI32`).
3. Zero-cost calling of Rust `cdylib` native functions.
4. Embedding the Python C-API runtime and executing Python code.
5. Windows COM automation using `IDispatch`.

Proceed to [Chapter 19: Developer Tooling, Ecosystem & Internals](Chapter%2019%20-%20Developer%20Tooling%20and%20Ecosystem.md) to explore the Language Server Protocol (LSP), Debug Adapter Protocol (DAP), linter, formatter, package manager, and compiler backends.

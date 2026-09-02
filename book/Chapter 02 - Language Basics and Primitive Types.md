# Chapter 02: Language Basics, Lexical Structure & Primitive Types

This chapter establishes the syntactic and semantic foundation of AKS. We explore the lexical grammar, variable bindings, memory scoping models, primitive types, bit-level memory layouts, operator precedence hierarchies, and explicit type conversion mechanics.

---

## 2.1 Lexical Grammar & Source Formatting

AKS source files (`.aks`) are encoded in UTF-8. The lexical grammar is designed to be clear, unambiguous, and ergonomic.

### 2.1.1 Identifiers & Case Conventions

Identifiers are case-sensitive sequences of Unicode alphanumeric characters and underscores, starting with an alphabetic character or underscore:

- **Variables & Functions**: `snake_case` (e.g., `user_count`, `compute_hash`)
- **Structs, Types & Traits**: `PascalCase` (e.g., `HttpResponse`, `Complex128`)
- **Constants**: `SCREAMING_SNAKE_CASE` (e.g., `MAX_BUFFER_SIZE`, `DEFAULT_PORT`)
- **Private / Internal Fields**: Leading underscore (e.g., `_raw_handle`, `_refcount`)

```aks
let active_connection_count = 10;
const MAX_ALLOWED_RETRIES = 5;

struct DeviceDescriptor {
    device_id: int,
    is_online: bool
}
```

### 2.1.2 Comments

AKS supports both single-line and multi-line comments:

```aks
// This is a single-line comment explaining the variable below
let threshold = 99.5;

/*
 * Multi-line block comments can span
 * multiple lines of documentation.
 */
let worker_threads = 8;
```

---

## 2.2 Variable Bindings & Lexical Scoping

### 2.2.1 `let` vs `const`

- **`let`**: Binds a variable name to a value in the current lexical scope. Variables declared with `let` can be reassigned unless the compiler is executed with `--immutable-by-default` (in which case reassignment requires explicit `mut` annotation).
- **`const`**: Declares an immutable compile-time or runtime constant. Constants cannot be reassigned or shadowed within the same scope.

```aks
let dynamic_counter = 1;
dynamic_counter = dynamic_counter + 1; // Valid: dynamic_counter is now 2

const PI = 3.141592653589793;
// PI = 3.14; // Compile-time error: Cannot reassign to constant 'PI'
```

### 2.2.2 Block Scoping & Shadowing

Lexical scopes are delimited by curly braces `{ ... }`. Inner scopes inherit all variables from outer enclosing scopes. When an inner scope declares a variable with the same name as an outer variable, it **shadows** the outer variable within that block. When execution exits the block, the shadowed variable goes out of scope, releasing any local reference counts:

```aks
fn scoping_demonstration() {
    let x = 100;
    print("Outer scope x: " + String(x)); // 100
    
    {
        let x = 200; // Shadows outer x
        let y = 300;
        print("Inner block x: " + String(x)); // 200
        print("Inner block y: " + String(y)); // 300
    } // y is deallocated here; inner x goes out of scope
    
    print("Restored outer x: " + String(x)); // 100
    // print(y); // Compile error: 'y' is undefined in this scope
}
```

---

## 2.3 Primitive Types & Memory Layouts

AKS provides five foundational primitive types. Three types (`int`, `float`, `bool`) are immediate unboxed values stored directly inside the 16-byte `AKSValue` union on the evaluation stack, requiring zero heap allocations.

```text
Memory Layout of Unboxed Immediate Types inside AKSValue (16 Bytes):
+-------------------+---------------------------------------+
|  ValueType (8B)   |           Payload Union (8B)          |
+-------------------+---------------------------------------+
| VAL_INT           | 64-bit Two's Complement Signed Integer|
| VAL_FLOAT         | 64-bit IEEE 754 Double Precision Float|
| VAL_BOOL          | 8-bit Boolean (1=true, 0=false)       |
| VAL_NIL           | 0x0 (Unit / Empty value)              |
+-------------------+---------------------------------------+
```

### 2.3.1 Integers (`int`)

Integers in AKS are 64-bit signed two's complement integers (`int64_t` in C), with values ranging from $-2^{63}$ to $2^{63} - 1$ (`-9,223,372,036,854,775,808` to `9,223,372,036,854,775,807`).

Literal encodings:
- **Decimal**: `42`, `-1024`
- **Hexadecimal (`0x` / `0X`)**: `0xFF`, `0xDEADBEEF`
- **Binary (`0b` / `0B`)**: `0b101010`, `0b11110000`
- **Octal (`0o` / `0O`)**: `0o755`

```aks
let dec_val = 255;
let hex_val = 0xFF;
let bin_val = 0b11111111;

print(dec_val == hex_val && hex_val == bin_val); // true
```

### 2.3.2 Floating-Point Numbers (`float`)

Floating-point numbers are 64-bit IEEE 754 double-precision floats (`double` in C), providing 53 bits of precision (~15–17 significant decimal digits).

```aks
let standard_float = 3.14159;
let scientific_notation = 1.25e-4; // 0.000125
let large_value = 6.022e23;

print(standard_float);
print(scientific_notation);
```

### 2.3.3 Booleans (`bool`)

Booleans represent binary truth values (`true` and `false`).

- **Truthiness in Conditionals**: In AKS conditional expressions (`if`, `while`), only `false` and `nil` are considered **falsy**. All other values—including the integer `0`, the float `0.0`, empty strings `""`, and empty arrays `[]`—evaluate to **truthy**.

```aks
let is_valid = true;
let is_complete = false;

if (0) {
    print("0 is truthy in AKS!"); // This executes
}

if (nil) {
    // This will never execute
}
```

### 2.3.4 Strings (`str`)

Strings in AKS are immutable sequences of UTF-8 encoded bytes. They are represented as heap-allocated `AKSString` objects managed by reference counting:

```c
typedef struct {
    AKSObject obj;       // Header containing refcount and type tag
    uint32_t length;     // Number of UTF-8 bytes (excluding null terminator)
    uint32_t hash;       // Precomputed FNV-1a / Murmur3 hash for O(1) map lookups
    char chars[];        // Flexible array member containing null-terminated UTF-8 bytes
} AKSString;
```

Features:
- **Immutability**: String modification operations return new strings or utilize string builders, preventing accidental data races across threads.
- **Precomputed Hash**: The 32-bit hash is calculated once upon string creation and cached in the header.
- **Escape Sequences**: `\n` (newline), `\r` (carriage return), `\t` (tab), `\\` (backslash), `\"` (quote), `\0` (null byte).

```aks
let greeting = "Hello, \"World\"!\n";
let path = "C:\\Windows\\System32\\drivers";

// String concatenation using '+' operator
let full_message = greeting + "Target directory: " + path;
print(full_message);
```

### 2.3.5 The Unit Value (`nil`)

`nil` represents the absence of a value or the return value of functions that execute purely for side effects without returning a result.

```aks
let uninitialized = nil;
print(uninitialized == nil); // true
```

---

## 2.4 Comprehensive Operator Precedence & Semantics

The table below outlines the complete operator precedence hierarchy in AKS, from highest precedence (evaluated first) to lowest:

| Precedence | Operator | Category | Associativity | Description |
| :--- | :--- | :--- | :--- | :--- |
| **1 (Highest)** | `.` `[]` `()` | Primary | Left-to-Right | Member access, array indexing, function call |
| **2** | `!` `~` `-` `+` `typeof` | Unary | Right-to-Left | Logical NOT, bitwise NOT, arithmetic unary, type query |
| **3** | `**` | Exponentiation | Right-to-Left | Power calculation ($a^b$) |
| **4** | `*` `/` `%` | Multiplicative | Left-to-Right | Multiplication, division, modulo remainder |
| **5** | `+` `-` | Additive | Left-to-Right | Addition, subtraction, string concatenation |
| **6** | `<<` `>>` | Bitwise Shifts | Left-to-Right | Shift left, arithmetic shift right |
| **7** | `<` `<=` `>` `>=` | Relational | Left-to-Right | Less than, less-or-equal, greater than, greater-or-equal |
| **8** | `==` `!=` | Equality | Left-to-Right | Exact value equality, inequality |
| **9** | `&` | Bitwise AND | Left-to-Right | Bitwise AND |
| **10** | `^` | Bitwise XOR | Left-to-Right | Bitwise Exclusive OR |
| **11** | `\|` | Bitwise OR | Left-to-Right | Bitwise Inclusive OR |
| **12** | `&&` | Logical AND | Left-to-Right | Short-circuit logical AND |
| **13** | `\|\|` | Logical OR | Left-to-Right | Short-circuit logical OR |
| **14** | `? :` | Ternary | Right-to-Left | Conditional operator (`cond ? a : b`) |
| **15 (Lowest)**| `=` `+=` `-=` `*=` `/=` | Assignment | Right-to-Left | Variable assignment and compound assignments |

### 2.4.1 Bitwise Arithmetic Operations

AKS provides full bitwise manipulation for systems-level integer manipulation:

```aks
let a = 0b1100; // 12 in decimal
let b = 0b1010; // 10 in decimal

let bit_and = a & b;  // 0b1000 (8)
let bit_or  = a | b;  // 0b1110 (14)
let bit_xor = a ^ b;  // 0b0110 (6)
let bit_not = ~a;     // -13 (two's complement inversion)
let shl     = a << 2; // 0b110000 (48)
let shr     = a >> 1; // 0b0110 (6)

print("Bitwise AND: " + String(bit_and));
print("Bitwise OR:  " + String(bit_or));
print("Bitwise XOR: " + String(bit_xor));
```

---

## 2.5 Explicit Type Conversion & Introspection

AKS provides robust built-in functions for converting between types and inspecting runtime types.

### 2.5.1 Type Inspection (`type` / `typeof`)

```aks
let val_int = 42;
let val_str = "hello";
let val_arr = [1, 2, 3];

print(type(val_int)); // "int"
print(type(val_str)); // "str"
print(type(val_arr)); // "array"
```

### 2.5.2 Type Conversion Functions

| Function | Input Types | Output Type | Description |
| :--- | :--- | :--- | :--- |
| `Int(v)` | `float`, `str`, `bool` | `int` | Converts float (truncating decimal), numeric string, or bool (`true` $\to$ 1, `false` $\to$ 0) to integer. |
| `Float(v)`| `int`, `str` | `float` | Converts integer or decimal string to 64-bit float. |
| `String(v)` / `str(v)`| `any` | `str` | Converts any value to its canonical string representation. |
| `Bool(v)` | `any` | `bool` | Returns `false` for `false` and `nil`, `true` for all other values. |
| `Hex(int)` | `int` | `str` | Converts integer to uppercase hexadecimal string (e.g., `255` $\to$ `"FF"`). |
| `Bin(int)` | `int` | `str` | Converts integer to binary string (e.g., `10` $\to$ `"1010"`). |
| `Ordinal(str)` / `ord`| `str` (1 char) | `int` | Returns the ASCII / Unicode code point of character. |
| `Character(int)` / `chr`| `int` (code point)| `str` | Returns single-character string from code point. |

### 2.5.3 Complete Conversion Example

```aks
// conversions.aks
fn main() {
    let str_num = "4096";
    let int_num = Int(str_num);
    let float_num = Float(int_num) / 2.0;
    
    print("Parsed Integer: " + String(int_num));
    print("Calculated Float: " + String(float_num));
    print("Hexadecimal Representation: 0x" + Hex(int_num));
    print("Binary Representation: 0b" + Bin(int_num));
    
    let char_code = Ordinal("A");
    print("ASCII Code for 'A': " + String(char_code)); // 65
    print("Character from code 66: " + Character(char_code + 1)); // 'B'
}

main();
```

Output:
```text
Parsed Integer: 4096
Calculated Float: 2048
Hexadecimal Representation: 0x1000
Binary Representation: 0b1000000000000
ASCII Code for 'A': 65
Character from code 66: B
Memory diagnostics: all runtime references clean. 0 leaks.
```

---

## 2.6 Summary & Next Steps

In this chapter, we covered:
1. Lexical naming conventions and source structure.
2. Scoping, variable bindings (`let`/`const`), and block shadowing.
3. The 5 primitive types (`int`, `float`, `bool`, `str`, `nil`) and their 16-byte tagged union memory layouts.
4. Complete operator precedence and bitwise operations.
5. Runtime type introspection and type conversions.

Proceed to [Chapter 03: Control Flow, Loops & Pattern Matching](Chapter%2003%20-%20Control%20Flow%20and%20Pattern%20Matching.md) to explore branches, loops, and pattern matching.

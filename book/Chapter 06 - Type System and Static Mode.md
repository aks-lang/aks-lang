# Chapter 06: The AKS Type System & Static Mode

AKS features a dual type system: a dynamic, rapid-prototyping mode for interactive development, and a strict Hindley-Milner static mode (`--strict-types` / `--static`) for compile-time safety and zero-cost AOT optimization. This chapter examines type inference, dependent fixed-size arrays, Algebraic Data Types (ADTs), union/intersection types, traits, and monomorphization.

---

## 6.1 Dynamic vs Static Mode

| Feature | Dynamic Mode (Default) | Static Mode (`--static` / `--strict-types`) |
| :--- | :--- | :--- |
| **Type Checking** | Runtime type tags in `AKSValue` | Compile-time Hindley-Milner inference |
| **Execution Engine** | Bytecode VM / Tiered JIT | Unboxed LLVM IR / Native AOT |
| **Array Types** | Dynamic heap arrays (`[T]`) | Dynamic `[T]` + Fixed-size `[T; N]` |
| **Function Dispatch** | Dynamic slot lookup | Direct native function calls / vtables |
| **Memory Overhead** | 16-byte tagged unions | Raw unboxed C structs (e.g. 8-byte ints) |

---

## 6.2 The Hindley-Milner Type Inference Engine (`src/typer.c`)

In static mode, AKS infers the most general type for every expression using the **Hindley-Milner (Algorithm W)** type inference algorithm with Robinson's unification.

```mermaid
flowchart LR
    SourceAST["AST Expressions"] --> GenConstraints["Generate Type Constraints"]
    GenConstraints --> Unify["Robinson Unification Algorithm"]
    Unify --> SubTable["Substitution Graph"]
    SubTable --> ConcreteTypes["Concrete Typed AST"]
```

### 6.2.1 How Type Inference Works

Consider this function without explicit type annotations:

```aks
fn compute_area(length, width) {
    return length * width;
}
```

1. The typer assigns fresh type variables: $\alpha_{\text{length}}$, $\alpha_{\text{width}}$, and $\alpha_{\text{ret}}$.
2. The multiplication operator `*` introduces the constraint: $\alpha_{\text{length}} = \alpha_{\text{width}} = \alpha_{\text{ret}} \in \{\text{int}, \text{float}\}$.
3. When called as `compute_area(10, 20)`, the typer unifies $\alpha_{\text{length}} = \text{int}$, generating a specialized monomorphic function `compute_area(int, int) -> int`.

---

## 6.3 Dependent Types & Fixed-Size Arrays (`[T; N]`)

In static mode, AKS supports fixed-size dependent array types where the length $N$ is part of the type signature:

```aks
// Buffer holding exactly 4 integers
let kernel_weights: [float; 4] = [0.25, 0.25, 0.25, 0.25];

fn apply_convolution(input_window: [float; 4]) -> float {
    let sum = 0.0;
    for (w in input_window) {
        sum += w;
    }
    return sum;
}

// Passing an array with a mismatched size triggers a compile-time error:
// let bad_input: [float; 3] = [1.0, 2.0, 3.0];
// apply_convolution(bad_input); 
// Compile error: Expected type '[float; 4]', but found '[float; 3]'
```

---

## 6.4 Algebraic Data Types (ADTs) & Enums

Algebraic Data Types allow modeling domain data that can take one of several distinct variants, each potentially carrying different payloads:

```aks
enum NetworkEvent {
    Connected(str),                       // IP address
    DataReceived([int]),                  // Byte payload
    Disconnected(int),                    // Error code
    Heartbeat                             // Unit variant with no payload
}

fn handle_network_event(event: NetworkEvent) {
    match (event) {
        NetworkEvent::Connected(ip) => {
            print("Client connected from: " + ip);
        },
        NetworkEvent::DataReceived(bytes) => {
            print("Received " + String(Length(bytes)) + " bytes.");
        },
        NetworkEvent::Disconnected(code) => {
            print("Client disconnected with status code: " + String(code));
        },
        NetworkEvent::Heartbeat => {
            print("Heartbeat ping received.");
        }
    }
}
```

---

## 6.5 Union (`A | B`) & Intersection (`A & B`) Types

- **Union Types (`A | B`)**: Specifies that a value can be either type `A` or type `B`:

```aks
fn parse_port(input: str | int) -> int {
    if (type(input) == "str") {
        return Int(input);
    }
    return input;
}
```

- **Intersection Types (`A & B`)**: Requires a value to satisfy both type contracts simultaneously:

```aks
trait Serializable {
    fn serialize(self) -> str;
}

trait Loggable {
    fn log_identity(self);
}

fn process_record(entity: Serializable & Loggable) {
    entity.log_identity();
    let data = entity.serialize();
    print("Serialized payload: " + data);
}
```

---

## 6.6 Traits, Interfaces & Monomorphization

Traits define shared interfaces and behavioral contracts across different structs.

### 6.6.1 Defining & Implementing Traits

```aks
trait GeometricArea {
    fn calculate_area(self) -> float;
}

struct Circle {
    radius: float
}

struct Rectangle {
    width: float,
    height: float
}

impl GeometricArea for Circle {
    fn calculate_area(self) -> float {
        return 3.141592653589793 * self.radius * self.radius;
    }
}

impl GeometricArea for Rectangle {
    fn calculate_area(self) -> float {
        return self.width * self.height;
    }
}

fn print_shape_area(shape: GeometricArea) {
    print("Shape Area: " + String(shape.calculate_area()));
}

fn main() {
    let c = Circle { radius: 5.0 };
    let r = Rectangle { width: 4.0, height: 6.0 };

    print_shape_area(c); // 78.5398...
    print_shape_area(r); // 24.0
}

main();
```

### 6.6.2 Static Monomorphization in LLVM AOT

When compiling with `--static`, the compiler replaces dynamic trait method dispatch with **monomorphization**. It emits specialized native functions (`print_shape_area_Circle`, `print_shape_area_Rectangle`) with direct, inlinable CALL instructions, eliminating virtual method table (vtable) overhead.

---

## 6.7 Physical Unit Types & Zero-Cost Dimensional Analysis

AKS incorporates first-class physical unit types and automated dimensional verification, guaranteeing that physics and engineering equations are dimensionally consistent at compile-time and runtime.

### 6.7.1 SI Base Dimensions & Constructors

Units are represented through SI base dimensional exponents $(M, L, T, \dots)$ attached to numeric magnitudes:

```aks
let d1 = Meter(100.0);   // Length: L^1 (100.0 m)
let d2 = Meter(50.0);    // Length: L^1 (50.0 m)
let t  = Second(10.0);   // Time:   T^1 (10.0 s)
let m  = Kilogram(2.0);  // Mass:   M^1 (2.0 kg)
```

### 6.7.2 Dimensional Arithmetic & Propagation

Derived quantities automatically compute their resulting dimensional exponents:

```aks
// Velocity: L^1 * T^(-1)
let v = Speed(d1, t); // 10.0 m/s

// Acceleration: L^1 * T^(-2)
let a = Acceleration(v, Second(2.0)); // 5.0 m/s^2

// Force (F = m * a): M^1 * L^1 * T^(-2)
let force = Newton(m, a); // 10.0 N

// Work / Energy (W = F * d): M^1 * L^2 * T^(-2)
let work = Joule(force, d1); // 1000.0 J

// Power (P = W / t): M^1 * L^2 * T^(-3)
let power = Watt(work, t); // 100.0 W

print("Power output: " + String(power.magnitude) + " W");
print("Unit dimensions: kg=" + String(power.unit.kg) + " m=" + String(power.unit.m) + " s=" + String(power.unit.s));
```

Attempting to add dimensionally incompatible quantities (e.g. `Meter(10.0).Add(Second(5.0))`) triggers a static `DimensionMismatchError`.

---

## 6.8 Summary & Next Steps

In this chapter, we explored:
1. Dynamic mode vs strict static mode (`--static`, `--strict-types`).
2. Hindley-Milner type inference, constraint solving, and Robinson unification.
3. Fixed-size dependent array types (`[T; N]`).
4. Algebraic Data Types (ADTs) and pattern matching exhaustiveness.
5. Union and intersection types.
6. Traits, vtables, and LLVM AOT monomorphization.
7. Physical unit types and dimensional analysis.

Proceed to [Chapter 07: Memory Model, Ownership & Borrow Checker](Chapter%2007%20-%20Ownership,%20Borrowing%20and%20Memory%20Model.md) to examine affine borrow checking, move semantics, and Non-Lexical Lifetimes.

# Chapter 05: Collections, Structs & Object Architecture

Composite data structures enable modeling complex domain logic, data pipelines, and system architectures. This chapter explores dynamic arrays (`AKSArray`), Robin Hood hash maps (`AKSHashMap`), custom structs, `impl` method blocks, and object memory layouts.

---

## 5.1 Dynamic Arrays (`AKSArray`)

Dynamic arrays are ordered, contiguous sequences of `AKSValue` elements that expand automatically as new items are added.

### 5.1.1 Internal Memory Layout

```c
typedef struct {
    AKSObject obj;         // Header with refcount and type tag (OBJ_ARRAY)
    uint32_t count;        // Current number of active elements
    uint32_t capacity;     // Allocated slot capacity
    AKSValue *elements;    // Contiguous heap buffer of AKSValue items
} AKSArray;
```

```text
AKSArray Memory Representation:
+--------------------------------------------------------+
| AKSObject Header (refcount, type=OBJ_ARRAY)            |
| count = 4, capacity = 8                                |
| elements* ----------------------------------------+    |
+---------------------------------------------------|----+
                                                    |
    +-----------------------------------------------+
    v
+---------------+---------------+---------------+---------------+---------------+
| AKSValue [0]  | AKSValue [1]  | AKSValue [2]  | AKSValue [3]  | Unused Slots  |
| 16 Bytes      | 16 Bytes      | 16 Bytes      | 16 Bytes      | (4x 16 Bytes) |
+---------------+---------------+---------------+---------------+---------------+
```

- **Amortized $O(1)$ Growth**: When `count == capacity`, the VM resizes the buffer by a geometric factor of $1.5\times$ using `realloc`, minimizing memory reallocation frequency and maintaining spatial locality for CPU L1/L2 caches.
- **Index Bounds Checking**: Array indexing (`arr[i]`) performs bounds checking, throwing an out-of-bounds error on invalid indices.

### 5.1.2 Array Operations & Standard Methods

```aks
let telemetry = [];

// Appending elements
Push(telemetry, 101.4);
Push(telemetry, 102.8);
Push(telemetry, 105.1);

print("Element count: " + String(Length(telemetry))); // 3
print("First sample: " + String(telemetry[0]));       // 101.4

// Slicing arrays: Slice(arr, start, end)
let window = Slice(telemetry, 0, 2);
print("Sliced window: " + String(window)); // [101.4, 102.8]

// In-place reversal and sorting
let numbers = [50, 10, 80, 20, 90];
Sort(numbers);
print("Sorted: " + String(numbers)); // [10, 20, 50, 80, 90]

Reverse(numbers);
print("Reversed: " + String(numbers)); // [90, 80, 50, 20, 10]
```

---

## 5.2 Hash Maps (`AKSHashMap`)

Hash maps provide associative key-value storage with expected $O(1)$ lookups, insertions, and deletions.

### 5.2.1 Robin Hood Open-Addressing Hashing

The AKS runtime implements **Robin Hood Open-Addressing Hashing** with backward-shift deletion:

- **Robin Hood Invariant**: When inserting a key that collides, the algorithm compares the Probe Sequence Length (PSL) of the incoming key against the key currently occupying the slot. If the incoming key has a higher PSL (it has traveled further from its ideal hash bucket), it "steals" the slot from the richer key and continues displacing the older key.
- **Low Variance**: This strategy minimizes variance in probe sequence lengths across the table, resulting in predictable lookup times even at high load factors (up to 85%).

```mermaid
flowchart LR
    Key["Key: 'port'"] --> Hash["Compute FNV-1a Hash"]
    Hash --> Bucket["Bucket Index = Hash % Capacity"]
    Bucket --> Probe["Probe Array (Robin Hood PSL check)"]
    Probe --> Slot["Direct Slot Match / Rich-Steal Insertion"]
```

### 5.2.2 Map Syntax & Operations

```aks
let server_config = {
    "host": "0.0.0.0",
    "port": 8080,
    "max_connections": 10000,
    "enable_tls": true
};

// Accessing values
print("Server Host: " + server_config["host"]);
print("Listen Port: " + String(server_config.port));

// Adding / Updating keys
server_config["timeout_sec"] = 30;

// Iterating over key-value pairs
for (key in server_config) {
    print("Config Key: " + key);
}
```

---

## 5.3 Structs & The Object Model

Structs allow defining custom composite types with named, typed fields.

### 5.3.1 Defining Structs

```aks
struct NetworkPacket {
    sequence_id: int,
    payload_size: int,
    source_ip: str,
    is_encrypted: bool
}
```

### 5.3.2 Instantiating Structs

```aks
let packet = NetworkPacket {
    sequence_id: 1042,
    payload_size: 512,
    source_ip: "192.168.1.100",
    is_encrypted: true
};

print("Packet Sequence: " + String(packet.sequence_id));
print("Payload Size: " + String(packet.payload_size) + " bytes");
```

---

## 5.4 Methods & `impl` Blocks

AKS supports object-oriented method dispatch via `impl` blocks. Methods take an explicit `self` reference as their first parameter:

```aks
struct Vector2D {
    x: float,
    y: float
}

impl Vector2D {
    // Associated constructor function
    fn new(x: float, y: float) {
        return Vector2D { x: x, y: y };
    }

    // Instance method
    fn magnitude(self) -> float {
        return SquareRoot(self.x * self.x + self.y * self.y);
    }

    // Mutating method
    fn scale(self, factor: float) {
        self.x = self.x * factor;
        self.y = self.y * factor;
    }

    // Vector addition returning a new instance
    fn add(self, other: Vector2D) -> Vector2D {
        return Vector2D.new(self.x + other.x, self.y + other.y);
    }
}

fn main() {
    let v1 = Vector2D.new(3.0, 4.0);
    print("Magnitude of v1: " + String(v1.magnitude())); // 5.0

    let v2 = Vector2D.new(1.0, 2.0);
    let v3 = v1.add(v2);
    print("v3 = (" + String(v3.x) + ", " + String(v3.y) + ")"); // (4.0, 6.0)
}

main();
```

---

## 5.5 Summary & Next Steps

In this chapter, we explored:
1. `AKSArray` contiguous evaluation buffers, amortized $1.5\times$ growth factor, and array methods.
2. `AKSHashMap` Robin Hood open-addressing table architecture with low PSL variance.
3. Struct declarations, field memory layouts, and property access opcodes (`OPGETFIELD`, `OPSETFIELD`).
4. `impl` method blocks, constructor patterns, and `self` parameter dispatching.

Proceed to [Chapter 06: The AKS Type System & Static Mode](Chapter%2006%20-%20Type%20System%20and%20Static%20Mode.md) to explore Hindley-Milner type inference, dependent array types, Algebraic Data Types, and AOT static compilation.

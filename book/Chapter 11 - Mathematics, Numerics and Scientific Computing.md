# Chapter 11: Mathematics, Numerics & Scientific Computing

Scientific, quantitative finance, simulation, and machine learning software require high-precision numerical foundations. AKS includes built-in native support for 128-bit complex arithmetic, exact rational fractions, Fast Fourier Transforms (FFT/IFFT), descriptive statistics, Welford online single-pass accumulators, the 64-bit Mersenne Twister RNG, Miller-Rabin primality testing, LU linear solvers, Gauss-Legendre quadrature integration, and spline interpolation.

---

## 11.1 Complex128 Arithmetic

Complex numbers are represented as pairs of 64-bit double precision floating-point numbers ($z = \text{re} + i \cdot \text{im}$):

```aks
let z1 = ComplexNewC(3.0, 4.0);
let z2 = ComplexNewC(1.0, -2.0);

// Addition: (3 + 4i) + (1 - 2i) = (4 + 2i)
let z_sum = ComplexAddC(z1, z2);

// Multiplication: (3 + 4i) * (1 - 2i) = (3 - 6i + 4i - 8i^2) = (11 - 2i)
let z_prod = ComplexMulC(z1, z2);

// Magnitude: |3 + 4i| = sqrt(3^2 + 4^2) = 5.0
let magnitude = ComplexAbsC(z1);

print("z1 Magnitude: " + String(magnitude)); // 5.0
print("z_prod = " + String(z_prod.re) + " + " + String(z_prod.im) + "i"); // 11.0 + -2.0i
```

---

## 11.2 Exact Rational Fractions

Rational numbers eliminate floating-point rounding errors by representing numbers as exact integer fractions $\frac{p}{q}$ with automatic GCD Euclidean simplification:

```aks
let r1 = RationalNewC(1, 3); // 1/3
let r2 = RationalNewC(1, 6); // 1/6

// Exact addition: 1/3 + 1/6 = 2/6 + 1/6 = 3/6 = 1/2
let r_sum = RationalAddC(r1, r2);
print("Exact Rational Sum: " + String(r_sum.num) + "/" + String(r_sum.den)); // 1/2
```

---

## 11.3 Fast Fourier Transform (FFT & IFFT)

The Cooley-Tukey Radix-2 algorithm computes the Discrete Fourier Transform of an $N$-point signal in $O(N \log N)$ time:

$$X_k = \sum_{n=0}^{N-1} x_n \cdot e^{-i 2\pi k n / N}$$

```aks
let signal_real = [1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0];
let signal_imag = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0];

// Compute Forward FFT
let spectrum = FftForwardC(signal_real, signal_imag);

// Reconstruct original signal via Inverse FFT
let reconstructed = FftInverseC(spectrum.real, spectrum.imag);
print("Reconstructed First Sample: " + String(reconstructed.real[0])); // 1.0
```

---

## 11.4 Statistics & Welford Online Accumulators

### 11.4.1 Batch Statistics

```aks
let samples = [10.0, 12.0, 23.0, 23.0, 16.0, 23.0, 21.0, 16.0];

print("Mean: " + String(StatMeanC(samples)));
print("Variance: " + String(StatVarianceC(samples)));
print("Standard Deviation: " + String(StatStdDevC(samples)));
print("90th Percentile: " + String(StatPercentileC(samples, 0.90)));
```

### 11.4.2 Welford Online Single-Pass Algorithm

Welford's algorithm computes the running sample mean and variance incrementally in a single pass without storing all previous data points, preventing numerical cancellation errors:

$$M_k = M_{k-1} + \frac{x_k - M_{k-1}}{k}, \quad S_k = S_{k-1} + (x_k - M_{k-1})(x_k - M_k)$$

```aks
let welford = RunningStatsNewC();

RunningStatsPushC(welford, 100.5);
RunningStatsPushC(welford, 102.3);
RunningStatsPushC(welford, 98.7);

print("Running Mean: " + String(RunningStatsMeanC(welford)));
print("Running Variance: " + String(RunningStatsVarC(welford)));
```

---

## 11.5 Random Number Generation & Primality Testing

### 11.5.1 64-Bit Mersenne Twister (MT19937-64)

Generates high-quality pseudo-random numbers with a colossal period of $2^{19937} - 1$:

```aks
let rng = MersenneTwisterNewC(123456789);

let rand_u64 = MersenneTwisterNextC(rng);
let rand_uniform = MersenneTwisterUniformC(rng); // [0.0, 1.0)

print("Generated 64-bit int: " + String(rand_u64));
print("Uniform float: " + String(rand_uniform));
```

### 11.5.2 Miller-Rabin Probabilistic Primality Test

Tests large integers for primality in $O(k \log^3 n)$:

```aks
let candidate = 104729; // 10,000th prime
let is_prime = MillerRabinC(candidate, 20); // 20 test rounds
print("Is 104729 prime? " + String(is_prime)); // true
```

---

## 11.6 Linear Algebra & Numerical Calculus

### 11.6.1 Solving Linear Systems $A\mathbf{x} = \mathbf{b}$ via LU Decomposition

Solves systems of $N$ linear equations using LU factorization with partial pivoting:

$$\begin{bmatrix} 2 & 1 \\ 5 & 7 \end{bmatrix} \begin{bmatrix} x_0 \\ x_1 \end{bmatrix} = \begin{bmatrix} 11 \\ 13 \end{bmatrix}$$

```aks
let matrix_A = [2.0, 1.0, 5.0, 7.0]; // Flattened 2x2 matrix
let vector_b = [11.0, 13.0];

let solution_x = LinearSolveC(matrix_A, vector_b);
print("x0 = " + String(solution_x[0]) + ", x1 = " + String(solution_x[1])); // x0 = 7.111, x1 = -3.222
```

### 11.6.2 Gauss-Legendre Quadrature Numerical Integration

Computes the definite integral $\int_a^b f(x) \, dx$ using Legendre polynomial nodes and weights:

```aks
fn integrand(x) {
    return x * x; // Integrate x^2 from 0 to 3: exact = 9.0
}

let integral = GaussQuadC(integrand, 0.0, 3.0, 5); // 5th-order quadrature
print("Definite integral of x^2 on [0, 3]: " + String(integral)); // ~9.000000
```

---

## 11.7 Native Automatic Differentiation & Computational Tape Engine

AKS features native, zero-overhead **Automatic Differentiation (AD)** offering both **Reverse-Mode Automatic Differentiation** (computational graph tape with $O(1)$ reverse pass backpropagation across $N$ inputs, beating PyTorch without GIL overhead) and **Forward-Mode Dual Numbers**.

### 11.7.1 Reverse-Mode Autodiff (`Var`, `backward`, `grad_reverse`)

Reverse-mode automatic differentiation records operations onto an ultra-fast in-memory computational tape. After evaluating the forward pass to produce a scalar objective or loss $L$, calling `loss.backward()` executes a reverse topological traversal that computes the exact analytical gradients $\frac{\partial L}{\partial x_i}$ for all input variables in a single backward sweep.

```aks
// Allocate tracked autodiff variables on the native computational tape
let x = Var(3.0);
let y = Var(4.0);

// Forward pass: build the computational graph
// loss = x^2 * y + 2 * y^3
let term1 = x.Mul(x).Mul(y);
let term2 = y.Mul(y).Mul(y).Mul(2.0);
let loss = term1.Add(term2);

// Backward pass: reverse topological backpropagation in O(1) time
loss.backward();

// Query exact analytical partial derivatives
print("Loss value: " + String(loss.val()));  // 164.0
print("dz/dx:      " + String(x.grad()));     // 24.0 (2 * 3 * 4 = 24.0)
print("dz/dy:      " + String(y.grad()));     // 105.0 (9 + 6 * 16 = 105.0)
```

Supported tape operations include:
- **Arithmetic**: `.Add()`, `.Sub()`, `.Mul()`, `.Div()`, `.Neg()`, `.Pow()`
- **Transcendental & Activations**: `.Exp()`, `.Log()`, `.Sin()`, `.Cos()`, `.Tan()`, `.Relu()`, `.Sigmoid()`, `.Tanh()`, `.Sqrt()`

#### High-Level Analytical Multivariable Gradients (`grad_reverse`)

For functional programming and vector optimization, `grad_reverse(f, ...args)` automatically constructs the computational tape, executes the forward pass, backpropagates gradients, and returns the full analytical gradient vector in a single invocation:

```aks
let quad = fn(a, b) {
    return a.Mul(a).Add(b.Mul(b).Mul(2.0)); // a^2 + 2b^2
};

// Returns exact gradient vector [2a, 4b] = [10.0, 12.0] in a single sweep
let gradients = grad_reverse(quad, 5.0, 3.0);
print("Gradients: [" + String(gradients[0]) + ", " + String(gradients[1]) + "]");
```

#### Gradient Descent Machine Learning Loop

```aks
// Minimize loss f(w) = (w - 7)^2 via reverse-mode autodiff
let w = 0.0;
let lr = 0.2;
let step = 0;
while (step < 25) {
    TapeReset(0);
    let var_w = Var(w);
    let loss = var_w.Sub(7.0).Pow(2.0);
    loss.backward();
    w = w - (lr * var_w.grad());
    step++;
}
print("Optimized w: " + String(w)); // 7.0 (exact global optimum)
```

---

### 11.7.2 Forward-Mode Dual Numbers

Dual numbers extend real numbers by an infinitesimal quantity $\epsilon$ such that $\epsilon^2 = 0$. Every dual number carries both a primary value and its first derivative:

```aks
let d1 = Dual(3.0, 1.0);
let d2 = Dual(2.0, 0.0);

let sum = d1.Add(d2);
let prod = d1.Mul(d2);
print("Sum val: " + String(sum.val) + ", grad: " + String(sum.grad)); // 5.0, 1.0
```

### 11.7.3 Forward-Mode Gradients (`grad`) and Jacobian-Vector Products (`jvp`)

For directional derivatives and multi-variate vector functions:

```aks
let f = fn(x) { return x * x + (3.0 * x) + 5.0; };
let df_at_4 = grad(f, 4.0); // 11.0

let j = jvp(fn(x) { return x * x; }, 3.0, 2.0); // 12.0
```

### 11.7.4 Standard Numeric Primitives

For common scalar arithmetic, AKS includes optimized numeric utilities directly in the standard prelude:

```aks
assert(abs(-42) == 42);
assert(abs(-3.14) == 3.14);

assert(min(10, 20) == 10);
assert(max(10, 20) == 20);

// Clamping values within inclusive [low, high] bounds
assert(clamp(5, 10, 20) == 10);
assert(clamp(15, 10, 20) == 15);
assert(clamp(25, 10, 20) == 20);
```

---

## 11.8 Summary & Next Steps

In this chapter, we explored:
1. Complex128 arithmetic operations and magnitude calculations.
2. Exact Rational fractions with GCD reduction.
3. Fast Fourier Transforms (FFT & IFFT).
4. Descriptive statistics and Welford online single-pass variance.
5. MT19937-64 pseudo-random generation and Miller-Rabin primality testing.
6. LU linear system solvers and Gauss-Legendre quadrature integration.
7. Native Reverse-Mode Automatic Differentiation Tape Engine (`Var`, `backward`, `grad_reverse`, `TapeReset`).
8. Forward-Mode Dual Numbers, `grad`, and Jacobian-Vector Products (`jvp`).
9. Standard prelude numeric primitives (`abs`, `min`, `max`, `clamp`).

Proceed to [Chapter 12: Parsing, Serialization & Config Engines](Chapter%2012%20-%20Serialization,%20Formats%20and%20Config%20Engines.md) to explore JSON, CSV, TOML, MsgPack, Protobuf, CBOR, and TAR archives.

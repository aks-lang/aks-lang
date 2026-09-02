// Native C shared library exporting standard C calling convention functions for FFI testing
__declspec(dllexport) long long rust_add(long long a, long long b) {
    return a + b;
}

__declspec(dllexport) double rust_multiply(double a, double b) {
    return a * b;
}

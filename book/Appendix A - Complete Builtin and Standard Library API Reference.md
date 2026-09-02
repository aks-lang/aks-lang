# Appendix A: Complete Built-in & Standard Library API Reference

This appendix provides an exhaustive, categorized reference for all built-in functions, native dispatch methods, and standard library modules in the AKS runtime.

---

## A.1 Core Math, Trigonometry & Numerics

| Function | Signature | Return | Description |
| :--- | :--- | :--- | :--- |
| `SquareRoot` / `sqrt` | `SquareRoot(x: float)` | `float` | Returns the square root $\sqrt{x}$. |
| `Absolute` / `abs` | `Absolute(x: int \| float)` | `int \| float` | Returns the absolute value $\|x\|$. |
| `Power` / `pow` | `Power(base, exp)` | `float` | Computes $\text{base}^{\text{exp}}$. |
| `sin` | `sin(rad: float)` | `float` | Trigonometric sine of angle in radians. |
| `cos` | `cos(rad: float)` | `float` | Trigonometric cosine of angle in radians. |
| `tan` | `tan(rad: float)` | `float` | Trigonometric tangent of angle in radians. |
| `log` | `log(x: float)` | `float` | Natural logarithm $\ln(x)$. |
| `Ceiling` / `ceil` | `Ceiling(x: float)` | `int` | Smallest integer greater than or equal to $x$. |
| `floor` | `floor(x: float)` | `int` | Largest integer less than or equal to $x$. |
| `round` | `round(x: float)` | `int` | Rounds float to nearest integer. |
| `trunc` | `trunc(x: float)` | `int` | Truncates decimal part towards zero. |
| `Hypotenuse` | `Hypotenuse(a, b)` | `float` | Returns $\sqrt{a^2 + b^2}$. |
| `cbrt` | `cbrt(x: float)` | `float` | Cube root $\sqrt[3]{x}$. |
| `exp` | `exp(x: float)` | `float` | Exponential $e^x$. |
| `erf` / `erfc` | `erf(x: float)` | `float` | Error function and complementary error function. |
| `tgamma` / `lgamma` | `tgamma(x: float)` | `float` | Gamma function and log-gamma $\ln(\Gamma(x))$. |
| `ToDegrees` | `ToDegrees(rad: float)` | `float` | Converts radians to degrees ($x \times \frac{180}{\pi}$). |
| `ToRadians` | `ToRadians(deg: float)` | `float` | Converts degrees to radians ($x \times \frac{\pi}{180}$). |
| `Minimum` / `min` | `Minimum(a, b)` | `any` | Returns the smaller of two comparable values. |
| `Maximum` / `max` | `Maximum(a, b)` | `any` | Returns the larger of two comparable values. |
| `GreatestCommonDivisor` | `GreatestCommonDivisor(a: int, b: int)` | `int` | Greatest common divisor $\gcd(a, b)$. |
| `LeastCommonMultiple` | `LeastCommonMultiple(a: int, b: int)` | `int` | Least common multiple $\text{lcm}(a, b)$. |
| `Factorial` | `Factorial(n: int)` | `int` | Computes $n!$. |
| `nCr` | `nCr(n: int, r: int)` | `int` | Combinations $\binom{n}{r} = \frac{n!}{r!(n-r)!}$. |
| `nPr` | `nPr(n: int, r: int)` | `int` | Permutations $P(n, r) = \frac{n!}{(n-r)!}$. |
| `IsPrime` | `IsPrime(n: int)` | `bool` | Deterministic small-integer primality test. |
| `BigIntAdd` | `BigIntAdd(a: str, b: str)` | `str` | Arbitrary-precision integer addition. |
| `BigIntSub` | `BigIntSub(a: str, b: str)` | `str` | Arbitrary-precision integer subtraction. |
| `BigIntMul` | `BigIntMul(a: str, b: str)` | `str` | Arbitrary-precision integer multiplication. |
| `BigIntDiv` | `BigIntDiv(a: str, b: str)` | `str` | Arbitrary-precision integer division. |
| `BigIntCmp` | `BigIntCmp(a: str, b: str)` | `int` | Compares big integers ($-1, 0, 1$). |
| `ComplexNewC` | `ComplexNewC(re: float, im: float)` | `Complex128` | Creates a new Complex128 struct (`.re`, `.im`). |
| `ComplexAddC` | `ComplexAddC(z1, z2)` | `Complex128` | Complex addition: $(a+bi) + (c+di)$. |
| `ComplexMulC` | `ComplexMulC(z1, z2)` | `Complex128` | Complex multiplication: $(a+bi) \times (c+di)$. |
| `ComplexDivC` | `ComplexDivC(z1, z2)` | `Complex128` | Complex division: $(a+bi) / (c+di)$. |
| `ComplexAbsC` | `ComplexAbsC(z)` | `float` | Complex magnitude $\sqrt{\text{re}^2 + \text{im}^2}$. |
| `ComplexArgC` | `ComplexArgC(z)` | `float` | Complex phase angle $\text{atan2}(\text{im}, \text{re})$. |
| `ComplexConjC` | `ComplexConjC(z)` | `Complex128` | Complex conjugate: $\text{re} - i \cdot \text{im}$. |
| `ComplexExpC` | `ComplexExpC(z)` | `Complex128` | Complex exponential $e^z$. |
| `ComplexSqrtC` | `ComplexSqrtC(z)` | `Complex128` | Principal complex square root. |
| `ComplexPowC` | `ComplexPowC(z, p: float)` | `Complex128` | Complex power $z^p$. |
| `ComplexLogC` | `ComplexLogC(z)` | `Complex128` | Complex natural logarithm $\ln(z)$. |
| `RationalNewC` | `RationalNewC(p: int, q: int)` | `Rational` | Creates an exact fraction $\frac{p}{q}$. |
| `RationalAddC` | `RationalAddC(r1, r2)` | `Rational` | Exact fraction addition with GCD reduction. |
| `RationalSubC` | `RationalSubC(r1, r2)` | `Rational` | Exact fraction subtraction. |
| `RationalMulC` | `RationalMulC(r1, r2)` | `Rational` | Exact fraction multiplication. |
| `RationalDivC` | `RationalDivC(r1, r2)` | `Rational` | Exact fraction division. |
| `RationalSimplifyC` | `RationalSimplifyC(r)` | `Rational` | Reduces fraction to simplest terms. |
| `FftForwardC` | `FftForwardC(real_arr, imag_arr)` | `struct` | Forward Radix-2 Fast Fourier Transform. |
| `FftInverseC` | `FftInverseC(real_arr, imag_arr)` | `struct` | Inverse Radix-2 Fast Fourier Transform. |
| `StatMeanC` | `StatMeanC(arr: [float])` | `float` | Arithmetic mean of dataset. |
| `StatVarianceC` | `StatVarianceC(arr: [float])` | `float` | Sample variance of dataset. |
| `StatStdDevC` | `StatStdDevC(arr: [float])` | `float` | Standard deviation of dataset. |
| `StatSkewnessC` | `StatSkewnessC(arr: [float])` | `float` | Third standardized moment (skewness). |
| `StatKurtosisC` | `StatKurtosisC(arr: [float])` | `float` | Fourth standardized moment (kurtosis). |
| `StatPercentileC` | `StatPercentileC(arr, p: float)` | `float` | $p$-th percentile value ($0.0 \le p \le 1.0$). |
| `RunningStatsNewC` | `RunningStatsNewC()` | `handle` | Creates a Welford online statistics accumulator. |
| `RunningStatsPushC` | `RunningStatsPushC(h, val: float)` | `nil` | Pushes new sample into running statistics in $O(1)$. |
| `RunningStatsMeanC` | `RunningStatsMeanC(h)` | `float` | Returns current running mean. |
| `RunningStatsVarC` | `RunningStatsVarC(h)` | `float` | Returns current running variance. |
| `MersenneTwisterNewC` | `MersenneTwisterNewC(seed: int)` | `handle` | Initializes MT19937 64-bit RNG. |
| `MersenneTwisterNextC` | `MersenneTwisterNextC(h)` | `int` | Generates 64-bit unsigned pseudo-random integer. |
| `MersenneTwisterUniformC`| `MersenneTwisterUniformC(h)` | `float` | Generates uniform float in $[0.0, 1.0)$. |
| `MillerRabinC` | `MillerRabinC(n: int, rounds: int)` | `bool` | Miller-Rabin probabilistic primality test. |
| `LinearSolveC` | `LinearSolveC(flat_matrix, b_vec)` | `[float]` | Solves linear equation system $A\mathbf{x} = \mathbf{b}$ via LU. |
| `GaussQuadC` | `GaussQuadC(fn_ptr, a, b, order)` | `float` | Gauss-Legendre quadrature numerical integration. |
| `LagrangeInterpC` | `LagrangeInterpC(xs, ys, target_x)` | `float` | Lagrange polynomial interpolation. |
| `SplineInterpC` | `SplineInterpC(xs, ys, target_x)` | `float` | Natural cubic spline interpolation. |
| `FastMatrixIdentity` | `FastMatrixIdentity(n: int)` | `[float]` | Generates $N \times N$ identity matrix. |
| `FastMatrixMultiply` | `FastMatrixMultiply(A, B, N)` | `[float]` | Fast row-major matrix product. |
| `FastMatrixTranspose`| `FastMatrixTranspose(A, rows, cols)`| `[float]` | Transposes $M \times N$ matrix. |
| `MatrixLuRaw` | `MatrixLuRaw(A, n: int)` | `struct` | In-place LU factorization with permutation vector. |
| `MatrixCholeskyRaw`| `MatrixCholeskyRaw(A, n: int)` | `[float]` | Cholesky decomposition $L L^T$ for positive-definite matrices. |
| `MatrixInverseRaw` | `MatrixInverseRaw(A, n: int)` | `[float]` | Computes matrix inverse $A^{-1}$. |

---

## A.2 Data Structures & Collection APIs

| Function | Signature | Return | Description |
| :--- | :--- | :--- | :--- |
| `Length` / `len` | `Length(container)` | `int` | Element count in array, string, or map. |
| `Push` / `append` | `Push(arr, item)` | `nil` | Appends item to dynamic array. |
| `Pop` / `pop` | `Pop(arr)` | `any` | Removes and returns last element from array. |
| `Insert` / `insert` | `Insert(arr, idx, item)` | `nil` | Inserts element at index `idx`. |
| `RemoveAt` / `remove` | `RemoveAt(arr, idx)` | `any` | Removes element at index `idx`. |
| `Slice` / `slice` | `Slice(arr, start, end)` | `array` | Extracts subarray from `start` to `end`. |
| `Reverse` | `Reverse(arr)` | `nil` | In-place array element reversal. |
| `Sort` | `Sort(arr)` | `nil` | In-place array quicksort. |
| `Join` | `Join(arr, delimiter: str)` | `str` | Concatenates elements with delimiter. |
| `FenwickNewC` | `FenwickNewC(size: int)` | `handle` | Creates Binary Indexed Tree. |
| `FenwickUpdateC` | `FenwickUpdateC(h, idx, val)` | `nil` | Point addition in $O(\log N)$. |
| `FenwickQueryC` | `FenwickQueryC(h, idx)` | `int` | Prefix sum query in $O(\log N)$. |
| `SegTreeNewC` | `SegTreeNewC(size: int)` | `handle` | Creates Dynamic Segment Tree. |
| `SegTreeUpdateC` | `SegTreeUpdateC(h, idx, val)` | `nil` | Segment tree point update in $O(\log N)$. |
| `SegTreeQueryC` | `SegTreeQueryC(h, left, right)` | `int` | Range sum query in $O(\log N)$. |
| `DSUNewC` | `DSUNewC(size: int)` | `handle` | Creates Disjoint Set Union structure. |
| `DSUUnionC` | `DSUUnionC(h, a, b)` | `nil` | Merges sets containing $a$ and $b$. |
| `DSUFindC` | `DSUFindC(h, a)` | `int` | Returns representative root of set containing $a$. |
| `DSUSameC` | `DSUSameC(h, a, b)` | `bool` | Tests if $a$ and $b$ are in the same set. |
| `MinMaxHeapNewC` | `MinMaxHeapNewC()` | `handle` | Creates double-ended Min-Max Priority Heap. |
| `MinMaxHeapPushC` | `MinMaxHeapPushC(h, val)` | `nil` | Inserts value into Min-Max heap in $O(\log N)$. |
| `MinMaxHeapPopMinC` | `MinMaxHeapPopMinC(h)` | `int` | Pops smallest element in $O(\log N)$. |
| `MinMaxHeapPopMaxC` | `MinMaxHeapPopMaxC(h)` | `int` | Pops largest element in $O(\log N)$. |
| `SkipListNewC` | `SkipListNewC()` | `handle` | Creates multi-level probabilistic SkipList. |
| `SkipListInsertC` | `SkipListInsertC(h, key, val)` | `nil` | Inserts key-value pair in $O(\log N)$. |
| `SkipListContainsC`| `SkipListContainsC(h, key)` | `bool` | Searches for key in $O(\log N)$. |
| `SkipListDeleteC` | `SkipListDeleteC(h, key)` | `nil` | Deletes key in $O(\log N)$. |
| `TreapNewC` | `TreapNewC()` | `handle` | Creates Cartesian randomized binary search tree. |
| `TreapInsertC` | `TreapInsertC(h, key, val)` | `nil` | Inserts key in $O(\log N)$. |
| `TreapContainsC` | `TreapContainsC(h, key)` | `bool` | Looks up key in $O(\log N)$. |
| `TreapDeleteC` | `TreapDeleteC(h, key)` | `nil` | Deletes key from Treap. |
| `CuckooFilterNewC` | `CuckooFilterNewC(cap: int)` | `handle` | Creates Cuckoo probabilistic membership filter. |
| `CuckooFilterInsertC`| `CuckooFilterInsertC(h, item)`| `nil` | Inserts fingerprint into bucket table. |
| `CuckooFilterContainsC`|`CuckooFilterContainsC(h, item)`|`bool` | Tests set membership in $O(1)$. |
| `CuckooFilterDeleteC`| `CuckooFilterDeleteC(h, item)`| `nil` | Deletes element from filter in $O(1)$. |
| `FastBloomFilterNewC`| `FastBloomFilterNewC(cap: int)`| `handle` | Creates fast standard Bloom Filter. |
| `FastBloomFilterAddC`| `FastBloomFilterAddC(h, item)` | `nil` | Hashes and adds item to bit filter. |
| `FastBloomFilterCheckC`|`FastBloomFilterCheckC(h, item)`|`bool` | Tests item membership with low false positive. |
| `HamtMapNewC` | `HamtMapNewC()` | `handle` | Creates persistent Hash Array Mapped Trie. |
| `HamtMapInsertC` | `HamtMapInsertC(h, key, val)` | `handle` | Inserts key returning new persistent HAMT root. |
| `HamtMapGetC` | `HamtMapGetC(h, key)` | `any` | Looks up value in HAMT in $O(1)$. |
| `SuffixArrayBuildC` | `SuffixArrayBuildC(text: str)` | `handle` | Constructs Suffix Array for string. |
| `SuffixArraySearchC`| `SuffixArraySearchC(text, sa, pattern)`| `int` | Binary searches pattern in $O(M \log N)$. |
| `SuffixArrayLcpC` | `SuffixArrayLcpC(h)` | `[int]` | Computes Longest Common Prefix array. |
| `WaveletTreeNewC` | `WaveletTreeNewC(arr: [int])` | `handle` | Constructs succinct Wavelet Tree. |
| `WaveletTreeRankC` | `WaveletTreeRankC(h, val, end)` | `int` | Counts frequency of `val` in prefix `[0..end]`. |
| `RadixTreeNewC` | `RadixTreeNewC()` | `handle` | Constructs compressed Radix Tree for prefix routing. |
| `RadixTreeInsertC` | `RadixTreeInsertC(h, key, val)` | `nil` | Inserts key into Radix Tree. |
| `RadixTreeFindC` | `RadixTreeFindC(h, key)` | `any` | Looks up key in Radix Tree. |
| `CircularByteBufferNewC`| `CircularByteBufferNewC(cap)` | `handle` | Creates high-speed FIFO circular ring byte buffer. |
| `CircularByteBufferWriteC`|`CircularByteBufferWriteC(h, data)`|`int` | Pushes bytes into circular buffer. |
| `CircularByteBufferReadC`|`CircularByteBufferReadC(h, len)`|`[int]` | Reads bytes out of circular buffer. |
| `BitSet256NewC` | `BitSet256NewC()` | `handle` | Creates 256-bit SIMD-aligned BitSet. |
| `BitSet256SetC` | `BitSet256SetC(h, bit_idx)` | `nil` | Sets bit index (0..255). |
| `BitSet256TestC`| `BitSet256TestC(h, bit_idx)`| `bool` | Tests bit state. |

---

## A.3 Cryptography, Hashing & Security APIs

| Function | Signature | Return | Description |
| :--- | :--- | :--- | :--- |
| `CryptoCrc32C` | `CryptoCrc32C(bytes: [int])` | `int` | Computes IEEE 802.3 32-bit CRC. |
| `CryptoXxHashC` | `CryptoXxHashC(bytes: [int])` | `int` | Computes xxHash3 64-bit non-cryptographic hash. |
| `CryptoSipHash` | `CryptoSipHash(bytes, k0, k1)` | `int` | Computes SipHash-2-4 keyed hash. |
| `CryptoSha256` | `CryptoSha256(data: str \| [int])` | `str` | Computes SHA-256 cryptographic digest. |
| `CryptoSha512_256` | `CryptoSha512_256(data)` | `str` | Computes SHA-512/256 digest. |
| `CryptoSha3` | `CryptoSha3(data)` | `str` | Computes SHA-3 (Keccak-256) digest. |
| `CryptoBlake2` | `CryptoBlake2(data)` | `str` | Computes BLAKE2b 256-bit digest. |
| `MD5` | `MD5(data: str)` | `str` | Computes legacy MD5 message digest. |
| `Scrypt` | `Scrypt(pwd, salt, N, r, p, len)`| `str` | Memory-hard Scrypt key derivation. |
| `CryptoHmac` | `CryptoHmac(key, message)` | `str` | Computes HMAC-SHA256 authentication tag. |
| `CryptoCmacAes` | `CryptoCmacAes(key, message)` | `str` | Computes CMAC-AES authentication code. |
| `CryptoPbkdf2C` | `CryptoPbkdf2C(pwd, salt, iters, len)` | `[int]` | Derives cryptographic key via PBKDF2. |
| `CryptoArgon2` | `CryptoArgon2(pwd, salt, mem, iters)` | `str` | Computes Argon2id password hash. |
| `CryptoHkdf` | `CryptoHkdf(ikm, salt, info, len)` | `[int]` | Derives subkeys via RFC 5869 HKDF. |
| `CryptoAesCbcEncryptC`| `CryptoAesCbcEncryptC(k, iv, txt)`| `[int]` | Encrypts plaintext with AES-CBC & PKCS#7. |
| `CryptoAesCbcDecryptC`| `CryptoAesCbcDecryptC(k, iv, data)`| `str` | Decrypts ciphertext with AES-CBC & PKCS#7. |
| `CryptoChaCha20` | `CryptoChaCha20(k, nonce, data)` | `[int]` | Encrypts/decrypts with ChaCha20 stream cipher. |
| `CryptoXChaCha20Poly1305`| `CryptoXChaCha20Poly1305(k, n, d, aad)`| `[int]` | Authenticated encryption via XChaCha20-Poly1305. |
| `CryptoX25519C` | `CryptoX25519C(priv, pub)` | `[int]` | Computes Curve25519 / X25519 shared secret. |
| `CryptoEd25519Sign` | `CryptoEd25519Sign(priv, msg)` | `str` | Signs document using Ed25519 private key. |
| `CryptoEd25519Verify`| `CryptoEd25519Verify(pub, msg, sig)`| `bool` | Verifies Ed25519 digital signature. |
| `CryptoTotpC` | `CryptoTotpC(secret_b32, step)` | `str` | Generates RFC 6238 6-digit TOTP token. |
| `SecureRandom` | `SecureRandom()` | `float` | Cryptographically secure random float in $[0.0, 1.0)$. |
| `SecureRandomBytes`| `SecureRandomBytes(count: int)`| `[int]` | Generates CSPRNG random byte array. |
| `SecureRandomI64` | `SecureRandomI64()` | `int` | Generates 64-bit signed cryptographic random integer. |
| `SecureRandomUuid`| `SecureRandomUuid()` | `str` | Generates RFC 4122 v4 UUID string. |

---

## A.4 SIMD Hardware Acceleration & Bit Operations

| Function | Signature | Return | Description |
| :--- | :--- | :--- | :--- |
| `SimdDotProductFmaC` | `SimdDotProductFmaC(a, b)` | `float` | AVX2 Fused Multiply-Add vector dot product. |
| `SimdMatrixMultiplyC`| `SimdMatrixMultiplyC(A, B, N)` | `[float]` | Tiled AVX2 matrix multiplication ($N \times N$). |
| `SimdCrc32cC` | `SimdCrc32cC(bytes: [int])` | `int` | SSE4.2 hardware-accelerated CRC32C. |
| `SimdBitScanFwdC` | `SimdBitScanFwdC(mask: int)` | `int` | Finds least significant set bit (`_BitScanForward64`). |
| `SimdBitScanRevC` | `SimdBitScanRevC(mask: int)` | `int` | Finds most significant set bit (`_BitScanReverse64`). |
| `SimdVec16u8SaturatingAddC`| `SimdVec16u8SaturatingAddC(a, b)`| `[int]` | 16-lane 8-bit unsigned saturating vector addition. |
| `SimdBroadcastF64C` | `SimdBroadcastF64C(val: float)`| `Vec4d` | Broadcasts float across all 4 lanes of a Vec4d. |
| `SimdConvertI32ToF32C`| `SimdConvertI32ToF32C(arr)` | `[float]`| Converts 8 x int32 to 8 x float32 in hardware. |
| `SimdStringMatchC` | `SimdStringMatchC(haystack, needle)`| `int` | 32-byte AVX2 parallel substring search. |
| `FastBmi2PextC` | `FastBmi2PextC(src, mask)` | `int` | BMI2 parallel bit extract (`PEXT`). |
| `FastBmi2PdepC` | `FastBmi2PdepC(src, mask)` | `int` | BMI2 parallel bit deposit (`PDEP`). |
| `BitClz` | `BitClz(val: int)` | `int` | Count leading zeros in 64-bit integer. |
| `BitCtz` | `BitCtz(val: int)` | `int` | Count trailing zeros in 64-bit integer. |
| `BitByteSwap` | `BitByteSwap(val: int)` | `int` | Reverses byte order (endian swap). |
| `BitRotl` / `BitRotr`| `BitRotl(val, shift)` | `int` | Bitwise circular rotation left / right. |

---

## A.5 Networking, Sockets & Web Protocols

| Function | Signature | Return | Description |
| :--- | :--- | :--- | :--- |
| `SocketConnectSync` | `SocketConnectSync(host, port)`| `int` | Connects TCP socket synchronously, returning handle. |
| `SocketAcceptSync` | `SocketAcceptSync(server_h)` | `int` | Accepts incoming client TCP connection. |
| `SocketSendSync` | `SocketSendSync(sock, data)` | `int` | Sends payload synchronously across TCP socket. |
| `SocketReceiveSync` | `SocketReceiveSync(sock, max_len)`| `str` | Receives up to `max_len` bytes from TCP socket. |
| `SocketClose` | `SocketClose(sock)` | `nil` | Closes socket descriptor. |
| `UdpMulticastJoinC` | `UdpMulticastJoinC(sock, group_ip)`| `bool` | Joins IP multicast group (`IP_ADD_MEMBERSHIP`). |
| `UdpMulticastSendC` | `UdpMulticastSendC(sock, ip, port, data)`| `nil` | Transmits datagram to multicast group. |
| `Http11ParserNewC` | `Http11ParserNewC()` | `handle` | Creates HTTP/1.1 stream parser. |
| `Http11ParserFeedC` | `Http11ParserFeedC(h, raw_chunk)`| `struct` | Feeds chunk, returning parsed headers and body. |
| `WsFrameEncodeC` | `WsFrameEncodeC(opcode, data, mask)`| `[int]` | Encodes RFC 6455 WebSocket binary frame. |
| `WsFrameDecodeC` | `WsFrameDecodeC(frame_bytes)` | `struct` | Decodes RFC 6455 WebSocket frame (`.opcode`, `.payload`). |
| `Socks5ConnectC` | `Socks5ConnectC(p_ip, p_port, d_host, d_port)`| `int` | Handshakes with SOCKS5 proxy, establishing tunnel. |
| `SmtpSendC` | `SmtpSendC(h, port, u, p, from, to, subj, body)`| `bool` | Dispatches email via SMTP with EHLO/AUTH. |
| `NtpQueryTimeC` | `NtpQueryTimeC(ntp_server)` | `int` | Queries atomic time via RFC 4330 SNTP/UDP. |
| `DnsResolveRawC` | `DnsResolveRawC(hostname, type)`| `str` | Resolves DNS query record. |
| `IcmpPingSendC` | `IcmpPingSendC(ip_str, timeout_ms)`| `bool` | Sends ICMP IPv4 Echo Request ping. |
| `Icmp6PingSendC` | `Icmp6PingSendC(ipv6_str, timeout_ms)`| `bool` | Sends ICMP IPv6 Echo Request ping. |
| `WinHttpOpenC` | `WinHttpOpenC(user_agent)` | `handle` | Opens Windows WinHTTP session handle. |
| `WinHttpConnectC` | `WinHttpConnectC(sess, host, port)`| `handle` | Establishes WinHTTP connection. |
| `WinHttpSendRequestC`| `WinHttpSendRequestC(conn, verb, path, body)`| `struct` | Sends HTTPS request and reads response body. |

---

## A.6 Windows OS, File System & Kernel Subsystems

| Function | Signature | Return | Description |
| :--- | :--- | :--- | :--- |
| `OsClipboardSetC` / `ClipboardSet` | `OsClipboardSetC(text: str)` | `bool` | Writes text to Windows OS clipboard. |
| `OsClipboardGetC` / `ClipboardGet` | `OsClipboardGetC()` | `str` | Reads text from Windows OS clipboard. |
| `OsClipboardClearC` | `OsClipboardClearC()` | `bool` | Clears Windows OS clipboard buffer. |
| `OsProcessSnapshotC` | `OsProcessSnapshotC()` | `[struct]`| Toolhelp32 process snapshot enumeration. |
| `OsModuleEnumC` | `OsModuleEnumC(pid: int)` | `[str]` | PSAPI enumeration of loaded DLL modules. |
| `OsWindowEnumC` | `OsWindowEnumC()` | `[struct]`| Traverses visible desktop top-level windows. |
| `OsConsoleRawModeC` | `OsConsoleRawModeC(mode: int)` | `int` | Switches between virtual terminal raw and cooked console mode. |
| `OsFiberCreateC` | `OsFiberCreateC(stack_size: int)`| `handle` | Creates Win32 user-mode cooperative Fiber. |
| `OsTokenPrivilegesC` | `OsTokenPrivilegesC()` | `[struct]`| Enumerates process token privileges (e.g. `SeDebugPrivilege`). |
| `OsHeapWalkC` | `OsHeapWalkC()` | `[struct]`| Walks Win32 private heap memory chunks. |
| `OsVirtualProtectC` | `OsVirtualProtectC(addr, size, prot)`| `int` | Modifies page permissions (`VirtualProtect`). |
| `OsVirtualQueryC` | `OsVirtualQueryC(addr)` | `struct` | Queries page memory state (`VirtualQuery`). |
| `OsWow64QueryC` | `OsWow64QueryC()` | `struct` | Queries Wow64 machine architecture attributes. |
| `OsNamedEventCreateC` | `OsNamedEventCreateC(name, manual)`| `handle` | Creates named cross-process synchronization event. |
| `OsNamedEventSetC` | `OsNamedEventSetC(event_h)` | `nil` | Signals named event to listening processes. |
| `OsNamedEventWaitC` | `OsNamedEventWaitC(event_h, timeout)`| `bool` | Waits on named event with timeout in milliseconds. |
| `OsNamedEventCloseC` | `OsNamedEventCloseC(event_h)` | `nil` | Closes named event handle. |
| `NtfsAdsWriteC` | `NtfsAdsWriteC(file, stream, data)`| `int` | Writes to NTFS Alternate Data Stream. |
| `NtfsAdsReadC` | `NtfsAdsReadC(file, stream)` | `str` | Reads from NTFS Alternate Data Stream. |
| `EtwEmitC` | `EtwEmitC(provider_guid, evt_name)`| `bool` | Emits structured telemetry event to ETW. |
| `PdhQueryC` | `PdhQueryC(counter_path: str)` | `float` | Queries Windows PDH real-time performance counter. |
| `FileSystemReadText` / `ReadFile` | `ReadFile(path: str)` | `str` | Reads complete text file. |
| `FileSystemWriteText` / `WriteFile`| `WriteFile(path, text)`| `nil` | Writes text file to disk. |
| `RemoveFile` | `RemoveFile(path: str)` | `bool` | Deletes file from disk. |
| `MakeDirectory` | `MakeDirectory(path: str)` | `bool` | Creates directory on disk. |
| `FileSystemSize` | `FileSystemSize(path: str)` | `int` | Returns file size in bytes. |
| `FileSystemModifiedTime`|`FileSystemModifiedTime(path)` | `int` | Returns file modification epoch timestamp. |
| `FileSystemIsDir` | `FileSystemIsDir(path: str)` | `bool` | Checks if path is a directory. |
| `FileSystemIsFile` | `FileSystemIsFile(path: str)` | `bool` | Checks if path is a regular file. |
| `FileLockAcquireExclusive`|`FileLockAcquireExclusive(file)`| `bool` | Locks file against concurrent access. |
| `FileLockRelease` | `FileLockRelease(file)` | `nil` | Releases file lock. |
| `FileWatcherNew` | `FileWatcherNew(dir_path)` | `handle` | Starts directory filesystem watcher. |
| `FileWatcherPoll` | `FileWatcherPoll(h)` | `[struct]`| Polls for filesystem change notifications. |
| `HardLinkCreate` | `HardLinkCreate(src, dst)` | `bool` | Creates NTFS hardlink. |
| `SymLinkCreateFile` | `SymLinkCreateFile(src, dst)` | `bool` | Creates NTFS file symlink. |
| `ProcessSpawn` | `ProcessSpawn(cmd: str)` | `handle` | Spawns child OS process. |
| `ProcessWait` | `ProcessWait(proc_h)` | `int` | Waits for process termination, returning exit code. |
| `ProcessKill` | `ProcessKill(proc_h)` | `bool` | Forcefully terminates process. |
| `ProcessWorkingSet` | `ProcessWorkingSet(pid: int)` | `int` | Queries process working set size in bytes. |
| `JobObjectCreate` | `JobObjectCreate(name: str)` | `handle` | Creates Windows Job Object for process group management. |
| `JobObjectAssignProcess`|`JobObjectAssignProcess(job, proc)`|`bool` | Assigns child process to Job Object. |
| `JobObjectSetMemoryLimit`|`JobObjectSetMemoryLimit(job, max_bytes)`|`bool`| Sets hard RAM limit for process group. |
| `ZipArchiveCreateC` | `ZipArchiveCreateC(zip_path, files)` | `bool` | Creates PKZIP standard archive containing listed files or struct records. |
| `ZipArchiveListC` | `ZipArchiveListC(zip_path)` | `[str]` | Lists file entry paths stored within a ZIP archive. |
| `ZipArchiveExtractC` | `ZipArchiveExtractC(zip_path, dest_dir)` | `int` | Extracts all member files from ZIP archive to target directory. |

---

## A.7 Media, Audio & Graphics

| Function | Signature | Return | Description |
| :--- | :--- | :--- | :--- |
| `WavFileParseC` | `WavFileParseC(filepath: str)` | `struct` | Parses RIFF/WAVE audio headers (`.sample_rate`, `.channels`, `.bit_depth`). |
| `BmpFileParseC` | `BmpFileParseC(filepath: str)` | `struct` | Decodes Windows BMP/DIB image metadata (`.width`, `.height`, `.bpp`). |
| `GdiBitmapCreateC` | `GdiBitmapCreateC(w: int, h: int)` | `struct` | Allocates 32-bit GDI DIB section bitmap (`.w`, `.h`, `.hbmp`, `.hdc`). |
| `GdiBitmapSetPixelC` | `GdiBitmapSetPixelC(bmp, x, y, color_rgb)`| `nil` | Sets RGB pixel color at coordinate $(x, y)$. |
| `GdiBitmapSaveC` | `GdiBitmapSaveC(bmp, filename)` | `bool` | Saves GDI canvas to uncompressed BMP file. |
| `WinmmAudioPlayC` | `WinmmAudioPlayC(pcm_bytes, sample_rate)`| `bool` | Direct low-latency wave audio playback via WinMM `waveOutWrite`. |

---

## A.8 Concurrency, Lock-Free & Memory Reclamation

| Function | Signature | Return | Description |
| :--- | :--- | :--- | :--- |
| `MutexNewC` | `MutexNewC()` | `handle` | Creates mutual exclusion lock. |
| `MutexLockC` | `MutexLockC(mtx)` | `nil` | Acquires exclusive lock. |
| `MutexUnlockC` | `MutexUnlockC(mtx)` | `nil` | Releases exclusive lock. |
| `SharedSpinlockNewC` | `SharedSpinlockNewC()` | `handle` | Creates user-space test-and-set atomic spinlock. |
| `SharedSpinlockLockC` | `SharedSpinlockLockC(spin)` | `nil` | Spins until lock is acquired. |
| `SharedSpinlockUnlockC`| `SharedSpinlockUnlockC(spin)` | `nil` | Releases spinlock. |
| `ThreadBarrierNewC` | `ThreadBarrierNewC(count: int)`| `handle` | Creates thread barrier for `count` threads. |
| `ThreadBarrierWaitC` | `ThreadBarrierWaitC(barrier)` | `nil` | Blocks until all participating threads arrive. |
| `ConditionVariableNewC`| `ConditionVariableNewC()` | `handle` | Creates Condition Variable. |
| `ConditionVariableSignalC`|`ConditionVariableSignalC(cv)` | `nil` | Wakes one thread waiting on condition variable. |
| `ReadWriteLockNewC` | `ReadWriteLockNewC()` | `handle` | Creates Read-Write Lock. |
| `LockFreeStackNewC` | `LockFreeStackNewC()` | `handle` | Creates atomic Treiber stack using CAS. |
| `LockFreeStackPushC` | `LockFreeStackPushC(h, val)` | `nil` | Pushes value atomically to stack head. |
| `LockFreeStackPopC` | `LockFreeStackPopC(h)` | `any` | Pops value atomically from stack head. |
| `MpscQueueNewC` | `MpscQueueNewC(capacity: int)` | `handle` | Creates Multi-Producer Single-Consumer queue. |
| `MpscQueuePushC` | `MpscQueuePushC(h, val)` | `nil` | Pushes task to queue concurrently. |
| `MpscQueuePopC` | `MpscQueuePopC(h)` | `any` | Consumer drains task from queue. |
| `StealingDequeNewC` | `StealingDequeNewC(cap: int)` | `handle` | Creates Chase-Lev Work-Stealing Deque. |
| `StealingDequePushC` | `StealingDequePushC(h, task)` | `nil` | Local worker pushes task to bottom. |
| `StealingDequePopC` | `StealingDequePopC(h)` | `any` | Local worker pops task from bottom (LIFO). |
| `StealingDequeStealC`| `StealingDequeStealC(h)` | `any` | Idle worker steals task from top (FIFO). |
| `HazardPointerDomainNewC`| `HazardPointerDomainNewC(threads)`| `handle` | Initializes Hazard Pointer domain. |
| `HazardPointerAcquireC`| `HazardPointerAcquireC(dom, tid, addr)`| `nil` | Registers hazard pointer protection on node. |
| `HazardPointerReleaseC`| `HazardPointerReleaseC(dom, tid)`| `nil` | Clears hazard pointer slot. |
| `HazardPointerRetireC`| `HazardPointerRetireC(dom, addr)`| `int` | Retires node, scanning hazard list for safe reclamation. |
| `OsIoCompletionPortCreateC`| `OsIoCompletionPortCreateC(threads)`| `handle` | Creates Windows I/O Completion Port (IOCP). |
| `OsIoCompletionPortPostC`| `OsIoCompletionPortPostC(iocp, bytes, key, ov)`| `nil` | Posts completion packet into IOCP queue. |
| `OsIoCompletionPortWaitC`| `OsIoCompletionPortWaitC(iocp, timeout_ms)`| `struct` | Dequeues completion packet (`.bytes`, `.key`, `.overlapped`). |
| `AtomicI64New` | `AtomicI64New(init_val: int)` | `handle` | Creates 64-bit atomic integer. |
| `AtomicI64Load` | `AtomicI64Load(h)` | `int` | Atomic load with sequential consistency. |
| `AtomicI64Store` | `AtomicI64Store(h, val: int)` | `nil` | Atomic store with sequential consistency. |
| `AtomicI64Add` | `AtomicI64Add(h, delta: int)` | `int` | Atomic fetch-and-add. |
| `AtomicI64Cas` | `AtomicI64Cas(h, exp, desired)`| `bool` | Atomic compare-and-swap. |
| `AtomicFence` | `AtomicFence()` | `nil` | Emits full hardware CPU memory fence (`mfence`). |
| `ShmRingNewC` | `ShmRingNewC(name: str, cap: int)`| `struct` | Creates high-speed lock-free SPSC shared-memory ring buffer. |
| `ShmRingPushC` | `ShmRingPushC(ring, data: str)` | `bool` | Enqueues string/byte frame to shared memory ring without locks. |
| `ShmRingPopC` | `ShmRingPopC(ring)` | `str` | Dequeues frame from shared memory ring buffer. |
| `ShmRingAvailableC` | `ShmRingAvailableC(ring)` | `int` | Queries byte payload count available to consume in ring. |
| `ShmRingCloseC` | `ShmRingCloseC(ring)` | `bool` | Unmaps and closes shared memory ring buffer mapping. |

---

## A.11 Automatic Differentiation, Physical Units & Geometric Vectors

| Function / Type | Signature | Return Type | Description |
| :--- | :--- | :--- | :--- |
| `Dual` | `Dual(val: float, grad: float)` | `struct` | Dual number forward-mode auto-diff wrapper with `.Add()`, `.Mul()`, `.val`, `.grad`. |
| `DualNew` | `DualNew(val: float, deriv: float)` | `struct` | Allocates dual number carrying scalar value and derivative component. |
| `DualSin` | `DualSin(d: struct)` | `struct` | Computes sine of dual number with automatic chain rule propagation ($\sin(x) + x' \cos(x) \epsilon$). |
| `grad` | `grad(f: fn, x: float)` | `float` | Computes exact derivative $f'(x)$ using forward-mode automatic differentiation. |
| `jvp` | `jvp(f: fn, x: float, v: float)` | `float` | Computes Jacobian-Vector Product $J_f(x) \cdot v$. |
| `MatrixNew` | `MatrixNew(rows: int, cols: int)` | `struct` | Allocates zero-initialized 2D numeric matrix with `.Get(r, c)` and `.Set(r, c, v)`. |
| `MatrixFrom`| `MatrixFrom(rows: int, cols: int, data: arr)` | `struct` | Allocates 2D matrix populated from row-major flattened array. |
| `Meter` | `Meter(mag: float)` | `struct` | Length unit with magnitude and dimensional exponent $L=1$. |
| `Second` | `Second(mag: float)` | `struct` | Time unit with magnitude and dimensional exponent $T=1$. |
| `Kilogram` | `Kilogram(mag: float)` | `struct` | Mass unit with magnitude and dimensional exponent $M=1$. |
| `Speed` | `Speed(d, t)` | `struct` | Velocity quantity ($L^1 \cdot T^{-1}$). |
| `Acceleration` | `Acceleration(v, t)` | `struct` | Acceleration quantity ($L^1 \cdot T^{-2}$). |
| `Newton` | `Newton(m, a)` | `struct` | Force quantity ($M^1 \cdot L^1 \cdot T^{-2}$). |
| `Joule` | `Joule(f, d)` | `struct` | Work / Energy quantity ($M^1 \cdot L^2 \cdot T^{-2}$). |
| `Watt` | `Watt(w, t)` | `struct` | Power quantity ($M^1 \cdot L^2 \cdot T^{-3}$). |
| `Vec2` | `Vec2(x: float, y: float)` | `struct` | 2D SIMD vector supporting component swizzles (`.yx`, `.xx`), `.Length()`, `.Normalize()`, `.Dot()`. |
| `Vec3` | `Vec3(x: float, y: float, z: float)` | `struct` | 3D SIMD vector supporting swizzles (`.zyx`, `.xy`), `.Cross()`, `.Dot()`, `.Normalize()`. |
| `Vec4` | `Vec4(x, y, z, w)` | `struct` | 4D SIMD vector supporting component swizzles (`.wzyx`, `.rgba`, `.xyz`). |

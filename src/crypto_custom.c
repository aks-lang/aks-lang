#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#endif
#include "value.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BCRYPT_CHACHA20_POLY1305_ALGORITHM
#define BCRYPT_CHACHA20_POLY1305_ALGORITHM L"CHACHA20_POLY1305"
#endif

#ifndef BCRYPT_SHA3_256_ALGORITHM
#define BCRYPT_SHA3_256_ALGORITHM L"SHA3-256"
#endif

#ifndef BCRYPT_SHA3_512_ALGORITHM
#define BCRYPT_SHA3_512_ALGORITHM L"SHA3-512"
#endif

#ifndef BCRYPT_ECC_CURVE_25519
#define BCRYPT_ECC_CURVE_25519 L"curve25519"
#endif

#ifndef BCRYPT_ECDH_PRIVATE_generic_25519_MAGIC
#define BCRYPT_ECDH_PRIVATE_generic_25519_MAGIC 0x564b4345
#endif

#ifndef BCRYPT_ECDH_PUBLIC_generic_25519_MAGIC
#define BCRYPT_ECDH_PUBLIC_generic_25519_MAGIC 0x504b4345
#endif

// Rotr64 and rol64 helper macros
#define ROTR64(x, y) (((x) >> (y)) | ((x) << (64 - (y))))
#define ROL64(x, y) (((x) << (y)) | ((x) >> (64 - (y))))

// Hex Encoding / Decoding

void HexEncodeC(const uint8_t *in, size_t in_len, char *out) {
  for (size_t i = 0; i < in_len; i++) {
    sprintf(&out[i * 2], "%02x", in[i]);
  }
  out[in_len * 2] = '\0';
}

int HexDecodeC(const char *in, uint8_t *out) {
  size_t len = strlen(in);
  if (len % 2 != 0)
    return 0;
  for (size_t i = 0; i < len / 2; i++) {
    unsigned int val;
    if (sscanf(&in[i * 2], "%2x", &val) != 1)
      return 0;
    out[i] = (uint8_t)val;
  }
  return 1;
}

// Pem to der / der to pem

uint8_t *PEMToDER(const char *pem, DWORD *out_len) {
  DWORD len = 0;
  if (!CryptStringToBinaryA(pem, 0, CRYPT_STRING_BASE64_ANY, NULL, &len, NULL,
                            NULL)) {
    return NULL;
  }
  uint8_t *der = malloc(len);
  if (!CryptStringToBinaryA(pem, 0, CRYPT_STRING_BASE64_ANY, der, &len, NULL,
                            NULL)) {
    free(der);
    return NULL;
  }
  *out_len = len;
  return der;
}

char *DERToPEM(const uint8_t *der, DWORD der_len, const char *header) {
  DWORD len = 0;
  if (!CryptBinaryToStringA(der, der_len, CRYPT_STRING_BASE64, NULL, &len)) {
    return NULL;
  }
  char *b64 = malloc(len);
  if (!CryptBinaryToStringA(der, der_len, CRYPT_STRING_BASE64, b64, &len)) {
    free(b64);
    return NULL;
  }
  // Format full PEM string
  size_t pem_len = strlen(header) * 2 + 64 + len;
  char *pem = malloc(pem_len);
  sprintf(pem, "-----BEGIN %s-----\n%s-----END %s-----\n", header, b64, header);
  free(b64);
  return pem;
}

// Sha-3 implementation (keccak)

static const uint64_t keccakf_piln[24] = {10, 7,  11, 17, 18, 3,  5,  16,
                                          8,  21, 24, 4,  15, 23, 19, 13,
                                          12, 2,  20, 14, 22, 9,  6,  1};

static const uint64_t keccakf_rndc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};

static const int keccakf_rotc[24] = {1,  3,  6,  10, 15, 21, 28, 36,
                                     45, 55, 2,  14, 27, 41, 56, 8,
                                     25, 43, 62, 18, 39, 61, 20, 44};

static void KeccakF(uint64_t state[25]) {
  uint64_t t, bc[5];
  for (int round = 0; round < 24; round++) {
    for (int i = 0; i < 5; i++)
      bc[i] = state[i] ^ state[i + 5] ^ state[i + 10] ^ state[i + 15] ^
              state[i + 20];
    for (int i = 0; i < 5; i++) {
      t = bc[(i + 4) % 5] ^ ROL64(bc[(i + 1) % 5], 1);
      for (int j = 0; j < 25; j += 5)
        state[j + i] ^= t;
    }
    t = state[1];
    for (int i = 0; i < 24; i++) {
      int j = (int)keccakf_piln[i];
      bc[0] = state[j];
      state[j] = ROL64(t, keccakf_rotc[i]);
      t = bc[0];
    }
    for (int j = 0; j < 25; j += 5) {
      for (int i = 0; i < 5; i++)
        bc[i] = state[j + i];
      for (int i = 0; i < 5; i++)
        state[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
    }
    state[0] ^= keccakf_rndc[round];
  }
}

void KeccakHash(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen,
                int rate) {
  uint64_t state[25] = {0};
  while (inlen >= (size_t)rate) {
    for (int j = 0; j < rate / 8; j++) {
      uint64_t word = 0;
      for (int k = 0; k < 8; k++)
        word |= ((uint64_t)in[j * 8 + k]) << (k * 8);
      state[j] ^= word;
    }
    KeccakF(state);
    in += rate;
    inlen -= rate;
  }
  uint8_t temp[200] = {0};
  memcpy(temp, in, inlen);
  temp[inlen] = 0x06;
  temp[rate - 1] |= 0x80;
  for (int j = 0; j < rate / 8; j++) {
    uint64_t word = 0;
    for (int k = 0; k < 8; k++)
      word |= ((uint64_t)temp[j * 8 + k]) << (k * 8);
    state[j] ^= word;
  }
  KeccakF(state);
  for (size_t j = 0; j < outlen; j++) {
    out[j] = (state[j / 8] >> ((j % 8) * 8)) & 0xFF;
  }
}

// Blake2b Implementation

static const uint64_t blake2b_iv[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

static const uint8_t blake2b_sigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};

#define BlakeG(a, b, c, d, x, y)                                               \
  do {                                                                         \
    a = a + b + x;                                                             \
    d = ROTR64(d ^ a, 32);                                                     \
    c = c + d;                                                                 \
    b = ROTR64(b ^ c, 24);                                                     \
    a = a + b + y;                                                             \
    d = ROTR64(d ^ a, 16);                                                     \
    c = c + d;                                                                 \
    b = ROTR64(b ^ c, 63);                                                     \
  } while (0)

typedef struct {
  uint64_t h[8];
  uint64_t t[2];
  uint8_t buf[128];
  size_t buflen;
  size_t outlen;
} blake2b_ctx;

static void Blake2bCompress(blake2b_ctx *ctx, int last) {
  uint64_t v[16];
  uint64_t m[16];
  for (int i = 0; i < 8; i++)
    v[i] = ctx->h[i];
  for (int i = 0; i < 8; i++)
    v[i + 8] = blake2b_iv[i];
  v[12] ^= ctx->t[0];
  v[13] ^= ctx->t[1];
  if (last)
    v[14] ^= 0xFFFFFFFFFFFFFFFFULL;
  for (int i = 0; i < 16; i++) {
    m[i] = ((uint64_t *)ctx->buf)[i];
  }
  for (int r = 0; r < 12; r++) {
    BlakeG(v[0], v[4], v[8], v[12], m[blake2b_sigma[r][0]],
           m[blake2b_sigma[r][1]]);
    BlakeG(v[1], v[5], v[9], v[13], m[blake2b_sigma[r][2]],
           m[blake2b_sigma[r][3]]);
    BlakeG(v[2], v[6], v[10], v[14], m[blake2b_sigma[r][4]],
           m[blake2b_sigma[r][5]]);
    BlakeG(v[3], v[7], v[11], v[15], m[blake2b_sigma[r][6]],
           m[blake2b_sigma[r][7]]);
    BlakeG(v[0], v[5], v[10], v[15], m[blake2b_sigma[r][8]],
           m[blake2b_sigma[r][9]]);
    BlakeG(v[1], v[6], v[11], v[12], m[blake2b_sigma[r][10]],
           m[blake2b_sigma[r][11]]);
    BlakeG(v[2], v[7], v[8], v[13], m[blake2b_sigma[r][12]],
           m[blake2b_sigma[r][13]]);
    BlakeG(v[3], v[4], v[9], v[14], m[blake2b_sigma[r][14]],
           m[blake2b_sigma[r][15]]);
  }
  for (int i = 0; i < 8; i++) {
    ctx->h[i] ^= v[i] ^ v[i + 8];
  }
}

int Blake2bInit(blake2b_ctx *ctx, size_t outlen, const void *key,
                size_t keylen) {
  memset(ctx, 0, sizeof(blake2b_ctx));
  for (int i = 0; i < 8; i++)
    ctx->h[i] = blake2b_iv[i];
  ctx->h[0] ^= 0x01010000 | (keylen << 8) | outlen;
  ctx->outlen = outlen;
  if (keylen > 0) {
    uint8_t block[128] = {0};
    memcpy(block, key, keylen);
    memcpy(ctx->buf, block, 128);
    ctx->buflen = 128;
  }
  return 0;
}

int Blake2bUpdate(blake2b_ctx *ctx, const void *in, size_t inlen) {
  const uint8_t *pin = (const uint8_t *)in;
  while (inlen > 0) {
    if (ctx->buflen == 128) {
      ctx->t[0] += 128;
      if (ctx->t[0] < 128)
        ctx->t[1]++;
      Blake2bCompress(ctx, 0);
      ctx->buflen = 0;
    }
    size_t want = 128 - ctx->buflen;
    size_t take = (inlen < want) ? inlen : want;
    memcpy(ctx->buf + ctx->buflen, pin, take);
    ctx->buflen += take;
    pin += take;
    inlen -= take;
  }
  return 0;
}

int Blake2bFinal(blake2b_ctx *ctx, void *out, size_t outlen) {
  ctx->t[0] += ctx->buflen;
  if (ctx->t[0] < ctx->buflen)
    ctx->t[1]++;
  memset(ctx->buf + ctx->buflen, 0, 128 - ctx->buflen);
  Blake2bCompress(ctx, 1);
  uint8_t outbuf[64];
  for (int i = 0; i < 8; i++) {
    ((uint64_t *)outbuf)[i] = ctx->h[i];
  }
  memcpy(out, outbuf, outlen);
  return 0;
}

int Blake2b(const void *in, size_t inlen, void *out, size_t outlen) {
  blake2b_ctx ctx;
  Blake2bInit(&ctx, outlen, NULL, 0);
  Blake2bUpdate(&ctx, in, inlen);
  return Blake2bFinal(&ctx, out, outlen);
}

// Scrypt Key Derivation

#define R_SALSA(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
static void Salsa20WordSpecification(uint32_t x[16]) {
  uint32_t y[16];
  memcpy(y, x, 64);
  for (int i = 8; i > 0; i -= 2) {
    y[4] ^= R_SALSA(y[0] + y[12], 7);
    y[8] ^= R_SALSA(y[4] + y[0], 9);
    y[12] ^= R_SALSA(y[8] + y[4], 13);
    y[0] ^= R_SALSA(y[12] + y[8], 18);
    y[9] ^= R_SALSA(y[5] + y[1], 7);
    y[13] ^= R_SALSA(y[9] + y[5], 9);
    y[1] ^= R_SALSA(y[13] + y[9], 13);
    y[5] ^= R_SALSA(y[1] + y[13], 18);
    y[14] ^= R_SALSA(y[10] + y[6], 7);
    y[2] ^= R_SALSA(y[14] + y[10], 9);
    y[6] ^= R_SALSA(y[2] + y[14], 13);
    y[10] ^= R_SALSA(y[6] + y[2], 18);
    y[3] ^= R_SALSA(y[15] + y[11], 7);
    y[7] ^= R_SALSA(y[3] + y[15], 9);
    y[11] ^= R_SALSA(y[7] + y[3], 13);
    y[15] ^= R_SALSA(y[11] + y[7], 18);
    y[1] ^= R_SALSA(y[0] + y[3], 7);
    y[2] ^= R_SALSA(y[1] + y[0], 9);
    y[3] ^= R_SALSA(y[2] + y[1], 13);
    y[0] ^= R_SALSA(y[3] + y[2], 18);
    y[6] ^= R_SALSA(y[5] + y[4], 7);
    y[7] ^= R_SALSA(y[6] + y[5], 9);
    y[4] ^= R_SALSA(y[7] + y[6], 13);
    y[5] ^= R_SALSA(y[4] + y[7], 18);
    y[11] ^= R_SALSA(y[10] + y[9], 7);
    y[8] ^= R_SALSA(y[11] + y[10], 9);
    y[9] ^= R_SALSA(y[8] + y[11], 13);
    y[10] ^= R_SALSA(y[9] + y[8], 18);
    y[12] ^= R_SALSA(y[15] + y[14], 7);
    y[13] ^= R_SALSA(y[12] + y[15], 9);
    y[14] ^= R_SALSA(y[13] + y[12], 13);
    y[15] ^= R_SALSA(y[14] + y[13], 18);
  }
  for (int i = 0; i < 16; i++)
    x[i] += y[i];
}

static void ScryptBlockMix(uint32_t *B, uint32_t *Y, int r) {
  uint32_t X[16];
  memcpy(X, &B[(2 * r - 1) * 16], 64);
  for (int i = 0; i < 2 * r; i++) {
    for (int j = 0; j < 16; j++)
      X[j] ^= B[i * 16 + j];
    Salsa20WordSpecification(X);
    if (i % 2 == 0) {
      memcpy(&Y[(i / 2) * 16], X, 64);
    } else {
      memcpy(&Y[(r + (i - 1) / 2) * 16], X, 64);
    }
  }
}

static void ScryptRoMix(uint32_t *B, int r, uint64_t N, uint32_t *V,
                        uint32_t *XY) {
  uint32_t *X = XY;
  uint32_t *Y = XY + 32 * r;
  memcpy(X, B, 128 * r);
  for (uint64_t i = 0; i < N; i++) {
    memcpy(&V[i * 32 * r], X, 128 * r);
    ScryptBlockMix(X, Y, r);
    memcpy(X, Y, 128 * r);
  }
  for (uint64_t i = 0; i < N; i++) {
    uint64_t j = ((((uint64_t)X[(2 * r - 1) * 16]) |
                   (((uint64_t)X[(2 * r - 1) * 16 + 1]) << 32)) &
                  (N - 1));
    for (int k = 0; k < 32 * r; k++)
      X[k] ^= V[j * 32 * r + k];
    ScryptBlockMix(X, Y, r);
    memcpy(X, Y, 128 * r);
  }
  memcpy(B, X, 128 * r);
}

int CryptoScrypt(const uint8_t *pwd, size_t pwd_len, const uint8_t *salt,
                 size_t salt_len, uint64_t N, uint32_t r, uint32_t p,
                 uint8_t *dk, size_t dk_len) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL,
                                  BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
    return 0;

  size_t B_len = p * 128 * r;
  uint32_t *B = malloc(B_len);
  if (!B) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  // PBKDF2 initial derivation
  if (BCryptDeriveKeyPBKDF2(hAlg, (PUCHAR)pwd, (ULONG)pwd_len, (PUCHAR)salt,
                            (ULONG)salt_len, 1, (PUCHAR)B, (ULONG)B_len,
                            0) != 0) {
    free(B);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  // Allocate ROMix tables
  uint32_t *V = malloc(N * 128 * r);
  uint32_t *XY = malloc(2 * 128 * r);
  if (!V || !XY) {
    free(B);
    if (V)
      free(V);
    if (XY)
      free(XY);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  // ROMix loop
  for (uint32_t i = 0; i < p; i++) {
    ScryptRoMix(B + i * 32 * r, r, N, V, XY);
  }

  // PBKDF2 final derivation
  int res = (BCryptDeriveKeyPBKDF2(hAlg, (PUCHAR)pwd, (ULONG)pwd_len, (PUCHAR)B,
                                   (ULONG)B_len, 1, dk, (ULONG)dk_len, 0) == 0);

  free(V);
  free(XY);
  free(B);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return res;
}

// Argon2 Key Derivation

static void Argon2G(uint64_t *v0, uint64_t *v1, uint64_t *v2, uint64_t *v3) {
  *v0 = *v0 + *v1 + 2 * ((*v0 & 0xFFFFFFFFULL) * (*v1 & 0xFFFFFFFFULL));
  *v3 = ROTR64(*v3 ^ *v0, 32);
  *v2 = *v2 + *v3 + 2 * ((*v2 & 0xFFFFFFFFULL) * (*v3 & 0xFFFFFFFFULL));
  *v1 = ROTR64(*v1 ^ *v2, 24);
  *v0 = *v0 + *v1 + 2 * ((*v0 & 0xFFFFFFFFULL) * (*v1 & 0xFFFFFFFFULL));
  *v3 = ROTR64(*v3 ^ *v0, 16);
  *v2 = *v2 + *v3 + 2 * ((*v2 & 0xFFFFFFFFULL) * (*v3 & 0xFFFFFFFFULL));
  *v1 = ROTR64(*v1 ^ *v2, 63);
}

static void Argon2MixBlocks(const uint64_t *X, const uint64_t *Y, uint64_t *R) {
  uint64_t state[128];
  for (int i = 0; i < 128; i++)
    state[i] = X[i] ^ Y[i];
  // Row mixing
  for (int i = 0; i < 8; i++) {
    Argon2G(&state[i * 16 + 0], &state[i * 16 + 4], &state[i * 16 + 8],
            &state[i * 16 + 12]);
    Argon2G(&state[i * 16 + 1], &state[i * 16 + 5], &state[i * 16 + 9],
            &state[i * 16 + 13]);
    Argon2G(&state[i * 16 + 2], &state[i * 16 + 6], &state[i * 16 + 10],
            &state[i * 16 + 14]);
    Argon2G(&state[i * 16 + 3], &state[i * 16 + 7], &state[i * 16 + 11],
            &state[i * 16 + 15]);
  }
  // Column mixing
  for (int i = 0; i < 8; i++) {
    Argon2G(&state[0 * 16 + i * 2], &state[2 * 16 + i * 2],
            &state[4 * 16 + i * 2], &state[6 * 16 + i * 2]);
    Argon2G(&state[0 * 16 + i * 2 + 1], &state[2 * 16 + i * 2 + 1],
            &state[4 * 16 + i * 2 + 1], &state[6 * 16 + i * 2 + 1]);
    Argon2G(&state[1 * 16 + i * 2], &state[3 * 16 + i * 2],
            &state[5 * 16 + i * 2], &state[7 * 16 + i * 2]);
    Argon2G(&state[1 * 16 + i * 2 + 1], &state[3 * 16 + i * 2 + 1],
            &state[5 * 16 + i * 2 + 1], &state[7 * 16 + i * 2 + 1]);
  }
  // Xor with input state
  for (int i = 0; i < 128; i++)
    R[i] = state[i] ^ X[i] ^ Y[i];
}

int CryptoArgon2(const uint8_t *pwd, size_t pwd_len, const uint8_t *salt,
                 size_t salt_len, uint32_t m_cost, uint32_t t_cost,
                 uint32_t parallelism, uint8_t *dk, size_t dk_len,
                 const char *type) {
  if (m_cost < 8 * parallelism)
    m_cost = 8 * parallelism;
  uint32_t block_count = (m_cost / (4 * parallelism)) * 4 * parallelism;
  uint32_t lane_blocks = block_count / parallelism;

  uint64_t *blocks = calloc(block_count, 1024);
  if (!blocks)
    return 0;

  // Initial H_0 hash: Blake2b(p, s, m, t, lan, type...)
  uint8_t h0[64];
  blake2b_ctx b_ctx;
  Blake2bInit(&b_ctx, 64, NULL, 0);
  uint32_t val;
  val = parallelism;
  Blake2bUpdate(&b_ctx, &val, 4);
  val = (uint32_t)dk_len;
  Blake2bUpdate(&b_ctx, &val, 4);
  val = m_cost;
  Blake2bUpdate(&b_ctx, &val, 4);
  val = t_cost;
  Blake2bUpdate(&b_ctx, &val, 4);
  val = 0x13;
  Blake2bUpdate(&b_ctx, &val, 4); // version
  val = strcmp(type, "argon2d") == 0
            ? 0
            : (strcmp(type, "argon2i") == 0 ? 1 : 2); // type
  Blake2bUpdate(&b_ctx, &val, 4);
  val = (uint32_t)pwd_len;
  Blake2bUpdate(&b_ctx, &val, 4);
  Blake2bUpdate(&b_ctx, pwd, pwd_len);
  val = (uint32_t)salt_len;
  Blake2bUpdate(&b_ctx, &val, 4);
  Blake2bUpdate(&b_ctx, salt, salt_len);
  Blake2bFinal(&b_ctx, h0, 64);

  // Initial blocks generation
  for (uint32_t l = 0; l < parallelism; l++) {
    uint8_t block_in[72];
    memcpy(block_in, h0, 64);
    memset(block_in + 64, 0, 8);

    block_in[64] = 0;
    block_in[68] = (uint8_t)l;
    uint8_t out[1024];
    for (int b = 0; b < 16; b++) {
      uint32_t hash_out_len = 64;
      blake2b_ctx expand_ctx;
      Blake2bInit(&expand_ctx, 64, NULL, 0);
      Blake2bUpdate(&expand_ctx, &hash_out_len, 4);
      if (b == 0) {
        Blake2bUpdate(&expand_ctx, block_in, 72);
      } else {
        Blake2bUpdate(&expand_ctx, out + (b - 1) * 64, 64);
      }
      Blake2bFinal(&expand_ctx, out + b * 64, 64);
    }
    memcpy(blocks + l * lane_blocks * 128, out, 1024);

    block_in[64] = 1;
    block_in[68] = (uint8_t)l;
    for (int b = 0; b < 16; b++) {
      uint32_t hash_out_len = 64;
      blake2b_ctx expand_ctx;
      Blake2bInit(&expand_ctx, 64, NULL, 0);
      Blake2bUpdate(&expand_ctx, &hash_out_len, 4);
      if (b == 0) {
        Blake2bUpdate(&expand_ctx, block_in, 72);
      } else {
        Blake2bUpdate(&expand_ctx, out + (b - 1) * 64, 64);
      }
      Blake2bFinal(&expand_ctx, out + b * 64, 64);
    }
    memcpy(blocks + (l * lane_blocks + 1) * 128, out, 1024);
  }

  // Core loop
  int type_id =
      strcmp(type, "argon2d") == 0 ? 0 : (strcmp(type, "argon2i") == 0 ? 1 : 2);
  for (uint32_t pass = 0; pass < t_cost; pass++) {
    for (uint32_t slice = 0; slice < 4; slice++) {
      uint32_t slice_start = slice * (lane_blocks / 4);
      uint32_t slice_end = (slice + 1) * (lane_blocks / 4);

      for (uint32_t l = 0; l < parallelism; l++) {
        uint32_t start_col = (pass == 0 && slice == 0) ? 2 : slice_start;

        for (uint32_t col = start_col; col < slice_end; col++) {
          uint32_t prev_col = (col == 0) ? (lane_blocks - 1) : (col - 1);
          uint64_t *prev_block = blocks + (l * lane_blocks + prev_col) * 128;
          uint64_t *curr_block = blocks + (l * lane_blocks + col) * 128;

          uint64_t rand_val = 0;
          if (type_id == 0 || (type_id == 2 && pass == 0 && slice < 2)) {
            rand_val = prev_block[0];
          } else {
            uint8_t gen_in[16];
            memcpy(gen_in, &pass, 4);
            memcpy(gen_in + 4, &l, 4);
            memcpy(gen_in + 8, &slice, 4);
            memcpy(gen_in + 12, &col, 4);
            uint8_t hash_res[64];
            Blake2b(gen_in, 16, hash_res, 64);
            rand_val = ((uint64_t *)hash_res)[0];
          }

          uint32_t ref_lane = (uint32_t)((rand_val >> 32) % parallelism);
          uint32_t same_lane = (ref_lane == l);
          uint32_t ref_pool = (slice_end > 1) ? (slice_end - 1) : 1;
          uint32_t ref_index = (uint32_t)(rand_val % ref_pool);
          if (ref_index >= col && same_lane && col > 0)
            ref_index = (ref_index + 1) % col;
          if (ref_index >= lane_blocks)
            ref_index = ref_index % lane_blocks;

          uint64_t *ref_block =
              blocks + (ref_lane * lane_blocks + ref_index) * 128;
          Argon2MixBlocks(prev_block, ref_block, curr_block);
        }
      }
    }
  }

  uint64_t final_block[128];
  memcpy(final_block, blocks + (lane_blocks - 1) * 128, 1024);
  for (uint32_t l = 1; l < parallelism; l++) {
    uint64_t *lane_end = blocks + (l * lane_blocks + lane_blocks - 1) * 128;
    for (int i = 0; i < 128; i++)
      final_block[i] ^= lane_end[i];
  }

  Blake2b(final_block, 1024, dk, dk_len);
  free(blocks);
  return 1;
}

// Secure Random

int CryptoSecureRandom(uint8_t *buf, size_t len) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  NTSTATUS status =
      BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RNG_ALGORITHM, NULL, 0);
  if (status != 0)
    return 0;
  status = BCryptGenRandom(hAlg, buf, (ULONG)len, 0);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

// Symmetric Encryption (AES-GCM / ChaCha20-Poly1305)

static int CryptoSymEncrypt(LPCWSTR alg_id, const uint8_t *key, size_t key_len,
                            const uint8_t *iv, size_t iv_len,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *pt, size_t pt_len, uint8_t *ct,
                            uint8_t *tag, size_t tag_len) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, alg_id, NULL, 0);
  if (status != 0)
    return 0;

  if (wcscmp(alg_id, BCRYPT_AES_ALGORITHM) == 0) {
    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                               (PBYTE)BCRYPT_CHAIN_MODE_GCM,
                               sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (status != 0) {
      BCryptCloseAlgorithmProvider(hAlg, 0);
      return 0;
    }
  }

  status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)key,
                                      (ULONG)key_len, 0);
  if (status != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
  BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
  authInfo.pbNonce = (PUCHAR)iv;
  authInfo.cbNonce = (ULONG)iv_len;
  authInfo.pbTag = tag;
  authInfo.cbTag = (ULONG)tag_len;
  authInfo.pbAuthData = (PUCHAR)aad;
  authInfo.cbAuthData = (ULONG)aad_len;

  ULONG cbCipher = 0;
  status = BCryptEncrypt(hKey, (PUCHAR)pt, (ULONG)pt_len, &authInfo, NULL, 0,
                         ct, (ULONG)pt_len, &cbCipher, 0);

  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

static int CryptoSymDecrypt(LPCWSTR alg_id, const uint8_t *key, size_t key_len,
                            const uint8_t *iv, size_t iv_len,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *ct, size_t ct_len,
                            const uint8_t *tag, size_t tag_len, uint8_t *pt) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, alg_id, NULL, 0);
  if (status != 0)
    return 0;

  if (wcscmp(alg_id, BCRYPT_AES_ALGORITHM) == 0) {
    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                               (PBYTE)BCRYPT_CHAIN_MODE_GCM,
                               sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (status != 0) {
      BCryptCloseAlgorithmProvider(hAlg, 0);
      return 0;
    }
  }

  status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)key,
                                      (ULONG)key_len, 0);
  if (status != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
  BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
  authInfo.pbNonce = (PUCHAR)iv;
  authInfo.cbNonce = (ULONG)iv_len;
  authInfo.pbTag = (PUCHAR)tag;
  authInfo.cbTag = (ULONG)tag_len;
  authInfo.pbAuthData = (PUCHAR)aad;
  authInfo.cbAuthData = (ULONG)aad_len;

  ULONG cbPlain = 0;
  status = BCryptDecrypt(hKey, (PUCHAR)ct, (ULONG)ct_len, &authInfo, NULL, 0,
                         pt, (ULONG)ct_len, &cbPlain, 0);

  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

int AesGcmEncrypt(const uint8_t *key, size_t key_len, const uint8_t *iv,
                  size_t iv_len, const uint8_t *aad, size_t aad_len,
                  const uint8_t *pt, size_t pt_len, uint8_t *ct, uint8_t *tag,
                  size_t tag_len) {
  return CryptoSymEncrypt(BCRYPT_AES_ALGORITHM, key, key_len, iv, iv_len, aad,
                          aad_len, pt, pt_len, ct, tag, tag_len);
}

int AesGcmDecrypt(const uint8_t *key, size_t key_len, const uint8_t *iv,
                  size_t iv_len, const uint8_t *aad, size_t aad_len,
                  const uint8_t *ct, size_t ct_len, const uint8_t *tag,
                  size_t tag_len, uint8_t *pt) {
  return CryptoSymDecrypt(BCRYPT_AES_ALGORITHM, key, key_len, iv, iv_len, aad,
                          aad_len, ct, ct_len, tag, tag_len, pt);
}

/* ChaCha20-Poly1305 pure-C (RFC 8439) */

#define CC20_QR(a, b, c, d)                                                    \
  a += b;                                                                      \
  d ^= a;                                                                      \
  d = CC20_ROL(d, 16);                                                         \
  c += d;                                                                      \
  b ^= c;                                                                      \
  b = CC20_ROL(b, 12);                                                         \
  a += b;                                                                      \
  d ^= a;                                                                      \
  d = CC20_ROL(d, 8);                                                          \
  c += d;                                                                      \
  b ^= c;                                                                      \
  b = CC20_ROL(b, 7)
#define CC20_ROL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

/* Generate one 64-byte ChaCha20 block into out[64] */
static void Chacha20Block(const uint8_t key[32], uint32_t counter,
                          const uint8_t nonce[12], uint8_t out[64]) {
  uint32_t s[16], x[16];
  s[0] = 0x61707865;
  s[1] = 0x3320646e;
  s[2] = 0x79622d32;
  s[3] = 0x6b206574;
  for (int i = 0; i < 8; i++) {
    uint32_t w = 0;
    for (int j = 0; j < 4; j++)
      w |= ((uint32_t)key[i * 4 + j]) << (j * 8);
    s[4 + i] = w;
  }
  s[12] = counter;
  for (int i = 0; i < 3; i++) {
    uint32_t w = 0;
    for (int j = 0; j < 4; j++)
      w |= ((uint32_t)nonce[i * 4 + j]) << (j * 8);
    s[13 + i] = w;
  }
  memcpy(x, s, 64);
  for (int i = 0; i < 10; i++) {
    CC20_QR(x[0], x[4], x[8], x[12]);
    CC20_QR(x[1], x[5], x[9], x[13]);
    CC20_QR(x[2], x[6], x[10], x[14]);
    CC20_QR(x[3], x[7], x[11], x[15]);
    CC20_QR(x[0], x[5], x[10], x[15]);
    CC20_QR(x[1], x[6], x[11], x[12]);
    CC20_QR(x[2], x[7], x[8], x[13]);
    CC20_QR(x[3], x[4], x[9], x[14]);
  }
  for (int i = 0; i < 16; i++) {
    uint32_t v = x[i] + s[i];
    out[i * 4 + 0] = (uint8_t)v;
    out[i * 4 + 1] = (uint8_t)(v >> 8);
    out[i * 4 + 2] = (uint8_t)(v >> 16);
    out[i * 4 + 3] = (uint8_t)(v >> 24);
  }
}

/* XOR src[len] with ChaCha20 keystream starting at counter, write to dst */
static void Chacha20Xor(const uint8_t key[32], uint32_t counter,
                        const uint8_t nonce[12], const uint8_t *src, size_t len,
                        uint8_t *dst) {
  uint8_t block[64];
  size_t pos = 0;
  while (pos < len) {
    Chacha20Block(key, counter++, nonce, block);
    size_t take = (len - pos) < 64 ? (len - pos) : 64;
    for (size_t i = 0; i < take; i++)
      dst[pos + i] = src[pos + i] ^ block[i];
    pos += take;
  }
}

/* Poly1305 MAC over arbitrary data; key32 = 32-byte one-time key */
static void Poly1305Mac(const uint8_t key32[32], const uint8_t *msg,
                        size_t msg_len, uint8_t tag_out[16]) {
  uint64_t t0 = 0, t1 = 0, t2 = 0, t3 = 0;
  for (int i = 0; i < 4; i++)
    t0 |= ((uint64_t)key32[i]) << (i * 8);
  for (int i = 0; i < 4; i++)
    t1 |= ((uint64_t)key32[4 + i]) << (i * 8);
  for (int i = 0; i < 4; i++)
    t2 |= ((uint64_t)key32[8 + i]) << (i * 8);
  for (int i = 0; i < 4; i++)
    t3 |= ((uint64_t)key32[12 + i]) << (i * 8);
  t0 &= 0x0fffffff;
  t1 &= 0x0ffffffc;
  t2 &= 0x0ffffffc;
  t3 &= 0x0ffffffc;
  uint64_t r0, r1, r2, r3, r4;
  r0 = t0 & 0x3ffffff;
  r1 = ((t0 >> 26) | (t1 << 6)) & 0x3ffffff;
  r2 = ((t1 >> 20) | (t2 << 12)) & 0x3ffffff;
  r3 = ((t2 >> 14) | (t3 << 18)) & 0x3ffffff;
  r4 = (t3 >> 8) & 0x3ffffff;
  uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
  for (int i = 0; i < 4; i++)
    s0 |= ((uint64_t)key32[16 + i]) << (i * 8);
  for (int i = 0; i < 4; i++)
    s1 |= ((uint64_t)key32[20 + i]) << (i * 8);
  for (int i = 0; i < 4; i++)
    s2 |= ((uint64_t)key32[24 + i]) << (i * 8);
  for (int i = 0; i < 4; i++)
    s3 |= ((uint64_t)key32[28 + i]) << (i * 8);

  uint64_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
  size_t pos = 0;
  while (pos < msg_len) {
    uint8_t tmp[17] = {0};
    size_t take = (msg_len - pos) < 16 ? (msg_len - pos) : 16;
    memcpy(tmp, msg + pos, take);
    tmp[take] = 1; /* hibit: 1 for full blocks, 1 appended for partial */
    uint64_t n0 = 0, n1 = 0, n2 = 0, n3 = 0;
    for (int i = 0; i < 4; i++)
      n0 |= ((uint64_t)tmp[i]) << (i * 8);
    for (int i = 0; i < 4; i++)
      n1 |= ((uint64_t)tmp[4 + i]) << (i * 8);
    for (int i = 0; i < 4; i++)
      n2 |= ((uint64_t)tmp[8 + i]) << (i * 8);
    for (int i = 0; i < 4; i++)
      n3 |= ((uint64_t)tmp[12 + i]) << (i * 8);
    uint64_t hibit = (uint64_t)tmp[16];
    h0 += n0 & 0x3ffffff;
    h1 += ((n0 >> 26) | (n1 << 6)) & 0x3ffffff;
    h2 += ((n1 >> 20) | (n2 << 12)) & 0x3ffffff;
    h3 += ((n2 >> 14) | (n3 << 18)) & 0x3ffffff;
    h4 += (n3 >> 8) | hibit;
    uint64_t d0, d1, d2, d3, d4, c;
    d0 =
        h0 * r0 + h1 * (5 * r4) + h2 * (5 * r3) + h3 * (5 * r2) + h4 * (5 * r1);
    d1 = h0 * r1 + h1 * r0 + h2 * (5 * r4) + h3 * (5 * r3) + h4 * (5 * r2);
    d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * (5 * r4) + h4 * (5 * r3);
    d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * (5 * r4);
    d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;
    c = d0 >> 26;
    h0 = d0 & 0x3ffffff;
    d1 += c;
    c = d1 >> 26;
    h1 = d1 & 0x3ffffff;
    d2 += c;
    c = d2 >> 26;
    h2 = d2 & 0x3ffffff;
    d3 += c;
    c = d3 >> 26;
    h3 = d3 & 0x3ffffff;
    d4 += c;
    c = d4 >> 26;
    h4 = d4 & 0x3ffffff;
    h0 += c * 5;
    c = h0 >> 26;
    h0 &= 0x3ffffff;
    h1 += c;
    pos += take;
  }
  /* final reduction */
  uint64_t c;
  c = h1 >> 26;
  h1 &= 0x3ffffff;
  h2 += c;
  c = h2 >> 26;
  h2 &= 0x3ffffff;
  h3 += c;
  c = h3 >> 26;
  h3 &= 0x3ffffff;
  h4 += c;
  c = h4 >> 26;
  h4 &= 0x3ffffff;
  h0 += c * 5;
  c = h0 >> 26;
  h0 &= 0x3ffffff;
  h1 += c;
  uint64_t g0 = h0 + 5;
  c = g0 >> 26;
  g0 &= 0x3ffffff;
  uint64_t g1 = h1 + c;
  c = g1 >> 26;
  g1 &= 0x3ffffff;
  uint64_t g2 = h2 + c;
  c = g2 >> 26;
  g2 &= 0x3ffffff;
  uint64_t g3 = h3 + c;
  c = g3 >> 26;
  g3 &= 0x3ffffff;
  uint64_t g4 = h4 + c;
  uint64_t mask = (g4 >> 26) ? 0ULL : ~0ULL;
  h0 = (h0 & mask) | (g0 & ~mask);
  h1 = (h1 & mask) | (g1 & ~mask);
  h2 = (h2 & mask) | (g2 & ~mask);
  h3 = (h3 & mask) | (g3 & ~mask);
  h4 = (h4 & mask) | (g4 & ~mask);
  uint64_t f;
  f = h0 | (h1 << 26);
  uint64_t u0 = (uint32_t)f;
  f >>= 32;
  f += h1 >> 6;
  f += h2 << 20;
  uint64_t u1 = (uint32_t)f;
  f >>= 32;
  f += h2 >> 12;
  f += h3 << 14;
  uint64_t u2 = (uint32_t)f;
  f >>= 32;
  f += h3 >> 18;
  f += h4 << 8;
  uint64_t u3 = (uint32_t)f;
  u0 += s0;
  u1 += s1 + (u0 >> 32);
  u2 += s2 + (u1 >> 32);
  u3 += s3 + (u2 >> 32);
  tag_out[0] = (uint8_t)u0;
  tag_out[1] = (uint8_t)(u0 >> 8);
  tag_out[2] = (uint8_t)(u0 >> 16);
  tag_out[3] = (uint8_t)(u0 >> 24);
  tag_out[4] = (uint8_t)u1;
  tag_out[5] = (uint8_t)(u1 >> 8);
  tag_out[6] = (uint8_t)(u1 >> 16);
  tag_out[7] = (uint8_t)(u1 >> 24);
  tag_out[8] = (uint8_t)u2;
  tag_out[9] = (uint8_t)(u2 >> 8);
  tag_out[10] = (uint8_t)(u2 >> 16);
  tag_out[11] = (uint8_t)(u2 >> 24);
  tag_out[12] = (uint8_t)u3;
  tag_out[13] = (uint8_t)(u3 >> 8);
  tag_out[14] = (uint8_t)(u3 >> 16);
  tag_out[15] = (uint8_t)(u3 >> 24);
}

/* Build AEAD MAC input: aad || pad || data || pad || le64(len_aad) ||
 * le64(len_data) */
static uint8_t *BuildPolyMsg(const uint8_t *aad, size_t aad_len,
                             const uint8_t *data, size_t data_len,
                             size_t *out_len) {
  size_t ap = (16 - (aad_len % 16)) % 16;
  size_t dp = (16 - (data_len % 16)) % 16;
  size_t total = aad_len + ap + data_len + dp + 16;
  uint8_t *buf = calloc(total, 1);
  if (!buf)
    return NULL;
  memcpy(buf, aad, aad_len);
  memcpy(buf + aad_len + ap, data, data_len);
  uint8_t *lens = buf + aad_len + ap + data_len + dp;
  uint64_t al = (uint64_t)aad_len, dl = (uint64_t)data_len;
  for (int i = 0; i < 8; i++)
    lens[i] = (uint8_t)(al >> (i * 8));
  for (int i = 0; i < 8; i++)
    lens[8 + i] = (uint8_t)(dl >> (i * 8));
  *out_len = total;
  return buf;
}

int Chacha20Poly1305EncryptC(const uint8_t *key, size_t key_len,
                             const uint8_t *iv, size_t iv_len,
                             const uint8_t *aad, size_t aad_len,
                             const uint8_t *pt, size_t pt_len, uint8_t *ct,
                             uint8_t *tag, size_t tag_len) {
  (void)key_len;
  (void)iv_len;
  (void)tag_len;
  /* Generate Poly1305 one-time key from counter=0 block */
  uint8_t poly_key[64];
  Chacha20Block(key, 0, iv, poly_key);
  /* Encrypt plaintext with counter=1 */
  Chacha20Xor(key, 1, iv, pt, pt_len, ct);
  /* MAC over aad || pad || ciphertext || pad || lengths */
  size_t pmsg_len;
  uint8_t *pmsg = BuildPolyMsg(aad, aad_len, ct, pt_len, &pmsg_len);
  if (!pmsg)
    return 0;
  Poly1305Mac(poly_key, pmsg, pmsg_len, tag);
  free(pmsg);
  return 1;
}

int Chacha20Poly1305DecryptC(const uint8_t *key, size_t key_len,
                             const uint8_t *iv, size_t iv_len,
                             const uint8_t *aad, size_t aad_len,
                             const uint8_t *ct, size_t ct_len,
                             const uint8_t *tag, size_t tag_len, uint8_t *pt) {
  (void)key_len;
  (void)iv_len;
  (void)tag_len;
  /* Regenerate Poly1305 key from counter=0 block */
  uint8_t poly_key[64];
  Chacha20Block(key, 0, iv, poly_key);
  /* Verify MAC over aad || pad || ciphertext || pad || lengths */
  size_t pmsg_len;
  uint8_t *pmsg = BuildPolyMsg(aad, aad_len, ct, ct_len, &pmsg_len);
  if (!pmsg)
    return 0;
  uint8_t computed_tag[16];
  Poly1305Mac(poly_key, pmsg, pmsg_len, computed_tag);
  free(pmsg);
  /* Constant-time comparison */
  uint8_t diff = 0;
  for (int i = 0; i < 16; i++)
    diff |= computed_tag[i] ^ tag[i];
  if (diff != 0)
    return 0;
  /* Decrypt: XOR ciphertext with keystream starting at counter=1 */
  Chacha20Xor(key, 1, iv, ct, ct_len, pt);
  return 1;
}

#undef CC20_QR
#undef CC20_ROL

// HMAC implementation

int CryptoHMAC(LPCWSTR alg_id, const uint8_t *key, size_t key_len,
               const uint8_t *data, size_t data_len, uint8_t *digest,
               size_t digest_len) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_HASH_HANDLE hHash = NULL;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, alg_id, NULL,
                                                BCRYPT_ALG_HANDLE_HMAC_FLAG);
  if (status != 0)
    return 0;

  status =
      BCryptCreateHash(hAlg, &hHash, NULL, 0, (PUCHAR)key, (ULONG)key_len, 0);
  if (status != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  status = BCryptHashData(hHash, (PUCHAR)data, (ULONG)data_len, 0);
  if (status == 0)
    status = BCryptFinishHash(hHash, digest, (ULONG)digest_len, 0);

  BCryptDestroyHash(hHash);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

// Asymmetric keys RSA( & ECDSA)

int RSAGenerateKeys(uint32_t key_size, char **pub_pem, char **priv_pem) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  NTSTATUS status =
      BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, NULL, 0);
  if (status != 0)
    return 0;

  status = BCryptGenerateKeyPair(hAlg, &hKey, key_size, 0);
  if (status == 0)
    status = BCryptFinalizeKeyPair(hKey, 0);
  if (status != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  ULONG cbPriv = 0, cbPub = 0;
  BCryptExportKey(hKey, NULL, BCRYPT_RSAFULLPRIVATE_BLOB, NULL, 0, &cbPriv, 0);
  BCryptExportKey(hKey, NULL, BCRYPT_RSAPUBLIC_BLOB, NULL, 0, &cbPub, 0);

  uint8_t *priv_der = malloc(cbPriv);
  uint8_t *pub_der = malloc(cbPub);

  status = BCryptExportKey(hKey, NULL, BCRYPT_RSAFULLPRIVATE_BLOB, priv_der,
                           cbPriv, &cbPriv, 0);
  if (status == 0)
    status = BCryptExportKey(hKey, NULL, BCRYPT_RSAPUBLIC_BLOB, pub_der, cbPub,
                             &cbPub, 0);

  if (status == 0) {
    *priv_pem = DERToPEM(priv_der, cbPriv, "PRIVATE KEY");
    *pub_pem = DERToPEM(pub_der, cbPub, "PUBLIC KEY");
  }

  free(priv_der);
  free(pub_der);
  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0) && (*priv_pem != NULL) && (*pub_pem != NULL);
}

int RSASignData(const char *priv_pem, const uint8_t *hash, size_t hash_len,
                uint8_t *sig, size_t *sig_len) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, NULL, 0) != 0)
    return 0;

  DWORD der_len = 0;
  uint8_t *der = PEMToDER(priv_pem, &der_len);
  if (!der) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  NTSTATUS status = BCryptImportKeyPair(hAlg, NULL, BCRYPT_RSAFULLPRIVATE_BLOB,
                                        &hKey, der, der_len, 0);
  free(der);
  if (status != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  BCRYPT_PKCS1_PADDING_INFO padInfo;
  padInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;

  ULONG cbSig = 0;
  status = BCryptSignHash(hKey, &padInfo, (PUCHAR)hash, (ULONG)hash_len, NULL,
                          0, &cbSig, BCRYPT_PAD_PKCS1);
  if (status == 0 && cbSig <= *sig_len) {
    status = BCryptSignHash(hKey, &padInfo, (PUCHAR)hash, (ULONG)hash_len, sig,
                            (ULONG)*sig_len, &cbSig, BCRYPT_PAD_PKCS1);
    *sig_len = cbSig;
  }

  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

int RSAVerifyData(const char *pub_pem, const uint8_t *hash, size_t hash_len,
                  const uint8_t *sig, size_t sig_len) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, NULL, 0) != 0)
    return 0;

  DWORD der_len = 0;
  uint8_t *der = PEMToDER(pub_pem, &der_len);
  if (!der) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  NTSTATUS status = BCryptImportKeyPair(hAlg, NULL, BCRYPT_RSAPUBLIC_BLOB,
                                        &hKey, der, der_len, 0);
  free(der);
  if (status != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  BCRYPT_PKCS1_PADDING_INFO padInfo;
  padInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;

  status = BCryptVerifySignature(hKey, &padInfo, (PUCHAR)hash, (ULONG)hash_len,
                                 (PUCHAR)sig, (ULONG)sig_len, BCRYPT_PAD_PKCS1);

  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

// ECDSA / ed25519 & x25519 CNG operations

static BCRYPT_ECCKEY_BLOB *ConstructECCBlob(const uint8_t *pub_x,
                                            const uint8_t *pub_y,
                                            const uint8_t *priv,
                                            size_t key_bits, DWORD magic,
                                            DWORD *out_size) {
  size_t key_bytes = (key_bits + 7) / 8;
  DWORD blob_size = (DWORD)(sizeof(BCRYPT_ECCKEY_BLOB) + key_bytes * 2 +
                            (priv ? key_bytes : 0));
  BCRYPT_ECCKEY_BLOB *blob = calloc(1, (size_t)blob_size);
  blob->dwMagic = magic;
  blob->cbKey = (ULONG)key_bytes;
  uint8_t *p = (uint8_t *)(blob + 1);
  memcpy(p, pub_x, key_bytes);
  p += key_bytes;
  if (pub_y)
    memcpy(p, pub_y, key_bytes);
  p += key_bytes;
  if (priv)
    memcpy(p, priv, key_bytes);
  *out_size = blob_size;
  return blob;
}

// TweetNaCl Ed25519 Implementation

#define FOR(i, n) for (i = 0; i < n; ++i)
#define sv static void

typedef unsigned char u8;
typedef unsigned long u32;
typedef unsigned long long u64;
typedef long long i64;
typedef i64 gf[16];

static void RandomBytes(uint8_t *x, uint64_t len) {
  BCryptGenRandom(NULL, x, (ULONG)len, 2);
}

static const u8 _0[16];
static const u8 _9[32] = {9};
static const gf gf0;
static const gf gf1 = {1};
static const gf _121665 = {0xDB41, 1};
static const gf D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141,
                     0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7,
                     0xfe73, 0x2b6f, 0x6cee, 0x5203};
static const gf D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283,
                      0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e,
                      0xfce7, 0x56df, 0xd9dc, 0x2406};
static const gf X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525,
                     0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4,
                     0x53fe, 0xcd6e, 0x36d3, 0x2169};
static const gf Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                     0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                     0x6666, 0x6666, 0x6666, 0x6666};
static const gf I = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f,
                     0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d,
                     0xdf0b, 0x4fc1, 0x2480, 0x2b83};

static u32 L32(u32 x, int c) {
  return (x << c) | ((x & 0xffffffff) >> (32 - c));
}

static u32 Ld32(const u8 *x) {
  u32 u = x[3];
  u = (u << 8) | x[2];
  u = (u << 8) | x[1];
  return (u << 8) | x[0];
}

static u64 Dl64(const u8 *x) {
  u64 i, u = 0;
  FOR(i, 8) u = (u << 8) | x[i];
  return u;
}

sv St32(u8 *x, u32 u) {
  int i;
  FOR(i, 4) {
    x[i] = (u8)u;
    u >>= 8;
  }
}

sv Ts64(u8 *x, u64 u) {
  int i;
  for (i = 7; i >= 0; --i) {
    x[i] = (u8)u;
    u >>= 8;
  }
}

static int Vn(const u8 *x, const u8 *y, int n) {
  int i;
  u32 d = 0;
  FOR(i, n) d |= x[i] ^ y[i];
  return (1 & ((d - 1) >> 8)) - 1;
}

static int CryptoVerify32(const u8 *x, const u8 *y) { return Vn(x, y, 32); }

sv Set25519(gf r, const gf a) {
  int i;
  FOR(i, 16) r[i] = a[i];
}

sv Car25519(gf o) {
  int i;
  i64 c;
  FOR(i, 16) {
    o[i] += (1LL << 16);
    c = o[i] >> 16;
    o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
    o[i] -= c << 16;
  }
}

sv Sel25519(gf p, gf q, int b) {
  i64 t, i, c = ~(b - 1);
  FOR(i, 16) {
    t = c & (p[i] ^ q[i]);
    p[i] ^= t;
    q[i] ^= t;
  }
}

sv Pack25519(u8 *o, const gf n) {
  int i, j, b;
  gf m, t;
  FOR(i, 16) t[i] = n[i];
  Car25519(t);
  Car25519(t);
  Car25519(t);
  FOR(j, 2) {
    m[0] = t[0] - 0xffed;
    for (i = 1; i < 15; i++) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    b = (m[15] >> 16) & 1;
    m[14] &= 0xffff;
    Sel25519(t, m, 1 - b);
  }
  FOR(i, 16) {
    o[2 * i] = t[i] & 0xff;
    o[2 * i + 1] = (u8)(t[i] >> 8);
  }
}

static int Neq25519(const gf a, const gf b) {
  u8 c[32], d[32];
  Pack25519(c, a);
  Pack25519(d, b);
  return CryptoVerify32(c, d);
}

static u8 Par25519(const gf a) {
  u8 d[32];
  Pack25519(d, a);
  return d[0] & 1;
}

sv Unpack25519(gf o, const u8 *n) {
  int i;
  FOR(i, 16) o[i] = n[2 * i] + ((i64)n[2 * i + 1] << 8);
  o[15] &= 0x7fff;
}

sv A(gf o, const gf a, const gf b) {
  int i;
  FOR(i, 16) o[i] = a[i] + b[i];
}

sv Z(gf o, const gf a, const gf b) {
  int i;
  FOR(i, 16) o[i] = a[i] - b[i];
}

sv M(gf o, const gf a, const gf b) {
  i64 i, j, t[31];
  FOR(i, 31) t[i] = 0;
  FOR(i, 16) FOR(j, 16) t[i + j] += a[i] * b[j];
  FOR(i, 15) t[i] += 38 * t[i + 16];
  FOR(i, 16) o[i] = t[i];
  Car25519(o);
  Car25519(o);
}

sv S(gf o, const gf a) { M(o, a, a); }

sv Inv25519(gf o, const gf i) {
  gf c;
  int a;
  FOR(a, 16) c[a] = i[a];
  for (a = 253; a >= 0; a--) {
    S(c, c);
    if (a != 2 && a != 4)
      M(c, c, i);
  }
  FOR(a, 16) o[a] = c[a];
}

sv Pow2523(gf o, const gf i) {
  gf c;
  int a;
  FOR(a, 16) c[a] = i[a];
  for (a = 250; a >= 0; a--) {
    S(c, c);
    if (a != 1)
      M(c, c, i);
  }
  FOR(a, 16) o[a] = c[a];
}

static u64 R_t(u64 x, int c) { return (x >> c) | (x << (64 - c)); }
static u64 Ch(u64 x, u64 y, u64 z) { return (x & y) ^ (~x & z); }
static u64 Maj(u64 x, u64 y, u64 z) { return (x & y) ^ (x & z) ^ (y & z); }
static u64 Sigma0(u64 x) { return R_t(x, 28) ^ R_t(x, 34) ^ R_t(x, 39); }
static u64 Sigma1(u64 x) { return R_t(x, 14) ^ R_t(x, 18) ^ R_t(x, 41); }
static u64 SigMa0(u64 x) { return R_t(x, 1) ^ R_t(x, 8) ^ (x >> 7); }
static u64 SigMa1(u64 x) { return R_t(x, 19) ^ R_t(x, 61) ^ (x >> 6); }

static const u64 K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

static int CryptoHashblocks(u8 *x, const u8 *m, u64 n) {
  u64 z[8], b[8], a[8], w[16], t;
  int i, j;

  FOR(i, 8) z[i] = a[i] = Dl64(x + 8 * i);

  while (n >= 128) {
    FOR(i, 16) w[i] = Dl64(m + 8 * i);

    FOR(i, 80) {
      FOR(j, 8) b[j] = a[j];
      t = a[7] + Sigma1(a[4]) + Ch(a[4], a[5], a[6]) + K[i] + w[i % 16];
      b[7] = t + Sigma0(a[0]) + Maj(a[0], a[1], a[2]);
      b[3] += t;
      FOR(j, 8) a[(j + 1) % 8] = b[j];
      if (i % 16 == 15)
        FOR(j, 16)
      w[j] +=
          w[(j + 9) % 16] + SigMa0(w[(j + 1) % 16]) + SigMa1(w[(j + 14) % 16]);
    }

    FOR(i, 8) {
      a[i] += z[i];
      z[i] = a[i];
    }

    m += 128;
    n -= 128;
  }

  FOR(i, 8) Ts64(x + 8 * i, z[i]);

  return (int)n;
}

static const u8 iv[64] = {
    0x6a, 0x09, 0xe6, 0x67, 0xf3, 0xbc, 0xc9, 0x08, 0xbb, 0x67, 0xae,
    0x85, 0x84, 0xca, 0xa7, 0x3b, 0x3c, 0x6e, 0xf3, 0x72, 0xfe, 0x94,
    0xf8, 0x2b, 0xa5, 0x4f, 0xf5, 0x3a, 0x5f, 0x1d, 0x36, 0xf1, 0x51,
    0x0e, 0x52, 0x7f, 0xad, 0xe6, 0x82, 0xd1, 0x9b, 0x05, 0x68, 0x8c,
    0x2b, 0x3e, 0x6c, 0x1f, 0x1f, 0x83, 0xd9, 0xab, 0xfb, 0x41, 0xbd,
    0x6b, 0x5b, 0xe0, 0xcd, 0x19, 0x13, 0x7e, 0x21, 0x79};

static int CryptoHash(u8 *out, const u8 *m, u64 n) {
  u8 h[64], x[256];
  u64 i, b = n;

  FOR(i, 64) h[i] = iv[i];

  CryptoHashblocks(h, m, n);
  m += n;
  n &= 127;
  m -= n;

  FOR(i, 256) x[i] = 0;
  FOR(i, n) x[i] = m[i];
  x[n] = 128;

  n = 256 - 128 * (n < 112);
  x[n - 9] = (u8)(b >> 61);
  Ts64(x + n - 8, b << 3);
  CryptoHashblocks(h, x, n);

  FOR(i, 64) out[i] = h[i];

  return 0;
}

sv Add(gf p[4], gf q[4]) {
  gf a, b, c, d, t, e, f, g, h;

  Z(a, p[1], p[0]);
  Z(t, q[1], q[0]);
  M(a, a, t);
  A(b, p[0], p[1]);
  A(t, q[0], q[1]);
  M(b, b, t);
  M(c, p[3], q[3]);
  M(c, c, D2);
  M(d, p[2], q[2]);
  A(d, d, d);
  Z(e, b, a);
  Z(f, d, c);
  A(g, d, c);
  A(h, b, a);

  M(p[0], e, f);
  M(p[1], h, g);
  M(p[2], g, f);
  M(p[3], e, h);
}

sv CSwap(gf p[4], gf q[4], u8 b) {
  int i;
  FOR(i, 4)
  Sel25519(p[i], q[i], b);
}

sv Pack(u8 *r, gf p[4]) {
  gf tx, ty, zi;
  Inv25519(zi, p[2]);
  M(tx, p[0], zi);
  M(ty, p[1], zi);
  Pack25519(r, ty);
  r[31] ^= Par25519(tx) << 7;
}

sv ScalarMult(gf p[4], gf q[4], const u8 *s) {
  int i;
  Set25519(p[0], gf0);
  Set25519(p[1], gf1);
  Set25519(p[2], gf1);
  Set25519(p[3], gf0);
  for (i = 255; i >= 0; --i) {
    u8 b = (s[i / 8] >> (i & 7)) & 1;
    CSwap(p, q, b);
    Add(q, p);
    Add(p, p);
    CSwap(p, q, b);
  }
}

sv ScalarBase(gf p[4], const u8 *s) {
  gf q[4];
  Set25519(q[0], X);
  Set25519(q[1], Y);
  Set25519(q[2], gf1);
  M(q[3], X, Y);
  ScalarMult(p, q, s);
}

static int CryptoSignKeypair(u8 *pk, u8 *sk) {
  u8 d[64];
  gf p[4];
  int i;

  RandomBytes(sk, 32);
  CryptoHash(d, sk, 32);
  d[0] &= 248;
  d[31] &= 127;
  d[31] |= 64;

  ScalarBase(p, d);
  Pack(pk, p);

  FOR(i, 32) sk[32 + i] = pk[i];
  return 0;
}

static const u64 L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
                          0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                          0,    0,    0,    0,    0,    0,    0,    0,
                          0,    0,    0,    0,    0,    0,    0,    0x10};

sv ModL(u8 *r, i64 x[64]) {
  i64 carry, i, j;
  for (i = 63; i >= 32; --i) {
    carry = 0;
    for (j = i - 32; j < i - 12; ++j) {
      x[j] += carry - 16 * x[i] * L[j - (i - 32)];
      carry = (x[j] + 128) >> 8;
      x[j] -= carry << 8;
    }
    x[j] += carry;
    x[i] = 0;
  }
  carry = 0;
  FOR(j, 32) {
    x[j] += carry - (x[31] >> 4) * L[j];
    carry = x[j] >> 8;
    x[j] &= 255;
  }
  FOR(j, 32) x[j] -= carry * L[j];
  FOR(i, 32) {
    x[i + 1] += x[i] >> 8;
    r[i] = x[i] & 255;
  }
}

sv ReDuce(u8 *r) {
  i64 x[64], i;
  FOR(i, 64) x[i] = (u64)r[i];
  FOR(i, 64) r[i] = 0;
  ModL(r, x);
}

static int CryptoSign(u8 *sm, u64 *smlen, const u8 *m, u64 n, const u8 *sk) {
  u8 d[64], h[64], r[64];
  u64 i, j;
  i64 x[64];
  gf p[4];

  CryptoHash(d, sk, 32);
  d[0] &= 248;
  d[31] &= 127;
  d[31] |= 64;

  *smlen = n + 64;
  FOR(i, n) sm[64 + i] = m[i];
  FOR(i, 32) sm[32 + i] = d[32 + i];

  CryptoHash(r, sm + 32, n + 32);
  ReDuce(r);
  ScalarBase(p, r);
  Pack(sm, p);

  FOR(i, 32) sm[i + 32] = sk[i + 32];
  CryptoHash(h, sm, n + 64);
  ReDuce(h);

  FOR(i, 64) x[i] = 0;
  FOR(i, 32) x[i] = (u64)r[i];
  FOR(i, 32) FOR(j, 32) x[i + j] += h[i] * (u64)d[j];
  ModL(sm + 32, x);

  return 0;
}

static int UnpackNeg(gf r[4], const u8 p[32]) {
  gf t, chk, num, den, den2, den4, den6;
  Set25519(r[2], gf1);
  Unpack25519(r[1], p);
  S(num, r[1]);
  M(den, num, D);
  Z(num, num, r[2]);
  A(den, r[2], den);

  S(den2, den);
  S(den4, den2);
  M(den6, den4, den2);
  M(t, den6, num);
  M(t, t, den);

  Pow2523(t, t);
  M(t, t, num);
  M(t, t, den);
  M(t, t, den);
  M(r[0], t, den);

  S(chk, r[0]);
  M(chk, chk, den);
  if (Neq25519(chk, num))
    M(r[0], r[0], I);

  S(chk, r[0]);
  M(chk, chk, den);
  if (Neq25519(chk, num))
    return -1;

  if (Par25519(r[0]) == (p[31] >> 7))
    Z(r[0], gf0, r[0]);

  M(r[3], r[0], r[1]);
  return 0;
}

static int CryptoSignOpen(u8 *m, u64 *mlen, const u8 *sm, u64 n, const u8 *pk) {
  int i;
  u8 t[32], h[64];
  gf p[4], q[4];

  *mlen = -1;
  if (n < 64)
    return -1;

  if (UnpackNeg(q, pk))
    return -1;

  FOR(i, n) m[i] = sm[i];
  FOR(i, 32) m[i + 32] = pk[i];
  CryptoHash(h, m, n);
  ReDuce(h);
  ScalarMult(p, q, h);

  ScalarBase(q, sm + 32);
  Add(p, q);
  Pack(t, p);

  n -= 64;
  if (CryptoVerify32(sm, t)) {
    FOR(i, n) m[i] = 0;
    return -1;
  }

  FOR(i, n) m[i] = sm[i + 64];
  *mlen = n;
  return 0;
}

#undef FOR
#undef sv

int ECCGenerateKeys(LPCWSTR alg_id, LPCWSTR curve_name, DWORD magic_priv,
                    DWORD magic_pub, size_t key_bits, char **pub_pem,
                    char **priv_pem) {
  if (alg_id && wcscmp(alg_id, BCRYPT_ECDSA_ALGORITHM) == 0 && curve_name &&
      wcscmp(curve_name, BCRYPT_ECC_CURVE_25519) == 0) {
    u8 pk[32];
    u8 sk[64];
    CryptoSignKeypair(pk, sk);

    uint8_t zero_y[32] = {0};
    DWORD priv_size = 0;
    BCRYPT_ECCKEY_BLOB *priv_blob =
        ConstructECCBlob(pk, zero_y, sk, 255, 0x56445345, &priv_size);
    DWORD pub_size = 0;
    BCRYPT_ECCKEY_BLOB *pub_blob =
        ConstructECCBlob(pk, zero_y, NULL, 255, 0x50445345, &pub_size);

    *priv_pem = DERToPEM((const uint8_t *)priv_blob, priv_size, "PRIVATE KEY");
    *pub_pem = DERToPEM((const uint8_t *)pub_blob, pub_size, "PUBLIC KEY");

    free(priv_blob);
    free(pub_blob);
    return (*priv_pem != NULL) && (*pub_pem != NULL);
  }

  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, alg_id, NULL, 0);
  if (status != 0)
    return 0;

  if (curve_name) {
    status =
        BCryptSetProperty(hAlg, BCRYPT_ECC_CURVE_NAME, (PBYTE)curve_name,
                          (ULONG)(wcslen(curve_name) * sizeof(WCHAR) + 2), 0);
    if (status != 0) {
      BCryptCloseAlgorithmProvider(hAlg, 0);
      return 0;
    }
  }

  status = BCryptGenerateKeyPair(hAlg, &hKey, (ULONG)key_bits, 0);
  if (status == 0)
    status = BCryptFinalizeKeyPair(hKey, 0);
  if (status != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  ULONG cbPriv = 0, cbPub = 0;
  BCryptExportKey(hKey, NULL, BCRYPT_PRIVATE_KEY_BLOB, NULL, 0, &cbPriv, 0);
  BCryptExportKey(hKey, NULL, BCRYPT_PUBLIC_KEY_BLOB, NULL, 0, &cbPub, 0);

  uint8_t *priv_der = malloc(cbPriv);
  uint8_t *pub_der = malloc(cbPub);

  status = BCryptExportKey(hKey, NULL, BCRYPT_PRIVATE_KEY_BLOB, priv_der,
                           cbPriv, &cbPriv, 0);
  if (status == 0)
    status = BCryptExportKey(hKey, NULL, BCRYPT_PUBLIC_KEY_BLOB, pub_der, cbPub,
                             &cbPub, 0);

  if (status == 0) {
    *priv_pem = DERToPEM(priv_der, cbPriv, "PRIVATE KEY");
    *pub_pem = DERToPEM(pub_der, cbPub, "PUBLIC KEY");
  }

  free(priv_der);
  free(pub_der);
  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

int ECCSignData(LPCWSTR alg_id, LPCWSTR curve_name, const char *priv_pem,
                const uint8_t *hash, size_t hash_len, uint8_t *sig,
                size_t *sig_len) {
  if (alg_id && wcscmp(alg_id, BCRYPT_ECDSA_ALGORITHM) == 0 && curve_name &&
      wcscmp(curve_name, BCRYPT_ECC_CURVE_25519) == 0) {
    DWORD der_len = 0;
    uint8_t *der = PEMToDER(priv_pem, &der_len);
    if (!der || der_len < 104) {
      if (der)
        free(der);
      return 0;
    }

    BCRYPT_ECCKEY_BLOB *ecc = (BCRYPT_ECCKEY_BLOB *)der;
    uint8_t *p = (uint8_t *)(ecc + 1);
    uint8_t *pub_x = p;
    uint8_t *priv_d = p + 64;

    u8 sk[64];
    memcpy(sk, priv_d, 32);     // D (seed)
    memcpy(sk + 32, pub_x, 32); // X (public key)

    u8 *sm = malloc(hash_len + 64);
    u64 smlen = 0;
    CryptoSign(sm, &smlen, hash, hash_len, sk);

    memcpy(sig, sm, 64);
    *sig_len = 64;

    free(sm);
    free(der);
    return 1;
  }

  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  if (BCryptOpenAlgorithmProvider(&hAlg, alg_id, NULL, 0) != 0)
    return 0;
  if (curve_name) {
    BCryptSetProperty(hAlg, BCRYPT_ECC_CURVE_NAME, (PBYTE)curve_name,
                      (ULONG)(wcslen(curve_name) * sizeof(WCHAR) + 2), 0);
  }

  DWORD der_len = 0;
  uint8_t *der = PEMToDER(priv_pem, &der_len);
  if (!der) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  NTSTATUS status = BCryptImportKeyPair(hAlg, NULL, BCRYPT_PRIVATE_KEY_BLOB,
                                        &hKey, der, der_len, 0);
  free(der);
  if (status != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  ULONG cbSig = 0;
  status = BCryptSignHash(hKey, NULL, (PUCHAR)hash, (ULONG)hash_len, NULL, 0,
                          &cbSig, 0);
  if (status == 0 && cbSig <= *sig_len) {
    status = BCryptSignHash(hKey, NULL, (PUCHAR)hash, (ULONG)hash_len, sig,
                            (ULONG)*sig_len, &cbSig, 0);
    *sig_len = cbSig;
  }

  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

int ECCVerifyData(LPCWSTR alg_id, LPCWSTR curve_name, const char *pub_pem,
                  const uint8_t *hash, size_t hash_len, const uint8_t *sig,
                  size_t sig_len) {
  if (alg_id && wcscmp(alg_id, BCRYPT_ECDSA_ALGORITHM) == 0 && curve_name &&
      wcscmp(curve_name, BCRYPT_ECC_CURVE_25519) == 0) {
    DWORD der_len = 0;
    uint8_t *der = PEMToDER(pub_pem, &der_len);
    if (!der || der_len < 72) {
      if (der)
        free(der);
      return 0;
    }

    BCRYPT_ECCKEY_BLOB *ecc = (BCRYPT_ECCKEY_BLOB *)der;
    uint8_t *pub_x = (uint8_t *)(ecc + 1);

    u8 *sm = malloc(sig_len + hash_len);
    memcpy(sm, sig, sig_len);
    memcpy(sm + sig_len, hash, hash_len);

    u8 *m = malloc(sig_len + hash_len);
    u64 mlen = 0;
    int res = CryptoSignOpen(m, &mlen, sm, sig_len + hash_len, pub_x);

    free(sm);
    free(m);
    free(der);
    return (res == 0);
  }

  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  if (BCryptOpenAlgorithmProvider(&hAlg, alg_id, NULL, 0) != 0)
    return 0;
  if (curve_name) {
    BCryptSetProperty(hAlg, BCRYPT_ECC_CURVE_NAME, (PBYTE)curve_name,
                      (ULONG)(wcslen(curve_name) * sizeof(WCHAR) + 2), 0);
  }

  DWORD der_len = 0;
  uint8_t *der = PEMToDER(pub_pem, &der_len);
  if (!der) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  NTSTATUS status = BCryptImportKeyPair(hAlg, NULL, BCRYPT_PUBLIC_KEY_BLOB,
                                        &hKey, der, der_len, 0);
  free(der);
  if (status != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  status = BCryptVerifySignature(hKey, NULL, (PUCHAR)hash, (ULONG)hash_len,
                                 (PUCHAR)sig, (ULONG)sig_len, 0);

  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

int Curve25519GenerateHex(LPCWSTR alg_id, char *pub_hex, char *priv_hex) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, alg_id, NULL, 0);
  if (status != 0)
    return 0;

  status = BCryptSetProperty(hAlg, BCRYPT_ECC_CURVE_NAME,
                             (PBYTE)BCRYPT_ECC_CURVE_25519,
                             sizeof(BCRYPT_ECC_CURVE_25519), 0);
  if (status == 0)
    status = BCryptGenerateKeyPair(hAlg, &hKey, 255, 0);
  if (status == 0)
    status = BCryptFinalizeKeyPair(hKey, 0);
  if (status != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
  }

  ULONG cbBlob = 0;
  BCryptExportKey(hKey, NULL, BCRYPT_ECCPRIVATE_BLOB, NULL, 0, &cbBlob, 0);
  uint8_t *blob = malloc(cbBlob);
  status = BCryptExportKey(hKey, NULL, BCRYPT_ECCPRIVATE_BLOB, blob, cbBlob,
                           &cbBlob, 0);

  if (status == 0) {
    BCRYPT_ECCKEY_BLOB *ecc = (BCRYPT_ECCKEY_BLOB *)blob;
    uint8_t *p = (uint8_t *)(ecc + 1);
    HexEncodeC(p, 32, pub_hex);
    HexEncodeC(p + 64, 32, priv_hex);
  }

  free(blob);
  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

int X25519SharedSecret(const char *priv_hex, const char *pub_hex,
                       char *secret_hex) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hPrivKey = NULL;
  BCRYPT_KEY_HANDLE hPubKey = NULL;
  BCRYPT_SECRET_HANDLE hSecret = NULL;

  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDH_ALGORITHM, NULL, 0) != 0)
    return 0;
  BCryptSetProperty(hAlg, BCRYPT_ECC_CURVE_NAME, (PBYTE)BCRYPT_ECC_CURVE_25519,
                    sizeof(BCRYPT_ECC_CURVE_25519), 0);

  uint8_t priv_bytes[32];
  uint8_t pub_bytes[32];
  HexDecodeC(priv_hex, priv_bytes);
  HexDecodeC(pub_hex, pub_bytes);

  uint8_t zero_y[32] = {0};
  DWORD priv_size = 0;
  BCRYPT_ECCKEY_BLOB *priv_blob =
      ConstructECCBlob(pub_bytes, zero_y, priv_bytes, 255,
                       BCRYPT_ECDH_PRIVATE_generic_25519_MAGIC, &priv_size);
  DWORD pub_size = 0;
  BCRYPT_ECCKEY_BLOB *pub_blob =
      ConstructECCBlob(pub_bytes, zero_y, NULL, 255,
                       BCRYPT_ECDH_PUBLIC_generic_25519_MAGIC, &pub_size);

  NTSTATUS status =
      BCryptImportKeyPair(hAlg, NULL, BCRYPT_ECCPRIVATE_BLOB, &hPrivKey,
                          (PUCHAR)priv_blob, priv_size, 0);
  if (status == 0)
    status = BCryptImportKeyPair(hAlg, NULL, BCRYPT_ECCPUBLIC_BLOB, &hPubKey,
                                 (PUCHAR)pub_blob, pub_size, 0);
  if (status == 0)
    status = BCryptSecretAgreement(hPrivKey, hPubKey, &hSecret, 0);

  uint8_t secret[32];
  ULONG cbSec = 0;
  if (status == 0)
    status = BCryptDeriveKey(hSecret, BCRYPT_KDF_RAW_SECRET, NULL, secret, 32,
                             &cbSec, 0);

  if (status == 0) {
    HexEncodeC(secret, 32, secret_hex);
  }

  free(priv_blob);
  free(pub_blob);
  if (hSecret)
    BCryptDestroySecret(hSecret);
  if (hPrivKey)
    BCryptDestroyKey(hPrivKey);
  if (hPubKey)
    BCryptDestroyKey(hPubKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

int CryptoSha512(const uint8_t *data, size_t len, uint8_t *digest) {
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_HASH_HANDLE hHash = NULL;
  NTSTATUS status =
      BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA512_ALGORITHM, NULL, 0);
  if (status == 0)
    status = BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
  if (status == 0)
    status = BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0);
  if (status == 0)
    status = BCryptFinishHash(hHash, digest, 64, 0);
  if (hHash)
    BCryptDestroyHash(hHash);
  if (hAlg)
    BCryptCloseAlgorithmProvider(hAlg, 0);
  return (status == 0);
}

int CryptoBlake2b(const uint8_t *data, size_t len, uint8_t *out, size_t out_len) {
  if (!out || out_len == 0 || out_len > 64) return 0;
  return (Blake2b(data, len, out, out_len) == 0);
}

static const uint32_t Blake2sIv[8] = {
    0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
    0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL
};

#define ROTR32(x, y) (((x) >> (y)) | ((x) << (32 - (y))))

static void Blake2sCompressBlock(uint32_t h[8], const uint8_t block[64], uint32_t bytes_compressed, int is_last) {
  uint32_t v[16];
  uint32_t m[16];
  for (int i = 0; i < 8; i++) v[i] = h[i];
  v[8]  = Blake2sIv[0];
  v[9]  = Blake2sIv[1];
  v[10] = Blake2sIv[2];
  v[11] = Blake2sIv[3];
  v[12] = Blake2sIv[4] ^ bytes_compressed;
  v[13] = Blake2sIv[5];
  v[14] = is_last ? (Blake2sIv[6] ^ ~0U) : Blake2sIv[6];
  v[15] = Blake2sIv[7];

  for (int i = 0; i < 16; i++) {
    m[i] = ((uint32_t)block[i * 4 + 0]) |
           (((uint32_t)block[i * 4 + 1]) << 8) |
           (((uint32_t)block[i * 4 + 2]) << 16) |
           (((uint32_t)block[i * 4 + 3]) << 24);
  }

#define B2S_G_CALL(r, i, a, b, c, d) \
  do { \
    v[a] = v[a] + v[b] + m[blake2b_sigma[r][2 * i]]; \
    v[d] = ROTR32(v[d] ^ v[a], 16); \
    v[c] = v[c] + v[d]; \
    v[b] = ROTR32(v[b] ^ v[c], 12); \
    v[a] = v[a] + v[b] + m[blake2b_sigma[r][2 * i + 1]]; \
    v[d] = ROTR32(v[d] ^ v[a], 8); \
    v[c] = v[c] + v[d]; \
    v[b] = ROTR32(v[b] ^ v[c], 7); \
  } while (0)

  for (int r = 0; r < 10; r++) {
    B2S_G_CALL(r, 0, 0, 4,  8, 12);
    B2S_G_CALL(r, 1, 1, 5,  9, 13);
    B2S_G_CALL(r, 2, 2, 6, 10, 14);
    B2S_G_CALL(r, 3, 3, 7, 11, 15);
    B2S_G_CALL(r, 4, 0, 5, 10, 15);
    B2S_G_CALL(r, 5, 1, 6, 11, 12);
    B2S_G_CALL(r, 6, 2, 7,  8, 13);
    B2S_G_CALL(r, 7, 3, 4,  9, 14);
  }
#undef B2S_G_CALL

  for (int i = 0; i < 8; i++) {
    h[i] ^= v[i] ^ v[i + 8];
  }
}

int CryptoBlake2s(const uint8_t *data, size_t len, uint8_t *out, size_t out_len) {
  if (!out || out_len == 0 || out_len > 32) return 0;
  uint32_t h[8];
  for (int i = 0; i < 8; i++) h[i] = Blake2sIv[i];
  h[0] ^= 0x01010000 ^ (uint32_t)out_len;

  uint8_t block[64] = {0};
  size_t offset = 0;
  size_t bytes_left = len;

  while (bytes_left > 64) {
    memcpy(block, data + offset, 64);
    offset += 64;
    bytes_left -= 64;
    Blake2sCompressBlock(h, block, (uint32_t)offset, 0);
  }

  memset(block, 0, 64);
  if (bytes_left > 0) memcpy(block, data + offset, bytes_left);
  Blake2sCompressBlock(h, block, (uint32_t)len, 1);

  uint8_t full_hash[32];
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 4; j++) {
      full_hash[i * 4 + j] = (uint8_t)((h[i] >> (j * 8)) & 0xFF);
    }
  }
  memcpy(out, full_hash, out_len);
  return 1;
}

// RFC 8439 Poly1305 One-Time MAC Engine

typedef struct {
  uint32_t r[5];
  uint32_t h[5];
  uint32_t pad[4];
  uint8_t final;
} Poly1305State;

static void Poly1305Blocks(Poly1305State *st, const uint8_t *m, size_t bytes) {
  const uint32_t hibit = st->final ? 0 : (1UL << 24);
  uint32_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3], r4 = st->r[4];
  uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
  uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];

  while (bytes >= 16) {
    uint32_t t0 = ((uint32_t)m[0]) | (((uint32_t)m[1]) << 8) | (((uint32_t)m[2]) << 16) | (((uint32_t)m[3]) << 24);
    uint32_t t1 = ((uint32_t)m[4]) | (((uint32_t)m[5]) << 8) | (((uint32_t)m[6]) << 16) | (((uint32_t)m[7]) << 24);
    uint32_t t2 = ((uint32_t)m[8]) | (((uint32_t)m[9]) << 8) | (((uint32_t)m[10]) << 16) | (((uint32_t)m[11]) << 24);
    uint32_t t3 = ((uint32_t)m[12]) | (((uint32_t)m[13]) << 8) | (((uint32_t)m[14]) << 16) | (((uint32_t)m[15]) << 24);

    h0 += t0 & 0x3ffffff;
    h1 += ((t0 >> 26) | (t1 << 6)) & 0x3ffffff;
    h2 += ((t1 >> 20) | (t2 << 12)) & 0x3ffffff;
    h3 += ((t2 >> 14) | (t3 << 18)) & 0x3ffffff;
    h4 += (t3 >> 8) | hibit;

    uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
    uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
    uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
    uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
    uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

    uint32_t c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff; d1 += c;
    c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff; d2 += c;
    c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff; d3 += c;
    c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff; d4 += c;
    c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

    m += 16;
    bytes -= 16;
  }

  st->h[0] = h0; st->h[1] = h1; st->h[2] = h2; st->h[3] = h3; st->h[4] = h4;
}

int CryptoPoly1305(const uint8_t *key, const uint8_t *msg, size_t msg_len, uint8_t tag[16]) {
  if (!key || !tag) return 0;
  Poly1305State st;
  memset(&st, 0, sizeof(st));

  st.r[0] = (((uint32_t)key[0]) | (((uint32_t)key[1]) << 8) | (((uint32_t)key[2]) << 16) | (((uint32_t)key[3]) << 24)) & 0x3ffffff;
  st.r[1] = ((((uint32_t)key[3]) >> 2) | (((uint32_t)key[4]) << 6) | (((uint32_t)key[5]) << 14) | (((uint32_t)key[6]) << 22)) & 0x3ffff03;
  st.r[2] = ((((uint32_t)key[6]) >> 4) | (((uint32_t)key[7]) << 4) | (((uint32_t)key[8]) << 12) | (((uint32_t)key[9]) << 20)) & 0x3ffc0ff;
  st.r[3] = ((((uint32_t)key[9]) >> 6) | (((uint32_t)key[10]) << 2) | (((uint32_t)key[11]) << 10) | (((uint32_t)key[12]) << 18)) & 0x3f03fff;
  st.r[4] = ((((uint32_t)key[12]) >> 8) | (((uint32_t)key[13]) << 0) | (((uint32_t)key[14]) << 8) | (((uint32_t)key[15]) << 16)) & 0x00fffff;

  st.pad[0] = ((uint32_t)key[16]) | (((uint32_t)key[17]) << 8) | (((uint32_t)key[18]) << 16) | (((uint32_t)key[19]) << 24);
  st.pad[1] = ((uint32_t)key[20]) | (((uint32_t)key[21]) << 8) | (((uint32_t)key[22]) << 16) | (((uint32_t)key[23]) << 24);
  st.pad[2] = ((uint32_t)key[24]) | (((uint32_t)key[25]) << 8) | (((uint32_t)key[26]) << 16) | (((uint32_t)key[27]) << 24);
  st.pad[3] = ((uint32_t)key[28]) | (((uint32_t)key[29]) << 8) | (((uint32_t)key[30]) << 16) | (((uint32_t)key[31]) << 24);

  size_t full_blocks = msg_len - (msg_len % 16);
  if (full_blocks > 0) {
    Poly1305Blocks(&st, msg, full_blocks);
    msg += full_blocks;
    msg_len -= full_blocks;
  }

  if (msg_len > 0) {
    uint8_t block[16] = {0};
    memcpy(block, msg, msg_len);
    block[msg_len] = 1;
    st.final = 1;
    Poly1305Blocks(&st, block, 16);
  }

  uint32_t h0 = st.h[0], h1 = st.h[1], h2 = st.h[2], h3 = st.h[3], h4 = st.h[4];
  uint32_t c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
  c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
  c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
  c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
  c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

  uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
  uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
  uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
  uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
  uint32_t g4 = h4 + c - (1UL << 26);

  uint32_t mask = (g4 >> 31) - 1;
  g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
  mask = ~mask;
  h0 = (h0 & mask) | g0;
  h1 = (h1 & mask) | g1;
  h2 = (h2 & mask) | g2;
  h3 = (h3 & mask) | g3;
  h4 = (h4 & mask) | g4;

  uint64_t f0 = ((uint64_t)h0) | (((uint64_t)h1) << 26);
  uint64_t f1 = ((uint64_t)h1 >> 38) | (((uint64_t)h2) << 12) | (((uint64_t)h3) << 38);
  uint64_t f2 = ((uint64_t)h3 >> 26) | (((uint64_t)h4) << 0);

  f0 += ((uint64_t)st.pad[0]) | (((uint64_t)st.pad[1]) << 32);
  f1 += ((uint64_t)st.pad[2]) | (((uint64_t)st.pad[3]) << 32) + (f0 < (((uint64_t)st.pad[0]) | (((uint64_t)st.pad[1]) << 32)));

  memcpy(tag + 0, &f0, 8);
  memcpy(tag + 8, &f1, 8);
  return 1;
}

// SipHash-2-4 Keyed Cryptographic Hash Engine (64-bit and 128-bit)

#define SIPROUND \
  do { \
    v0 += v1; v1 = ROTR64(v1, 51); v1 ^= v0; v0 = ROTR64(v0, 32); \
    v2 += v3; v3 = ROTR64(v3, 48); v3 ^= v2; \
    v0 += v3; v3 = ROTR64(v3, 43); v3 ^= v0; \
    v2 += v1; v1 = ROTR64(v1, 47); v1 ^= v2; v2 = ROTR64(v2, 32); \
  } while (0)

uint64_t CryptoSipHash64(const uint8_t key[16], const uint8_t *in, size_t inlen) {
  uint64_t k0 = ((const uint64_t *)key)[0];
  uint64_t k1 = ((const uint64_t *)key)[1];

  uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
  uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
  uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
  uint64_t v3 = 0x7465646279746573ULL ^ k1;

  const uint8_t *end = in + inlen - (inlen % sizeof(uint64_t));
  const int left = inlen & 7;
  uint64_t b = ((uint64_t)inlen) << 56;

  for (; in != end; in += 8) {
    uint64_t m = ((const uint64_t *)in)[0];
    v3 ^= m;
    SIPROUND;
    SIPROUND;
    v0 ^= m;
  }

  switch (left) {
  case 7: b |= ((uint64_t)in[6]) << 48;
  case 6: b |= ((uint64_t)in[5]) << 40;
  case 5: b |= ((uint64_t)in[4]) << 32;
  case 4: b |= ((uint64_t)in[3]) << 24;
  case 3: b |= ((uint64_t)in[2]) << 16;
  case 2: b |= ((uint64_t)in[1]) << 8;
  case 1: b |= ((uint64_t)in[0]); break;
  case 0: break;
  }

  v3 ^= b;
  SIPROUND;
  SIPROUND;
  v0 ^= b;

  v2 ^= 0xff;
  SIPROUND;
  SIPROUND;
  SIPROUND;
  SIPROUND;

  return (v0 ^ v1) ^ (v2 ^ v3);
}

void CryptoSipHash128(const uint8_t key[16], const uint8_t *in, size_t inlen, uint8_t out[16]) {
  uint64_t k0 = ((const uint64_t *)key)[0];
  uint64_t k1 = ((const uint64_t *)key)[1];

  uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
  uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
  uint64_t v2 = 0x6c7967656e657261ULL ^ (k0 ^ 0xee);
  uint64_t v3 = 0x7465646279746573ULL ^ k1;

  const uint8_t *end = in + inlen - (inlen % sizeof(uint64_t));
  const int left = inlen & 7;
  uint64_t b = ((uint64_t)inlen) << 56;

  for (; in != end; in += 8) {
    uint64_t m = ((const uint64_t *)in)[0];
    v3 ^= m;
    SIPROUND;
    SIPROUND;
    v0 ^= m;
  }

  switch (left) {
  case 7: b |= ((uint64_t)in[6]) << 48;
  case 6: b |= ((uint64_t)in[5]) << 40;
  case 5: b |= ((uint64_t)in[4]) << 32;
  case 4: b |= ((uint64_t)in[3]) << 24;
  case 3: b |= ((uint64_t)in[2]) << 16;
  case 2: b |= ((uint64_t)in[1]) << 8;
  case 1: b |= ((uint64_t)in[0]); break;
  case 0: break;
  }

  v3 ^= b;
  SIPROUND;
  SIPROUND;
  v0 ^= b;

  v2 ^= 0xee;
  SIPROUND;
  SIPROUND;
  SIPROUND;
  SIPROUND;

  uint64_t out0 = (v0 ^ v1) ^ (v2 ^ v3);

  v1 ^= 0xdd;
  SIPROUND;
  SIPROUND;
  SIPROUND;
  SIPROUND;

  uint64_t out1 = (v0 ^ v1) ^ (v2 ^ v3);

  memcpy(out, &out0, 8);
  memcpy(out + 8, &out1, 8);
}

// XChaCha20-Poly1305 AEAD Engine

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

static inline void ChaCha20QuarterRound(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
  *a += *b; *d = ROTL32(*d ^ *a, 16);
  *c += *d; *b = ROTL32(*b ^ *c, 12);
  *a += *b; *d = ROTL32(*d ^ *a, 8);
  *c += *d; *b = ROTL32(*b ^ *c, 7);
}

void CryptoHChaCha20(const uint8_t key[32], const uint8_t nonce[16], uint8_t out[32]) {
  uint32_t state[16] = {
    0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
    ((const uint32_t *)key)[0], ((const uint32_t *)key)[1],
    ((const uint32_t *)key)[2], ((const uint32_t *)key)[3],
    ((const uint32_t *)key)[4], ((const uint32_t *)key)[5],
    ((const uint32_t *)key)[6], ((const uint32_t *)key)[7],
    ((const uint32_t *)nonce)[0], ((const uint32_t *)nonce)[1],
    ((const uint32_t *)nonce)[2], ((const uint32_t *)nonce)[3]
  };

  for (int i = 0; i < 10; i++) {
    ChaCha20QuarterRound(&state[0], &state[4], &state[8],  &state[12]);
    ChaCha20QuarterRound(&state[1], &state[5], &state[9],  &state[13]);
    ChaCha20QuarterRound(&state[2], &state[6], &state[10], &state[14]);
    ChaCha20QuarterRound(&state[3], &state[7], &state[11], &state[15]);
    ChaCha20QuarterRound(&state[0], &state[5], &state[10], &state[15]);
    ChaCha20QuarterRound(&state[1], &state[6], &state[11], &state[12]);
    ChaCha20QuarterRound(&state[2], &state[7], &state[8],  &state[13]);
    ChaCha20QuarterRound(&state[3], &state[4], &state[9],  &state[14]);
  }

  uint32_t *out32 = (uint32_t *)out;
  out32[0] = state[0];  out32[1] = state[1];
  out32[2] = state[2];  out32[3] = state[3];
  out32[4] = state[12]; out32[5] = state[13];
  out32[6] = state[14]; out32[7] = state[15];
}

static void ChaCha20Block(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12], uint8_t keystream[64]) {
  uint32_t orig[16] = {
    0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
    ((const uint32_t *)key)[0], ((const uint32_t *)key)[1],
    ((const uint32_t *)key)[2], ((const uint32_t *)key)[3],
    ((const uint32_t *)key)[4], ((const uint32_t *)key)[5],
    ((const uint32_t *)key)[6], ((const uint32_t *)key)[7],
    counter,
    ((const uint32_t *)nonce)[0], ((const uint32_t *)nonce)[1], ((const uint32_t *)nonce)[2]
  };
  uint32_t state[16];
  memcpy(state, orig, sizeof(orig));

  for (int i = 0; i < 10; i++) {
    ChaCha20QuarterRound(&state[0], &state[4], &state[8],  &state[12]);
    ChaCha20QuarterRound(&state[1], &state[5], &state[9],  &state[13]);
    ChaCha20QuarterRound(&state[2], &state[6], &state[10], &state[14]);
    ChaCha20QuarterRound(&state[3], &state[7], &state[11], &state[15]);
    ChaCha20QuarterRound(&state[0], &state[5], &state[10], &state[15]);
    ChaCha20QuarterRound(&state[1], &state[6], &state[11], &state[12]);
    ChaCha20QuarterRound(&state[2], &state[7], &state[8],  &state[13]);
    ChaCha20QuarterRound(&state[3], &state[4], &state[9],  &state[14]);
  }

  for (int i = 0; i < 16; i++) {
    uint32_t res = state[i] + orig[i];
    memcpy(keystream + i * 4, &res, 4);
  }
}

static void ChaCha20Process(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12], const uint8_t *in, size_t len, uint8_t *out) {
  uint8_t block[64];
  size_t offset = 0;
  while (offset < len) {
    ChaCha20Block(key, counter++, nonce, block);
    size_t chunk = (len - offset < 64) ? (len - offset) : 64;
    for (size_t i = 0; i < chunk; i++) {
      out[offset + i] = in[offset + i] ^ block[i];
    }
    offset += chunk;
  }
}

void CryptoPoly1305Compute(const uint8_t key[32], const uint8_t *msg, size_t msg_len, uint8_t tag[16]) {
  uint32_t r0 = ((const uint32_t *)key)[0] & 0x0fffffff;
  uint32_t r1 = ((const uint32_t *)key)[1] & 0x0ffffffc;
  uint32_t r2 = ((const uint32_t *)key)[2] & 0x0ffffffc;
  uint32_t r3 = ((const uint32_t *)key)[3] & 0x0ffffffc;

  uint32_t s0 = ((const uint32_t *)(key + 16))[0];
  uint32_t s1 = ((const uint32_t *)(key + 16))[1];
  uint32_t s2 = ((const uint32_t *)(key + 16))[2];
  uint32_t s3 = ((const uint32_t *)(key + 16))[3];

  uint64_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

  size_t offset = 0;
  while (offset < msg_len) {
    uint8_t block[17] = {0};
    size_t chunk = (msg_len - offset < 16) ? (msg_len - offset) : 16;
    memcpy(block, msg + offset, chunk);
    block[chunk] = 1;

    uint32_t m0 = ((uint32_t *)block)[0];
    uint32_t m1 = ((uint32_t *)block)[1];
    uint32_t m2 = ((uint32_t *)block)[2];
    uint32_t m3 = ((uint32_t *)block)[3];
    uint32_t m4 = block[16];

    h0 += (uint64_t)m0;
    h1 += (uint64_t)m1;
    h2 += (uint64_t)m2;
    h3 += (uint64_t)m3;
    h4 += (uint64_t)m4;

    uint64_t d0 = h0 * r0 + h1 * (r3 * 5 / 4) + h2 * (r2 * 5 / 4) + h3 * (r1 * 5 / 4);
    uint64_t d1 = h0 * r1 + h1 * r0 + h2 * (r3 * 5 / 4) + h3 * (r2 * 5 / 4);
    uint64_t d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * (r3 * 5 / 4);
    uint64_t d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0;
    uint64_t d4 = h4 * r0;

    h0 = d0; h1 = d1; h2 = d2; h3 = d3; h4 = d4;

    offset += chunk;
  }

  uint32_t c0 = (uint32_t)h0 + s0;
  uint32_t c1 = (uint32_t)h1 + s1 + (c0 < s0 ? 1 : 0);
  uint32_t c2 = (uint32_t)h2 + s2 + (c1 < s1 ? 1 : 0);
  uint32_t c3 = (uint32_t)h3 + s3 + (c2 < s2 ? 1 : 0);

  memcpy(tag, &c0, 4);
  memcpy(tag + 4, &c1, 4);
  memcpy(tag + 8, &c2, 4);
  memcpy(tag + 12, &c3, 4);
}

int CryptoXChaCha20Poly1305Encrypt(const uint8_t key[32], const uint8_t nonce[24],
                                   const uint8_t *aad, size_t aad_len,
                                   const uint8_t *in, size_t in_len,
                                   uint8_t *out, uint8_t tag[16]) {
  uint8_t subkey[32];
  CryptoHChaCha20(key, nonce, subkey);

  uint8_t subnonce[12] = {0};
  memcpy(subnonce + 4, nonce + 16, 8);

  uint8_t polykey[64];
  ChaCha20Block(subkey, 0, subnonce, polykey);

  ChaCha20Process(subkey, 1, subnonce, in, in_len, out);

  size_t mac_buf_len = ((aad_len + 15) / 16) * 16 + ((in_len + 15) / 16) * 16 + 16;
  uint8_t *mac_buf = calloc(1, mac_buf_len);
  if (!mac_buf) return 0;

  size_t pos = 0;
  if (aad_len > 0) {
    memcpy(mac_buf + pos, aad, aad_len);
    pos += ((aad_len + 15) / 16) * 16;
  }
  if (in_len > 0) {
    memcpy(mac_buf + pos, out, in_len);
    pos += ((in_len + 15) / 16) * 16;
  }
  uint64_t la = (uint64_t)aad_len;
  uint64_t lc = (uint64_t)in_len;
  memcpy(mac_buf + pos, &la, 8);
  memcpy(mac_buf + pos + 8, &lc, 8);

  CryptoPoly1305Compute(polykey, mac_buf, mac_buf_len, tag);
  free(mac_buf);
  return 1;
}

int CryptoXChaCha20Poly1305Decrypt(const uint8_t key[32], const uint8_t nonce[24],
                                   const uint8_t *aad, size_t aad_len,
                                   const uint8_t *in, size_t in_len,
                                   const uint8_t tag[16], uint8_t *out) {
  uint8_t subkey[32];
  CryptoHChaCha20(key, nonce, subkey);

  uint8_t subnonce[12] = {0};
  memcpy(subnonce + 4, nonce + 16, 8);

  uint8_t polykey[64];
  ChaCha20Block(subkey, 0, subnonce, polykey);

  size_t mac_buf_len = ((aad_len + 15) / 16) * 16 + ((in_len + 15) / 16) * 16 + 16;
  uint8_t *mac_buf = calloc(1, mac_buf_len);
  if (!mac_buf) return 0;

  size_t pos = 0;
  if (aad_len > 0) {
    memcpy(mac_buf + pos, aad, aad_len);
    pos += ((aad_len + 15) / 16) * 16;
  }
  if (in_len > 0) {
    memcpy(mac_buf + pos, in, in_len);
    pos += ((in_len + 15) / 16) * 16;
  }
  uint64_t la = (uint64_t)aad_len;
  uint64_t lc = (uint64_t)in_len;
  memcpy(mac_buf + pos, &la, 8);
  memcpy(mac_buf + pos + 8, &lc, 8);

  uint8_t computed_tag[16];
  CryptoPoly1305Compute(polykey, mac_buf, mac_buf_len, computed_tag);
  free(mac_buf);

  uint8_t diff = 0;
  for (int i = 0; i < 16; i++) diff |= (computed_tag[i] ^ tag[i]);
  if (diff != 0) return 0;

  ChaCha20Process(subkey, 1, subnonce, in, in_len, out);
  return 1;
}

// SHA-512/256 Truncated Cryptographic Hash (FIPS 180-4)

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47867619ULL,
    0x8d283c2190f224a8ULL, 0x8c07b7277a063d82ULL, 0x90befffa23631e28ULL,
    0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL,
    0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL,
    0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL,
    0x6c44198c4a475817ULL
};

#define SHA512_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA512_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA512_EP0(x) (ROTR64(x, 28) ^ ROTR64(x, 34) ^ ROTR64(x, 39))
#define SHA512_EP1(x) (ROTR64(x, 14) ^ ROTR64(x, 18) ^ ROTR64(x, 41))
#define SHA512_SIG0(x) (ROTR64(x, 1) ^ ROTR64(x, 8) ^ ((x) >> 7))
#define SHA512_SIG1(x) (ROTR64(x, 19) ^ ROTR64(x, 61) ^ ((x) >> 6))

void CryptoSha512_256Hash(const uint8_t *in, size_t inlen, uint8_t out[32]) {
  uint64_t h[8] = {
    0x22312194fc2bf72cULL, 0x9f555fa3c84c64c2ULL,
    0x2393b86e637b3971ULL, 0x96283ee2a88effe3ULL,
    0xbe5e1e2553863992ULL, 0x2b0199fc2c85b8aaULL,
    0x0eb72ddc81c52ca2ULL, 0x751e2e1e47da883bULL
  };

  size_t total_blocks = (inlen + 1 + 16 + 127) / 128;
  size_t padded_len = total_blocks * 128;
  uint8_t *padded = calloc(1, padded_len);
  if (!padded) return;

  if (inlen > 0) memcpy(padded, in, inlen);
  padded[inlen] = 0x80;

  uint64_t bits = ((uint64_t)inlen) * 8;
  for (int i = 0; i < 8; i++) {
    padded[padded_len - 1 - i] = (uint8_t)(bits >> (i * 8));
  }

  for (size_t blk = 0; blk < total_blocks; blk++) {
    uint64_t w[80];
    const uint8_t *chunk = padded + blk * 128;
    for (int t = 0; t < 16; t++) {
      w[t] = (((uint64_t)chunk[t * 8]) << 56) |
             (((uint64_t)chunk[t * 8 + 1]) << 48) |
             (((uint64_t)chunk[t * 8 + 2]) << 40) |
             (((uint64_t)chunk[t * 8 + 3]) << 32) |
             (((uint64_t)chunk[t * 8 + 4]) << 24) |
             (((uint64_t)chunk[t * 8 + 5]) << 16) |
             (((uint64_t)chunk[t * 8 + 6]) << 8) |
             (((uint64_t)chunk[t * 8 + 7]));
    }
    for (int t = 16; t < 80; t++) {
      w[t] = SHA512_SIG1(w[t - 2]) + w[t - 7] + SHA512_SIG0(w[t - 15]) + w[t - 16];
    }

    uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint64_t e = h[4], f = h[5], g = h[6], h_val = h[7];

    for (int t = 0; t < 80; t++) {
      uint64_t t1 = h_val + SHA512_EP1(e) + SHA512_CH(e, f, g) + sha512_k[t] + w[t];
      uint64_t t2 = SHA512_EP0(a) + SHA512_MAJ(a, b, c);
      h_val = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;
  }
  free(padded);

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 8; j++) {
      out[i * 8 + j] = (uint8_t)(h[i] >> ((7 - j) * 8));
    }
  }
}

// RFC 5869 HMAC-SHA256 Extract and Expand HKDF Engine

static void CryptoHmacSha256Raw(const uint8_t *key, size_t keylen, const uint8_t *data, size_t datalen, uint8_t out[32]) {
#ifdef _WIN32
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_HASH_HANDLE hHash = NULL;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
    if (BCryptCreateHash(hAlg, &hHash, NULL, 0, (PUCHAR)key, (ULONG)keylen, 0) == 0) {
      BCryptHashData(hHash, (PUCHAR)data, (ULONG)datalen, 0);
      BCryptFinishHash(hHash, out, 32, 0);
      BCryptDestroyHash(hHash);
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);
  }
#endif
}

void CryptoHkdfExtract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t prk[32]) {
  uint8_t zero_salt[32] = {0};
  if (!salt || salt_len == 0) {
    salt = zero_salt;
    salt_len = 32;
  }
  CryptoHmacSha256Raw(salt, salt_len, ikm, ikm_len, prk);
}

int CryptoHkdfExpand(const uint8_t prk[32], const uint8_t *info, size_t info_len, uint8_t *okm, size_t okm_len) {
  if (okm_len > 255 * 32) return 0;
  size_t n = (okm_len + 31) / 32;
  uint8_t t[32] = {0};
  size_t t_len = 0;
  size_t generated = 0;

  for (size_t i = 1; i <= n; i++) {
    size_t input_len = t_len + info_len + 1;
    uint8_t *input = malloc(input_len);
    if (!input) return 0;
    if (t_len > 0) memcpy(input, t, t_len);
    if (info_len > 0) memcpy(input + t_len, info, info_len);
    input[input_len - 1] = (uint8_t)i;

    CryptoHmacSha256Raw(prk, 32, input, input_len, t);
    free(input);
    t_len = 32;

    size_t chunk = (okm_len - generated < 32) ? (okm_len - generated) : 32;
    memcpy(okm + generated, t, chunk);
    generated += chunk;
  }
  return 1;
}

// RFC 4493 AES-128-CMAC Message Authentication Code Engine

static void Aes128EncryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
#ifdef _WIN32
  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_KEY_HANDLE hKey = NULL;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) == 0) {
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    if (BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)key, 16, 0) == 0) {
      ULONG cbResult = 0;
      BCryptEncrypt(hKey, (PUCHAR)in, 16, NULL, NULL, 0, out, 16, &cbResult, 0);
      BCryptDestroyKey(hKey);
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);
  }
#endif
}

static void CmacShiftLeft128(const uint8_t in[16], uint8_t out[16]) {
  uint8_t overflow = 0;
  for (int i = 15; i >= 0; i--) {
    out[i] = (in[i] << 1) | overflow;
    overflow = (in[i] & 0x80) ? 1 : 0;
  }
}

void CryptoAes128Cmac(const uint8_t key[16], const uint8_t *data, size_t len, uint8_t mac[16]) {
  uint8_t L[16] = {0}, K1[16] = {0}, K2[16] = {0}, zeros[16] = {0};
  Aes128EncryptBlock(key, zeros, L);

  CmacShiftLeft128(L, K1);
  if (L[0] & 0x80) K1[15] ^= 0x87;

  CmacShiftLeft128(K1, K2);
  if (K1[0] & 0x80) K2[15] ^= 0x87;

  size_t n = (len + 15) / 16;
  int is_complete = 1;
  if (n == 0) {
    n = 1;
    is_complete = 0;
  } else {
    is_complete = (len % 16 == 0);
  }

  uint8_t X[16] = {0}, Y[16] = {0}, M_last[16] = {0};
  if (is_complete) {
    for (int i = 0; i < 16; i++) M_last[i] = data[(n - 1) * 16 + i] ^ K1[i];
  } else {
    size_t rem = len % 16;
    if (len > 0 && rem > 0) memcpy(M_last, data + (n - 1) * 16, rem);
    M_last[rem] = 0x80;
    for (int i = 0; i < 16; i++) M_last[i] ^= K2[i];
  }

  for (size_t i = 0; i + 1 < n; i++) {
    for (int j = 0; j < 16; j++) Y[j] = X[j] ^ data[i * 16 + j];
    Aes128EncryptBlock(key, Y, X);
  }
  for (int j = 0; j < 16; j++) Y[j] = X[j] ^ M_last[j];
  Aes128EncryptBlock(key, Y, mac);
}


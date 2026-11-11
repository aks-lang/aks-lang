// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "vm_stdlib.h"


int ExecuteStdlibMathSimd(VM *vm, VMTask *task, const char *name, int argc,
            AKSValue **stacktopptr) {
  AKSValue *stacktop = *stacktopptr;
  AKSValue *stacklimit = task->stack + (task->stackcap > 0 ? task->stackcap : MAXSTACK);
#define PUSH(val) AKS_STDLIB_PUSH(val)
#define POP() AKS_STDLIB_POP()

  if (strcmp(name, "SimdVec8fNew") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SimdVec8fNew() expects 1 argument (array8)\n");
      return -1;
    }
    AKSValue arr_val = POP();
    typedef struct {
      float v[8];
    } AKSSimdVec8f;
    AKSSimdVec8f *sv = (AKSSimdVec8f *)malloc(sizeof(AKSSimdVec8f));
    for (int i = 0; i < 8; i++) {
      sv->v[i] = 0.0f;
    }
    if (ValKind(arr_val) == VALARR) {
      AKSArray *a = ValToArr(arr_val);
      int n = a->count < 8 ? a->count : 8;
      for (int i = 0; i < n; i++) {
        AKSValue el = a->items[i];
        double dv = (ValKind(el) == VALINT)
                        ? (double)ValToInt(el)
                        : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
        sv->v[i] = (float)dv;
      }
    }
    ValFree(&arr_val);
    PUSH(ValInt((int64_t)(uintptr_t)sv));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec8fAdd") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec8fAdd() expects 2 arguments\n");
      return -1;
    }
    AKSValue v2_val = POP();
    AKSValue v1_val = POP();
    typedef struct {
      float v[8];
    } AKSSimdVec8f;
    AKSSimdVec8f *p1 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v1_val);
    AKSSimdVec8f *p2 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v2_val);
    AKSSimdVec8f *res = (AKSSimdVec8f *)malloc(sizeof(AKSSimdVec8f));
#if defined(__AVX2__) || defined(_M_X64)
    __m256 a = _mm256_loadu_ps(p1->v);
    __m256 b = _mm256_loadu_ps(p2->v);
    __m256 c = _mm256_add_ps(a, b);
    _mm256_storeu_ps(res->v, c);
#else
    for (int i = 0; i < 8; i++)
      res->v[i] = p1->v[i] + p2->v[i];
#endif
    ValFree(&v2_val);
    ValFree(&v1_val);
    PUSH(ValInt((int64_t)(uintptr_t)res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec8fMul") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdVec8fMul() expects 2 arguments\n");
      return -1;
    }
    AKSValue v2_val = POP();
    AKSValue v1_val = POP();
    typedef struct {
      float v[8];
    } AKSSimdVec8f;
    AKSSimdVec8f *p1 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v1_val);
    AKSSimdVec8f *p2 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v2_val);
    AKSSimdVec8f *res = (AKSSimdVec8f *)malloc(sizeof(AKSSimdVec8f));
#if defined(__AVX2__) || defined(_M_X64)
    __m256 a = _mm256_loadu_ps(p1->v);
    __m256 b = _mm256_loadu_ps(p2->v);
    __m256 c = _mm256_mul_ps(a, b);
    _mm256_storeu_ps(res->v, c);
#else
    for (int i = 0; i < 8; i++)
      res->v[i] = p1->v[i] * p2->v[i];
#endif
    ValFree(&v2_val);
    ValFree(&v1_val);
    PUSH(ValInt((int64_t)(uintptr_t)res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec8fFmadd") == 0) {
    if (argc != 3) {
      fprintf(
          stderr,
          "Runtime error: SimdVec8fFmadd() expects 3 arguments (a, b, c)\n");
      return -1;
    }
    AKSValue v3_val = POP();
    AKSValue v2_val = POP();
    AKSValue v1_val = POP();
    typedef struct {
      float v[8];
    } AKSSimdVec8f;
    AKSSimdVec8f *p1 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v1_val);
    AKSSimdVec8f *p2 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v2_val);
    AKSSimdVec8f *p3 = (AKSSimdVec8f *)(uintptr_t)ValToInt(v3_val);
    AKSSimdVec8f *res = (AKSSimdVec8f *)malloc(sizeof(AKSSimdVec8f));
#if defined(__AVX2__) || defined(_M_X64)
    __m256 a = _mm256_loadu_ps(p1->v);
    __m256 b = _mm256_loadu_ps(p2->v);
    __m256 c = _mm256_loadu_ps(p3->v);
#if defined(_M_X64) || defined(__FMA__)
    __m256 r = _mm256_fmadd_ps(a, b, c);
#else
    __m256 r = _mm256_add_ps(_mm256_mul_ps(a, b), c);
#endif
    _mm256_storeu_ps(res->v, r);
#else
    for (int i = 0; i < 8; i++)
      res->v[i] = p1->v[i] * p2->v[i] + p3->v[i];
#endif
    ValFree(&v3_val);
    ValFree(&v2_val);
    ValFree(&v1_val);
    PUSH(ValInt((int64_t)(uintptr_t)res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec8fSum") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fSum() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    typedef struct {
      float v[8];
    } AKSSimdVec8f;
    AKSSimdVec8f *p = (AKSSimdVec8f *)(uintptr_t)ValToInt(v_val);
#if defined(__AVX2__) || defined(_M_X64)
    __m256 y = _mm256_loadu_ps(p->v);
    __m128 lo = _mm256_castps256_ps128(y);
    __m128 hi = _mm256_extractf128_ps(y, 1);
    __m128 s = _mm_add_ps(lo, hi);
    s = _mm_hadd_ps(s, s);
    s = _mm_hadd_ps(s, s);
    float total = _mm_cvtss_f32(s);
#else
    float total = 0.0f;
    for (int i = 0; i < 8; i++)
      total += p->v[i];
#endif
    ValFree(&v_val);
    PUSH(ValFloat((double)total));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec8fMinElement") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SimdVec8fMinElement() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    typedef struct {
      float v[8];
    } AKSSimdVec8f;
    AKSSimdVec8f *p = (AKSSimdVec8f *)(uintptr_t)ValToInt(v_val);
    float min_val = p->v[0];
    for (int i = 1; i < 8; i++) {
      if (p->v[i] < min_val)
        min_val = p->v[i];
    }
    ValFree(&v_val);
    PUSH(ValFloat((double)min_val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec8fMaxElement") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SimdVec8fMaxElement() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    typedef struct {
      float v[8];
    } AKSSimdVec8f;
    AKSSimdVec8f *p = (AKSSimdVec8f *)(uintptr_t)ValToInt(v_val);
    float max_val = p->v[0];
    for (int i = 1; i < 8; i++) {
      if (p->v[i] > max_val)
        max_val = p->v[i];
    }
    ValFree(&v_val);
    PUSH(ValFloat((double)max_val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec8fNorm") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fNorm() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    typedef struct {
      float v[8];
    } AKSSimdVec8f;
    AKSSimdVec8f *p = (AKSSimdVec8f *)(uintptr_t)ValToInt(v_val);
    float sum_sq = 0.0f;
    for (int i = 0; i < 8; i++) {
      sum_sq += p->v[i] * p->v[i];
    }
    float norm_val = sqrtf(sum_sq);
    ValFree(&v_val);
    PUSH(ValFloat((double)norm_val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec8fToArray") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fToArray() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    typedef struct {
      float v[8];
    } AKSSimdVec8f;
    AKSSimdVec8f *p = (AKSSimdVec8f *)(uintptr_t)ValToInt(v_val);
    AKSValue res_arr = ValArr(8);
    AKSArray *arr = ValToArr(res_arr);
    for (int i = 0; i < 8; i++) {
      arr->items[arr->count++] = ValFloat((double)p->v[i]);
    }
    ValFree(&v_val);
    PUSH(res_arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec8fFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdVec8fFree() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    void *p = (void *)(uintptr_t)ValToInt(v_val);
    if (p)
      free(p);
    ValFree(&v_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitPopCount") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: BitPopCount() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    int64_t v = ValToInt(v_val);
    int64_t count = 0;
#ifdef _WIN32
    count = (int64_t)__popcnt64((uint64_t)v);
#else
    count = (int64_t)__builtin_popcountll((unsigned long long)v);
#endif
    ValFree(&v_val);
    PUSH(ValInt(count));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitClz") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: BitClz() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    uint64_t v = (uint64_t)ValToInt(v_val);
    int64_t clz = 64;
#ifdef _WIN32
    unsigned long idx = 0;
    if (_BitScanReverse64(&idx, v)) {
      clz = 63 - (int64_t)idx;
    }
#else
    if (v != 0)
      clz = (int64_t)__builtin_clzll((unsigned long long)v);
#endif
    ValFree(&v_val);
    PUSH(ValInt(clz));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitCtz") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: BitCtz() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    uint64_t v = (uint64_t)ValToInt(v_val);
    int64_t ctz = 64;
#ifdef _WIN32
    unsigned long idx = 0;
    if (_BitScanForward64(&idx, v)) {
      ctz = (int64_t)idx;
    }
#else
    if (v != 0)
      ctz = (int64_t)__builtin_ctzll((unsigned long long)v);
#endif
    ValFree(&v_val);
    PUSH(ValInt(ctz));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitRotl") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: BitRotl() expects 2 arguments (val, shift)\n");
      return -1;
    }
    AKSValue s_val = POP();
    AKSValue v_val = POP();
    uint64_t v = (uint64_t)ValToInt(v_val);
    int shift = (int)ValToInt(s_val);
    uint64_t res = 0;
#ifdef _WIN32
    res = _rotl64(v, shift);
#else
    shift = shift & 63;
    res = (v << shift) | (v >> (64 - shift));
#endif
    ValFree(&s_val);
    ValFree(&v_val);
    PUSH(ValInt((int64_t)res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitRotr") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: BitRotr() expects 2 arguments (val, shift)\n");
      return -1;
    }
    AKSValue s_val = POP();
    AKSValue v_val = POP();
    uint64_t v = (uint64_t)ValToInt(v_val);
    int shift = (int)ValToInt(s_val);
    uint64_t res = 0;
#ifdef _WIN32
    res = _rotr64(v, shift);
#else
    shift = shift & 63;
    res = (v >> shift) | (v << (64 - shift));
#endif
    ValFree(&s_val);
    ValFree(&v_val);
    PUSH(ValInt((int64_t)res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BitByteSwap") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: BitByteSwap() expects 1 argument\n");
      return -1;
    }
    AKSValue v_val = POP();
    uint32_t v = (uint32_t)ValToInt(v_val);
    uint32_t res = 0;
#ifdef _WIN32
    res = _byteswap_ulong(v);
#else
    res = __builtin_bswap32(v);
#endif
    ValFree(&v_val);
    PUSH(ValInt((int64_t)res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMathSqrtVec8f") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FastMathSqrtVec8f() expects 1 argument (arr8)\n");
      return -1;
    }
    AKSValue arr_val = POP();
    AKSValue res = ValNull();
    if (ValKind(arr_val) == VALARR) {
      AKSArray *arr = ValToArr(arr_val);
      if (arr && arr->count >= 8) {
        float inFloats[8];
        for (int i = 0; i < 8; i++) {
          AKSValue el = arr->items[i];
          double dv = (ValKind(el) == VALINT)
                          ? (double)ValToInt(el)
                          : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
          inFloats[i] = (float)dv;
        }
        float outFloats[8];
#if defined(__AVX2__) || defined(_M_X64)
        __m256 v = _mm256_loadu_ps(inFloats);
        __m256 r = _mm256_sqrt_ps(v);
        _mm256_storeu_ps(outFloats, r);
#else
        for (int i = 0; i < 8; i++)
          outFloats[i] = (inFloats[i] >= 0.0f) ? sqrtf(inFloats[i]) : 0.0f;
#endif
        res = ValArr(8);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 8; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)outFloats[i]);
        }
      }
    }
    ValFree(&arr_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMathRsqrtVec8f") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: FastMathRsqrtVec8f() expects 1 argument (arr8)\n");
      return -1;
    }
    AKSValue arr_val = POP();
    AKSValue res = ValNull();
    if (ValKind(arr_val) == VALARR) {
      AKSArray *arr = ValToArr(arr_val);
      if (arr && arr->count >= 8) {
        float inFloats[8];
        for (int i = 0; i < 8; i++) {
          AKSValue el = arr->items[i];
          double dv = (ValKind(el) == VALINT)
                          ? (double)ValToInt(el)
                          : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
          inFloats[i] = (float)dv;
        }
        float outFloats[8];
#if defined(__AVX2__) || defined(_M_X64)
        __m256 v = _mm256_loadu_ps(inFloats);
        __m256 r = _mm256_rsqrt_ps(v);
        _mm256_storeu_ps(outFloats, r);
#else
        for (int i = 0; i < 8; i++)
          outFloats[i] = (inFloats[i] > 0.0f) ? (1.0f / sqrtf(inFloats[i])) : 0.0f;
#endif
        res = ValArr(8);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 8; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)outFloats[i]);
        }
      }
    }
    ValFree(&arr_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMathMinVec8f") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMathMinVec8f() expects 2 arguments "
                      "(arr_a, arr_b)\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue a_val = POP();
    AKSValue res = ValNull();
    if (ValKind(a_val) == VALARR && ValKind(b_val) == VALARR) {
      AKSArray *arrA = ValToArr(a_val);
      AKSArray *arrB = ValToArr(b_val);
      if (arrA && arrB && arrA->count >= 8 && arrB->count >= 8) {
        float aFloats[8], bFloats[8];
        for (int i = 0; i < 8; i++) {
          AKSValue elA = arrA->items[i];
          AKSValue elB = arrB->items[i];
          double dvA =
              (ValKind(elA) == VALINT)
                  ? (double)ValToInt(elA)
                  : ((ValKind(elA) == VALFLOAT) ? ValToFloat(elA) : 0.0);
          double dvB =
              (ValKind(elB) == VALINT)
                  ? (double)ValToInt(elB)
                  : ((ValKind(elB) == VALFLOAT) ? ValToFloat(elB) : 0.0);
          aFloats[i] = (float)dvA;
          bFloats[i] = (float)dvB;
        }
        float outFloats[8];
#if defined(__AVX2__) || defined(_M_X64)
        __m256 va = _mm256_loadu_ps(aFloats);
        __m256 vb = _mm256_loadu_ps(bFloats);
        __m256 vr = _mm256_min_ps(va, vb);
        _mm256_storeu_ps(outFloats, vr);
#else
        for (int i = 0; i < 8; i++)
          outFloats[i] = (aFloats[i] < bFloats[i]) ? aFloats[i] : bFloats[i];
#endif
        res = ValArr(8);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 8; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)outFloats[i]);
        }
      }
    }
    ValFree(&b_val);
    ValFree(&a_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMathMaxVec8f") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMathMaxVec8f() expects 2 arguments "
                      "(arr_a, arr_b)\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue a_val = POP();
    AKSValue res = ValNull();
    if (ValKind(a_val) == VALARR && ValKind(b_val) == VALARR) {
      AKSArray *arrA = ValToArr(a_val);
      AKSArray *arrB = ValToArr(b_val);
      if (arrA && arrB && arrA->count >= 8 && arrB->count >= 8) {
        float aFloats[8], bFloats[8];
        for (int i = 0; i < 8; i++) {
          AKSValue elA = arrA->items[i];
          AKSValue elB = arrB->items[i];
          double dvA =
              (ValKind(elA) == VALINT)
                  ? (double)ValToInt(elA)
                  : ((ValKind(elA) == VALFLOAT) ? ValToFloat(elA) : 0.0);
          double dvB =
              (ValKind(elB) == VALINT)
                  ? (double)ValToInt(elB)
                  : ((ValKind(elB) == VALFLOAT) ? ValToFloat(elB) : 0.0);
          aFloats[i] = (float)dvA;
          bFloats[i] = (float)dvB;
        }
        float outFloats[8];
#if defined(__AVX2__) || defined(_M_X64)
        __m256 va = _mm256_loadu_ps(aFloats);
        __m256 vb = _mm256_loadu_ps(bFloats);
        __m256 vr = _mm256_max_ps(va, vb);
        _mm256_storeu_ps(outFloats, vr);
#else
        for (int i = 0; i < 8; i++)
          outFloats[i] = (aFloats[i] > bFloats[i]) ? aFloats[i] : bFloats[i];
#endif
        res = ValArr(8);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 8; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)outFloats[i]);
        }
      }
    }
    ValFree(&b_val);
    ValFree(&a_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMatrixIdentity") == 0) {
    AKSValue res = ValArr(16);
    AKSArray *arr = ValToArr(res);
    for (int i = 0; i < 16; i++) {
      int r = i / 4;
      int c = i % 4;
      arr->items[arr->count++] = ValFloat((r == c) ? 1.0 : 0.0);
    }
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMatrixMultiply") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMatrixMultiply() expects 2 arguments "
                      "(mat4_a, mat4_b)\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue a_val = POP();
    AKSValue res = ValNull();
    if (ValKind(a_val) == VALARR && ValKind(b_val) == VALARR) {
      AKSArray *arrA = ValToArr(a_val);
      AKSArray *arrB = ValToArr(b_val);
      if (arrA && arrB && arrA->count >= 16 && arrB->count >= 16) {
        float a[16], b[16], c[16];
        for (int i = 0; i < 16; i++) {
          AKSValue elA = arrA->items[i];
          AKSValue elB = arrB->items[i];
          double dvA =
              (ValKind(elA) == VALINT)
                  ? (double)ValToInt(elA)
                  : ((ValKind(elA) == VALFLOAT) ? ValToFloat(elA) : 0.0);
          double dvB =
              (ValKind(elB) == VALINT)
                  ? (double)ValToInt(elB)
                  : ((ValKind(elB) == VALFLOAT) ? ValToFloat(elB) : 0.0);
          a[i] = (float)dvA;
          b[i] = (float)dvB;
        }
        for (int i = 0; i < 4; i++) {
          for (int j = 0; j < 4; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
              sum += a[i * 4 + k] * b[k * 4 + j];
            }
            c[i * 4 + j] = sum;
          }
        }
        res = ValArr(16);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 16; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)c[i]);
        }
      }
    }
    ValFree(&b_val);
    ValFree(&a_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMatrixTransformVec4") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastMatrixTransformVec4() expects 2 "
                      "arguments (mat4, vec4)\n");
      return -1;
    }
    AKSValue v_val = POP();
    AKSValue m_val = POP();
    AKSValue res = ValNull();
    if (ValKind(m_val) == VALARR && ValKind(v_val) == VALARR) {
      AKSArray *arrM = ValToArr(m_val);
      AKSArray *arrV = ValToArr(v_val);
      if (arrM && arrV && arrM->count >= 16 && arrV->count >= 4) {
        float m[16], v[4], out[4];
        for (int i = 0; i < 16; i++) {
          AKSValue el = arrM->items[i];
          double dv = (ValKind(el) == VALINT)
                          ? (double)ValToInt(el)
                          : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
          m[i] = (float)dv;
        }
        for (int i = 0; i < 4; i++) {
          AKSValue el = arrV->items[i];
          double dv = (ValKind(el) == VALINT)
                          ? (double)ValToInt(el)
                          : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
          v[i] = (float)dv;
        }
        for (int i = 0; i < 4; i++) {
          out[i] = m[i * 4 + 0] * v[0] + m[i * 4 + 1] * v[1] +
                   m[i * 4 + 2] * v[2] + m[i * 4 + 3] * v[3];
        }
        res = ValArr(4);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 4; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)out[i]);
        }
      }
    }
    ValFree(&v_val);
    ValFree(&m_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMatrixTranspose") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: FastMatrixTranspose() expects 1 argument (mat4)\n");
      return -1;
    }
    AKSValue m_val = POP();
    AKSValue res = ValNull();
    if (ValKind(m_val) == VALARR) {
      AKSArray *arrM = ValToArr(m_val);
      if (arrM && arrM->count >= 16) {
        float m[16], t[16];
        for (int i = 0; i < 16; i++) {
          AKSValue el = arrM->items[i];
          double dv = (ValKind(el) == VALINT)
                          ? (double)ValToInt(el)
                          : ((ValKind(el) == VALFLOAT) ? ValToFloat(el) : 0.0);
          m[i] = (float)dv;
        }
        for (int i = 0; i < 4; i++) {
          for (int j = 0; j < 4; j++) {
            t[j * 4 + i] = m[i * 4 + j];
          }
        }
        res = ValArr(16);
        AKSArray *res_arr = ValToArr(res);
        for (int i = 0; i < 16; i++) {
          res_arr->items[res_arr->count++] = ValFloat((double)t[i]);
        }
      }
    }
    ValFree(&m_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastGeometryDot3") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastGeometryDot3() expects 2 arguments "
                      "(vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    double DotResult = 0.0;
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      if (ArrA->count >= 3 && ArrB->count >= 3) {
        double A0 = (ValKind(ArrA->items[0]) == VALINT)
                        ? (double)ValToInt(ArrA->items[0])
                        : ValToFloat(ArrA->items[0]);
        double A1 = (ValKind(ArrA->items[1]) == VALINT)
                        ? (double)ValToInt(ArrA->items[1])
                        : ValToFloat(ArrA->items[1]);
        double A2 = (ValKind(ArrA->items[2]) == VALINT)
                        ? (double)ValToInt(ArrA->items[2])
                        : ValToFloat(ArrA->items[2]);
        double B0 = (ValKind(ArrB->items[0]) == VALINT)
                        ? (double)ValToInt(ArrB->items[0])
                        : ValToFloat(ArrB->items[0]);
        double B1 = (ValKind(ArrB->items[1]) == VALINT)
                        ? (double)ValToInt(ArrB->items[1])
                        : ValToFloat(ArrB->items[1]);
        double B2 = (ValKind(ArrB->items[2]) == VALINT)
                        ? (double)ValToInt(ArrB->items[2])
                        : ValToFloat(ArrB->items[2]);
        DotResult = A0 * B0 + A1 * B1 + A2 * B2;
      }
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ValFloat(DotResult));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastGeometryCross3") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastGeometryCross3() expects 2 arguments "
                      "(vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(3);
    AKSArray *ResArr = ValToArr(ResArrVal);
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      if (ArrA->count >= 3 && ArrB->count >= 3) {
        double A0 = (ValKind(ArrA->items[0]) == VALINT)
                        ? (double)ValToInt(ArrA->items[0])
                        : ValToFloat(ArrA->items[0]);
        double A1 = (ValKind(ArrA->items[1]) == VALINT)
                        ? (double)ValToInt(ArrA->items[1])
                        : ValToFloat(ArrA->items[1]);
        double A2 = (ValKind(ArrA->items[2]) == VALINT)
                        ? (double)ValToInt(ArrA->items[2])
                        : ValToFloat(ArrA->items[2]);
        double B0 = (ValKind(ArrB->items[0]) == VALINT)
                        ? (double)ValToInt(ArrB->items[0])
                        : ValToFloat(ArrB->items[0]);
        double B1 = (ValKind(ArrB->items[1]) == VALINT)
                        ? (double)ValToInt(ArrB->items[1])
                        : ValToFloat(ArrB->items[1]);
        double B2 = (ValKind(ArrB->items[2]) == VALINT)
                        ? (double)ValToInt(ArrB->items[2])
                        : ValToFloat(ArrB->items[2]);
        ResArr->items[ResArr->count++] = ValFloat(A1 * B2 - A2 * B1);
        ResArr->items[ResArr->count++] = ValFloat(A2 * B0 - A0 * B2);
        ResArr->items[ResArr->count++] = ValFloat(A0 * B1 - A1 * B0);
      }
    }
    while (ResArr->count < 3) {
      ResArr->items[ResArr->count++] = ValFloat(0.0);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastGeometryNormalize3") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: FastGeometryNormalize3() expects 1 argument (vec)\n");
      return -1;
    }
    AKSValue VecVal = POP();
    AKSValue ResArrVal = ValArr(3);
    AKSArray *ResArr = ValToArr(ResArrVal);
    if (ValKind(VecVal) == VALARR) {
      AKSArray *SrcArr = ValToArr(VecVal);
      if (SrcArr->count >= 3) {
        double V0 = (ValKind(SrcArr->items[0]) == VALINT)
                        ? (double)ValToInt(SrcArr->items[0])
                        : ValToFloat(SrcArr->items[0]);
        double V1 = (ValKind(SrcArr->items[1]) == VALINT)
                        ? (double)ValToInt(SrcArr->items[1])
                        : ValToFloat(SrcArr->items[1]);
        double V2 = (ValKind(SrcArr->items[2]) == VALINT)
                        ? (double)ValToInt(SrcArr->items[2])
                        : ValToFloat(SrcArr->items[2]);
        double Magnitude = sqrt(V0 * V0 + V1 * V1 + V2 * V2);
        double InvMag = (Magnitude > 1e-12) ? (1.0 / Magnitude) : 0.0;
        ResArr->items[ResArr->count++] = ValFloat(V0 * InvMag);
        ResArr->items[ResArr->count++] = ValFloat(V1 * InvMag);
        ResArr->items[ResArr->count++] = ValFloat(V2 * InvMag);
      }
    }
    while (ResArr->count < 3) {
      ResArr->items[ResArr->count++] = ValFloat(0.0);
    }
    ValFree(&VecVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastGeometryDistance3") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastGeometryDistance3() expects 2 "
                      "arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    double DistResult = 0.0;
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      if (ArrA->count >= 3 && ArrB->count >= 3) {
        double A0 = (ValKind(ArrA->items[0]) == VALINT)
                        ? (double)ValToInt(ArrA->items[0])
                        : ValToFloat(ArrA->items[0]);
        double A1 = (ValKind(ArrA->items[1]) == VALINT)
                        ? (double)ValToInt(ArrA->items[1])
                        : ValToFloat(ArrA->items[1]);
        double A2 = (ValKind(ArrA->items[2]) == VALINT)
                        ? (double)ValToInt(ArrA->items[2])
                        : ValToFloat(ArrA->items[2]);
        double B0 = (ValKind(ArrB->items[0]) == VALINT)
                        ? (double)ValToInt(ArrB->items[0])
                        : ValToFloat(ArrB->items[0]);
        double B1 = (ValKind(ArrB->items[1]) == VALINT)
                        ? (double)ValToInt(ArrB->items[1])
                        : ValToFloat(ArrB->items[1]);
        double B2 = (ValKind(ArrB->items[2]) == VALINT)
                        ? (double)ValToInt(ArrB->items[2])
                        : ValToFloat(ArrB->items[2]);
        double D0 = A0 - B0;
        double D1 = A1 - B1;
        double D2 = A2 - B2;
        DistResult = sqrt(D0 * D0 + D1 * D1 + D2 * D2);
      }
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ValFloat(DistResult));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strncmp(name, "SimdVec8i", 9) == 0 &&
      (strcmp(name + 9, "Add") == 0 || strcmp(name + 9, "Sub") == 0 ||
       strcmp(name + 9, "Mul") == 0 || strcmp(name + 9, "BitwiseAnd") == 0 ||
       strcmp(name + 9, "BitwiseOr") == 0 ||
       strcmp(name + 9, "BitwiseXor") == 0)) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s() expects 2 arguments (vec_a, vec_b)\n",
              name);
      return -1;
    }
    const char *suf = name + 9;
    int vec8i_op = 0;
    if (strcmp(suf, "Sub") == 0)
      vec8i_op = 1;
    else if (strcmp(suf, "Mul") == 0)
      vec8i_op = 2;
    else if (strcmp(suf, "BitwiseAnd") == 0)
      vec8i_op = 3;
    else if (strcmp(suf, "BitwiseOr") == 0)
      vec8i_op = 4;
    else if (strcmp(suf, "BitwiseXor") == 0)
      vec8i_op = 5;

    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(8);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int32_t DataA[8] = {0}, DataB[8] = {0}, DataRes[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++)
        DataA[i] = (int32_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++)
        DataB[i] = (int32_t)ValToInt(ArrB->items[i]);
#if defined(_WIN32) || defined(__AVX2__)
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = Va;
      switch (vec8i_op) {
      case 0: Vres = _mm256_add_epi32(Va, Vb); break;
      case 1: Vres = _mm256_sub_epi32(Va, Vb); break;
      case 2: Vres = _mm256_mullo_epi32(Va, Vb); break;
      case 3: Vres = _mm256_and_si256(Va, Vb); break;
      case 4: Vres = _mm256_or_si256(Va, Vb); break;
      case 5: Vres = _mm256_xor_si256(Va, Vb); break;
      }
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#else
      for (int i = 0; i < 8; i++) {
        switch (vec8i_op) {
        case 0: DataRes[i] = (int32_t)((uint32_t)DataA[i] + (uint32_t)DataB[i]); break;
        case 1: DataRes[i] = (int32_t)((uint32_t)DataA[i] - (uint32_t)DataB[i]); break;
        case 2: DataRes[i] = (int32_t)((uint32_t)DataA[i] * (uint32_t)DataB[i]); break;
        case 3: DataRes[i] = DataA[i] & DataB[i]; break;
        case 4: DataRes[i] = DataA[i] | DataB[i]; break;
        case 5: DataRes[i] = DataA[i] ^ DataB[i]; break;
        }
      }
#endif
      for (int i = 0; i < 8; i++)
        ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }
  int vec4d_op = -1;
  if (strcmp(name, "SimdVec4dAdd") == 0) vec4d_op = 0;
  else if (strcmp(name, "SimdVec4dSub") == 0) vec4d_op = 1;
  else if (strcmp(name, "SimdVec4dMul") == 0) vec4d_op = 2;
  else if (strcmp(name, "SimdVec4dDiv") == 0) vec4d_op = 3;
  if (vec4d_op >= 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s() expects 2 arguments (vec_a, vec_b)\n",
              name);
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(4);
    AKSArray *ResArr = ValToArr(ResArrVal);
    double DataA[4] = {0}, DataB[4] = {0}, DataRes[4] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 4 && i < ArrA->count; i++)
        DataA[i] = ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 4 && i < ArrB->count; i++)
        DataB[i] = ValToFloat(ArrB->items[i]);
#if defined(__AVX2__) || defined(_M_X64)
      __m256d Va = _mm256_loadu_pd(DataA);
      __m256d Vb = _mm256_loadu_pd(DataB);
      __m256d Vres = Va;
      switch (vec4d_op) {
      case 0: Vres = _mm256_add_pd(Va, Vb); break;
      case 1: Vres = _mm256_sub_pd(Va, Vb); break;
      case 2: Vres = _mm256_mul_pd(Va, Vb); break;
      case 3: Vres = _mm256_div_pd(Va, Vb); break;
      }
      _mm256_storeu_pd(DataRes, Vres);
#else
      for (int i = 0; i < 4; i++) {
        switch (vec4d_op) {
        case 0: DataRes[i] = DataA[i] + DataB[i]; break;
        case 1: DataRes[i] = DataA[i] - DataB[i]; break;
        case 2: DataRes[i] = DataA[i] * DataB[i]; break;
        case 3: DataRes[i] = (DataB[i] == 0.0) ? 0.0 : (DataA[i] / DataB[i]); break;
        }
      }
#endif
      for (int i = 0; i < 4; i++)
        ResArr->items[ResArr->count++] = ValFloat(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec4dSqrt") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SimdVec4dSqrt() expects 1 argument (vec)\n");
      return -1;
    }
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(4);
    AKSArray *ResArr = ValToArr(ResArrVal);
    double DataA[4] = {0}, DataRes[4] = {0};
    if (ValKind(AVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      for (int i = 0; i < 4 && i < ArrA->count; i++)
        DataA[i] = ValToFloat(ArrA->items[i]);
#if defined(__AVX2__) || defined(_M_X64)
      __m256d Va = _mm256_loadu_pd(DataA);
      __m256d Vres = _mm256_sqrt_pd(Va);
      _mm256_storeu_pd(DataRes, Vres);
#else
      for (int i = 0; i < 4; i++)
        DataRes[i] = (DataA[i] >= 0.0) ? sqrt(DataA[i]) : 0.0;
#endif
      for (int i = 0; i < 4; i++)
        ResArr->items[ResArr->count++] = ValFloat(DataRes[i]);
    }
    ValFree(&AVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec4dDot") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: SimdVec4dDot() expects 2 arguments (vec_a, vec_b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    double DotProduct = 0.0;
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 4 && i < ArrA->count && i < ArrB->count; i++) {
        DotProduct += ValToFloat(ArrA->items[i]) * ValToFloat(ArrB->items[i]);
      }
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ValFloat(DotProduct));
    *stacktopptr = stacktop;
    return 1;
  }
  int vec16s_op = -1;
  if (strcmp(name, "SimdVec16sAdd") == 0) vec16s_op = 0;
  else if (strcmp(name, "SimdVec16sSub") == 0) vec16s_op = 1;
  else if (strcmp(name, "SimdVec16sMul") == 0) vec16s_op = 2;
  else if (strcmp(name, "SimdVec16sAddSaturated") == 0) vec16s_op = 3;
  else if (strcmp(name, "SimdVec16sSubSaturated") == 0) vec16s_op = 4;
  if (vec16s_op >= 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s() expects 2 arguments (vec_a, vec_b)\n",
              name);
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(16);
    AKSArray *ResArr = ValToArr(ResArrVal);
    int16_t DataA[16] = {0}, DataB[16] = {0}, DataRes[16] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 16 && i < ArrA->count; i++)
        DataA[i] = (int16_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 16 && i < ArrB->count; i++)
        DataB[i] = (int16_t)ValToInt(ArrB->items[i]);
#if defined(__AVX2__) || defined(_M_X64)
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = Va;
      switch (vec16s_op) {
      case 0: Vres = _mm256_add_epi16(Va, Vb); break;
      case 1: Vres = _mm256_sub_epi16(Va, Vb); break;
      case 2: Vres = _mm256_mullo_epi16(Va, Vb); break;
      case 3: Vres = _mm256_adds_epi16(Va, Vb); break;
      case 4: Vres = _mm256_subs_epi16(Va, Vb); break;
      }
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#else
      for (int i = 0; i < 16; i++) {
        int32_t s = 0;
        switch (vec16s_op) {
        case 0: DataRes[i] = (int16_t)(DataA[i] + DataB[i]); break;
        case 1: DataRes[i] = (int16_t)(DataA[i] - DataB[i]); break;
        case 2: DataRes[i] = (int16_t)(DataA[i] * DataB[i]); break;
        case 3:
          s = (int32_t)DataA[i] + (int32_t)DataB[i];
          DataRes[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
          break;
        case 4:
          s = (int32_t)DataA[i] - (int32_t)DataB[i];
          DataRes[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
          break;
        }
      }
#endif
      for (int i = 0; i < 16; i++)
        ResArr->items[ResArr->count++] = ValInt(DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }
  int vec32u8_op = -1;
  if (strcmp(name, "SimdVec32u8AddSaturated") == 0) vec32u8_op = 0;
  else if (strcmp(name, "SimdVec32u8SubSaturated") == 0) vec32u8_op = 1;
  else if (strcmp(name, "SimdVec32u8Average") == 0) vec32u8_op = 2;
  else if (strcmp(name, "SimdVec32u8Min") == 0) vec32u8_op = 3;
  else if (strcmp(name, "SimdVec32u8Max") == 0) vec32u8_op = 4;
  if (vec32u8_op >= 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s() expects 2 arguments (vec_a, vec_b)\n",
              name);
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    AKSValue ResArrVal = ValArr(32);
    AKSArray *ResArr = ValToArr(ResArrVal);
    uint8_t DataA[32] = {0}, DataB[32] = {0}, DataRes[32] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 32 && i < ArrA->count; i++)
        DataA[i] = (uint8_t)ValToInt(ArrA->items[i]);
      for (int i = 0; i < 32 && i < ArrB->count; i++)
        DataB[i] = (uint8_t)ValToInt(ArrB->items[i]);
#if defined(__AVX2__) || defined(_M_X64)
      __m256i Va = _mm256_loadu_si256((const __m256i *)DataA);
      __m256i Vb = _mm256_loadu_si256((const __m256i *)DataB);
      __m256i Vres = Va;
      switch (vec32u8_op) {
      case 0: Vres = _mm256_adds_epu8(Va, Vb); break;
      case 1: Vres = _mm256_subs_epu8(Va, Vb); break;
      case 2: Vres = _mm256_avg_epu8(Va, Vb); break;
      case 3: Vres = _mm256_min_epu8(Va, Vb); break;
      case 4: Vres = _mm256_max_epu8(Va, Vb); break;
      }
      _mm256_storeu_si256((__m256i *)DataRes, Vres);
#else
      for (int i = 0; i < 32; i++) {
        int s = 0;
        switch (vec32u8_op) {
        case 0: s = (int)DataA[i] + (int)DataB[i]; DataRes[i] = (uint8_t)(s > 255 ? 255 : s); break;
        case 1: s = (int)DataA[i] - (int)DataB[i]; DataRes[i] = (uint8_t)(s < 0 ? 0 : s); break;
        case 2: DataRes[i] = (uint8_t)(((unsigned)DataA[i] + (unsigned)DataB[i] + 1u) >> 1); break;
        case 3: DataRes[i] = DataA[i] < DataB[i] ? DataA[i] : DataB[i]; break;
        case 4: DataRes[i] = DataA[i] > DataB[i] ? DataA[i] : DataB[i]; break;
        }
      }
#endif
      for (int i = 0; i < 32; i++)
        ResArr->items[ResArr->count++] = ValInt((int64_t)DataRes[i]);
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ResArrVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastBmi2Pext") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: FastBmi2Pext() expects 2 arguments (val, mask)\n");
      return -1;
    }
    AKSValue MaskVal = POP();
    AKSValue SrcVal = POP();
    uint64_t Src = (uint64_t)ValToInt(SrcVal);
    uint64_t Mask = (uint64_t)ValToInt(MaskVal);
    uint64_t Res = 0;
#if (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))) ||           \
    defined(__BMI2__)
    Res = _pext_u64(Src, Mask);
#else
    uint64_t Bit = 1;
    for (int i = 0; i < 64; i++) {
      if (Mask & (1ULL << i)) {
        if (Src & (1ULL << i))
          Res |= Bit;
        Bit <<= 1;
      }
    }
#endif
    ValFree(&SrcVal);
    ValFree(&MaskVal);
    PUSH(ValInt((int64_t)Res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastBmi2Pdep") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: FastBmi2Pdep() expects 2 arguments (val, mask)\n");
      return -1;
    }
    AKSValue MaskVal = POP();
    AKSValue SrcVal = POP();
    uint64_t Src = (uint64_t)ValToInt(SrcVal);
    uint64_t Mask = (uint64_t)ValToInt(MaskVal);
    uint64_t Res = 0;
#if (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))) ||           \
    defined(__BMI2__)
    Res = _pdep_u64(Src, Mask);
#else
    uint64_t Bit = 1;
    for (int i = 0; i < 64; i++) {
      if (Mask & (1ULL << i)) {
        if (Src & Bit)
          Res |= (1ULL << i);
        Bit <<= 1;
      }
    }
#endif
    ValFree(&SrcVal);
    ValFree(&MaskVal);
    PUSH(ValInt((int64_t)Res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastBmi2Bzhi") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: FastBmi2Bzhi() expects 2 arguments (val, index)\n");
      return -1;
    }
    AKSValue IndexVal = POP();
    AKSValue SrcVal = POP();
    uint64_t Src = (uint64_t)ValToInt(SrcVal);
    int64_t Index = ValToInt(IndexVal);
    uint64_t Res = 0;
#if (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))) ||           \
    defined(__BMI2__)
    if (Index <= 0)
      Res = 0;
    else if (Index >= 64)
      Res = Src;
    else
      Res = _bzhi_u64(Src, (unsigned int)Index);
#else
    if (Index <= 0)
      Res = 0;
    else if (Index >= 64)
      Res = Src;
    else
      Res = Src & ((1ULL << Index) - 1ULL);
#endif
    ValFree(&SrcVal);
    ValFree(&IndexVal);
    PUSH(ValInt((int64_t)Res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdStringMatchFind") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SimdStringMatchFind() expects 3 "
                      "arguments (haystack, needle, start_offset)\n");
      return -1;
    }
    AKSValue OffsetVal = POP();
    AKSValue NeedleVal = POP();
    AKSValue HaystackVal = POP();
    int64_t FoundIdx = -1;
    if (ValKind(HaystackVal) == VALSTR && ValKind(NeedleVal) == VALSTR) {
      AKSString *H = ValToStrPtr(HaystackVal);
      AKSString *N = ValToStrPtr(NeedleVal);
      size_t StartOff = (size_t)ValToInt(OffsetVal);
      if (N->len == 0) {
        FoundIdx = (int64_t)StartOff;
      } else if (StartOff < (size_t)H->len && (size_t)N->len <= ((size_t)H->len - StartOff)) {
        const char *Haystack = H->data + StartOff;
        size_t HaystackLen = (size_t)H->len - StartOff;
        const char *Needle = N->data;
        size_t NeedleLen = (size_t)N->len;
        const char *Match = NULL;
        (void)HaystackLen;
        (void)NeedleLen;
#if defined(__AVX2__)
        if (HaystackLen >= 32 && NeedleLen <= HaystackLen) {
          __m256i FirstCharVec = _mm256_set1_epi8(Needle[0]);
          size_t Limit = HaystackLen - NeedleLen;
          size_t i = 0;
          for (; i + 32 <= Limit + 1; i += 32) {
            __m256i Block = _mm256_loadu_si256((const __m256i *)(Haystack + i));
            __m256i Cmp = _mm256_cmpeq_epi8(Block, FirstCharVec);
            unsigned int Mask = (unsigned int)_mm256_movemask_epi8(Cmp);
            while (Mask != 0) {
              unsigned long Pos = 0;
              _BitScanForward(&Pos, Mask);
              if (i + Pos <= Limit &&
                  memcmp(Haystack + i + Pos, Needle, NeedleLen) == 0) {
                Match = Haystack + i + Pos;
                break;
              }
              Mask &= Mask - 1;
            }
            if (Match)
              break;
          }
          if (!Match && i <= Limit) {
            for (size_t j = i; j <= Limit; j++) {
              if (Haystack[j] == Needle[0] &&
                  memcmp(Haystack + j, Needle, NeedleLen) == 0) {
                Match = Haystack + j;
                break;
              }
            }
          }
        }
#endif
        if (!Match) {
          Match = (const char *)strstr(Haystack, Needle);
        }
        if (Match) {
          FoundIdx = (int64_t)(StartOff + (size_t)(Match - Haystack));
        }
      }
    }
    ValFree(&HaystackVal);
    ValFree(&NeedleVal);
    ValFree(&OffsetVal);
    PUSH(ValInt(FoundIdx));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec4qNew") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: SimdVec4qNew() expects 4 arguments\n");
      return -1;
    }
    AKSValue Q3 = POP(), Q2 = POP(), Q1 = POP(), Q0 = POP();
    AKSValue Res = ValStruct("SimdVec4q");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 4;
    S->fieldnames = malloc(4 * sizeof(char *));
    S->fieldvals = malloc(4 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("q0");
    S->fieldvals[0] = Q0;
    S->fieldnames[1] = strdup("q1");
    S->fieldvals[1] = Q1;
    S->fieldnames[2] = strdup("q2");
    S->fieldvals[2] = Q2;
    S->fieldnames[3] = strdup("q3");
    S->fieldvals[3] = Q3;
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec4qAdd") == 0 || strcmp(name, "SimdVec4qSub") == 0 ||
      strcmp(name, "SimdVec4qBitAnd") == 0 ||
      strcmp(name, "SimdVec4qBitOr") == 0 ||
      strcmp(name, "SimdVec4qBitXor") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s expects 2 arguments\n", name);
      return -1;
    }
    AKSValue V2 = POP(), V1 = POP();
    int64_t a[4] = {0}, b[4] = {0}, r[4] = {0};
    if (ValKind(V1) == VALSTRUCT && ValKind(V2) == VALSTRUCT) {
      AKSStruct *S1 = ValToObj(V1);
      AKSStruct *S2 = ValToObj(V2);
      for (int i = 0; i < 4 && i < S1->fieldcount; i++)
        a[i] = ValToInt(S1->fieldvals[i]);
      for (int i = 0; i < 4 && i < S2->fieldcount; i++)
        b[i] = ValToInt(S2->fieldvals[i]);
    }
#if defined(__AVX2__)
    __m256i va = _mm256_loadu_si256((const __m256i *)a);
    __m256i vb = _mm256_loadu_si256((const __m256i *)b);
    __m256i vr;
    if (strcmp(name, "SimdVec4qAdd") == 0)
      vr = _mm256_add_epi64(va, vb);
    else if (strcmp(name, "SimdVec4qSub") == 0)
      vr = _mm256_sub_epi64(va, vb);
    else if (strcmp(name, "SimdVec4qBitAnd") == 0)
      vr = _mm256_and_si256(va, vb);
    else if (strcmp(name, "SimdVec4qBitOr") == 0)
      vr = _mm256_or_si256(va, vb);
    else
      vr = _mm256_xor_si256(va, vb);
    _mm256_storeu_si256((__m256i *)r, vr);
#else
    for (int i = 0; i < 4; i++) {
      if (strcmp(name, "SimdVec4qAdd") == 0)
        r[i] = a[i] + b[i];
      else if (strcmp(name, "SimdVec4qSub") == 0)
        r[i] = a[i] - b[i];
      else if (strcmp(name, "SimdVec4qBitAnd") == 0)
        r[i] = a[i] & b[i];
      else if (strcmp(name, "SimdVec4qBitOr") == 0)
        r[i] = a[i] | b[i];
      else
        r[i] = a[i] ^ b[i];
    }
#endif
    ValFree(&V1);
    ValFree(&V2);
    AKSValue Res = ValStruct("SimdVec4q");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 4;
    S->fieldnames = malloc(4 * sizeof(char *));
    S->fieldvals = malloc(4 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("q0");
    S->fieldvals[0] = ValInt(r[0]);
    S->fieldnames[1] = strdup("q1");
    S->fieldvals[1] = ValInt(r[1]);
    S->fieldnames[2] = strdup("q2");
    S->fieldvals[2] = ValInt(r[2]);
    S->fieldnames[3] = strdup("q3");
    S->fieldvals[3] = ValInt(r[3]);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec4qGet") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: SimdVec4qGet() expects 2 arguments (vec, index)\n");
      return -1;
    }
    AKSValue IdxVal = POP();
    AKSValue VecVal = POP();
    int64_t Val = 0;
    if (ValKind(VecVal) == VALSTRUCT) {
      AKSStruct *S = ValToObj(VecVal);
      int Idx = (int)ValToInt(IdxVal);
      if (Idx >= 0 && Idx < S->fieldcount) {
        Val = ValToInt(S->fieldvals[Idx]);
      }
    }
    ValFree(&VecVal);
    ValFree(&IdxVal);
    PUSH(ValInt(Val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastBitPermuteBswap64") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FastBitPermuteBswap64() expects 1 argument\n");
      return -1;
    }
    AKSValue Val = POP();
    uint64_t V = (uint64_t)ValToInt(Val);
    ValFree(&Val);
    PUSH(ValInt((int64_t)_byteswap_uint64(V)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastBitPermuteBswap32") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FastBitPermuteBswap32() expects 1 argument\n");
      return -1;
    }
    AKSValue Val = POP();
    uint32_t V = (uint32_t)ValToInt(Val);
    ValFree(&Val);
    PUSH(ValInt((int64_t)_byteswap_ulong(V)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastBitPermuteBswap16") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FastBitPermuteBswap16() expects 1 argument\n");
      return -1;
    }
    AKSValue Val = POP();
    unsigned short V = (unsigned short)ValToInt(Val);
    ValFree(&Val);
    PUSH(ValInt((int64_t)_byteswap_ushort(V)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastBitPermuteRbit64") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FastBitPermuteRbit64() expects 1 argument\n");
      return -1;
    }
    AKSValue Val = POP();
    uint64_t V = (uint64_t)ValToInt(Val);
    V = ((V & 0x5555555555555555ULL) << 1) | ((V >> 1) & 0x5555555555555555ULL);
    V = ((V & 0x3333333333333333ULL) << 2) | ((V >> 2) & 0x3333333333333333ULL);
    V = ((V & 0x0F0F0F0F0F0F0F0FULL) << 4) | ((V >> 4) & 0x0F0F0F0F0F0F0F0FULL);
    V = _byteswap_uint64(V);
    ValFree(&Val);
    PUSH(ValInt((int64_t)V));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdDotProductFmaDot8f") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdDotProductFmaDot8f() expects 2 "
                      "arguments (a, b)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    float a[8] = {0}, b[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++)
        a[i] = (float)ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++)
        b[i] = (float)ValToFloat(ArrB->items[i]);
    }
    float Dot = 0.0f;
#if defined(__AVX2__)
    __m256 va = _mm256_loadu_ps(a);
    __m256 vb = _mm256_loadu_ps(b);
    __m256 prod = _mm256_mul_ps(va, vb);
    __m128 lo = _mm256_castps256_ps128(prod);
    __m128 hi = _mm256_extractf128_ps(prod, 1);
    __m128 sum128 = _mm_add_ps(lo, hi);
    sum128 = _mm_hadd_ps(sum128, sum128);
    sum128 = _mm_hadd_ps(sum128, sum128);
    Dot = _mm_cvtss_f32(sum128);
#else
    for (int i = 0; i < 8; i++)
      Dot += a[i] * b[i];
#endif
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ValFloat((double)Dot));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdDotProductFmaFma") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SimdDotProductFmaFma() expects 3 "
                      "arguments (a, b, c)\n");
      return -1;
    }
    AKSValue CVal = POP(), BVal = POP(), AVal = POP();
    float a[8] = {0}, b[8] = {0}, c[8] = {0}, res[8] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR &&
        ValKind(CVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      AKSArray *ArrC = ValToArr(CVal);
      for (int i = 0; i < 8 && i < ArrA->count; i++)
        a[i] = (float)ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 8 && i < ArrB->count; i++)
        b[i] = (float)ValToFloat(ArrB->items[i]);
      for (int i = 0; i < 8 && i < ArrC->count; i++)
        c[i] = (float)ValToFloat(ArrC->items[i]);
    }
#if defined(__AVX2__)
    __m256 va = _mm256_loadu_ps(a);
    __m256 vb = _mm256_loadu_ps(b);
    __m256 vc = _mm256_loadu_ps(c);
    __m256 vr = _mm256_add_ps(_mm256_mul_ps(va, vb), vc);
    _mm256_storeu_ps(res, vr);
#else
    for (int i = 0; i < 8; i++)
      res[i] = a[i] * b[i] + c[i];
#endif
    ValFree(&AVal);
    ValFree(&BVal);
    ValFree(&CVal);
    AKSValue OutArr = ValArr(8);
    AKSArray *Arr = ValToArr(OutArr);
    Arr->count = 8;
    for (int i = 0; i < 8; i++)
      Arr->items[i] = ValFloat((double)res[i]);
    PUSH(OutArr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec16u8Adds") == 0 ||
      strcmp(name, "SimdVec16u8Subs") == 0 ||
      strcmp(name, "SimdVec16u8Avg") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s expects 2 arguments (a_hex, b_hex)\n",
              name);
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    char OutHex[65] = {0};
    if (ValKind(AVal) == VALSTR && ValKind(BVal) == VALSTR) {
      AKSString *SA = ValToStrPtr(AVal);
      AKSString *SB = ValToStrPtr(BVal);
      uint8_t a[32] = {0}, b[32] = {0}, r[32] = {0};
      static const char hexchars[] = "0123456789abcdef";
      size_t LenA = 0, LenB = 0;
      for (size_t i = 0; i + 1 < (size_t)SA->len && LenA < 32; i += 2) {
        char c1 = SA->data[i], c2 = SA->data[i + 1];
        uint8_t b1 =
            (c1 >= '0' && c1 <= '9')
                ? (c1 - '0')
                : ((c1 >= 'a' && c1 <= 'f')
                       ? (c1 - 'a' + 10)
                       : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 =
            (c2 >= '0' && c2 <= '9')
                ? (c2 - '0')
                : ((c2 >= 'a' && c2 <= 'f')
                       ? (c2 - 'a' + 10)
                       : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        a[LenA++] = (b1 << 4) | b2;
      }
      for (size_t i = 0; i + 1 < (size_t)SB->len && LenB < 32; i += 2) {
        char c1 = SB->data[i], c2 = SB->data[i + 1];
        uint8_t b1 =
            (c1 >= '0' && c1 <= '9')
                ? (c1 - '0')
                : ((c1 >= 'a' && c1 <= 'f')
                       ? (c1 - 'a' + 10)
                       : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0));
        uint8_t b2 =
            (c2 >= '0' && c2 <= '9')
                ? (c2 - '0')
                : ((c2 >= 'a' && c2 <= 'f')
                       ? (c2 - 'a' + 10)
                       : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0));
        b[LenB++] = (b1 << 4) | b2;
      }
      size_t MaxLen = (LenA > LenB) ? LenA : LenB;
      if (MaxLen > 32)
        MaxLen = 32;
      if (MaxLen == 0)
        MaxLen = 16;
#if defined(__AVX2__)
      __m256i va = _mm256_loadu_si256((const __m256i *)a);
      __m256i vb = _mm256_loadu_si256((const __m256i *)b);
      __m256i vr;
      if (strcmp(name, "SimdVec16u8Adds") == 0)
        vr = _mm256_adds_epu8(va, vb);
      else if (strcmp(name, "SimdVec16u8Subs") == 0)
        vr = _mm256_subs_epu8(va, vb);
      else
        vr = _mm256_avg_epu8(va, vb);
      _mm256_storeu_si256((__m256i *)r, vr);
#else
      for (size_t i = 0; i < 32; i++) {
        if (strcmp(name, "SimdVec16u8Adds") == 0) {
          uint32_t s = (uint32_t)a[i] + (uint32_t)b[i];
          r[i] = (s > 255) ? 255 : (uint8_t)s;
        } else if (strcmp(name, "SimdVec16u8Subs") == 0) {
          r[i] = (a[i] < b[i]) ? 0 : (a[i] - b[i]);
        } else {
          r[i] = (uint8_t)(((uint32_t)a[i] + (uint32_t)b[i] + 1) / 2);
        }
      }
#endif
      for (size_t i = 0; i < MaxLen; i++) {
        OutHex[i * 2] = hexchars[(r[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[r[i] & 0x0F];
      }
      OutHex[MaxLen * 2] = '\0';
    }
    ValFree(&AVal);
    ValFree(&BVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdMatrix4x4Mul") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: SimdMatrix4x4Mul() expects 2 arguments (m1, m2)\n");
      return -1;
    }
    AKSValue BVal = POP();
    AKSValue AVal = POP();
    float m1[16] = {0}, m2[16] = {0}, res[16] = {0};
    if (ValKind(AVal) == VALARR && ValKind(BVal) == VALARR) {
      AKSArray *ArrA = ValToArr(AVal);
      AKSArray *ArrB = ValToArr(BVal);
      for (int i = 0; i < 16 && i < ArrA->count; i++)
        m1[i] = (float)ValToFloat(ArrA->items[i]);
      for (int i = 0; i < 16 && i < ArrB->count; i++)
        m2[i] = (float)ValToFloat(ArrB->items[i]);
    }
    for (int row = 0; row < 4; row++) {
      for (int col = 0; col < 4; col++) {
        float sum = 0.0f;
        for (int k = 0; k < 4; k++) {
          sum += m1[row * 4 + k] * m2[k * 4 + col];
        }
        res[row * 4 + col] = sum;
      }
    }
    ValFree(&AVal);
    ValFree(&BVal);
    AKSValue OutArr = ValArr(16);
    AKSArray *Arr = ValToArr(OutArr);
    Arr->count = 16;
    for (int i = 0; i < 16; i++)
      Arr->items[i] = ValFloat((double)res[i]);
    PUSH(OutArr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdMatrix4x4Det") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SimdMatrix4x4Det() expects 1 argument (m)\n");
      return -1;
    }
    AKSValue MVal = POP();
    float m[16] = {0};
    if (ValKind(MVal) == VALARR) {
      AKSArray *Arr = ValToArr(MVal);
      for (int i = 0; i < 16 && i < Arr->count; i++)
        m[i] = (float)ValToFloat(Arr->items[i]);
    }
    float det = m[0] * (m[5] * (m[10] * m[15] - m[11] * m[14]) -
                        m[6] * (m[9] * m[15] - m[11] * m[13]) +
                        m[7] * (m[9] * m[14] - m[10] * m[13])) -
                m[1] * (m[4] * (m[10] * m[15] - m[11] * m[14]) -
                        m[6] * (m[8] * m[15] - m[11] * m[12]) +
                        m[7] * (m[8] * m[14] - m[10] * m[12])) +
                m[2] * (m[4] * (m[9] * m[15] - m[11] * m[13]) -
                        m[5] * (m[8] * m[15] - m[11] * m[12]) +
                        m[7] * (m[8] * m[13] - m[9] * m[12])) -
                m[3] * (m[4] * (m[9] * m[14] - m[10] * m[13]) -
                        m[5] * (m[8] * m[14] - m[10] * m[12]) +
                        m[6] * (m[8] * m[13] - m[9] * m[12]));
    ValFree(&MVal);
    PUSH(ValFloat((double)det));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128NewC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: Complex128NewC() expects 2 args\n");
      return -1;
    }
    AKSValue ImV = POP(), ReV = POP();
    double re =
        (ValKind(ReV) == VALFLOAT) ? ValToFloat(ReV) : (double)ValToInt(ReV);
    double im =
        (ValKind(ImV) == VALFLOAT) ? ValToFloat(ImV) : (double)ValToInt(ImV);
    AKSValue R = ValStruct("Complex128");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("re");
    S->fieldvals[0] = ValFloat(re);
    S->fieldnames[1] = strdup("im");
    S->fieldvals[1] = ValFloat(im);
    ValFree(&ReV);
    ValFree(&ImV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128AddC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: Complex128AddC() expects 2 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    double re = ValToFloat(A->fieldvals[0]) + ValToFloat(B->fieldvals[0]);
    double im = ValToFloat(A->fieldvals[1]) + ValToFloat(B->fieldvals[1]);
    AKSValue R = ValStruct("Complex128");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("re");
    S->fieldvals[0] = ValFloat(re);
    S->fieldnames[1] = strdup("im");
    S->fieldvals[1] = ValFloat(im);
    ValFree(&AV);
    ValFree(&BV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128SubC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: Complex128SubC() expects 2 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    double re = ValToFloat(A->fieldvals[0]) - ValToFloat(B->fieldvals[0]);
    double im = ValToFloat(A->fieldvals[1]) - ValToFloat(B->fieldvals[1]);
    AKSValue R = ValStruct("Complex128");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("re");
    S->fieldvals[0] = ValFloat(re);
    S->fieldnames[1] = strdup("im");
    S->fieldvals[1] = ValFloat(im);
    ValFree(&AV);
    ValFree(&BV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128MulC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: Complex128MulC() expects 2 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    double ar = ValToFloat(A->fieldvals[0]), ai = ValToFloat(A->fieldvals[1]);
    double br = ValToFloat(B->fieldvals[0]), bi = ValToFloat(B->fieldvals[1]);
    double re = ar * br - ai * bi, im = ar * bi + ai * br;
    AKSValue R = ValStruct("Complex128");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("re");
    S->fieldvals[0] = ValFloat(re);
    S->fieldnames[1] = strdup("im");
    S->fieldvals[1] = ValFloat(im);
    ValFree(&AV);
    ValFree(&BV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128DivC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: Complex128DivC() expects 2 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    double ar = ValToFloat(A->fieldvals[0]), ai = ValToFloat(A->fieldvals[1]);
    double br = ValToFloat(B->fieldvals[0]), bi = ValToFloat(B->fieldvals[1]);
    double denom = br * br + bi * bi;
    double re = (ar * br + ai * bi) / denom, im = (ai * br - ar * bi) / denom;
    AKSValue R = ValStruct("Complex128");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("re");
    S->fieldvals[0] = ValFloat(re);
    S->fieldnames[1] = strdup("im");
    S->fieldvals[1] = ValFloat(im);
    ValFree(&AV);
    ValFree(&BV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128AbsC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Complex128AbsC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSStruct *A = ValToObj(AV);
    double re = ValToFloat(A->fieldvals[0]), im = ValToFloat(A->fieldvals[1]);
    double mag = sqrt(re * re + im * im);
    ValFree(&AV);
    PUSH(ValFloat(mag));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128ArgC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Complex128ArgC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSStruct *A = ValToObj(AV);
    double re = ValToFloat(A->fieldvals[0]), im = ValToFloat(A->fieldvals[1]);
    double arg = atan2(im, re);
    ValFree(&AV);
    PUSH(ValFloat(arg));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128ConjC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Complex128ConjC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSStruct *A = ValToObj(AV);
    double re = ValToFloat(A->fieldvals[0]), im = ValToFloat(A->fieldvals[1]);
    AKSValue R = ValStruct("Complex128");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("re");
    S->fieldvals[0] = ValFloat(re);
    S->fieldnames[1] = strdup("im");
    S->fieldvals[1] = ValFloat(-im);
    ValFree(&AV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128ExpC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Complex128ExpC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSStruct *A = ValToObj(AV);
    double re = ValToFloat(A->fieldvals[0]), im = ValToFloat(A->fieldvals[1]);
    double er = exp(re);
    double rout = er * cos(im), iout = er * sin(im);
    AKSValue R = ValStruct("Complex128");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("re");
    S->fieldvals[0] = ValFloat(rout);
    S->fieldnames[1] = strdup("im");
    S->fieldvals[1] = ValFloat(iout);
    ValFree(&AV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128SqrtC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Complex128SqrtC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSStruct *A = ValToObj(AV);
    double re = ValToFloat(A->fieldvals[0]), im = ValToFloat(A->fieldvals[1]);
    double mag = sqrt(sqrt(re * re + im * im));
    double arg = atan2(im, re) / 2.0;
    double rout = mag * cos(arg), iout = mag * sin(arg);
    AKSValue R = ValStruct("Complex128");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("re");
    S->fieldvals[0] = ValFloat(rout);
    S->fieldnames[1] = strdup("im");
    S->fieldvals[1] = ValFloat(iout);
    ValFree(&AV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RationalNewC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RationalNewC() expects 2 args (p, q)\n");
      return -1;
    }
    AKSValue QV = POP(), PV = POP();
    int64_t p = ValToInt(PV), q = ValToInt(QV);
    if (q == 0) {
      fprintf(stderr,
              "Runtime error: RationalNewC: denominator cannot be zero\n");
      return -1;
    }
    // normalize sign: keep q positive
    if (q < 0) {
      p = -p;
      q = -q;
    }
    // reduce by GCD
    int64_t a = p < 0 ? -p : p, b = q;
    while (b) {
      int64_t t = b;
      b = a % b;
      a = t;
    }
    if (a > 0) {
      p /= a;
      q /= a;
    }
    AKSValue R = ValStruct("Rational");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("p");
    S->fieldvals[0] = ValInt((int)p);
    S->fieldnames[1] = strdup("q");
    S->fieldvals[1] = ValInt((int)q);
    ValFree(&PV);
    ValFree(&QV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RationalAddC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RationalAddC() expects 2 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    int64_t ap = ValToInt(A->fieldvals[0]), aq = ValToInt(A->fieldvals[1]);
    int64_t bp = ValToInt(B->fieldvals[0]), bq = ValToInt(B->fieldvals[1]);
    int64_t rp = ap * bq + bp * aq, rq = aq * bq;
    int64_t a = rp < 0 ? -rp : rp, b = rq;
    while (b) {
      int64_t t = b;
      b = a % b;
      a = t;
    }
    if (a > 0) {
      rp /= a;
      rq /= a;
    }
    AKSValue R = ValStruct("Rational");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("p");
    S->fieldvals[0] = ValInt((int)rp);
    S->fieldnames[1] = strdup("q");
    S->fieldvals[1] = ValInt((int)rq);
    ValFree(&AV);
    ValFree(&BV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RationalMulC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RationalMulC() expects 2 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    int64_t ap = ValToInt(A->fieldvals[0]), aq = ValToInt(A->fieldvals[1]);
    int64_t bp = ValToInt(B->fieldvals[0]), bq = ValToInt(B->fieldvals[1]);
    int64_t rp = ap * bp, rq = aq * bq;
    int64_t a = rp < 0 ? -rp : rp, b = rq;
    while (b) {
      int64_t t = b;
      b = a % b;
      a = t;
    }
    if (a > 0) {
      rp /= a;
      rq /= a;
    }
    AKSValue R = ValStruct("Rational");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("p");
    S->fieldvals[0] = ValInt((int)rp);
    S->fieldnames[1] = strdup("q");
    S->fieldvals[1] = ValInt((int)rq);
    ValFree(&AV);
    ValFree(&BV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RationalSubC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RationalSubC() expects 2 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    int64_t ap = ValToInt(A->fieldvals[0]), aq = ValToInt(A->fieldvals[1]);
    int64_t bp = ValToInt(B->fieldvals[0]), bq = ValToInt(B->fieldvals[1]);
    int64_t rp = ap * bq - bp * aq, rq = aq * bq;
    int64_t a = rp < 0 ? -rp : rp, b = rq;
    while (b) {
      int64_t t = b;
      b = a % b;
      a = t;
    }
    if (a > 0) {
      rp /= a;
      rq /= a;
    }
    AKSValue R = ValStruct("Rational");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("p");
    S->fieldvals[0] = ValInt((int)rp);
    S->fieldnames[1] = strdup("q");
    S->fieldvals[1] = ValInt((int)rq);
    ValFree(&AV);
    ValFree(&BV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RationalDivC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RationalDivC() expects 2 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    int64_t ap = ValToInt(A->fieldvals[0]), aq = ValToInt(A->fieldvals[1]);
    int64_t bp = ValToInt(B->fieldvals[0]), bq = ValToInt(B->fieldvals[1]);
    int64_t rp = ap * bq, rq = aq * bp;
    if (rq < 0) {
      rp = -rp;
      rq = -rq;
    }
    int64_t a = rp < 0 ? -rp : rp, b = rq;
    while (b) {
      int64_t t = b;
      b = a % b;
      a = t;
    }
    if (a > 0) {
      rp /= a;
      rq /= a;
    }
    AKSValue R = ValStruct("Rational");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("p");
    S->fieldvals[0] = ValInt((int)rp);
    S->fieldnames[1] = strdup("q");
    S->fieldvals[1] = ValInt((int)rq);
    ValFree(&AV);
    ValFree(&BV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RationalToFloatC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: RationalToFloatC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSStruct *A = ValToObj(AV);
    double v =
        (double)ValToInt(A->fieldvals[0]) / (double)ValToInt(A->fieldvals[1]);
    ValFree(&AV);
    PUSH(ValFloat(v));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RationalEqC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RationalEqC() expects 2 args\n");
      return -1;
    }
    AKSValue BV = POP(), AV = POP();
    AKSStruct *A = ValToObj(AV), *B = ValToObj(BV);
    int eq = ValToInt(A->fieldvals[0]) == ValToInt(B->fieldvals[0]) &&
             ValToInt(A->fieldvals[1]) == ValToInt(B->fieldvals[1]);
    ValFree(&AV);
    ValFree(&BV);
    PUSH(ValBool(eq));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FFTForwardC") == 0 || strcmp(name, "FFTInverseC") == 0) {
    int is_inverse = (strcmp(name, "FFTInverseC") == 0);
    if (argc != 1) {
      fprintf(stderr, "Runtime error: %s() expects 1 arg (array of floats)\n",
              name);
      return -1;
    }
    AKSValue ArrV = POP();
    AKSArray *Input = ValToArr(ArrV);
    int n = Input->count;
    // verify n is power of 2
    if (n == 0 || (n & (n - 1)) != 0) {
      fprintf(stderr,
              "Runtime error: FFT input length must be a power of 2, got %d\n",
              n);
      ValFree(&ArrV);
      return -1;
    }
    // allocate real/imag buffers
    double *re = malloc(n * sizeof(double));
    double *im = malloc(n * sizeof(double));
    for (int i = 0; i < n; i++) {
      AKSValue v = Input->items[i];
      if (ValKind(v) == VALFLOAT) {
        re[i] = ValToFloat(v);
        im[i] = 0.0;
      } else {
        re[i] = (double)ValToInt(v);
        im[i] = 0.0;
      }
    }
    // bit-reversal permutation
    for (int i = 1, j = 0; i < n; i++) {
      int bit = n >> 1;
      for (; j & bit; bit >>= 1)
        j ^= bit;
      j ^= bit;
      if (i < j) {
        double tr = re[i];
        re[i] = re[j];
        re[j] = tr;
        double ti = im[i];
        im[i] = im[j];
        im[j] = ti;
      }
    }
    // Cooley-Tukey butterfly
    double sign = is_inverse ? 1.0 : -1.0;
    for (int len = 2; len <= n; len <<= 1) {
      double ang = sign * 2.0 * 3.14159265358979323846 / len;
      double wr = cos(ang), wi = sin(ang);
      for (int i = 0; i < n; i += len) {
        double cur_r = 1.0, cur_i = 0.0;
        for (int j = 0; j < len / 2; j++) {
          double ur = re[i + j], ui = im[i + j];
          double vr = re[i + j + len / 2] * cur_r - im[i + j + len / 2] * cur_i;
          double vi = re[i + j + len / 2] * cur_i + im[i + j + len / 2] * cur_r;
          re[i + j] = ur + vr;
          im[i + j] = ui + vi;
          re[i + j + len / 2] = ur - vr;
          im[i + j + len / 2] = ui - vi;
          double nr = cur_r * wr - cur_i * wi;
          cur_i = cur_r * wi + cur_i * wr;
          cur_r = nr;
        }
      }
    }
    if (is_inverse) {
      for (int i = 0; i < n; i++) {
        re[i] /= n;
        im[i] /= n;
      }
    }
    // build output: array of [re, im] pairs
    AKSValue OutArr = ValArr(n);
    AKSArray *Out = ValToArr(OutArr);
    Out->count = n;
    Out->capacity = n;
    Out->items = realloc(Out->items, n * sizeof(AKSValue));
    for (int i = 0; i < n; i++) {
      AKSValue Pair = ValStruct("Complex128");
      AKSStruct *SP = ValToObj(Pair);
      SP->fieldcount = 2;
      SP->fieldnames = malloc(2 * sizeof(char *));
      SP->fieldvals = malloc(2 * sizeof(AKSValue));
      SP->fieldnames[0] = strdup("re");
      SP->fieldvals[0] = ValFloat(re[i]);
      SP->fieldnames[1] = strdup("im");
      SP->fieldvals[1] = ValFloat(im[i]);
      Out->items[i] = Pair;
    }
    free(re);
    free(im);
    ValFree(&ArrV);
    PUSH(OutArr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StatMeanC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: StatMeanC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSArray *A = ValToArr(AV);
    double s = 0;
    int n = A->count;
    for (int i = 0; i < n; i++)
      s += (ValKind(A->items[i]) == VALFLOAT) ? ValToFloat(A->items[i])
                                              : (double)ValToInt(A->items[i]);
    ValFree(&AV);
    PUSH(ValFloat(n > 0 ? s / n : 0.0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StatVarianceC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: StatVarianceC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSArray *A = ValToArr(AV);
    int n = A->count;
    double s = 0;
    for (int i = 0; i < n; i++)
      s += (ValKind(A->items[i]) == VALFLOAT) ? ValToFloat(A->items[i])
                                              : (double)ValToInt(A->items[i]);
    double mean = n > 0 ? s / n : 0;
    double v = 0;
    for (int i = 0; i < n; i++) {
      double d =
          ((ValKind(A->items[i]) == VALFLOAT) ? ValToFloat(A->items[i])
                                              : (double)ValToInt(A->items[i])) -
          mean;
      v += d * d;
    }
    ValFree(&AV);
    PUSH(ValFloat(n > 1 ? v / (n - 1) : 0.0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StatStddevC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: StatStddevC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSArray *A = ValToArr(AV);
    int n = A->count;
    double s = 0;
    for (int i = 0; i < n; i++)
      s += (ValKind(A->items[i]) == VALFLOAT) ? ValToFloat(A->items[i])
                                              : (double)ValToInt(A->items[i]);
    double mean = n > 0 ? s / n : 0;
    double v = 0;
    for (int i = 0; i < n; i++) {
      double d =
          ((ValKind(A->items[i]) == VALFLOAT) ? ValToFloat(A->items[i])
                                              : (double)ValToInt(A->items[i])) -
          mean;
      v += d * d;
    }
    ValFree(&AV);
    PUSH(ValFloat(n > 1 ? sqrt(v / (n - 1)) : 0.0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StatMinC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: StatMinC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSArray *A = ValToArr(AV);
    int n = A->count;
    double mn = n > 0 ? ((ValKind(A->items[0]) == VALFLOAT)
                             ? ValToFloat(A->items[0])
                             : (double)ValToInt(A->items[0]))
                      : 0.0;
    for (int i = 1; i < n; i++) {
      double v = (ValKind(A->items[i]) == VALFLOAT)
                     ? ValToFloat(A->items[i])
                     : (double)ValToInt(A->items[i]);
      if (v < mn)
        mn = v;
    }
    ValFree(&AV);
    PUSH(ValFloat(mn));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StatMaxC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: StatMaxC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSArray *A = ValToArr(AV);
    int n = A->count;
    double mx = n > 0 ? ((ValKind(A->items[0]) == VALFLOAT)
                             ? ValToFloat(A->items[0])
                             : (double)ValToInt(A->items[0]))
                      : 0.0;
    for (int i = 1; i < n; i++) {
      double v = (ValKind(A->items[i]) == VALFLOAT)
                     ? ValToFloat(A->items[i])
                     : (double)ValToInt(A->items[i]);
      if (v > mx)
        mx = v;
    }
    ValFree(&AV);
    PUSH(ValFloat(mx));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StatPercentileC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: StatPercentileC() expects 2 args (data, p)\n");
      return -1;
    }
    AKSValue PV = POP(), AV = POP();
    AKSArray *A = ValToArr(AV);
    int n = A->count;
    double pct =
        (ValKind(PV) == VALFLOAT) ? ValToFloat(PV) : (double)ValToInt(PV);
    if (n == 0) {
      ValFree(&AV);
      ValFree(&PV);
      PUSH(ValFloat(0.0));
      *stacktopptr = stacktop;
      return 1;
    }
    double *buf = malloc(n * sizeof(double));
    for (int i = 0; i < n; i++)
      buf[i] = (ValKind(A->items[i]) == VALFLOAT)
                   ? ValToFloat(A->items[i])
                   : (double)ValToInt(A->items[i]);
    // insertion sort
    for (int i = 1; i < n; i++) {
      double key = buf[i];
      int j = i - 1;
      while (j >= 0 && buf[j] > key) {
        buf[j + 1] = buf[j];
        j--;
      }
      buf[j + 1] = key;
    }
    double idx = (pct / 100.0) * (n - 1);
    int lo = (int)idx;
    double frac = idx - lo;
    double result =
        (lo + 1 < n) ? buf[lo] * (1.0 - frac) + buf[lo + 1] * frac : buf[lo];
    free(buf);
    ValFree(&AV);
    ValFree(&PV);
    PUSH(ValFloat(result));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StatSkewnessC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: StatSkewnessC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSArray *A = ValToArr(AV);
    int n = A->count;
    double s = 0;
    for (int i = 0; i < n; i++)
      s += (ValKind(A->items[i]) == VALFLOAT) ? ValToFloat(A->items[i])
                                              : (double)ValToInt(A->items[i]);
    double mean = n > 0 ? s / n : 0;
    double v = 0, sk = 0;
    for (int i = 0; i < n; i++) {
      double d =
          ((ValKind(A->items[i]) == VALFLOAT) ? ValToFloat(A->items[i])
                                              : (double)ValToInt(A->items[i])) -
          mean;
      v += d * d;
      sk += d * d * d;
    }
    double std = n > 1 ? sqrt(v / (n - 1)) : 0;
    double skewness =
        (n > 2 && std > 1e-12) ? (sk / n) / (std * std * std) : 0.0;
    ValFree(&AV);
    PUSH(ValFloat(skewness));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StatKurtosisC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: StatKurtosisC() expects 1 arg\n");
      return -1;
    }
    AKSValue AV = POP();
    AKSArray *A = ValToArr(AV);
    int n = A->count;
    double s = 0;
    for (int i = 0; i < n; i++)
      s += (ValKind(A->items[i]) == VALFLOAT) ? ValToFloat(A->items[i])
                                              : (double)ValToInt(A->items[i]);
    double mean = n > 0 ? s / n : 0;
    double v = 0, k4 = 0;
    for (int i = 0; i < n; i++) {
      double d =
          ((ValKind(A->items[i]) == VALFLOAT) ? ValToFloat(A->items[i])
                                              : (double)ValToInt(A->items[i])) -
          mean;
      v += d * d;
      k4 += d * d * d * d;
    }
    double var = n > 1 ? v / (n - 1) : 0;
    double kurt = (n > 3 && var > 1e-24) ? (k4 / n) / (var * var) - 3.0 : 0.0;
    ValFree(&AV);
    PUSH(ValFloat(kurt));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MTNewC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MTNewC() expects 1 arg (seed)\n");
      return -1;
    }
    AKSValue SeedV = POP();
    uint64_t seed = (uint64_t)ValToInt(SeedV);
    // MT19937-64: state array of 312 uint64_t stored in array
    AKSValue StateArr = ValArr(313);
    AKSArray *SA = ValToArr(StateArr);
    SA->count = 313;
    SA->capacity = 313;
    SA->items = realloc(SA->items, 313 * sizeof(AKSValue));
    // mt[0..311] = state, mt[312] = index
    SA->items[0] = ValInt((int)(seed & 0xFFFFFFFF));
    uint64_t mt0 = seed;
    for (int i = 1; i < 312; i++) {
      mt0 = 6364136223846793005ULL * (mt0 ^ (mt0 >> 62)) + (uint64_t)i;
      SA->items[i] = ValInt((int)(mt0 & 0x7FFFFFFF));
      // store upper 32 bits in a secondary slot would lose precision; store as
      // float to preserve 64 bits We simplify: store full 64-bit as double
      // (exact for values < 2^53)
      SA->items[i] = ValFloat((double)mt0);
    }
    SA->items[312] = ValInt(312); // index = 312 -> needs generation
    AKSValue R = ValStruct("MersenneTwister");
    AKSStruct *S = ValToObj(R);
    S->fieldcount = 1;
    S->fieldnames = malloc(sizeof(char *));
    S->fieldvals = malloc(sizeof(AKSValue));
    S->fieldnames[0] = strdup("state");
    S->fieldvals[0] = StateArr;
    ValFree(&SeedV);
    PUSH(R);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MTNextU64C") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: MTNextU64C() expects 1 arg (mt struct)\n");
      return -1;
    }
    AKSValue SelfV = POP();
    AKSStruct *S = ValToObj(SelfV);
    AKSArray *SA = ValToArr(S->fieldvals[0]);
    int idx = (int)ValToInt(SA->items[312]);
    if (idx >= 312) {
      // generate next 312 values
      for (int i = 0; i < 312; i++) {
        uint64_t x =
            ((uint64_t)(ValToFloat(SA->items[i]) >= 0
                            ? (uint64_t)ValToFloat(SA->items[i])
                            : 0) &
             0xFFFFFFFF80000000ULL) |
            ((uint64_t)(ValToFloat(SA->items[(i + 1) % 312]) >= 0
                            ? (uint64_t)ValToFloat(SA->items[(i + 1) % 312])
                            : 0) &
             0x7FFFFFFFULL);
        uint64_t xa = (x >> 1) ^ ((x & 1) ? 0xB5026F5AA96619E9ULL : 0);
        uint64_t nv = (uint64_t)ValToFloat(SA->items[(i + 156) % 312]) ^ xa;
        ValFree(&SA->items[i]);
        SA->items[i] = ValFloat((double)nv);
      }
      idx = 0;
    }
    uint64_t y = (uint64_t)ValToFloat(SA->items[idx]);
    ValFree(&SA->items[312]);
    SA->items[312] = ValInt(idx + 1);
    y ^= (y >> 29) & 0x5555555555555555ULL;
    y ^= (y << 17) & 0x71D67FFFEDA60000ULL;
    y ^= (y << 37) & 0xFFF7EEE000000000ULL;
    y ^= (y >> 43);
    // return as float (exact for < 2^53)
    ValFree(&SelfV);
    PUSH(ValFloat((double)y));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MTNextFloatC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: MTNextFloatC() expects 1 arg (mt struct)\n");
      return -1;
    }
    AKSValue SelfV = POP();
    AKSStruct *S = ValToObj(SelfV);
    AKSArray *SA = ValToArr(S->fieldvals[0]);
    int idx = (int)ValToInt(SA->items[312]);
    if (idx >= 312) {
      for (int i = 0; i < 312; i++) {
        uint64_t x =
            ((uint64_t)(ValToFloat(SA->items[i]) >= 0
                            ? (uint64_t)ValToFloat(SA->items[i])
                            : 0) &
             0xFFFFFFFF80000000ULL) |
            ((uint64_t)(ValToFloat(SA->items[(i + 1) % 312]) >= 0
                            ? (uint64_t)ValToFloat(SA->items[(i + 1) % 312])
                            : 0) &
             0x7FFFFFFFULL);
        uint64_t xa = (x >> 1) ^ ((x & 1) ? 0xB5026F5AA96619E9ULL : 0);
        uint64_t nv = (uint64_t)ValToFloat(SA->items[(i + 156) % 312]) ^ xa;
        ValFree(&SA->items[i]);
        SA->items[i] = ValFloat((double)nv);
      }
      idx = 0;
    }
    uint64_t y = (uint64_t)ValToFloat(SA->items[idx]);
    ValFree(&SA->items[312]);
    SA->items[312] = ValInt(idx + 1);
    y ^= (y >> 29) & 0x5555555555555555ULL;
    y ^= (y << 17) & 0x71D67FFFEDA60000ULL;
    y ^= (y << 37) & 0xFFF7EEE000000000ULL;
    y ^= (y >> 43);
    double f = (double)(y >> 11) * (1.0 / 9007199254740992.0);
    ValFree(&SelfV);
    PUSH(ValFloat(f));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MillerRabinC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: MillerRabinC() expects 2 args (n, rounds)\n");
      return -1;
    }
    AKSValue RoundsV = POP(), NV = POP();
    uint64_t mr_n = (uint64_t)ValToInt(NV);
    int mr_rounds = (int)ValToInt(RoundsV);
    if (mr_n < 2) {
      ValFree(&NV);
      ValFree(&RoundsV);
      PUSH(ValBool(0));
      *stacktopptr = stacktop;
      return 1;
    }
    if (mr_n == 2 || mr_n == 3) {
      ValFree(&NV);
      ValFree(&RoundsV);
      PUSH(ValBool(1));
      *stacktopptr = stacktop;
      return 1;
    }
    if (mr_n % 2 == 0) {
      ValFree(&NV);
      ValFree(&RoundsV);
      PUSH(ValBool(0));
      *stacktopptr = stacktop;
      return 1;
    }
    uint64_t mr_d = mr_n - 1;
    int mr_r = 0;
    while (mr_d % 2 == 0) {
      mr_d /= 2;
      mr_r++;
    }
    static const uint64_t mr_witnesses[] = {2,  3,  5,  7,  11, 13,
                                            17, 19, 23, 29, 31, 37};
    int mr_wcount = mr_rounds < 12 ? mr_rounds : 12;
    int mr_prime = 1;
    for (int wi = 0; wi < mr_wcount && mr_prime; wi++) {
      uint64_t mr_a = mr_witnesses[wi];
      if (mr_a >= mr_n)
        continue;
      uint64_t mr_x = 1, mr_base = mr_a % mr_n, mr_exp = mr_d;
      while (mr_exp > 0) {
        if (mr_exp & 1)
          mr_x = mr_x * mr_base % mr_n;
        mr_base = mr_base * mr_base % mr_n;
        mr_exp >>= 1;
      }
      if (mr_x == 1 || mr_x == mr_n - 1)
        continue;
      int mr_comp = 1;
      for (int j = 0; j < mr_r - 1; j++) {
        mr_x = mr_x * mr_x % mr_n;
        if (mr_x == mr_n - 1) {
          mr_comp = 0;
          break;
        }
      }
      if (mr_comp)
        mr_prime = 0;
    }
    ValFree(&NV);
    ValFree(&RoundsV);
    PUSH(ValBool(mr_prime));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MillerRabinNextPrimeC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MillerRabinNextPrimeC() expects 1 arg\n");
      return -1;
    }
    AKSValue NV = POP();
    uint64_t np = (uint64_t)ValToInt(NV);
    if (np < 2) {
      ValFree(&NV);
      PUSH(ValInt(2));
      *stacktopptr = stacktop;
      return 1;
    }
    np++;
    if (np % 2 == 0)
      np++;
    static const uint64_t mrn_wits[] = {2,  3,  5,  7,  11, 13,
                                        17, 19, 23, 29, 31, 37};
    while (1) {
      if (np == 2 || np == 3) {
        break;
      }
      if (np % 2 == 0) {
        np++;
        continue;
      }
      uint64_t mrn_d = np - 1;
      int mrn_r = 0;
      while (mrn_d % 2 == 0) {
        mrn_d /= 2;
        mrn_r++;
      }
      int mrn_ok = 1;
      for (int wi = 0; wi < 12 && mrn_ok; wi++) {
        if (mrn_wits[wi] >= np)
          continue;
        uint64_t mrn_x = 1, mrn_base = mrn_wits[wi] % np, mrn_exp = mrn_d;
        while (mrn_exp > 0) {
          if (mrn_exp & 1)
            mrn_x = mrn_x * mrn_base % np;
          mrn_base = mrn_base * mrn_base % np;
          mrn_exp >>= 1;
        }
        if (mrn_x == 1 || mrn_x == np - 1)
          continue;
        int mrn_comp = 1;
        for (int j = 0; j < mrn_r - 1; j++) {
          mrn_x = mrn_x * mrn_x % np;
          if (mrn_x == np - 1) {
            mrn_comp = 0;
            break;
          }
        }
        if (mrn_comp)
          mrn_ok = 0;
      }
      if (mrn_ok)
        break;
      np += 2;
    }
    ValFree(&NV);
    PUSH(ValInt((int)np));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec4dAddC") == 0 ||
      strcmp(name, "SimdVec4dMulC") == 0 ||
      strcmp(name, "SimdVec4dSubC") == 0 ||
      strcmp(name, "SimdVec4dDivC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s() expects 2 args (arr4, arr4)\n",
              name);
      return -1;
    }
    AKSValue BV2 = POP(), AV3 = POP();
    AKSArray *aa3 = ValToArr(AV3), *ba3 = ValToArr(BV2);
    double av4[4] = {0}, bv4[4] = {0};
    for (int i = 0; i < 4 && i < aa3->count; i++)
      av4[i] = ValToFloat(aa3->items[i]);
    for (int i = 0; i < 4 && i < ba3->count; i++)
      bv4[i] = ValToFloat(ba3->items[i]);
    double rv4[4];
#if defined(__AVX2__) || defined(_M_X64)
    __m256d va4 = _mm256_loadu_pd(av4), vb4 = _mm256_loadu_pd(bv4), vc4;
    if (name[10] == 'A')
      vc4 = _mm256_add_pd(va4, vb4);
    else if (name[10] == 'M')
      vc4 = _mm256_mul_pd(va4, vb4);
    else if (name[10] == 'S')
      vc4 = _mm256_sub_pd(va4, vb4);
    else
      vc4 = _mm256_div_pd(va4, vb4);
    _mm256_storeu_pd(rv4, vc4);
#else
    for (int i = 0; i < 4; i++) {
      if (name[10] == 'A') rv4[i] = av4[i] + bv4[i];
      else if (name[10] == 'M') rv4[i] = av4[i] * bv4[i];
      else if (name[10] == 'S') rv4[i] = av4[i] - bv4[i];
      else rv4[i] = (bv4[i] == 0.0) ? 0.0 : av4[i] / bv4[i];
    }
#endif
    AKSValue r4 = ValArr(4);
    AKSArray *ra4 = ValToArr(r4);
    for (int i = 0; i < 4; i++)
      AksArrPush(ra4, ValFloat(rv4[i]));
    ValFree(&AV3);
    ValFree(&BV2);
    PUSH(r4);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdVec16sAddC") == 0 ||
      strcmp(name, "SimdVec16sSubC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s() expects 2 args\n", name);
      return -1;
    }
    AKSValue BV3 = POP(), AV4 = POP();
    AKSArray *aa4 = ValToArr(AV4), *ba4 = ValToArr(BV3);
    int16_t av16[16] = {0}, bv16[16] = {0};
    for (int i = 0; i < 16 && i < aa4->count; i++)
      av16[i] = (int16_t)ValToInt(aa4->items[i]);
    for (int i = 0; i < 16 && i < ba4->count; i++)
      bv16[i] = (int16_t)ValToInt(ba4->items[i]);
    int16_t rv16[16];
#if defined(__AVX2__) || defined(_M_X64)
    __m256i va16 = _mm256_loadu_si256((const __m256i *)av16);
    __m256i vb16 = _mm256_loadu_si256((const __m256i *)bv16);
    __m256i vc16 = (name[11] == 'A') ? _mm256_add_epi16(va16, vb16)
                                     : _mm256_sub_epi16(va16, vb16);
    _mm256_storeu_si256((__m256i *)rv16, vc16);
#else
    for (int i = 0; i < 16; i++)
      rv16[i] = (name[11] == 'A') ? (int16_t)(av16[i] + bv16[i])
                                  : (int16_t)(av16[i] - bv16[i]);
#endif
    AKSValue r16 = ValArr(16);
    AKSArray *ra16 = ValToArr(r16);
    for (int i = 0; i < 16; i++)
      AksArrPush(ra16, ValInt(rv16[i]));
    ValFree(&AV4);
    ValFree(&BV3);
    PUSH(r16);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdBitScanFwdC") == 0 ||
      strcmp(name, "SimdBitScanRevC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: %s() expects 1 arg\n", name);
      return -1;
    }
    AKSValue IV2 = POP();
    unsigned long bs_idx = 0;
    int bs_ok = 0;
    uint64_t bs_v = (uint64_t)ValToInt(IV2);
    if (name[12] == 'F')
      bs_ok = _BitScanForward64(&bs_idx, bs_v);
    else
      bs_ok = _BitScanReverse64(&bs_idx, bs_v);
    ValFree(&IV2);
    PUSH(bs_ok ? ValInt((int64_t)bs_idx) : ValInt(-1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdConvertIntToFloatC") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: SimdConvertIntToFloatC() expects 1 arg (arr8_int)\n");
      return -1;
    }
    AKSValue AV5 = POP();
    AKSArray *aa5 = ValToArr(AV5);
    int32_t cv8[8] = {0};
    for (int i = 0; i < 8 && i < aa5->count; i++)
      cv8[i] = (int32_t)ValToInt(aa5->items[i]);
    float cvr[8];
#if defined(__AVX2__) || defined(_M_X64)
    __m256i cvi = _mm256_loadu_si256((const __m256i *)cv8);
    __m256 cvf = _mm256_cvtepi32_ps(cvi);
    _mm256_storeu_ps(cvr, cvf);
#else
    for (int i = 0; i < 8; i++)
      cvr[i] = (float)cv8[i];
#endif
    AKSValue cvres = ValArr(8);
    AKSArray *cvra = ValToArr(cvres);
    for (int i = 0; i < 8; i++)
      AksArrPush(cvra, ValFloat((double)cvr[i]));
    ValFree(&AV5);
    PUSH(cvres);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdBroadcastC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SimdBroadcastC() expects 1 arg (scalar_float)\n");
      return -1;
    }
    AKSValue FV = POP();
    float bc_f = (float)ValToFloat(FV);
    float bc_r[8];
#if defined(__AVX2__) || defined(_M_X64)
    __m256 bc_v = _mm256_set1_ps(bc_f);
    _mm256_storeu_ps(bc_r, bc_v);
#else
    for (int i = 0; i < 8; i++)
      bc_r[i] = bc_f;
#endif
    AKSValue bc_res = ValArr(8);
    AKSArray *bc_a = ValToArr(bc_res);
    for (int i = 0; i < 8; i++)
      AksArrPush(bc_a, ValFloat((double)bc_r[i]));
    ValFree(&FV);
    PUSH(bc_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RunningStatsNewC") == 0) {
    AKSValue rs = ValStruct("RunningStats");
    AKSStruct *rss = ValToObj(rs);
    rss->fieldnames = malloc(4 * sizeof(char *));
    rss->fieldvals = malloc(4 * sizeof(AKSValue));
    rss->fieldnames[0] = strdup("n");
    rss->fieldvals[0] = ValInt(0);
    rss->fieldnames[1] = strdup("mean");
    rss->fieldvals[1] = ValFloat(0.0);
    rss->fieldnames[2] = strdup("M2");
    rss->fieldvals[2] = ValFloat(0.0);
    rss->fieldnames[3] = strdup("M3");
    rss->fieldvals[3] = ValFloat(0.0);
    rss->fieldcount = 4;
    PUSH(rs);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RunningStatsAddC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RunningStatsAddC() expects 2 args\n");
      return -1;
    }
    AKSValue XV = POP(), RV = POP();
    AKSStruct *rss = ValToObj(RV);
    double rs_x = ValToFloat(XV);
    int64_t rs_n = ValToInt(rss->fieldvals[0]) + 1;
    double rs_old_mean = ValToFloat(rss->fieldvals[1]);
    double rs_delta = rs_x - rs_old_mean;
    double rs_mean = rs_old_mean + rs_delta / rs_n;
    double rs_delta2 = rs_x - rs_mean;
    double rs_M2 = ValToFloat(rss->fieldvals[2]) + rs_delta * rs_delta2;
    rss->fieldvals[0] = ValInt(rs_n);
    rss->fieldvals[1] = ValFloat(rs_mean);
    rss->fieldvals[2] = ValFloat(rs_M2);
    ValFree(&XV);
    PUSH(RV);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RunningStatsMeanC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: RunningStatsMeanC() expects 1 arg\n");
      return -1;
    }
    AKSValue RV = POP();
    AKSStruct *rss = ValToObj(RV);
    double mean = ValToFloat(rss->fieldvals[1]);
    ValFree(&RV);
    PUSH(ValFloat(mean));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RunningStatsVarC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: RunningStatsVarC() expects 1 arg\n");
      return -1;
    }
    AKSValue RV = POP();
    AKSStruct *rss = ValToObj(RV);
    int64_t rs_n = ValToInt(rss->fieldvals[0]);
    double var = rs_n > 1 ? ValToFloat(rss->fieldvals[2]) / (rs_n - 1) : 0.0;
    ValFree(&RV);
    PUSH(ValFloat(var));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RunningStatsStdC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: RunningStatsStdC() expects 1 arg\n");
      return -1;
    }
    AKSValue RV = POP();
    AKSStruct *rss = ValToObj(RV);
    int64_t rs_n = ValToInt(rss->fieldvals[0]);
    double var = rs_n > 1 ? ValToFloat(rss->fieldvals[2]) / (rs_n - 1) : 0.0;
    ValFree(&RV);
    PUSH(ValFloat(sqrt(var)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LagrangeInterpC") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: LagrangeInterpC() expects 3 args (xs, ys, x)\n");
      return -1;
    }
    AKSValue XV2 = POP(), YAV = POP(), XAV = POP();
    double li_x = ValToFloat(XV2);
    AKSArray *li_xa = ValToArr(XAV), *li_ya = ValToArr(YAV);
    int li_n =
        li_xa->count < li_ya->count ? (int)li_xa->count : (int)li_ya->count;
    double li_res = 0.0;
    for (int i = 0; i < li_n; i++) {
      double li_xi = ValToFloat(li_xa->items[i]),
             li_yi = ValToFloat(li_ya->items[i]);
      double li_l = 1.0;
      for (int j = 0; j < li_n; j++) {
        if (j == i)
          continue;
        double li_xj = ValToFloat(li_xa->items[j]);
        if (li_xi - li_xj == 0)
          continue;
        li_l *= (li_x - li_xj) / (li_xi - li_xj);
      }
      li_res += li_yi * li_l;
    }
    ValFree(&XV2);
    ValFree(&YAV);
    ValFree(&XAV);
    PUSH(ValFloat(li_res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LinearSolveC") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: LinearSolveC() expects 2 args (A_flat_n2, b_n)\n");
      return -1;
    }
    AKSValue BV4 = POP(), AV6 = POP();
    AKSArray *ls_A = ValToArr(AV6), *ls_b = ValToArr(BV4);
    int ls_n = (int)ls_b->count;
    if (ls_n <= 0 || (size_t)ls_A->count < (size_t)(ls_n * ls_n)) {
      ValFree(&BV4);
      ValFree(&AV6);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    double *ls_m = malloc(ls_n * ls_n * sizeof(double));
    double *ls_v = malloc(ls_n * sizeof(double));
    for (int i = 0; i < ls_n * ls_n; i++)
      ls_m[i] = ValToFloat(ls_A->items[i]);
    for (int i = 0; i < ls_n; i++)
      ls_v[i] = ValToFloat(ls_b->items[i]);
    for (int col = 0; col < ls_n; col++) {
      int pivot = col;
      for (int row = col + 1; row < ls_n; row++)
        if (fabs(ls_m[row * ls_n + col]) > fabs(ls_m[pivot * ls_n + col]))
          pivot = row;
      for (int k = 0; k < ls_n; k++) {
        double t = ls_m[col * ls_n + k];
        ls_m[col * ls_n + k] = ls_m[pivot * ls_n + k];
        ls_m[pivot * ls_n + k] = t;
      }
      double t2 = ls_v[col];
      ls_v[col] = ls_v[pivot];
      ls_v[pivot] = t2;
      double diag = ls_m[col * ls_n + col];
      if (fabs(diag) < 1e-15)
        continue;
      for (int row2 = col + 1; row2 < ls_n; row2++) {
        double fac = ls_m[row2 * ls_n + col] / diag;
        for (int k = col; k < ls_n; k++)
          ls_m[row2 * ls_n + k] -= fac * ls_m[col * ls_n + k];
        ls_v[row2] -= fac * ls_v[col];
      }
    }
    double *ls_x = calloc(ls_n, sizeof(double));
    for (int i = ls_n - 1; i >= 0; i--) {
      ls_x[i] = ls_v[i];
      for (int j = i + 1; j < ls_n; j++)
        ls_x[i] -= ls_m[i * ls_n + j] * ls_x[j];
      if (fabs(ls_m[i * ls_n + i]) > 1e-15)
        ls_x[i] /= ls_m[i * ls_n + i];
    }
    AKSValue ls_res = ValArr(ls_n);
    AKSArray *ls_ra = ValToArr(ls_res);
    for (int i = 0; i < ls_n; i++)
      AksArrPush(ls_ra, ValFloat(ls_x[i]));
    free(ls_m);
    free(ls_v);
    free(ls_x);
    ValFree(&BV4);
    ValFree(&AV6);
    PUSH(ls_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GaussQuadC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: GaussQuadC() expects 3 args (xs_arr, "
                      "ys_arr, n_segments)\n");
      return -1;
    }
    AKSValue nv = POP(), yav = POP(), xav = POP();
    AKSArray *gq_xs = ValToArr(xav), *gq_ys = ValToArr(yav);
    int gq_n = (int)gq_xs->count < (int)gq_ys->count ? (int)gq_xs->count
                                                     : (int)gq_ys->count;
    (void)nv;
    double gq_sum = 0.0;
    for (int i = 0; i < gq_n - 1; i++) {
      double gq_x0 = ValToFloat(gq_xs->items[i]),
             gq_x1 = ValToFloat(gq_xs->items[i + 1]);
      double gq_y0 = ValToFloat(gq_ys->items[i]),
             gq_y1 = ValToFloat(gq_ys->items[i + 1]);
      gq_sum += (gq_x1 - gq_x0) * (gq_y0 + gq_y1) * 0.5;
    }
    ValFree(&nv);
    ValFree(&yav);
    ValFree(&xav);
    PUSH(ValFloat(gq_sum));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128LogC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Complex128LogC() expects 2 args (re, im)\n");
      return -1;
    }
    AKSValue IV3 = POP(), RV2 = POP();
    double cx_re = ValToFloat(RV2), cx_im = ValToFloat(IV3);
    double cx_r = sqrt(cx_re * cx_re + cx_im * cx_im);
    double cx_lre = log(cx_r), cx_lim = atan2(cx_im, cx_re);
    AKSValue cx_res = ValArr(2);
    AKSArray *cx_a = ValToArr(cx_res);
    AksArrPush(cx_a, ValFloat(cx_lre));
    AksArrPush(cx_a, ValFloat(cx_lim));
    ValFree(&IV3);
    ValFree(&RV2);
    PUSH(cx_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Complex128PowC") == 0) {
    if (argc != 4) {
      fprintf(
          stderr,
          "Runtime error: Complex128PowC() expects 4 args (re,im,pre,pim)\n");
      return -1;
    }
    AKSValue pi2 = POP(), pr2 = POP(), im2 = POP(), re2 = POP();
    double cx_re = ValToFloat(re2), cx_im = ValToFloat(im2);
    double cx_pre = ValToFloat(pr2), cx_pim = ValToFloat(pi2);
    double cx_r = sqrt(cx_re * cx_re + cx_im * cx_im),
           cx_th = atan2(cx_im, cx_re);
    if (cx_r < 1e-300) {
      ValFree(&pi2);
      ValFree(&pr2);
      ValFree(&im2);
      ValFree(&re2);
      PUSH(ValArr(2));
      *stacktopptr = stacktop;
      return 1;
    }
    double cx_lr = log(cx_r);
    double cx_nre = cx_pre * cx_lr - cx_pim * cx_th;
    double cx_nim = cx_pre * cx_th + cx_pim * cx_lr;
    double cx_rexp = exp(cx_nre);
    AKSValue cx_res = ValArr(2);
    AKSArray *cx_a = ValToArr(cx_res);
    AksArrPush(cx_a, ValFloat(cx_rexp * cos(cx_nim)));
    AksArrPush(cx_a, ValFloat(cx_rexp * sin(cx_nim)));
    ValFree(&pi2);
    ValFree(&pr2);
    ValFree(&im2);
    ValFree(&re2);
    PUSH(cx_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdOp") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SimdOp() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue op_val = POP();
    AKSValue arr2_val = POP();
    AKSValue arr1_val = POP();
    int op = (int)ValToInt(op_val);
    AKSArray *a1 = ValToArr(arr1_val);
    AKSArray *a2 = ValToArr(arr2_val);
    int N = a1->count < a2->count ? a1->count : a2->count;
    AKSValue res = ValArr(N);
    AKSArray *res_arr = ValToArr(res);
    res_arr->count = N;
    SimdOpArrays(a1, a2, res_arr, N, op);
    PUSH(res);
    ValFree(&op_val);
    ValFree(&arr2_val);
    ValFree(&arr1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdFma") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SimdFma() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arr3_val = POP();
    AKSValue arr2_val = POP();
    AKSValue arr1_val = POP();
    AKSArray *a1 = ValToArr(arr1_val);
    AKSArray *a2 = ValToArr(arr2_val);
    AKSArray *a3 = ValToArr(arr3_val);
    int N = a1->count;
    if (a2->count < N)
      N = a2->count;
    if (a3->count < N)
      N = a3->count;
    AKSValue res = ValArr(N);
    AKSArray *res_arr = ValToArr(res);
    res_arr->count = N;
    SimdFmaArrays(a1, a2, a3, res_arr, N);
    PUSH(res);
    ValFree(&arr3_val);
    ValFree(&arr2_val);
    ValFree(&arr1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdDot") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SimdDot() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arr2_val = POP();
    AKSValue arr1_val = POP();
    AKSArray *a1 = ValToArr(arr1_val);
    AKSArray *a2 = ValToArr(arr2_val);
    int N = a1->count < a2->count ? a1->count : a2->count;
    double sum = 0.0;
    int i = 0;
    for (; i + 3 < N; i += 4) {
      sum += ValToFloat(a1->items[i]) * ValToFloat(a2->items[i]);
      sum += ValToFloat(a1->items[i + 1]) * ValToFloat(a2->items[i + 1]);
      sum += ValToFloat(a1->items[i + 2]) * ValToFloat(a2->items[i + 2]);
      sum += ValToFloat(a1->items[i + 3]) * ValToFloat(a2->items[i + 3]);
    }
    for (; i < N; i++) {
      sum += ValToFloat(a1->items[i]) * ValToFloat(a2->items[i]);
    }
    PUSH(ValFloat(sum));
    ValFree(&arr2_val);
    ValFree(&arr1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdSum") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdSum() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arr_val = POP();
    AKSArray *arr = ValToArr(arr_val);
    int N = arr->count;
    double sum = 0.0;
    int i = 0;
    for (; i + 3 < N; i += 4) {
      sum += ValToFloat(arr->items[i]) + ValToFloat(arr->items[i + 1]) +
             ValToFloat(arr->items[i + 2]) + ValToFloat(arr->items[i + 3]);
    }
    for (; i < N; i++) {
      sum += ValToFloat(arr->items[i]);
    }
    PUSH(ValFloat(sum));
    ValFree(&arr_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MatrixMulRaw") == 0) {
    if (argc != 6) {
      fprintf(stderr,
              "Runtime error: MatrixMulRaw() expects 6 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue d2_val = POP();
    AKSValue c2_val = POP();
    AKSValue r2_val = POP();
    AKSValue d1_val = POP();
    AKSValue c1_val = POP();
    AKSValue r1_val = POP();

    int r1 = (int)ValToInt(r1_val);
    int c1 = (int)ValToInt(c1_val);
    int r2 = (int)ValToInt(r2_val);
    int c2 = (int)ValToInt(c2_val);
    AKSArray *a1 = ValToArr(d1_val);
    AKSArray *a2 = ValToArr(d2_val);

    double *d1 = malloc(a1->count * sizeof(double));
    for (int i = 0; i < a1->count; i++)
      d1[i] = ValToFloat(a1->items[i]);
    double *d2 = malloc(a2->count * sizeof(double));
    for (int i = 0; i < a2->count; i++)
      d2[i] = ValToFloat(a2->items[i]);

    double *res_raw = malloc(r1 * c2 * sizeof(double));
    MatrixMulC(r1, c1, d1, r2, c2, d2, res_raw);

    AKSValue res_val = ValArr(r1 * c2);
    AKSArray *a_res = ValToArr(res_val);
    a_res->count = r1 * c2;
    for (int i = 0; i < r1 * c2; i++)
      a_res->items[i] = ValFloat(res_raw[i]);

    PUSH(res_val);
    free(d1);
    free(d2);
    free(res_raw);
    ValFree(&d2_val);
    ValFree(&c2_val);
    ValFree(&r2_val);
    ValFree(&d1_val);
    ValFree(&c1_val);
    ValFree(&r1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MatrixTransposeRaw") == 0) {
    if (argc != 3) {
      fprintf(
          stderr,
          "Runtime error: MatrixTransposeRaw() expects 3 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue d_val = POP();
    AKSValue c_val = POP();
    AKSValue r_val = POP();
    int r = (int)ValToInt(r_val);
    int c = (int)ValToInt(c_val);
    AKSArray *a = ValToArr(d_val);

    AKSValue res_val = ValArr(r * c);
    AKSArray *a_res = ValToArr(res_val);
    a_res->count = r * c;
    for (int i = 0; i < r; i++) {
      for (int j = 0; j < c; j++) {
        a_res->items[j * r + i] = ValCopy(a->items[i * c + j]);
      }
    }
    PUSH(res_val);
    ValFree(&d_val);
    ValFree(&c_val);
    ValFree(&r_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MatrixInverseRaw") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: MatrixInverseRaw() expects 2 arguments\n");
      return -1;
    }
    AKSValue d_val = POP();
    AKSValue n_val = POP();
    int n = (int)ValToInt(n_val);
    AKSArray *a = ValToArr(d_val);

    double *da = malloc(n * n * sizeof(double));
    for (int i = 0; i < n * n; i++)
      da[i] = ValToFloat(a->items[i]);
    double *dres = malloc(n * n * sizeof(double));

    int ok = MatrixInverseC(n, da, dres);
    if (!ok) {
      PUSH(ValNull());
    } else {
      AKSValue res_val = ValArr(n * n);
      AKSArray *a_res = ValToArr(res_val);
      a_res->count = n * n;
      for (int i = 0; i < n * n; i++)
        a_res->items[i] = ValFloat(dres[i]);
      PUSH(res_val);
    }
    free(da);
    free(dres);
    ValFree(&d_val);
    ValFree(&n_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MatrixLuRaw") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MatrixLuRaw() expects 2 arguments\n");
      return -1;
    }
    AKSValue d_val = POP();
    AKSValue n_val = POP();
    int n = (int)ValToInt(n_val);
    AKSArray *a = ValToArr(d_val);

    double *da = malloc(n * n * sizeof(double));
    for (int i = 0; i < n * n; i++)
      da[i] = ValToFloat(a->items[i]);
    double *dL = malloc(n * n * sizeof(double));
    double *dU = malloc(n * n * sizeof(double));
    double *dP = malloc(n * sizeof(double));

    MatrixLuC(n, da, dL, dU, dP);

    AKSValue valL = ValArr(n * n);
    AKSArray *aL = ValToArr(valL);
    aL->count = n * n;
    for (int i = 0; i < n * n; i++)
      aL->items[i] = ValFloat(dL[i]);

    AKSValue valU = ValArr(n * n);
    AKSArray *aU = ValToArr(valU);
    aU->count = n * n;
    for (int i = 0; i < n * n; i++)
      aU->items[i] = ValFloat(dU[i]);

    AKSValue valP = ValArr(n);
    AKSArray *aP = ValToArr(valP);
    aP->count = n;
    for (int i = 0; i < n; i++)
      aP->items[i] = ValInt((int64_t)dP[i]);

    AKSValue res_arr = ValArr(3);
    AKSArray *a_res = ValToArr(res_arr);
    a_res->count = 3;
    a_res->items[0] = valL;
    a_res->items[1] = valU;
    a_res->items[2] = valP;

    PUSH(res_arr);
    free(da);
    free(dL);
    free(dU);
    free(dP);
    ValFree(&d_val);
    ValFree(&n_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MatrixCholeskyRaw") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: MatrixCholeskyRaw() expects 2 arguments\n");
      return -1;
    }
    AKSValue d_val = POP();
    AKSValue n_val = POP();
    int n = (int)ValToInt(n_val);
    AKSArray *a = ValToArr(d_val);

    double *da = malloc(n * n * sizeof(double));
    for (int i = 0; i < n * n; i++)
      da[i] = ValToFloat(a->items[i]);
    double *dL = malloc(n * n * sizeof(double));

    int ok = MatrixCholeskyC(n, da, dL);
    if (!ok) {
      PUSH(ValNull());
    } else {
      AKSValue valL = ValArr(n * n);
      AKSArray *aL = ValToArr(valL);
      aL->count = n * n;
      for (int i = 0; i < n * n; i++)
        aL->items[i] = ValFloat(dL[i]);
      PUSH(valL);
    }
    free(da);
    free(dL);
    ValFree(&d_val);
    ValFree(&n_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FftRaw") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: FftRaw() expects 3 arguments\n");
      return -1;
    }
    AKSValue inv_val = POP();
    AKSValue imag_val = POP();
    AKSValue real_val = POP();
    int inverse = ValToBool(inv_val);
    AKSArray *areal = ValToArr(real_val);
    AKSArray *aimag = ValToArr(imag_val);
    int n = areal->count;

    double *dreal = malloc(n * sizeof(double));
    double *dimag = malloc(n * sizeof(double));
    for (int i = 0; i < n; i++) {
      dreal[i] = ValToFloat(areal->items[i]);
      dimag[i] = ValToFloat(aimag->items[i]);
    }

    FFTC(dreal, dimag, n, inverse);

    AKSValue valreal = ValArr(n);
    AKSArray *a_r = ValToArr(valreal);
    a_r->count = n;
    AKSValue valimag = ValArr(n);
    AKSArray *a_i = ValToArr(valimag);
    a_i->count = n;
    for (int i = 0; i < n; i++) {
      a_r->items[i] = ValFloat(dreal[i]);
      a_i->items[i] = ValFloat(dimag[i]);
    }

    AKSValue res_arr = ValArr(2);
    AKSArray *a_res = ValToArr(res_arr);
    a_res->count = 2;
    a_res->items[0] = valreal;
    a_res->items[1] = valimag;
    PUSH(res_arr);

    free(dreal);
    free(dimag);
    ValFree(&inv_val);
    ValFree(&imag_val);
    ValFree(&real_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BigIntAdd") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BigIntAdd() expects 2 arguments\n");
      return -1;
    }
    AKSValue s2_val = POP();
    AKSValue s1_val = POP();
    char *r = BigIntAddC(ValToStrPtr(s1_val)->data, ValToStrPtr(s2_val)->data);
    PUSH(ValStr(r));
    free(r);
    ValFree(&s2_val);
    ValFree(&s1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BigIntSub") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BigIntSub() expects 2 arguments\n");
      return -1;
    }
    AKSValue s2_val = POP();
    AKSValue s1_val = POP();
    char *r = BigIntSubC(ValToStrPtr(s1_val)->data, ValToStrPtr(s2_val)->data);
    PUSH(ValStr(r));
    free(r);
    ValFree(&s2_val);
    ValFree(&s1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BigIntMul") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BigIntMul() expects 2 arguments\n");
      return -1;
    }
    AKSValue s2_val = POP();
    AKSValue s1_val = POP();
    char *r = BigIntMulC(ValToStrPtr(s1_val)->data, ValToStrPtr(s2_val)->data);
    PUSH(ValStr(r));
    free(r);
    ValFree(&s2_val);
    ValFree(&s1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BigIntDiv") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BigIntDiv() expects 2 arguments\n");
      return -1;
    }
    AKSValue s2_val = POP();
    AKSValue s1_val = POP();
    char *rem = NULL;
    char *q =
        BigIntDivC(ValToStrPtr(s1_val)->data, ValToStrPtr(s2_val)->data, &rem);

    AKSValue res_arr = ValArr(2);
    AKSArray *a_res = ValToArr(res_arr);
    a_res->count = 2;
    a_res->items[0] = ValStr(q);
    a_res->items[1] = ValStr(rem);
    PUSH(res_arr);

    free(q);
    free(rem);
    ValFree(&s2_val);
    ValFree(&s1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BigIntCmp") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: BigIntCmp() expects 2 arguments\n");
      return -1;
    }
    AKSValue s2_val = POP();
    AKSValue s1_val = POP();
    int r = BigIntCmpC(ValToStrPtr(s1_val)->data, ValToStrPtr(s2_val)->data);
    PUSH(ValInt(r));
    ValFree(&s2_val);
    ValFree(&s1_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdAddRaw") == 0 || strcmp(name, "SimdMulRaw") == 0 ||
      strcmp(name, "SimdSubRaw") == 0 || strcmp(name, "SimdDivRaw") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: %s expects 2 array arguments, got %d\n",
              name, argc);
      return -1;
    }
    AKSValue arg2 = POP();
    AKSValue arg1 = POP();
    if (ValKind(arg1) != VALARR || ValKind(arg2) != VALARR) {
      fprintf(stderr, "Runtime error: %s expects arguments to be arrays\n",
              name);
      ValFree(&arg1);
      ValFree(&arg2);
      return -1;
    }
    if (ValToArr(arg1)->count != ValToArr(arg2)->count) {
      fprintf(stderr, "Runtime error: %s expects arrays of matching size\n",
              name);
      ValFree(&arg1);
      ValFree(&arg2);
      return -1;
    }
    int size = ValToArr(arg1)->count;
    AKSValue resultval = ValArr(size);
    ValToArr(resultval)->count = size;

    // =add, 1=mul, 2=sub, 3=div
    int simdop = 0;
    if (strcmp(name, "SimdMulRaw") == 0)
      simdop = 1;
    else if (strcmp(name, "SimdSubRaw") == 0)
      simdop = 2;
    else if (strcmp(name, "SimdDivRaw") == 0)
      simdop = 3;

    double *adata = malloc(size * sizeof(double));
    double *bdata = malloc(size * sizeof(double));
    double *cdata = malloc(size * sizeof(double));

    for (int i = 0; i < size; i++) {
      AKSValue va = ValToArr(arg1)->items[i];
      AKSValue vb = ValToArr(arg2)->items[i];
      adata[i] =
          ValKind(va) == VALFLOAT ? ValToFloat(va) : (double)ValToInt(va);
      bdata[i] =
          ValKind(vb) == VALFLOAT ? ValToFloat(vb) : (double)ValToInt(vb);
    }

    int simd_pos = 0;
#if defined(__AVX2__) || defined(_M_X64)
    int avxlimit = (size / 4) * 4;
    for (int i = 0; i < avxlimit; i += 4) {
      __m256d va = _mm256_loadu_pd(&adata[i]);
      __m256d vb = _mm256_loadu_pd(&bdata[i]);
      __m256d vc;
      switch (simdop) {
      case 1:
        vc = _mm256_mul_pd(va, vb);
        break;
      case 2:
        vc = _mm256_sub_pd(va, vb);
        break;
      case 3:
        vc = _mm256_div_pd(va, vb);
        break;
      default:
        vc = _mm256_add_pd(va, vb);
        break;
      }
      _mm256_storeu_pd(&cdata[i], vc);
    }
    simd_pos = avxlimit;
#endif

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
    int sselimit = ((size - simd_pos) / 2) * 2 + simd_pos;
    for (int i = simd_pos; i < sselimit; i += 2) {
      __m128d va = _mm_loadu_pd(&adata[i]);
      __m128d vb = _mm_loadu_pd(&bdata[i]);
      __m128d vc;
      switch (simdop) {
      case 1:
        vc = _mm_mul_pd(va, vb);
        break;
      case 2:
        vc = _mm_sub_pd(va, vb);
        break;
      case 3:
        vc = _mm_div_pd(va, vb);
        break;
      default:
        vc = _mm_add_pd(va, vb);
        break;
      }
      _mm_storeu_pd(&cdata[i], vc);
    }
    simd_pos = sselimit;
#endif

    for (int i = simd_pos; i < size; i++) {
      switch (simdop) {
      case 1:
        cdata[i] = adata[i] * bdata[i];
        break;
      case 2:
        cdata[i] = adata[i] - bdata[i];
        break;
      case 3:
        cdata[i] = adata[i] / bdata[i];
        break;
      default:
        cdata[i] = adata[i] + bdata[i];
        break;
      }
    }

    for (int i = 0; i < size; i++) {
      ValToArr(resultval)->items[i] = ValFloat(cdata[i]);
    }

    free(adata);
    free(bdata);
    free(cdata);

    PUSH(resultval);
    ValFree(&arg1);
    ValFree(&arg2);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SquareRoot") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SquareRoot() expects 1 argument, got %d\n", argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(sqrt(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Absolute") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Absolute() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) == VALINT) {
      PUSH(ValInt(ValToInt(arg) < 0 ? -ValToInt(arg) : ValToInt(arg)));
    } else if (ValKind(arg) == VALFLOAT) {
      PUSH(ValFloat(ValToFloat(arg) < 0 ? -ValToFloat(arg) : ValToFloat(arg)));
    } else {
      PUSH(ValCopy(arg));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Power") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: Power() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue y = POP();
    AKSValue x = POP();
    double dx = ValKind(x) == VALFLOAT ? ValToFloat(x) : (double)ValToInt(x);
    double dy = ValKind(y) == VALFLOAT ? ValToFloat(y) : (double)ValToInt(y);
    PUSH(ValFloat(pow(dx, dy)));
    ValFree(&x);
    ValFree(&y);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "sqrt") == 0 || strcmp(name, "Sqrt") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: sqrt() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(sqrt(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "sin") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: sin() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(sin(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "cos") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: cos() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(cos(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "tan") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: tan() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(tan(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "log") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: log() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(log(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Ceiling") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Ceiling() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValInt((int64_t)ceil(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "floor") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: floor() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValInt((int64_t)floor(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Minimum") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: Minimum() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValInt(ValToInt(a) < ValToInt(b) ? ValToInt(a) : ValToInt(b)));
    } else {
      double da = ValKind(a) == VALFLOAT ? ValToFloat(a) : (double)ValToInt(a);
      double db = ValKind(b) == VALFLOAT ? ValToFloat(b) : (double)ValToInt(b);
      PUSH(ValFloat(da < db ? da : db));
    }
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Maximum") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: Maximum() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValInt(ValToInt(a) > ValToInt(b) ? ValToInt(a) : ValToInt(b)));
    } else {
      double da = ValKind(a) == VALFLOAT ? ValToFloat(a) : (double)ValToInt(a);
      double db = ValKind(b) == VALFLOAT ? ValToFloat(b) : (double)ValToInt(b);
      PUSH(ValFloat(da > db ? da : db));
    }
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Random") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: Random() expects 0 arguments, got %d\n",
              argc);
      return -1;
    }
    double r = (double)rand() / RAND_MAX;
    PUSH(ValFloat(r));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "round") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: round() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValInt((int64_t)round(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
#define MATHUNARY(namestr, func)                                               \
  if (strcmp(name, namestr) == 0) {                                            \
    if (argc != 1) {                                                           \
      fprintf(stderr, "Runtime error: %s() expects 1 argument, got %d\n",      \
              namestr, argc);                                                  \
      return -1;                                                               \
    }                                                                          \
    AKSValue arg = POP();                                                      \
    double vald =                                                              \
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);    \
    PUSH(ValFloat(func(vald)));                                                \
    ValFree(&arg);                                                             \
    *stacktopptr = stacktop;                                                   \
    return 1;                                                                  \
  }

  MATHUNARY("asin", asin)
  MATHUNARY("acos", acos)
  MATHUNARY("atan", atan)
  MATHUNARY("sinh", sinh)
  MATHUNARY("cosh", cosh)
  MATHUNARY("tanh", tanh)
#undef MATHUNARY

  if (strcmp(name, "atan2") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: atan2() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue x = POP();
    AKSValue y = POP();
    double dy = ValKind(y) == VALFLOAT ? ValToFloat(y) : (double)ValToInt(y);
    double dx = ValKind(x) == VALFLOAT ? ValToFloat(x) : (double)ValToInt(x);
    PUSH(ValFloat(atan2(dy, dx)));
    ValFree(&y);
    ValFree(&x);
    *stacktopptr = stacktop;
    return 1;
  }
#define MATHUNARYEXT(namestr, func)                                            \
  if (strcmp(name, namestr) == 0) {                                            \
    if (argc != 1) {                                                           \
      fprintf(stderr, "Runtime error: %s() expects 1 argument, got %d\n",      \
              namestr, argc);                                                  \
      return -1;                                                               \
    }                                                                          \
    AKSValue arg = POP();                                                      \
    double vald =                                                              \
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);    \
    PUSH(ValFloat(func(vald)));                                                \
    ValFree(&arg);                                                             \
    *stacktopptr = stacktop;                                                   \
    return 1;                                                                  \
  }

  MATHUNARYEXT("asinh", asinh)
  MATHUNARYEXT("acosh", acosh)
  MATHUNARYEXT("atanh", atanh)
  MATHUNARYEXT("exp", exp)
  MATHUNARYEXT("log10", log10)
  MATHUNARYEXT("log2", log2)
  MATHUNARYEXT("cbrt", cbrt)
  MATHUNARYEXT("trunc", trunc)
  MATHUNARYEXT("erf", erf)
  MATHUNARYEXT("erfc", erfc)
  MATHUNARYEXT("tgamma", tgamma)
  MATHUNARYEXT("lgamma", lgamma)
#undef MATHUNARYEXT

  if (strcmp(name, "Hypotenuse") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Hypotenuse() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue y = POP();
    AKSValue x = POP();
    double dx = ValKind(x) == VALFLOAT ? ValToFloat(x) : (double)ValToInt(x);
    double dy = ValKind(y) == VALFLOAT ? ValToFloat(y) : (double)ValToInt(y);
    PUSH(ValFloat(hypot(dx, dy)));
    ValFree(&x);
    ValFree(&y);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GreatestCommonDivisor") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: GreatestCommonDivisor() expects 2 arguments, got "
              "%d\n",
              argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    int64_t ia = ValKind(a) == VALFLOAT ? (int64_t)ValToFloat(a) : ValToInt(a);
    int64_t ib = ValKind(b) == VALFLOAT ? (int64_t)ValToFloat(b) : ValToInt(b);
    PUSH(ValInt(MathGcd(ia, ib)));
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "LeastCommonMultiple") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: LeastCommonMultiple() expects 2 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    int64_t ia = ValKind(a) == VALFLOAT ? (int64_t)ValToFloat(a) : ValToInt(a);
    int64_t ib = ValKind(b) == VALFLOAT ? (int64_t)ValToFloat(b) : ValToInt(b);
    PUSH(ValInt(MathLcm(ia, ib)));
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Factorial") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Factorial() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(MathFact(vald)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "nPr") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: nPr() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue r = POP();
    AKSValue n = POP();
    double dn = ValKind(n) == VALFLOAT ? ValToFloat(n) : (double)ValToInt(n);
    double dr = ValKind(r) == VALFLOAT ? ValToFloat(r) : (double)ValToInt(r);
    PUSH(ValFloat(MathNpr(dn, dr)));
    ValFree(&n);
    ValFree(&r);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "nCr") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: nCr() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue r = POP();
    AKSValue n = POP();
    double dn = ValKind(n) == VALFLOAT ? ValToFloat(n) : (double)ValToInt(n);
    double dr = ValKind(r) == VALFLOAT ? ValToFloat(r) : (double)ValToInt(r);
    PUSH(ValFloat(MathNcr(dn, dr)));
    ValFree(&n);
    ValFree(&r);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "IsPrime") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: IsPrime() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    int64_t vali =
        ValKind(arg) == VALFLOAT ? (int64_t)ValToFloat(arg) : ValToInt(arg);
    PUSH(ValBool(MathIsPrime(vali)));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ToRadians") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ToRadians() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(vald * 3.14159265358979323846 / 180.0));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ToDegrees") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ToDegrees() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    double vald =
        ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
    PUSH(ValFloat(vald * 180.0 / 3.14159265358979323846));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "mean") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: mean() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALARR) {
      fprintf(stderr, "Runtime error: mean() argument must be an array\n");
      ValFree(&arg);
      return -1;
    }
    if (ValToArr(arg)->count == 0) {
      PUSH(ValFloat(0.0));
    } else {
      double sum = 0.0;
      for (int i = 0; i < ValToArr(arg)->count; i++) {
        AKSValue item = ValToArr(arg)->items[i];
        sum += ValKind(item) == VALFLOAT ? ValToFloat(item)
                                         : (double)ValToInt(item);
      }
      PUSH(ValFloat(sum / ValToArr(arg)->count));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "median") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: median() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALARR) {
      fprintf(stderr, "Runtime error: median() argument must be an array\n");
      ValFree(&arg);
      return -1;
    }
    if (ValToArr(arg)->count == 0) {
      PUSH(ValFloat(0.0));
    } else {
      int cnt = ValToArr(arg)->count;
      AKSValue *copy = malloc(cnt * sizeof(AKSValue));
      for (int i = 0; i < cnt; i++) {
        copy[i] = ValCopy(ValToArr(arg)->items[i]);
      }
      qsort(copy, cnt, sizeof(AKSValue), CompareAKSVals);
      if (cnt % 2 == 1) {
        AKSValue mid = copy[cnt / 2];
        PUSH(ValFloat(ValKind(mid) == VALFLOAT ? ValToFloat(mid)
                                               : (double)ValToInt(mid)));
      } else {
        AKSValue mid1 = copy[cnt / 2 - 1];
        AKSValue mid2 = copy[cnt / 2];
        double d1 = ValKind(mid1) == VALFLOAT ? ValToFloat(mid1)
                                              : (double)ValToInt(mid1);
        double d2 = ValKind(mid2) == VALFLOAT ? ValToFloat(mid2)
                                              : (double)ValToInt(mid2);
        PUSH(ValFloat((d1 + d2) / 2.0));
      }
      for (int i = 0; i < cnt; i++)
        ValFree(&copy[i]);
      free(copy);
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "variance") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: variance() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALARR) {
      fprintf(stderr, "Runtime error: variance() argument must be an array\n");
      ValFree(&arg);
      return -1;
    }
    if (ValToArr(arg)->count == 0) {
      PUSH(ValFloat(0.0));
    } else {
      double sum = 0.0;
      for (int i = 0; i < ValToArr(arg)->count; i++) {
        AKSValue item = ValToArr(arg)->items[i];
        sum += ValKind(item) == VALFLOAT ? ValToFloat(item)
                                         : (double)ValToInt(item);
      }
      double meanval = sum / ValToArr(arg)->count;
      double sqdiffsum = 0.0;
      for (int i = 0; i < ValToArr(arg)->count; i++) {
        AKSValue item = ValToArr(arg)->items[i];
        double vald = ValKind(item) == VALFLOAT ? ValToFloat(item)
                                                : (double)ValToInt(item);
        sqdiffsum += (vald - meanval) * (vald - meanval);
      }
      PUSH(ValFloat(sqdiffsum / ValToArr(arg)->count));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StandardDeviation") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: StandardDeviation() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALARR) {
      fprintf(stderr,
              "Runtime error: StandardDeviation() argument must be an array\n");
      ValFree(&arg);
      return -1;
    }
    if (ValToArr(arg)->count == 0) {
      PUSH(ValFloat(0.0));
    } else {
      double sum = 0.0;
      for (int i = 0; i < ValToArr(arg)->count; i++) {
        AKSValue item = ValToArr(arg)->items[i];
        sum += ValKind(item) == VALFLOAT ? ValToFloat(item)
                                         : (double)ValToInt(item);
      }
      double meanval = sum / ValToArr(arg)->count;
      double sqdiffsum = 0.0;
      for (int i = 0; i < ValToArr(arg)->count; i++) {
        AKSValue item = ValToArr(arg)->items[i];
        double vald = ValKind(item) == VALFLOAT ? ValToFloat(item)
                                                : (double)ValToInt(item);
        sqdiffsum += (vald - meanval) * (vald - meanval);
      }
      PUSH(ValFloat(sqrt(sqdiffsum / ValToArr(arg)->count)));
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastDotProduct") == 0) {
    if (argc < 2) {
      fprintf(stderr,
              "Runtime error: FastDotProduct expects 2 array arguments\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue a_val = POP();
    if (ValKind(a_val) != VALARR || ValKind(b_val) != VALARR) {
      ValFree(&a_val);
      ValFree(&b_val);
      fprintf(stderr,
              "Runtime error: FastDotProduct expects array arguments\n");
      return -1;
    }
    AKSArray *arrA = ValToArr(a_val);
    AKSArray *arrB = ValToArr(b_val);
    int n = arrA->count < arrB->count ? arrA->count : arrB->count;

    double total = 0.0;
    int i = 0;
#if defined(__AVX2__) || defined(_M_X64)
    __m256d acc0 = _mm256_setzero_pd();
    __m256d acc1 = _mm256_setzero_pd();

    for (; i + 7 < n; i += 8) {
      double d0[4], d1[4], e0[4], e1[4];
      for (int k = 0; k < 4; k++) {
        AKSValue vA = arrA->items[i + k];
        d0[k] =
            (ValKind(vA) == VALFLOAT) ? ValToFloat(vA) : (double)ValToInt(vA);
        AKSValue vB = arrB->items[i + k];
        e0[k] =
            (ValKind(vB) == VALFLOAT) ? ValToFloat(vB) : (double)ValToInt(vB);
      }
      for (int k = 0; k < 4; k++) {
        AKSValue vA = arrA->items[i + 4 + k];
        d1[k] =
            (ValKind(vA) == VALFLOAT) ? ValToFloat(vA) : (double)ValToInt(vA);
        AKSValue vB = arrB->items[i + 4 + k];
        e1[k] =
            (ValKind(vB) == VALFLOAT) ? ValToFloat(vB) : (double)ValToInt(vB);
      }
#if defined(_WIN32) || defined(__FMA__)
      acc0 = _mm256_fmadd_pd(_mm256_loadu_pd(d0), _mm256_loadu_pd(e0), acc0);
      acc1 = _mm256_fmadd_pd(_mm256_loadu_pd(d1), _mm256_loadu_pd(e1), acc1);
#else
      acc0 = _mm256_add_pd(_mm256_mul_pd(_mm256_loadu_pd(d0), _mm256_loadu_pd(e0)), acc0);
      acc1 = _mm256_add_pd(_mm256_mul_pd(_mm256_loadu_pd(d1), _mm256_loadu_pd(e1)), acc1);
#endif
    }
    __m256d sum_acc = _mm256_add_pd(acc0, acc1);
    double res_lanes[4];
    _mm256_storeu_pd(res_lanes, sum_acc);
    total = res_lanes[0] + res_lanes[1] + res_lanes[2] + res_lanes[3];
#endif
    for (; i < n; i++) {
      AKSValue va = arrA->items[i];
      AKSValue vb = arrB->items[i];
      double a =
          (ValKind(va) == VALFLOAT) ? ValToFloat(va) : (double)ValToInt(va);
      double b =
          (ValKind(vb) == VALFLOAT) ? ValToFloat(vb) : (double)ValToInt(vb);
      total += a * b;
    }

    ValFree(&a_val);
    ValFree(&b_val);
    PUSH(ValFloat(total));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastMatMul2D") == 0) {
    if (argc < 2) {
      fprintf(stderr,
              "Runtime error: FastMatMul2D expects 2 matrix arguments\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue a_val = POP();
    if (ValKind(a_val) != VALARR || ValKind(b_val) != VALARR) {
      ValFree(&a_val);
      ValFree(&b_val);
      fprintf(stderr, "Runtime error: FastMatMul2D expects array arguments\n");
      return -1;
    }
    AKSArray *A = ValToArr(a_val);
    AKSArray *B = ValToArr(b_val);
    int rowsA = A->count;
    int colsA = (rowsA > 0 && ValKind(A->items[0]) == VALARR)
                    ? ValToArr(A->items[0])->count
                    : 0;
    int rowsB = B->count;
    int colsB = (rowsB > 0 && ValKind(B->items[0]) == VALARR)
                    ? ValToArr(B->items[0])->count
                    : 0;

    if (colsA != rowsB || rowsA == 0 || colsB == 0) {
      ValFree(&a_val);
      ValFree(&b_val);
      fprintf(stderr,
              "DimensionError: matrix dimensions incompatible for "
              "multiplication (%dx%d vs %dx%d)\n",
              rowsA, colsA, rowsB, colsB);
      return -1;
    }

    /* Extract dense contiguous buffers for cache locality */
    double *denseA = (double *)malloc(rowsA * colsA * sizeof(double));
    if (!denseA) {
      ValFree(&a_val);
      ValFree(&b_val);
      return -1;
    }
    for (int r = 0; r < rowsA; r++) {
      AKSArray *row = ValToArr(A->items[r]);
      for (int c = 0; c < colsA; c++) {
        AKSValue v = row->items[c];
        denseA[r * colsA + c] =
            (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      }
    }

    /* Transpose matrix B into denseBT (colsB x rowsB) to ensure streaming
     * contiguous reads */
    double *denseBT = (double *)malloc(colsB * rowsB * sizeof(double));
    if (!denseBT) {
      free(denseA);
      ValFree(&a_val);
      ValFree(&b_val);
      return -1;
    }
    for (int r = 0; r < rowsB; r++) {
      AKSArray *row = ValToArr(B->items[r]);
      for (int c = 0; c < colsB; c++) {
        AKSValue v = row->items[c];
        denseBT[c * rowsB + r] =
            (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      }
    }

    /* Create destination array */
    AKSValue C_val = ValArr(rowsA);
    AKSArray *C = ValToArr(C_val);

    for (int i = 0; i < rowsA; i++) {
      AKSValue row_val = ValArr(colsB);
      AKSArray *row = ValToArr(row_val);
      const double *a_row = &denseA[i * colsA];

      for (int j = 0; j < colsB; j++) {
        const double *b_col = &denseBT[j * rowsB];
        double sum = 0.0;
        int k = 0;
#if defined(__AVX2__) || defined(_M_X64)
        __m256d acc = _mm256_setzero_pd();
        for (; k + 3 < colsA; k += 4) {
          __m256d va = _mm256_loadu_pd(&a_row[k]);
          __m256d vb = _mm256_loadu_pd(&b_col[k]);
#if defined(_WIN32) || defined(__FMA__)
          acc = _mm256_fmadd_pd(va, vb, acc);
#else
          acc = _mm256_add_pd(_mm256_mul_pd(va, vb), acc);
#endif
        }
        double lanes[4];
        _mm256_storeu_pd(lanes, acc);
        sum = lanes[0] + lanes[1] + lanes[2] + lanes[3];
#endif
        for (; k < colsA; k++) {
          sum += a_row[k] * b_col[k];
        }
        row->items[j] = ValFloat(sum);
      }
      row->count = colsB;
      C->items[i] = row_val;
    }
    C->count = rowsA;

    free(denseA);
    free(denseBT);
    ValFree(&a_val);
    ValFree(&b_val);

    PUSH(C_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastArraySort") == 0) {
    if (argc < 1) {
      fprintf(stderr, "Runtime error: FastArraySort expects an array\n");
      return -1;
    }
    AKSValue arr_val = POP();
    if (ValKind(arr_val) != VALARR) {
      ValFree(&arr_val);
      fprintf(stderr,
              "Runtime error: FastArraySort expects an array argument\n");
      return -1;
    }
    AKSArray *arr = ValToArr(arr_val);
    int count = arr->count;
    if (count <= 1) {
      PUSH(arr_val);
      *stacktopptr = stacktop;
      return 1;
    }

    double *dense = (double *)malloc(count * sizeof(double));
    if (!dense) {
      PUSH(arr_val);
      *stacktopptr = stacktop;
      return 1;
    }
    for (int i = 0; i < count; i++) {
      AKSValue v = arr->items[i];
      dense[i] = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
    }

    /* Dual-pivot quicksort with insertion sort for small partitions */
    int stack_low[64];
    int stack_high[64];
    int top = 0;
    stack_low[top] = 0;
    stack_high[top] = count - 1;
    top++;

    while (top > 0) {
      top--;
      int low = stack_low[top];
      int high = stack_high[top];
      if (high <= low)
        continue;

      if (high - low < 16) {
        for (int i = low + 1; i <= high; i++) {
          double key = dense[i];
          int j = i - 1;
          while (j >= low && dense[j] > key) {
            dense[j + 1] = dense[j];
            j--;
          }
          dense[j + 1] = key;
        }
        continue;
      }

      if (dense[low] > dense[high]) {
        double t = dense[low];
        dense[low] = dense[high];
        dense[high] = t;
      }
      double p = dense[low];
      double q = dense[high];
      int l = low + 1;
      int g = high - 1;
      int k = l;
      while (k <= g) {
        if (dense[k] < p) {
          double t = dense[k];
          dense[k] = dense[l];
          dense[l] = t;
          l++;
        } else if (dense[k] >= q) {
          while (dense[g] > q && k < g)
            g--;
          double t = dense[k];
          dense[k] = dense[g];
          dense[g] = t;
          g--;
          if (dense[k] < p) {
            double t2 = dense[k];
            dense[k] = dense[l];
            dense[l] = t2;
            l++;
          }
        }
        k++;
      }
      l--;
      g++;
      double t = dense[low];
      dense[low] = dense[l];
      dense[l] = t;
      double t2 = dense[high];
      dense[high] = dense[g];
      dense[g] = t2;

      if (top + 3 < 64) {
        stack_low[top] = low;
        stack_high[top] = l - 1;
        top++;
        stack_low[top] = l + 1;
        stack_high[top] = g - 1;
        top++;
        stack_low[top] = g + 1;
        stack_high[top] = high;
        top++;
      }
    }

    AKSValue res_val = ValArr(count);
    AKSArray *res_arr = ValToArr(res_val);
    for (int i = 0; i < count; i++) {
      res_arr->items[i] = ValFloat(dense[i]);
    }
    res_arr->count = count;
    free(dense);
    ValFree(&arr_val);

    PUSH(res_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastParallelReduce") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: FastParallelReduce expects (arr, op)\n");
      return -1;
    }
    AKSValue op_val = POP();
    AKSValue arr_val = POP();
    if (ValKind(arr_val) != VALARR) {
      ValFree(&op_val);
      ValFree(&arr_val);
      fprintf(stderr,
              "Runtime error: FastParallelReduce expects an array argument\n");
      return -1;
    }
    AKSArray *arr = ValToArr(arr_val);
    const char *op = (ValKind(op_val) == VALSTR && ValToStrPtr(op_val))
                         ? ValToStrPtr(op_val)->data
                         : "+";
    int count = arr->count;

    double accum = 0.0;
    if (strcmp(op, "*") == 0)
      accum = 1.0;
    else if (strcmp(op, "min") == 0 && count > 0)
      accum = 1e300;
    else if (strcmp(op, "max") == 0 && count > 0)
      accum = -1e300;

    int i = 0;
#if defined(__AVX2__) || defined(_M_X64)
    if (strcmp(op, "+") == 0) {
      __m256d acc = _mm256_setzero_pd();
      for (; i + 3 < count; i += 4) {
        double d[4];
        for (int k = 0; k < 4; k++) {
          AKSValue v = arr->items[i + k];
          d[k] = (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
        }
        acc = _mm256_add_pd(acc, _mm256_loadu_pd(d));
      }
      double lanes[4];
      _mm256_storeu_pd(lanes, acc);
      accum += lanes[0] + lanes[1] + lanes[2] + lanes[3];
    }
#endif
    for (; i < count; i++) {
      AKSValue v = arr->items[i];
      double val =
          (ValKind(v) == VALFLOAT) ? ValToFloat(v) : (double)ValToInt(v);
      if (strcmp(op, "*") == 0)
        accum *= val;
      else if (strcmp(op, "min") == 0) {
        if (val < accum)
          accum = val;
      } else if (strcmp(op, "max") == 0) {
        if (val > accum)
          accum = val;
      } else
        accum += val;
    }

    ValFree(&op_val);
    ValFree(&arr_val);
    PUSH(ValFloat(accum));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FloatToBits") == 0) {
    if (argc != 1)
      return -1;
    AKSValue arg = POP();
    double d = 0.0;
    if (ValKind(arg) == VALFLOAT) {
      d = ValToFloat(arg);
    } else if (ValKind(arg) == VALINT) {
      d = (double)ValToInt(arg);
    }
    ValFree(&arg);
    uint64_t bits = 0;
    memcpy(&bits, &d, sizeof(double));
    uint32_t hi = (uint32_t)(bits >> 32);
    uint32_t lo = (uint32_t)(bits & 0xFFFFFFFFULL);
    AKSValue res = ValArr(2);
    AKSArray *arr = ValToArr(res);
    arr->items[0] = ValInt((int64_t)hi);
    arr->items[1] = ValInt((int64_t)lo);
    arr->count = 2;
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FloatFromBits") == 0) {
    if (argc == 1) {
      AKSValue arg = POP();
      uint32_t hi = 0;
      uint32_t lo = 0;
      if (ValKind(arg) == VALARR) {
        AKSArray *arr = ValToArr(arg);
        if (arr && arr->count >= 2) {
          hi = (uint32_t)ValToInt(arr->items[0]);
          lo = (uint32_t)ValToInt(arr->items[1]);
        }
      }
      ValFree(&arg);
      uint64_t bits = (((uint64_t)hi) << 32) | (uint64_t)lo;
      double d = 0.0;
      memcpy(&d, &bits, sizeof(double));
      PUSH(ValFloat(d));
      *stacktopptr = stacktop;
      return 1;
    } else if (argc == 2) {
      AKSValue lo_val = POP();
      AKSValue hi_val = POP();
      uint32_t hi = (uint32_t)ValToInt(hi_val);
      uint32_t lo = (uint32_t)ValToInt(lo_val);
      ValFree(&hi_val);
      ValFree(&lo_val);
      uint64_t bits = (((uint64_t)hi) << 32) | (uint64_t)lo;
      double d = 0.0;
      memcpy(&d, &bits, sizeof(double));
      PUSH(ValFloat(d));
      *stacktopptr = stacktop;
      return 1;
    }
    return -1;
  }
  if (strcmp(name, "ByteSwap16") == 0) {
    if (argc != 1)
      return -1;
    AKSValue arg = POP();
    uint16_t v = (uint16_t)(ValKind(arg) == VALINT ? ValToInt(arg) : 0);
    ValFree(&arg);
    uint16_t swapped = (uint16_t)((v >> 8) | (v << 8));
    PUSH(ValInt((int64_t)swapped));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ByteSwap32") == 0) {
    if (argc != 1)
      return -1;
    AKSValue arg = POP();
    uint32_t v = (uint32_t)(ValKind(arg) == VALINT ? ValToInt(arg) : 0);
    ValFree(&arg);
    uint32_t swapped = ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
                       ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
    PUSH(ValInt((int64_t)swapped));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ByteSwap64") == 0) {
    if (argc != 1)
      return -1;
    AKSValue arg = POP();
    if (ValKind(arg) == VALARR) {
      AKSArray *arr = ValToArr(arg);
      uint32_t hi =
          arr && arr->count > 0 ? (uint32_t)ValToInt(arr->items[0]) : 0;
      uint32_t lo =
          arr && arr->count > 1 ? (uint32_t)ValToInt(arr->items[1]) : 0;
      ValFree(&arg);
      uint32_t s_hi = ((lo >> 24) & 0xFF) | ((lo >> 8) & 0xFF00) |
                      ((lo << 8) & 0xFF0000) | ((lo << 24) & 0xFF000000);
      uint32_t s_lo = ((hi >> 24) & 0xFF) | ((hi >> 8) & 0xFF00) |
                      ((hi << 8) & 0xFF0000) | ((hi << 24) & 0xFF000000);
      AKSValue res = ValArr(2);
      AKSArray *res_arr = ValToArr(res);
      res_arr->items[0] = ValInt((int64_t)s_hi);
      res_arr->items[1] = ValInt((int64_t)s_lo);
      res_arr->count = 2;
      PUSH(res);
      *stacktopptr = stacktop;
      return 1;
    } else {
      uint64_t v = (uint64_t)(ValKind(arg) == VALINT ? ValToInt(arg) : 0);
      ValFree(&arg);
      uint64_t swapped = 0;
      for (int i = 0; i < 8; i++) {
        swapped = (swapped << 8) | (v & 0xFF);
        v >>= 8;
      }
      PUSH(ValInt((int64_t)(swapped & 0x0000FFFFFFFFFFFFULL)));
      *stacktopptr = stacktop;
      return 1;
    }
  }
  if (strcmp(name, "RandomGaussian") == 0) {
    static int has_spare = 0;
    static double spare = 0.0;
    double z = 0.0;
    if (has_spare) {
      z = spare;
      has_spare = 0;
    } else {
      double u = ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
      double v = ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
      double r = sqrt(-2.0 * log(u));
      double theta = 2.0 * 3.14159265358979323846 * v;
      z = r * cos(theta);
      spare = r * sin(theta);
      has_spare = 1;
    }
    PUSH(ValFloat(z));
    *stacktopptr = stacktop;
    return 1;
  }

#undef PUSH
#undef POP
  return 0;
}

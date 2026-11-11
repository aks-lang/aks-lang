// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "vm_stdlib.h"


int ExecuteStdlibCryptoDb(VM *vm, VMTask *task, const char *name, int argc,
            AKSValue **stacktopptr) {
  AKSValue *stacktop = *stacktopptr;
  AKSValue *stacklimit = task->stack + (task->stackcap > 0 ? task->stackcap : MAXSTACK);
#define PUSH(val) AKS_STDLIB_PUSH(val)
#define POP() AKS_STDLIB_POP()

  if (strcmp(name, "TarExtractListC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: TarExtractListC() expects 1 arg (tar_path)\n");
      return -1;
    }
    AKSValue path_v = POP();
    AKSValue res_arr = ValArr(0);
    if (ValKind(path_v) == VALSTR) {
      FILE *fp = fopen(ValToStrPtr(path_v)->data, "rb");
      if (fp) {
        char header[512];
        AKSArray *ra = ValToArr(res_arr);
        while (fread(header, 1, 512, fp) == 512) {
          if (header[0] == 0)
            break;
          char name_buf[101];
          memcpy(name_buf, header, 100);
          name_buf[100] = 0;
          AksArrPush(ra, ValStr(name_buf));
          char size_buf[13];
          memcpy(size_buf, header + 124, 12);
          size_buf[12] = 0;
          long size = strtol(size_buf, NULL, 8);
          long blocks = (size + 511) / 512;
          fseek(fp, blocks * 512, SEEK_CUR);
        }
        fclose(fp);
      }
    }
    ValFree(&path_v);
    PUSH(res_arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SecureRandomBytes") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: SecureRandomBytes() expects 1 argument (length)\n");
      return -1;
    }
    AKSValue len_val = POP();
    int count = (int)ValToInt(len_val);
    if (count <= 0)
      count = 16;
    if (count > 65536)
      count = 65536;
    uint8_t *buf = (uint8_t *)malloc((size_t)count);
#ifdef _WIN32
    BCryptGenRandom(NULL, buf, (ULONG)count, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    memset(buf, 0, (size_t)count);
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
      size_t rd = fread(buf, 1, (size_t)count, f);
      (void)rd;
      fclose(f);
    }
#endif
    char *hex = (char *)malloc((size_t)count * 2 + 1);
    for (int i = 0; i < count; i++) {
      sprintf(hex + i * 2, "%02x", buf[i]);
    }
    hex[count * 2] = '\0';
    AKSValue res = ValStrLen(hex, count * 2);
    free(hex);
    free(buf);
    ValFree(&len_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SecureRandomI64") == 0) {
    int64_t val = 0;
#ifdef _WIN32
    BCryptGenRandom(NULL, (PUCHAR)&val, sizeof(val),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
      size_t rd = fread(&val, 1, sizeof(val), f);
      (void)rd;
      fclose(f);
    }
#endif
    PUSH(ValInt(val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SecureRandomUuid") == 0) {
    uint8_t b[16];
#ifdef _WIN32
    BCryptGenRandom(NULL, b, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    memset(b, 0, sizeof(b));
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
      size_t rd = fread(b, 1, 16, f);
      (void)rd;
      fclose(f);
    }
#endif
    b[6] = (b[6] & 0x0f) | 0x40; // Version 4
    b[8] = (b[8] & 0x3f) | 0x80; // Variant rfc 4122
    char uuid[37];
    sprintf(
        uuid,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10],
        b[11], b[12], b[13], b[14], b[15]);
    PUSH(ValStr(uuid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoSha256") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CryptoSha256() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    char hex[65];
    hex[0] = '\0';
    if (ValKind(data_val) == VALSTR) {
      AKSString *str = ValToStrPtr(data_val);
#ifdef _WIN32
      BCRYPT_ALG_HANDLE hAlg = NULL;
      BCRYPT_HASH_HANDLE hHash = NULL;
      if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL,
                                      0) == 0) {
        DWORD hashObjSize = 0, dataLen = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize,
                          sizeof(hashObjSize), &dataLen, 0);
        PUCHAR pbHashObject = (PUCHAR)malloc(hashObjSize);
        uint8_t hashBuf[32];
        if (BCryptCreateHash(hAlg, &hHash, pbHashObject, hashObjSize, NULL, 0,
                             0) == 0) {
          BCryptHashData(hHash, (PUCHAR)str->data, (ULONG)str->len, 0);
          BCryptFinishHash(hHash, (PUCHAR)hashBuf, 32, 0);
          BCryptDestroyHash(hHash);
          for (int i = 0; i < 32; i++) {
            sprintf(hex + i * 2, "%02x", hashBuf[i]);
          }
          hex[64] = '\0';
        }
        free(pbHashObject);
        BCryptCloseAlgorithmProvider(hAlg, 0);
      }
#else
      uint8_t hashBuf[32];
      AKSSHA256CTX ctx;
      Sha256Init(&ctx);
      Sha256Update(&ctx, (const uint8_t *)str->data, (size_t)str->len);
      Sha256Final(&ctx, hashBuf);
      HexEncodeC(hashBuf, 32, hex);
#endif
    }
    ValFree(&data_val);
    PUSH(ValStr(hex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoSha512") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CryptoSha512() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    char hex[129];
    hex[0] = '\0';
    if (ValKind(data_val) == VALSTR) {
      AKSString *str = ValToStrPtr(data_val);
#ifdef _WIN32
      BCRYPT_ALG_HANDLE hAlg = NULL;
      BCRYPT_HASH_HANDLE hHash = NULL;
      if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA512_ALGORITHM, NULL,
                                      0) == 0) {
        DWORD hashObjSize = 0, dataLen = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize,
                          sizeof(hashObjSize), &dataLen, 0);
        PUCHAR pbHashObject = (PUCHAR)malloc(hashObjSize);
        uint8_t hashBuf[64];
        if (BCryptCreateHash(hAlg, &hHash, pbHashObject, hashObjSize, NULL, 0,
                             0) == 0) {
          BCryptHashData(hHash, (PUCHAR)str->data, (ULONG)str->len, 0);
          BCryptFinishHash(hHash, (PUCHAR)hashBuf, 64, 0);
          BCryptDestroyHash(hHash);
          for (int i = 0; i < 64; i++) {
            sprintf(hex + i * 2, "%02x", hashBuf[i]);
          }
          hex[128] = '\0';
        }
        free(pbHashObject);
        BCryptCloseAlgorithmProvider(hAlg, 0);
      }
#else
      uint8_t hashBuf[64];
      if (CryptoSha512((const uint8_t *)str->data, (size_t)str->len, hashBuf)) {
        HexEncodeC(hashBuf, 64, hex);
      }
#endif
    }
    ValFree(&data_val);
    PUSH(ValStr(hex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoHmacSha256") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoHmacSha256() expects 2 arguments "
                      "(key, data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    AKSValue key_val = POP();
    char hex[65];
    hex[0] = '\0';
    if (ValKind(key_val) == VALSTR && ValKind(data_val) == VALSTR) {
      AKSString *keyStr = ValToStrPtr(key_val);
      AKSString *dataStr = ValToStrPtr(data_val);
#ifdef _WIN32
      BCRYPT_ALG_HANDLE hAlg = NULL;
      BCRYPT_HASH_HANDLE hHash = NULL;
      if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL,
                                      BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        DWORD hashObjSize = 0, dataLen = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize,
                          sizeof(hashObjSize), &dataLen, 0);
        PUCHAR pbHashObject = (PUCHAR)malloc(hashObjSize);
        uint8_t hashBuf[32];
        if (BCryptCreateHash(hAlg, &hHash, pbHashObject, hashObjSize,
                             (PUCHAR)keyStr->data, (ULONG)keyStr->len,
                             0) == 0) {
          BCryptHashData(hHash, (PUCHAR)dataStr->data, (ULONG)dataStr->len, 0);
          BCryptFinishHash(hHash, (PUCHAR)hashBuf, 32, 0);
          BCryptDestroyHash(hHash);
          for (int i = 0; i < 32; i++) {
            sprintf(hex + i * 2, "%02x", hashBuf[i]);
          }
          hex[64] = '\0';
        }
        free(pbHashObject);
        BCryptCloseAlgorithmProvider(hAlg, 0);
      }
#else
      uint8_t hashBuf[32];
      if (CryptoHMAC(BCRYPT_SHA256_ALGORITHM, (const uint8_t *)keyStr->data,
                     (size_t)keyStr->len, (const uint8_t *)dataStr->data,
                     (size_t)dataStr->len, hashBuf, 32)) {
        HexEncodeC(hashBuf, 32, hex);
      }
#endif
    }
    ValFree(&data_val);
    ValFree(&key_val);
    PUSH(ValStr(hex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CompressDeflate") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CompressDeflate() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    char *outHex = NULL;
    int outHexLen = 0;
    if (ValKind(data_val) == VALSTR) {
      AKSString *str = ValToStrPtr(data_val);
      (void)str;
#ifdef _WIN32
      COMPRESSOR_HANDLE hCompressor = NULL;
      if (CreateCompressor(COMPRESS_ALGORITHM_MSZIP, NULL, &hCompressor)) {
        SIZE_T compSize = 0;
        Compress(hCompressor, str->data, (SIZE_T)str->len, NULL, 0, &compSize);
        if (compSize > 0) {
          uint8_t *compBuf = (uint8_t *)malloc(compSize);
          SIZE_T written = 0;
          if (Compress(hCompressor, str->data, (SIZE_T)str->len, compBuf,
                       compSize, &written)) {
            outHex = (char *)malloc(written * 2 + 1);
            for (SIZE_T i = 0; i < written; i++) {
              sprintf(outHex + i * 2, "%02x", compBuf[i]);
            }
            outHex[written * 2] = '\0';
            outHexLen = (int)(written * 2);
          }
          free(compBuf);
        }
        CloseCompressor(hCompressor);
      }
#endif
    }
    ValFree(&data_val);
    AKSValue res = outHex ? ValStrLen(outHex, outHexLen) : ValStr("");
    if (outHex)
      free(outHex);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CompressInflate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CompressInflate() expects 2 arguments "
                      "(compressed_hex, original_size)\n");
      return -1;
    }
    AKSValue orig_size_val = POP();
    AKSValue hex_val = POP();
    char *decompStr = NULL;
    int decompStrLen = 0;
    if (ValKind(hex_val) == VALSTR) {
      AKSString *hexStr = ValToStrPtr(hex_val);
      int rawLen = hexStr->len / 2;
      uint8_t *rawBuf = (uint8_t *)malloc((size_t)(rawLen > 0 ? rawLen : 1));
      for (int i = 0; i < rawLen; i++) {
        unsigned int byteVal = 0;
        sscanf(hexStr->data + i * 2, "%02x", &byteVal);
        rawBuf[i] = (uint8_t)byteVal;
      }
#ifdef _WIN32
      DECOMPRESSOR_HANDLE hDecompressor = NULL;
      if (CreateDecompressor(COMPRESS_ALGORITHM_MSZIP, NULL, &hDecompressor)) {
        SIZE_T origSize = (SIZE_T)ValToInt(orig_size_val);
        if (origSize <= 0)
          origSize = 65536;
        uint8_t *decompBuf = (uint8_t *)malloc(origSize + 1);
        SIZE_T written = 0;
        if (Decompress(hDecompressor, rawBuf, (SIZE_T)rawLen, decompBuf,
                       origSize, &written)) {
          decompBuf[written] = '\0';
          decompStr = (char *)decompBuf;
          decompStrLen = (int)written;
        } else {
          free(decompBuf);
        }
        CloseDecompressor(hDecompressor);
      }
#endif
      free(rawBuf);
    }
    ValFree(&orig_size_val);
    ValFree(&hex_val);
    AKSValue res = decompStr ? ValStrLen(decompStr, decompStrLen) : ValStr("");
    if (decompStr)
      free(decompStr);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AesGcmEncrypt") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: AesGcmEncrypt() expects 4 arguments "
                      "(key_hex, iv_hex, plaintext, auth_data)\n");
      return -1;
    }
    AKSValue auth_val = POP();
    AKSValue pt_val = POP();
    AKSValue iv_val = POP();
    AKSValue key_val = POP();
    char *outHex = NULL;
    if (ValKind(key_val) == VALSTR && ValKind(iv_val) == VALSTR &&
        ValKind(pt_val) == VALSTR) {
      AKSString *keyStr = ValToStrPtr(key_val);
      AKSString *ivStr = ValToStrPtr(iv_val);
      AKSString *ptStr = ValToStrPtr(pt_val);
      const char *authData =
          (ValKind(auth_val) == VALSTR) ? ValToStrPtr(auth_val)->data : NULL;
      ULONG authDataLen =
          (ValKind(auth_val) == VALSTR) ? (ULONG)ValToStrPtr(auth_val)->len : 0;

      uint8_t keyBuf[32];
      int keyLen = keyStr->len / 2;
      if (keyLen > 32)
        keyLen = 32;
      memset(keyBuf, 0, sizeof(keyBuf));
      for (int i = 0; i < keyLen; i++) {
        unsigned int b = 0;
        sscanf(keyStr->data + i * 2, "%02x", &b);
        keyBuf[i] = (uint8_t)b;
      }

      uint8_t ivBuf[16];
      int ivLen = ivStr->len / 2;
      if (ivLen > 16)
        ivLen = 16;
      memset(ivBuf, 0, sizeof(ivBuf));
      for (int i = 0; i < ivLen; i++) {
        unsigned int b = 0;
        sscanf(ivStr->data + i * 2, "%02x", &b);
        ivBuf[i] = (uint8_t)b;
      }

#ifdef _WIN32
      BCRYPT_ALG_HANDLE hAlg = NULL;
      BCRYPT_KEY_HANDLE hKey = NULL;
      if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) ==
          0) {
        BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                          (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                          sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        if (BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, keyBuf, 32, 0) ==
            0) {
          uint8_t tagBuf[16];
          memset(tagBuf, 0, sizeof(tagBuf));
          BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
          BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
          authInfo.pbNonce = ivBuf;
          authInfo.cbNonce = (ULONG)ivLen;
          authInfo.pbAuthData = (PUCHAR)authData;
          authInfo.cbAuthData = authDataLen;
          authInfo.pbTag = tagBuf;
          authInfo.cbTag = sizeof(tagBuf);

          ULONG ptLen = (ULONG)ptStr->len;
          uint8_t *ctBuf = (uint8_t *)malloc(ptLen > 0 ? ptLen : 1);
          ULONG written = 0;
          if (BCryptEncrypt(hKey, (PUCHAR)ptStr->data, ptLen, &authInfo, NULL,
                            0, ctBuf, ptLen, &written, 0) == 0) {
            size_t totalBytes = (size_t)written + 16;
            outHex = (char *)malloc(totalBytes * 2 + 1);
            for (size_t i = 0; i < written; i++) {
              sprintf(outHex + i * 2, "%02x", ctBuf[i]);
            }
            for (size_t i = 0; i < 16; i++) {
              sprintf(outHex + (written + i) * 2, "%02x", tagBuf[i]);
            }
            outHex[totalBytes * 2] = '\0';
          }
          free(ctBuf);
        BCryptDestroyKey(hKey);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
      }
#else
      {
        uint8_t tagBuf[16];
        size_t ptLen = (size_t)ptStr->len;
        uint8_t *ctBuf = (uint8_t *)malloc(ptLen > 0 ? ptLen : 1);
        if (ctBuf && AesGcmEncrypt(keyBuf, 32, ivBuf, (size_t)ivLen,
                                   (const uint8_t *)authData, (size_t)authDataLen,
                                   (const uint8_t *)ptStr->data, ptLen, ctBuf,
                                   tagBuf, 16)) {
          size_t totalBytes = ptLen + 16;
          outHex = (char *)malloc(totalBytes * 2 + 1);
          if (outHex) {
            HexEncodeC(ctBuf, ptLen, outHex);
            HexEncodeC(tagBuf, 16, outHex + ptLen * 2);
          }
        }
        if (ctBuf) free(ctBuf);
      }
#endif
    }
    ValFree(&auth_val);
    ValFree(&pt_val);
    ValFree(&iv_val);
    ValFree(&key_val);
    AKSValue res = outHex ? ValStr(outHex) : ValNull();
    if (outHex)
      free(outHex);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AesGcmDecrypt") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: AesGcmDecrypt() expects 4 arguments "
                      "(key_hex, iv_hex, ciphertext_tag_hex, auth_data)\n");
      return -1;
    }
    AKSValue auth_val = POP();
    AKSValue ct_val = POP();
    AKSValue iv_val = POP();
    AKSValue key_val = POP();
    char *outPt = NULL;
    int outPtLen = 0;
    if (ValKind(key_val) == VALSTR && ValKind(iv_val) == VALSTR &&
        ValKind(ct_val) == VALSTR) {
      AKSString *keyStr = ValToStrPtr(key_val);
      AKSString *ivStr = ValToStrPtr(iv_val);
      AKSString *ctStr = ValToStrPtr(ct_val);
      const char *authData =
          (ValKind(auth_val) == VALSTR) ? ValToStrPtr(auth_val)->data : NULL;
      ULONG authDataLen =
          (ValKind(auth_val) == VALSTR) ? (ULONG)ValToStrPtr(auth_val)->len : 0;

      uint8_t keyBuf[32];
      int keyLen = keyStr->len / 2;
      if (keyLen > 32)
        keyLen = 32;
      memset(keyBuf, 0, sizeof(keyBuf));
      for (int i = 0; i < keyLen; i++) {
        unsigned int b = 0;
        sscanf(keyStr->data + i * 2, "%02x", &b);
        keyBuf[i] = (uint8_t)b;
      }

      uint8_t ivBuf[16];
      int ivLen = ivStr->len / 2;
      if (ivLen > 16)
        ivLen = 16;
      memset(ivBuf, 0, sizeof(ivBuf));
      for (int i = 0; i < ivLen; i++) {
        unsigned int b = 0;
        sscanf(ivStr->data + i * 2, "%02x", &b);
        ivBuf[i] = (uint8_t)b;
      }

      int totalRawLen = ctStr->len / 2;
      if (totalRawLen >= 16) {
        int ctRawLen = totalRawLen - 16;
        uint8_t *ctBuf =
            (uint8_t *)malloc((size_t)(ctRawLen > 0 ? ctRawLen : 1));
        for (int i = 0; i < ctRawLen; i++) {
          unsigned int b = 0;
          sscanf(ctStr->data + i * 2, "%02x", &b);
          ctBuf[i] = (uint8_t)b;
        }
        uint8_t tagBuf[16];
        for (int i = 0; i < 16; i++) {
          unsigned int b = 0;
          sscanf(ctStr->data + (ctRawLen + i) * 2, "%02x", &b);
          tagBuf[i] = (uint8_t)b;
        }

#ifdef _WIN32
        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_KEY_HANDLE hKey = NULL;
        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) ==
            0) {
          BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                            (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                            sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
          if (BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, keyBuf, 32, 0) ==
              0) {
            BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
            BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
            authInfo.pbNonce = ivBuf;
            authInfo.cbNonce = (ULONG)ivLen;
            authInfo.pbAuthData = (PUCHAR)authData;
            authInfo.cbAuthData = authDataLen;
            authInfo.pbTag = tagBuf;
            authInfo.cbTag = sizeof(tagBuf);

            uint8_t *ptBuf =
                (uint8_t *)malloc((size_t)(ctRawLen > 0 ? ctRawLen + 1 : 1));
            ULONG written = 0;
            if (BCryptDecrypt(hKey, ctBuf, (ULONG)ctRawLen, &authInfo, NULL, 0,
                              ptBuf, (ULONG)ctRawLen, &written, 0) == 0) {
              ptBuf[written] = '\0';
              outPt = (char *)ptBuf;
              outPtLen = (int)written;
            } else {
              free(ptBuf);
            }
            BCryptDestroyKey(hKey);
          }
          BCryptCloseAlgorithmProvider(hAlg, 0);
        }
#else
        {
          uint8_t *ptBuf =
              (uint8_t *)malloc((size_t)(ctRawLen > 0 ? ctRawLen + 1 : 1));
          if (ptBuf && AesGcmDecrypt(keyBuf, 32, ivBuf, (size_t)ivLen,
                                     (const uint8_t *)authData,
                                     (size_t)authDataLen, ctBuf, (size_t)ctRawLen,
                                     tagBuf, 16, ptBuf)) {
            ptBuf[ctRawLen] = '\0';
            outPt = (char *)ptBuf;
            outPtLen = ctRawLen;
          } else if (ptBuf) {
            free(ptBuf);
          }
        }
#endif
        free(ctBuf);
      }
    }
    ValFree(&auth_val);
    ValFree(&ct_val);
    ValFree(&iv_val);
    ValFree(&key_val);
    AKSValue res = outPt ? ValStrLen(outPt, outPtLen) : ValNull();
    if (outPt)
      free(outPt);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UuidGenerate") == 0) {
#ifdef _WIN32
    UUID uuid;
    UuidCreate(&uuid);
    RPC_CSTR str = NULL;
    if (UuidToStringA(&uuid, &str) == RPC_S_OK && str != NULL) {
      AKSValue res = ValStr((char *)str);
      RpcStringFreeA(&str);
      PUSH(res);
    } else {
      PUSH(ValNull());
    }
#else
      uint8_t ub[16];
      memset(ub, 0, sizeof(ub));
      CryptoSecureRandom(ub, 16);
      ub[6] = (ub[6] & 0x0f) | 0x40;
      ub[8] = (ub[8] & 0x3f) | 0x80;
      char ustr[37];
      snprintf(ustr, sizeof(ustr),
               "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
               ub[0], ub[1], ub[2], ub[3], ub[4], ub[5], ub[6], ub[7],
               ub[8], ub[9], ub[10], ub[11], ub[12], ub[13], ub[14], ub[15]);
      PUSH(ValStr(ustr));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UuidIsValid") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: UuidIsValid() expects 1 argument\n");
      return -1;
    }
    AKSValue s_val = POP();
    int isValid = 0;
    if (ValKind(s_val) == VALSTR) {
      const char *str = ValToStrPtr(s_val)->data;
#ifdef _WIN32
      UUID uuid;
      RPC_STATUS status = UuidFromStringA((RPC_CSTR)str, &uuid);
      isValid = (status == RPC_S_OK) ? 1 : 0;
#else
      if (strlen(str) == 36) {
        isValid = 1;
        for (int ui = 0; ui < 36; ui++) {
          if (ui == 8 || ui == 13 || ui == 18 || ui == 23) {
            if (str[ui] != '-') { isValid = 0; break; }
          } else {
            char ch = str[ui];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                  (ch >= 'A' && ch <= 'F'))) {
              isValid = 0; break;
            }
          }
        }
      }
#endif
    }
    ValFree(&s_val);
    PUSH(ValBool(isValid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastChecksumCrc32c") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: FastChecksumCrc32c() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    uint32_t RunningCrc = 0xFFFFFFFF;
    if (ValKind(DataVal) == VALSTR) {
      const uint8_t *DataBytes = (const uint8_t *)ValToStrPtr(DataVal)->data;
      size_t DataLength = (size_t)ValToStrPtr(DataVal)->len;
#if defined(__SSE4_2__) || defined(_M_X64) || defined(_M_IX86)
      while (DataLength >= 8) {
        RunningCrc =
            (uint32_t)_mm_crc32_u64(RunningCrc, *(const uint64_t *)DataBytes);
        DataBytes += 8;
        DataLength -= 8;
      }
      while (DataLength > 0) {
        RunningCrc = _mm_crc32_u8(RunningCrc, *DataBytes);
        DataBytes++;
        DataLength--;
      }
#else
      RunningCrc = AksCrc32cSw(RunningCrc, DataBytes, DataLength);
#endif
    }
    ValFree(&DataVal);
    PUSH(ValInt((int64_t)(RunningCrc ^ 0xFFFFFFFF)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastChecksumCrc32cUpdate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FastChecksumCrc32cUpdate() expects 2 "
                      "arguments (current_crc, data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    AKSValue CrcVal = POP();
    uint32_t RunningCrc = ((uint32_t)ValToInt(CrcVal)) ^ 0xFFFFFFFF;
    if (ValKind(DataVal) == VALSTR) {
      const uint8_t *DataBytes = (const uint8_t *)ValToStrPtr(DataVal)->data;
      size_t DataLength = (size_t)ValToStrPtr(DataVal)->len;
#if defined(__SSE4_2__) || defined(_M_X64) || defined(_M_IX86)
      while (DataLength >= 8) {
        RunningCrc =
            (uint32_t)_mm_crc32_u64(RunningCrc, *(const uint64_t *)DataBytes);
        DataBytes += 8;
        DataLength -= 8;
      }
      while (DataLength > 0) {
        RunningCrc = _mm_crc32_u8(RunningCrc, *DataBytes);
        DataBytes++;
        DataLength--;
      }
#else
      RunningCrc = AksCrc32cSw(RunningCrc, DataBytes, DataLength);
#endif
    }
    ValFree(&CrcVal);
    ValFree(&DataVal);
    PUSH(ValInt((int64_t)(RunningCrc ^ 0xFFFFFFFF)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastChecksumAdler32") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: FastChecksumAdler32() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    uint32_t AdlerA = 1;
    uint32_t AdlerB = 0;
    if (ValKind(DataVal) == VALSTR) {
      const uint8_t *DataBytes = (const uint8_t *)ValToStrPtr(DataVal)->data;
      size_t DataLength = (size_t)ValToStrPtr(DataVal)->len;
      for (size_t Index = 0; Index < DataLength; Index++) {
        AdlerA = (AdlerA + DataBytes[Index]) % 65521;
        AdlerB = (AdlerB + AdlerA) % 65521;
      }
    }
    ValFree(&DataVal);
    uint32_t FinalAdler = (AdlerB << 16) | AdlerA;
    PUSH(ValInt((int64_t)FinalAdler));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FastChecksumFnv1a64") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: FastChecksumFnv1a64() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    uint64_t FnvHash = 14695981039346656037ULL;
    if (ValKind(DataVal) == VALSTR) {
      const uint8_t *DataBytes = (const uint8_t *)ValToStrPtr(DataVal)->data;
      size_t DataLength = (size_t)ValToStrPtr(DataVal)->len;
      for (size_t Index = 0; Index < DataLength; Index++) {
        FnvHash ^= (uint64_t)DataBytes[Index];
        FnvHash *= 1099511628211ULL;
      }
    }
    ValFree(&DataVal);
    PUSH(ValInt((int64_t)FnvHash));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EntropyPoolHardwareSeed") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: EntropyPoolHardwareSeed() expects 0 arguments\n");
      return -1;
    }
    uint64_t SeedVal = 0;
#ifdef _WIN32
    unsigned __int64 wSeed = 0;
    int SeedOk = _rdseed64_step(&wSeed);
    if (!SeedOk) {
      _rdrand64_step(&wSeed);
    }
    SeedVal = (uint64_t)wSeed;
#else
    FILE *uf = fopen("/dev/urandom", "rb");
    if (uf) {
      if (fread(&SeedVal, sizeof(SeedVal), 1, uf) != 1)
        SeedVal = (uint64_t)time(NULL) ^ (uint64_t)clock();
      fclose(uf);
    } else {
      SeedVal = (uint64_t)time(NULL) ^ (uint64_t)clock();
    }
#endif
    PUSH(ValInt((int64_t)SeedVal));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EntropyPoolHardwareRandomInt64") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: EntropyPoolHardwareRandomInt64() expects "
                      "0 arguments\n");
      return -1;
    }
    uint64_t RandVal = 0;
#ifdef _WIN32
    unsigned __int64 wRand = 0;
    int RandOk = _rdrand64_step(&wRand);
    if (!RandOk) {
      BCryptGenRandom(NULL, (PUCHAR)&wRand, sizeof(wRand),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    }
    RandVal = (uint64_t)wRand;
#else
    FILE *uf = fopen("/dev/urandom", "rb");
    if (uf) {
      if (fread(&RandVal, sizeof(RandVal), 1, uf) != 1)
        RandVal = ((uint64_t)rand() << 32) ^ (uint64_t)rand() ^ (uint64_t)clock();
      fclose(uf);
    } else {
      RandVal = ((uint64_t)rand() << 32) ^ (uint64_t)rand() ^ (uint64_t)clock();
    }
#endif
    PUSH(ValInt((int64_t)RandVal));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EntropyPoolHardwareEntropyAvailable") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: EntropyPoolHardwareEntropyAvailable() "
                      "expects 0 arguments\n");
      return -1;
    }
    BOOL HasRdrand = FALSE;
#ifdef _WIN32
    int CpuInfo[4];
    __cpuid(CpuInfo, 1);
    HasRdrand = (CpuInfo[2] & (1 << 30)) != 0;
#else
    HasRdrand = (access("/dev/urandom", R_OK) == 0) ? TRUE : FALSE;
#endif
    PUSH(ValBool(HasRdrand ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EntropyPoolSystemEntropyBytes") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: EntropyPoolSystemEntropyBytes() expects "
                      "1 argument (count)\n");
      return -1;
    }
    AKSValue CountVal = POP();
    int ByteCount = (int)ValToInt(CountVal);
    if (ByteCount <= 0)
      ByteCount = 16;
    if (ByteCount > 1024)
      ByteCount = 1024;
    char HexOutput[2049] = {0};
#ifdef _WIN32
    uint8_t EntropyBuf[1024];
    if (BCryptGenRandom(NULL, EntropyBuf, (ULONG)ByteCount,
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
      static const char HexChars[] = "0123456789abcdef";
      for (int Idx = 0; Idx < ByteCount; Idx++) {
        HexOutput[Idx * 2] = HexChars[(EntropyBuf[Idx] >> 4) & 0x0F];
        HexOutput[Idx * 2 + 1] = HexChars[EntropyBuf[Idx] & 0x0F];
      }
      HexOutput[ByteCount * 2] = '\0';
    }
#else
    uint8_t EntropyBuf[1024];
    int got = 0;
    FILE *uf = fopen("/dev/urandom", "rb");
    if (uf) {
      got = ((int)fread(EntropyBuf, 1, (size_t)ByteCount, uf) == ByteCount);
      fclose(uf);
    }
    if (!got) {
      for (int Idx = 0; Idx < ByteCount; Idx++)
        EntropyBuf[Idx] = (uint8_t)(rand() & 0xFF);
    }
    static const char HexChars[] = "0123456789abcdef";
    for (int Idx = 0; Idx < ByteCount; Idx++) {
      HexOutput[Idx * 2] = HexChars[(EntropyBuf[Idx] >> 4) & 0x0F];
      HexOutput[Idx * 2 + 1] = HexChars[EntropyBuf[Idx] & 0x0F];
    }
    HexOutput[ByteCount * 2] = '\0';
#endif
    ValFree(&CountVal);
    PUSH(ValStr(HexOutput));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoKdfPbkdf2Sha256") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoKdfPbkdf2Sha256() expects 4 "
                      "arguments (password, salt, iterations, key_length)\n");
      return -1;
    }
    AKSValue LenVal = POP();
    AKSValue IterVal = POP();
    AKSValue SaltVal = POP();
    AKSValue PwdVal = POP();
    size_t KeyLength = (size_t)ValToInt(LenVal);
    uint64_t Iterations = (uint64_t)ValToInt(IterVal);
    if (KeyLength == 0 || KeyLength > 512)
      KeyLength = 32;
    if (Iterations == 0)
      Iterations = 10000;
    char HexResult[1025] = {0};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE AlgHandle = NULL;
    if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA256_ALGORITHM, NULL,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
      uint8_t DerivedKey[512] = {0};
      const char *PwdStr =
          (ValKind(PwdVal) == VALSTR) ? ValToStrPtr(PwdVal)->data : "";
      size_t PwdLen =
          (ValKind(PwdVal) == VALSTR) ? (size_t)ValToStrPtr(PwdVal)->len : 0;
      const char *SaltStr =
          (ValKind(SaltVal) == VALSTR) ? ValToStrPtr(SaltVal)->data : "";
      size_t SaltLen =
          (ValKind(SaltVal) == VALSTR) ? (size_t)ValToStrPtr(SaltVal)->len : 0;
      if (BCryptDeriveKeyPBKDF2(AlgHandle, (PUCHAR)PwdStr, (ULONG)PwdLen,
                                (PUCHAR)SaltStr, (ULONG)SaltLen, Iterations,
                                DerivedKey, (ULONG)KeyLength, 0) == 0) {
        static const char HexChars[] = "0123456789abcdef";
        for (size_t i = 0; i < KeyLength; i++) {
          HexResult[i * 2] = HexChars[(DerivedKey[i] >> 4) & 0x0F];
          HexResult[i * 2 + 1] = HexChars[DerivedKey[i] & 0x0F];
        }
        HexResult[KeyLength * 2] = '\0';
      }
      BCryptCloseAlgorithmProvider(AlgHandle, 0);
    }
#endif
    ValFree(&PwdVal);
    ValFree(&SaltVal);
    ValFree(&IterVal);
    ValFree(&LenVal);
    PUSH(ValStr(HexResult));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoKdfPbkdf2Sha512") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoKdfPbkdf2Sha512() expects 4 "
                      "arguments (password, salt, iterations, key_length)\n");
      return -1;
    }
    AKSValue LenVal = POP();
    AKSValue IterVal = POP();
    AKSValue SaltVal = POP();
    AKSValue PwdVal = POP();
    size_t KeyLength = (size_t)ValToInt(LenVal);
    uint64_t Iterations = (uint64_t)ValToInt(IterVal);
    if (KeyLength == 0 || KeyLength > 512)
      KeyLength = 64;
    if (Iterations == 0)
      Iterations = 10000;
    char HexResult[1025] = {0};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE AlgHandle = NULL;
    if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA512_ALGORITHM, NULL,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
      uint8_t DerivedKey[512] = {0};
      const char *PwdStr =
          (ValKind(PwdVal) == VALSTR) ? ValToStrPtr(PwdVal)->data : "";
      size_t PwdLen =
          (ValKind(PwdVal) == VALSTR) ? (size_t)ValToStrPtr(PwdVal)->len : 0;
      const char *SaltStr =
          (ValKind(SaltVal) == VALSTR) ? ValToStrPtr(SaltVal)->data : "";
      size_t SaltLen =
          (ValKind(SaltVal) == VALSTR) ? (size_t)ValToStrPtr(SaltVal)->len : 0;
      if (BCryptDeriveKeyPBKDF2(AlgHandle, (PUCHAR)PwdStr, (ULONG)PwdLen,
                                (PUCHAR)SaltStr, (ULONG)SaltLen, Iterations,
                                DerivedKey, (ULONG)KeyLength, 0) == 0) {
        static const char HexChars[] = "0123456789abcdef";
        for (size_t i = 0; i < KeyLength; i++) {
          HexResult[i * 2] = HexChars[(DerivedKey[i] >> 4) & 0x0F];
          HexResult[i * 2 + 1] = HexChars[DerivedKey[i] & 0x0F];
        }
        HexResult[KeyLength * 2] = '\0';
      }
      BCryptCloseAlgorithmProvider(AlgHandle, 0);
    }
#endif
    ValFree(&PwdVal);
    ValFree(&SaltVal);
    ValFree(&IterVal);
    ValFree(&LenVal);
    PUSH(ValStr(HexResult));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoKdfVerifyPassword") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoKdfVerifyPassword() expects 4 "
                      "arguments (password, salt, iterations, expected_hex)\n");
      return -1;
    }
    AKSValue ExpVal = POP();
    AKSValue IterVal = POP();
    AKSValue SaltVal = POP();
    AKSValue PwdVal = POP();
    BOOL VerifyMatch = FALSE;
#ifdef _WIN32
    if (ValKind(ExpVal) == VALSTR) {
      const char *ExpHex = ValToStrPtr(ExpVal)->data;
      size_t ExpLen = (size_t)ValToStrPtr(ExpVal)->len;
      size_t KeyLength = ExpLen / 2;
      uint64_t Iterations = (uint64_t)ValToInt(IterVal);
      if (Iterations == 0)
        Iterations = 10000;
      BCRYPT_ALG_HANDLE AlgHandle = NULL;
      if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA256_ALGORITHM, NULL,
                                      BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        uint8_t DerivedKey[512] = {0};
        const char *PwdStr =
            (ValKind(PwdVal) == VALSTR) ? ValToStrPtr(PwdVal)->data : "";
        size_t PwdLen =
            (ValKind(PwdVal) == VALSTR) ? (size_t)ValToStrPtr(PwdVal)->len : 0;
        const char *SaltStr =
            (ValKind(SaltVal) == VALSTR) ? ValToStrPtr(SaltVal)->data : "";
        size_t SaltLen = (ValKind(SaltVal) == VALSTR)
                             ? (size_t)ValToStrPtr(SaltVal)->len
                             : 0;
        if (BCryptDeriveKeyPBKDF2(AlgHandle, (PUCHAR)PwdStr, (ULONG)PwdLen,
                                  (PUCHAR)SaltStr, (ULONG)SaltLen, Iterations,
                                  DerivedKey, (ULONG)KeyLength, 0) == 0) {
          char HexResult[1025] = {0};
          static const char HexChars[] = "0123456789abcdef";
          for (size_t i = 0; i < KeyLength; i++) {
            HexResult[i * 2] = HexChars[(DerivedKey[i] >> 4) & 0x0F];
            HexResult[i * 2 + 1] = HexChars[DerivedKey[i] & 0x0F];
          }
          HexResult[KeyLength * 2] = '\0';
          VerifyMatch = (_stricmp(HexResult, ExpHex) == 0);
        }
        BCryptCloseAlgorithmProvider(AlgHandle, 0);
      }
    }
#endif
    ValFree(&PwdVal);
    ValFree(&SaltVal);
    ValFree(&IterVal);
    ValFree(&ExpVal);
    PUSH(ValBool(VerifyMatch ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoHmacSha256") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoHmacSha256() expects 2 arguments "
                      "(key, data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    AKSValue KeyVal = POP();
    char HexResult[65] = {0};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE AlgHandle = NULL;
    if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA256_ALGORITHM, NULL,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
      BCRYPT_HASH_HANDLE HashHandle = NULL;
      const char *KeyStr =
          (ValKind(KeyVal) == VALSTR) ? ValToStrPtr(KeyVal)->data : "";
      size_t KeyLen =
          (ValKind(KeyVal) == VALSTR) ? (size_t)ValToStrPtr(KeyVal)->len : 0;
      const char *DataStr =
          (ValKind(DataVal) == VALSTR) ? ValToStrPtr(DataVal)->data : "";
      size_t DataLen =
          (ValKind(DataVal) == VALSTR) ? (size_t)ValToStrPtr(DataVal)->len : 0;
      if (BCryptCreateHash(AlgHandle, &HashHandle, NULL, 0, (PUCHAR)KeyStr,
                           (ULONG)KeyLen, 0) == 0) {
        BCryptHashData(HashHandle, (PUCHAR)DataStr, (ULONG)DataLen, 0);
        uint8_t HashBuf[32] = {0};
        BCryptFinishHash(HashHandle, HashBuf, 32, 0);
        BCryptDestroyHash(HashHandle);
        static const char HexChars[] = "0123456789abcdef";
        for (int i = 0; i < 32; i++) {
          HexResult[i * 2] = HexChars[(HashBuf[i] >> 4) & 0x0F];
          HexResult[i * 2 + 1] = HexChars[HashBuf[i] & 0x0F];
        }
        HexResult[64] = '\0';
      }
      BCryptCloseAlgorithmProvider(AlgHandle, 0);
    }
#endif
    ValFree(&KeyVal);
    ValFree(&DataVal);
    PUSH(ValStr(HexResult));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoHmacSha512") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoHmacSha512() expects 2 arguments "
                      "(key, data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    AKSValue KeyVal = POP();
    char HexResult[129] = {0};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE AlgHandle = NULL;
    if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA512_ALGORITHM, NULL,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
      BCRYPT_HASH_HANDLE HashHandle = NULL;
      const char *KeyStr =
          (ValKind(KeyVal) == VALSTR) ? ValToStrPtr(KeyVal)->data : "";
      size_t KeyLen =
          (ValKind(KeyVal) == VALSTR) ? (size_t)ValToStrPtr(KeyVal)->len : 0;
      const char *DataStr =
          (ValKind(DataVal) == VALSTR) ? ValToStrPtr(DataVal)->data : "";
      size_t DataLen =
          (ValKind(DataVal) == VALSTR) ? (size_t)ValToStrPtr(DataVal)->len : 0;
      if (BCryptCreateHash(AlgHandle, &HashHandle, NULL, 0, (PUCHAR)KeyStr,
                           (ULONG)KeyLen, 0) == 0) {
        BCryptHashData(HashHandle, (PUCHAR)DataStr, (ULONG)DataLen, 0);
        uint8_t HashBuf[64] = {0};
        BCryptFinishHash(HashHandle, HashBuf, 64, 0);
        BCryptDestroyHash(HashHandle);
        static const char HexChars[] = "0123456789abcdef";
        for (int i = 0; i < 64; i++) {
          HexResult[i * 2] = HexChars[(HashBuf[i] >> 4) & 0x0F];
          HexResult[i * 2 + 1] = HexChars[HashBuf[i] & 0x0F];
        }
        HexResult[128] = '\0';
      }
      BCryptCloseAlgorithmProvider(AlgHandle, 0);
    }
#endif
    ValFree(&KeyVal);
    ValFree(&DataVal);
    PUSH(ValStr(HexResult));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoHmacVerify") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: CryptoHmacVerify() expects 3 arguments "
                      "(key, data, expected_hex)\n");
      return -1;
    }
    AKSValue ExpVal = POP();
    AKSValue DataVal = POP();
    AKSValue KeyVal = POP();
    BOOL VerifyOk = FALSE;
#ifdef _WIN32
    if (ValKind(ExpVal) == VALSTR) {
      const char *ExpHex = ValToStrPtr(ExpVal)->data;
      BCRYPT_ALG_HANDLE AlgHandle = NULL;
      if (BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA256_ALGORITHM, NULL,
                                      BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        BCRYPT_HASH_HANDLE HashHandle = NULL;
        const char *KeyStr =
            (ValKind(KeyVal) == VALSTR) ? ValToStrPtr(KeyVal)->data : "";
        size_t KeyLen =
            (ValKind(KeyVal) == VALSTR) ? (size_t)ValToStrPtr(KeyVal)->len : 0;
        const char *DataStr =
            (ValKind(DataVal) == VALSTR) ? ValToStrPtr(DataVal)->data : "";
        size_t DataLen = (ValKind(DataVal) == VALSTR)
                             ? (size_t)ValToStrPtr(DataVal)->len
                             : 0;
        if (BCryptCreateHash(AlgHandle, &HashHandle, NULL, 0, (PUCHAR)KeyStr,
                             (ULONG)KeyLen, 0) == 0) {
          BCryptHashData(HashHandle, (PUCHAR)DataStr, (ULONG)DataLen, 0);
          uint8_t HashBuf[32] = {0};
          BCryptFinishHash(HashHandle, HashBuf, 32, 0);
          BCryptDestroyHash(HashHandle);
          char HexResult[65] = {0};
          static const char HexChars[] = "0123456789abcdef";
          for (int i = 0; i < 32; i++) {
            HexResult[i * 2] = HexChars[(HashBuf[i] >> 4) & 0x0F];
            HexResult[i * 2 + 1] = HexChars[HashBuf[i] & 0x0F];
          }
          HexResult[64] = '\0';
          VerifyOk = (_stricmp(HexResult, ExpHex) == 0);
        }
        BCryptCloseAlgorithmProvider(AlgHandle, 0);
      }
    }
#endif
    ValFree(&KeyVal);
    ValFree(&DataVal);
    ValFree(&ExpVal);
    PUSH(ValBool(VerifyOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoChaCha20Encrypt") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoChaCha20Encrypt() expects 4 "
                      "arguments (key, nonce, aad, plaintext)\n");
      return -1;
    }
    AKSValue PtVal = POP();
    AKSValue AadVal = POP();
    AKSValue NonceVal = POP();
    AKSValue KeyVal = POP();
    char CipherHex[4097] = {0};
    char TagHex[33] = {0};
#ifdef _WIN32
    if (ValKind(KeyVal) == VALSTR && ValKind(NonceVal) == VALSTR) {
      const char *KeyHex = ValToStrPtr(KeyVal)->data;
      const char *NonceHex = ValToStrPtr(NonceVal)->data;
      const char *AadHex =
          (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->data : "";
      const char *PtHex =
          (ValKind(PtVal) == VALSTR) ? ValToStrPtr(PtVal)->data : "";
      size_t KeyBytes = ValToStrPtr(KeyVal)->len / 2;
      size_t NonceBytes = ValToStrPtr(NonceVal)->len / 2;
      size_t AadBytes =
          (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->len / 2 : 0;
      size_t PtBytes =
          (ValKind(PtVal) == VALSTR) ? ValToStrPtr(PtVal)->len / 2 : 0;
      if (KeyBytes >= 16) {
        uint8_t KeyBuf[32] = {0}, NonceBuf[16] = {0};
        uint8_t *AadBuf = AadBytes ? (uint8_t *)malloc(AadBytes) : NULL;
        uint8_t *PtBuf = PtBytes ? (uint8_t *)malloc(PtBytes) : NULL;
        uint8_t *CtBuf = PtBytes ? (uint8_t *)malloc(PtBytes) : NULL;
        uint8_t TagBuf[16] = {0};
        for (size_t i = 0; i < 32 && i < KeyBytes; i++)
          sscanf(&KeyHex[i * 2], "%02hhx", &KeyBuf[i]);
        for (size_t i = 0; i < 16 && i < NonceBytes; i++)
          sscanf(&NonceHex[i * 2], "%02hhx", &NonceBuf[i]);
        if (AadBuf)
          for (size_t i = 0; i < AadBytes; i++)
            sscanf(&AadHex[i * 2], "%02hhx", &AadBuf[i]);
        if (PtBuf)
          for (size_t i = 0; i < PtBytes; i++)
            sscanf(&PtHex[i * 2], "%02hhx", &PtBuf[i]);
        // Direct AES-GCM/ChaCha20 cipher block execution
        AesGcmEncrypt(KeyBuf, (KeyBytes >= 32 ? 32 : 16), NonceBuf,
                      (NonceBytes >= 12 ? 12 : NonceBytes), AadBuf, AadBytes,
                      PtBuf, PtBytes, CtBuf, TagBuf, 16);
        static const char HexChars[] = "0123456789abcdef";
        if (CtBuf) {
          for (size_t i = 0; i < PtBytes && i < 2048; i++) {
            CipherHex[i * 2] = HexChars[(CtBuf[i] >> 4) & 0x0F];
            CipherHex[i * 2 + 1] = HexChars[CtBuf[i] & 0x0F];
          }
          CipherHex[PtBytes * 2] = '\0';
        }
        for (size_t i = 0; i < 16; i++) {
          TagHex[i * 2] = HexChars[(TagBuf[i] >> 4) & 0x0F];
          TagHex[i * 2 + 1] = HexChars[TagBuf[i] & 0x0F];
        }
        TagHex[32] = '\0';
        if (AadBuf)
          free(AadBuf);
        if (PtBuf)
          free(PtBuf);
        if (CtBuf)
          free(CtBuf);
      }
    }
#endif
    AKSValue ResStruct = ValStruct("ChaCha20CipherResult");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("ciphertext");
    StructPtr->fieldvals[0] = ValStr(CipherHex);
    StructPtr->fieldnames[1] = strdup("tag");
    StructPtr->fieldvals[1] = ValStr(TagHex);
    ValFree(&KeyVal);
    ValFree(&NonceVal);
    ValFree(&AadVal);
    ValFree(&PtVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoChaCha20Decrypt") == 0) {
    if (argc != 5) {
      fprintf(stderr, "Runtime error: CryptoChaCha20Decrypt() expects 5 "
                      "arguments (key, nonce, aad, ciphertext, tag)\n");
      return -1;
    }
    AKSValue TagVal = POP();
    AKSValue CtVal = POP();
    AKSValue AadVal = POP();
    AKSValue NonceVal = POP();
    AKSValue KeyVal = POP();
    char PlainHex[4097] = {0};
    BOOL DecryptOk = FALSE;
#ifdef _WIN32
    if (ValKind(KeyVal) == VALSTR && ValKind(NonceVal) == VALSTR &&
        ValKind(TagVal) == VALSTR) {
      const char *KeyHex = ValToStrPtr(KeyVal)->data;
      const char *NonceHex = ValToStrPtr(NonceVal)->data;
      const char *AadHex =
          (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->data : "";
      const char *CtHex =
          (ValKind(CtVal) == VALSTR) ? ValToStrPtr(CtVal)->data : "";
      const char *TagHex = ValToStrPtr(TagVal)->data;
      size_t KeyBytes = ValToStrPtr(KeyVal)->len / 2;
      size_t NonceBytes = ValToStrPtr(NonceVal)->len / 2;
      size_t AadBytes =
          (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->len / 2 : 0;
      size_t CtBytes =
          (ValKind(CtVal) == VALSTR) ? ValToStrPtr(CtVal)->len / 2 : 0;
      if (KeyBytes >= 16) {
        uint8_t KeyBuf[32] = {0}, NonceBuf[16] = {0}, TagBuf[16] = {0};
        uint8_t *AadBuf = AadBytes ? (uint8_t *)malloc(AadBytes) : NULL;
        uint8_t *CtBuf = CtBytes ? (uint8_t *)malloc(CtBytes) : NULL;
        uint8_t *PtBuf = CtBytes ? (uint8_t *)malloc(CtBytes) : NULL;
        for (size_t i = 0; i < 32 && i < KeyBytes; i++)
          sscanf(&KeyHex[i * 2], "%02hhx", &KeyBuf[i]);
        for (size_t i = 0; i < 16 && i < NonceBytes; i++)
          sscanf(&NonceHex[i * 2], "%02hhx", &NonceBuf[i]);
        for (size_t i = 0; i < 16; i++)
          sscanf(&TagHex[i * 2], "%02hhx", &TagBuf[i]);
        if (AadBuf)
          for (size_t i = 0; i < AadBytes; i++)
            sscanf(&AadHex[i * 2], "%02hhx", &AadBuf[i]);
        if (CtBuf)
          for (size_t i = 0; i < CtBytes; i++)
            sscanf(&CtHex[i * 2], "%02hhx", &CtBuf[i]);
        int Res = AesGcmDecrypt(KeyBuf, (KeyBytes >= 32 ? 32 : 16), NonceBuf,
                                (NonceBytes >= 12 ? 12 : NonceBytes), AadBuf,
                                AadBytes, CtBuf, CtBytes, TagBuf, 16, PtBuf);
        if (Res != 0) {
          DecryptOk = TRUE;
          static const char HexChars[] = "0123456789abcdef";
          if (PtBuf) {
            for (size_t i = 0; i < CtBytes && i < 2048; i++) {
              PlainHex[i * 2] = HexChars[(PtBuf[i] >> 4) & 0x0F];
              PlainHex[i * 2 + 1] = HexChars[PtBuf[i] & 0x0F];
            }
            PlainHex[CtBytes * 2] = '\0';
          }
        }
        if (AadBuf)
          free(AadBuf);
        if (CtBuf)
          free(CtBuf);
        if (PtBuf)
          free(PtBuf);
      }
    }
#endif
    AKSValue ResStruct = ValStruct("ChaCha20PlainResult");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("plaintext");
    StructPtr->fieldvals[0] = ValStr(PlainHex);
    StructPtr->fieldnames[1] = strdup("ok");
    StructPtr->fieldvals[1] = ValBool(DecryptOk ? 1 : 0);
    ValFree(&KeyVal);
    ValFree(&NonceVal);
    ValFree(&AadVal);
    ValFree(&CtVal);
    ValFree(&TagVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoSha3Sha256") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CryptoSha3Sha256() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    char OutHex[65] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *S = ValToStrPtr(DataVal);
      uint8_t Digest[32] = {0};
      KeccakHash((const uint8_t *)S->data, S->len, Digest, 32, 136);
      HexEncodeC(Digest, 32, OutHex);
    }
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoSha3Sha512") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: CryptoSha3Sha512() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    char OutHex[129] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *S = ValToStrPtr(DataVal);
      uint8_t Digest[64] = {0};
      KeccakHash((const uint8_t *)S->data, S->len, Digest, 64, 72);
      HexEncodeC(Digest, 64, OutHex);
    }
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoSha3Keccak256") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: CryptoSha3Keccak256() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    char OutHex[65] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *S = ValToStrPtr(DataVal);
      uint8_t Digest[32] = {0};
      KeccakHash((const uint8_t *)S->data, S->len, Digest, 32, 136);
      HexEncodeC(Digest, 32, OutHex);
    }
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoBlake2Blake2b") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoBlake2Blake2b() expects 2 "
                      "arguments (data, out_bytes)\n");
      return -1;
    }
    AKSValue OutBytesVal = POP();
    AKSValue DataVal = POP();
    char OutHex[129] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *S = ValToStrPtr(DataVal);
      size_t OutLen = (size_t)ValToInt(OutBytesVal);
      if (OutLen == 0 || OutLen > 64)
        OutLen = 64;
      uint8_t Digest[64] = {0};
      CryptoBlake2b((const uint8_t *)S->data, S->len, Digest, OutLen);
      HexEncodeC(Digest, OutLen, OutHex);
    }
    ValFree(&DataVal);
    ValFree(&OutBytesVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoBlake2Blake2s") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoBlake2Blake2s() expects 2 "
                      "arguments (data, out_bytes)\n");
      return -1;
    }
    AKSValue OutBytesVal = POP();
    AKSValue DataVal = POP();
    char OutHex[65] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *S = ValToStrPtr(DataVal);
      size_t OutLen = (size_t)ValToInt(OutBytesVal);
      if (OutLen == 0 || OutLen > 32)
        OutLen = 32;
      uint8_t Digest[32] = {0};
      CryptoBlake2s((const uint8_t *)S->data, S->len, Digest, OutLen);
      HexEncodeC(Digest, OutLen, OutHex);
    }
    ValFree(&DataVal);
    ValFree(&OutBytesVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoPoly1305Auth") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoPoly1305Auth() expects 2 arguments "
                      "(key, msg)\n");
      return -1;
    }
    AKSValue MsgVal = POP();
    AKSValue KeyVal = POP();
    char OutHex[33] = {0};
    if (ValKind(KeyVal) == VALSTR && ValKind(MsgVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *M = ValToStrPtr(MsgVal);
      uint8_t KeyBytes[32] = {0};
      if (K->len == 64) {
        HexDecodeC(K->data, KeyBytes);
      } else {
        size_t CopyLen = (K->len > 32) ? 32 : K->len;
        memcpy(KeyBytes, K->data, CopyLen);
      }
      uint8_t Tag[16] = {0};
      CryptoPoly1305(KeyBytes, (const uint8_t *)M->data, M->len, Tag);
      HexEncodeC(Tag, 16, OutHex);
    }
    ValFree(&KeyVal);
    ValFree(&MsgVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoArgon2Hash") == 0) {
    if (argc != 6) {
      fprintf(stderr, "Runtime error: CryptoArgon2Hash() expects 6 arguments "
                      "(pwd, salt, iter, mem_kb, lanes, out_bytes)\n");
      return -1;
    }
    AKSValue OutBytesVal = POP();
    AKSValue LanesVal = POP();
    AKSValue MemVal = POP();
    AKSValue IterVal = POP();
    AKSValue SaltVal = POP();
    AKSValue PwdVal = POP();

    char OutHex[129] = {0};
    if (ValKind(PwdVal) == VALSTR && ValKind(SaltVal) == VALSTR) {
      AKSString *P = ValToStrPtr(PwdVal);
      AKSString *S = ValToStrPtr(SaltVal);
      uint32_t Iter = (uint32_t)ValToInt(IterVal);
      uint32_t MemKb = (uint32_t)ValToInt(MemVal);
      uint32_t Lanes = (uint32_t)ValToInt(LanesVal);
      size_t OutLen = (size_t)ValToInt(OutBytesVal);
      if (OutLen == 0 || OutLen > 64)
        OutLen = 32;
      uint8_t OutDigest[64] = {0};
      if (CryptoArgon2((const uint8_t *)P->data, P->len,
                       (const uint8_t *)S->data, S->len, MemKb, Iter, Lanes,
                       OutDigest, OutLen, "argon2id")) {
        HexEncodeC(OutDigest, OutLen, OutHex);
      }
    }
    ValFree(&PwdVal);
    ValFree(&SaltVal);
    ValFree(&IterVal);
    ValFree(&MemVal);
    ValFree(&LanesVal);
    ValFree(&OutBytesVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoSipHash64Hash") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoSipHash64Hash() expects 2 "
                      "arguments (key_hex, data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    AKSValue KeyVal = POP();
    char OutHex[17] = {0};
    if (ValKind(KeyVal) == VALSTR && ValKind(DataVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *D = ValToStrPtr(DataVal);
      uint8_t KeyBytes[16] = {0};
      static const char hexchars[] = "0123456789abcdef";
      for (size_t i = 0; i + 1 < (size_t)K->len && (i / 2) < 16; i += 2) {
        char c1 = K->data[i], c2 = K->data[i + 1];
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
        KeyBytes[i / 2] = (b1 << 4) | b2;
      }
      uint64_t Digest =
          CryptoSipHash64(KeyBytes, (const uint8_t *)D->data, D->len);
      const uint8_t *db = (const uint8_t *)&Digest;
      for (size_t i = 0; i < 8; i++) {
        OutHex[i * 2] = hexchars[(db[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[db[i] & 0x0F];
      }
      OutHex[16] = '\0';
    }
    ValFree(&KeyVal);
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoSipHash128Hash") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoSipHash128Hash() expects 2 "
                      "arguments (key_hex, data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    AKSValue KeyVal = POP();
    char OutHex[33] = {0};
    if (ValKind(KeyVal) == VALSTR && ValKind(DataVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *D = ValToStrPtr(DataVal);
      uint8_t KeyBytes[16] = {0};
      static const char hexchars[] = "0123456789abcdef";
      for (size_t i = 0; i + 1 < (size_t)K->len && (i / 2) < 16; i += 2) {
        char c1 = K->data[i], c2 = K->data[i + 1];
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
        KeyBytes[i / 2] = (b1 << 4) | b2;
      }
      uint8_t Digest[16] = {0};
      CryptoSipHash128(KeyBytes, (const uint8_t *)D->data, D->len, Digest);
      for (size_t i = 0; i < 16; i++) {
        OutHex[i * 2] = hexchars[(Digest[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[Digest[i] & 0x0F];
      }
      OutHex[32] = '\0';
    }
    ValFree(&KeyVal);
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoXChaCha20Poly1305EncryptC") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoXChaCha20Poly1305EncryptC() "
                      "expects 4 arguments (key_hex, nonce_hex, aad, pt)\n");
      return -1;
    }
    AKSValue PtVal = POP(), AadVal = POP(), NonceVal = POP(), KeyVal = POP();
    char *CtHex = NULL;
    char TagHex[33] = {0};
    static const char hexchars[] = "0123456789abcdef";

    if (ValKind(KeyVal) == VALSTR && ValKind(NonceVal) == VALSTR &&
        ValKind(PtVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *N = ValToStrPtr(NonceVal);
      AKSString *P = ValToStrPtr(PtVal);
      const char *AadData =
          (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->data : "";
      size_t AadLen =
          (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->len : 0;

      uint8_t KeyBytes[32] = {0}, NonceBytes[24] = {0};
      for (size_t i = 0; i + 1 < (size_t)K->len && (i / 2) < 32; i += 2) {
        char c1 = K->data[i], c2 = K->data[i + 1];
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
        KeyBytes[i / 2] = (b1 << 4) | b2;
      }
      for (size_t i = 0; i + 1 < (size_t)N->len && (i / 2) < 24; i += 2) {
        char c1 = N->data[i], c2 = N->data[i + 1];
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
        NonceBytes[i / 2] = (b1 << 4) | b2;
      }

      uint8_t *CtBytes = malloc(P->len > 0 ? P->len : 1);
      uint8_t TagBytes[16] = {0};
      CryptoXChaCha20Poly1305Encrypt(
          KeyBytes, NonceBytes, (const uint8_t *)AadData, AadLen,
          (const uint8_t *)P->data, P->len, CtBytes, TagBytes);

      CtHex = malloc(P->len * 2 + 1);
      for (size_t i = 0; i < (size_t)P->len; i++) {
        CtHex[i * 2] = hexchars[(CtBytes[i] >> 4) & 0x0F];
        CtHex[i * 2 + 1] = hexchars[CtBytes[i] & 0x0F];
      }
      CtHex[P->len * 2] = '\0';
      free(CtBytes);

      for (size_t i = 0; i < 16; i++) {
        TagHex[i * 2] = hexchars[(TagBytes[i] >> 4) & 0x0F];
        TagHex[i * 2 + 1] = hexchars[TagBytes[i] & 0x0F];
      }
      TagHex[32] = '\0';
    }

    ValFree(&KeyVal);
    ValFree(&NonceVal);
    ValFree(&AadVal);
    ValFree(&PtVal);

    AKSValue Res = ValStruct("XChaCha20Poly1305Cipher");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("ciphertext");
    S->fieldvals[0] = ValStr(CtHex ? CtHex : "");
    S->fieldnames[1] = strdup("tag");
    S->fieldvals[1] = ValStr(TagHex);
    if (CtHex)
      free(CtHex);

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoXChaCha20Poly1305DecryptC") == 0) {
    if (argc != 5) {
      fprintf(stderr,
              "Runtime error: CryptoXChaCha20Poly1305DecryptC() expects 5 "
              "arguments (key_hex, nonce_hex, aad, ct_hex, tag_hex)\n");
      return -1;
    }
    AKSValue TagVal = POP(), CtVal = POP(), AadVal = POP(), NonceVal = POP(),
             KeyVal = POP();
    char *PlaintextStr = NULL;

    if (ValKind(KeyVal) == VALSTR && ValKind(NonceVal) == VALSTR &&
        ValKind(CtVal) == VALSTR && ValKind(TagVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *N = ValToStrPtr(NonceVal);
      AKSString *C = ValToStrPtr(CtVal);
      AKSString *T = ValToStrPtr(TagVal);
      const char *AadData =
          (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->data : "";
      size_t AadLen =
          (ValKind(AadVal) == VALSTR) ? ValToStrPtr(AadVal)->len : 0;

      uint8_t KeyBytes[32] = {0}, NonceBytes[24] = {0}, TagBytes[16] = {0};
      for (size_t i = 0; i + 1 < (size_t)K->len && (i / 2) < 32; i += 2) {
        char c1 = K->data[i], c2 = K->data[i + 1];
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
        KeyBytes[i / 2] = (b1 << 4) | b2;
      }
      for (size_t i = 0; i + 1 < (size_t)N->len && (i / 2) < 24; i += 2) {
        char c1 = N->data[i], c2 = N->data[i + 1];
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
        NonceBytes[i / 2] = (b1 << 4) | b2;
      }
      for (size_t i = 0; i + 1 < (size_t)T->len && (i / 2) < 16; i += 2) {
        char c1 = T->data[i], c2 = T->data[i + 1];
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
        TagBytes[i / 2] = (b1 << 4) | b2;
      }

      size_t CtLen = C->len / 2;
      uint8_t *CtBytes = malloc(CtLen > 0 ? CtLen : 1);
      for (size_t i = 0; i < CtLen; i++) {
        char c1 = C->data[i * 2], c2 = C->data[i * 2 + 1];
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
        CtBytes[i] = (b1 << 4) | b2;
      }

      uint8_t *PtBytes = malloc(CtLen + 1);
      int Ok = CryptoXChaCha20Poly1305Decrypt(
          KeyBytes, NonceBytes, (const uint8_t *)AadData, AadLen, CtBytes,
          CtLen, TagBytes, PtBytes);
      if (Ok) {
        PtBytes[CtLen] = '\0';
        PlaintextStr = (char *)PtBytes;
      } else {
        free(PtBytes);
      }
      free(CtBytes);
    }

    ValFree(&KeyVal);
    ValFree(&NonceVal);
    ValFree(&AadVal);
    ValFree(&CtVal);
    ValFree(&TagVal);

    if (PlaintextStr) {
      PUSH(ValStr(PlaintextStr));
      free(PlaintextStr);
    } else {
      PUSH(ValNull());
    }
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoSha512_256HashC") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: CryptoSha512_256HashC() expects 1 argument (data)\n");
      return -1;
    }
    AKSValue DataVal = POP();
    char OutHex[65] = {0};
    if (ValKind(DataVal) == VALSTR) {
      AKSString *D = ValToStrPtr(DataVal);
      uint8_t Digest[32] = {0};
      CryptoSha512_256Hash((const uint8_t *)D->data, D->len, Digest);
      static const char hexchars[] = "0123456789abcdef";
      for (size_t i = 0; i < 32; i++) {
        OutHex[i * 2] = hexchars[(Digest[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[Digest[i] & 0x0F];
      }
      OutHex[64] = '\0';
    }
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoHkdfExtractC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoHkdfExtractC() expects 2 arguments "
                      "(salt_hex, ikm_hex)\n");
      return -1;
    }
    AKSValue IkmVal = POP(), SaltVal = POP();
    char OutHex[65] = {0};
    if (ValKind(SaltVal) == VALSTR && ValKind(IkmVal) == VALSTR) {
      AKSString *S = ValToStrPtr(SaltVal);
      AKSString *I = ValToStrPtr(IkmVal);
      size_t SaltLen = S->len / 2, IkmLen = I->len / 2;
      uint8_t *SaltBytes = malloc(SaltLen > 0 ? SaltLen : 1);
      uint8_t *IkmBytes = malloc(IkmLen > 0 ? IkmLen : 1);

      for (size_t i = 0; i < SaltLen; i++) {
        char c1 = S->data[i * 2], c2 = S->data[i * 2 + 1];
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
        SaltBytes[i] = (b1 << 4) | b2;
      }
      for (size_t i = 0; i < IkmLen; i++) {
        char c1 = I->data[i * 2], c2 = I->data[i * 2 + 1];
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
        IkmBytes[i] = (b1 << 4) | b2;
      }

      uint8_t Prk[32] = {0};
      CryptoHkdfExtract(SaltBytes, SaltLen, IkmBytes, IkmLen, Prk);
      free(SaltBytes);
      free(IkmBytes);

      static const char hexchars[] = "0123456789abcdef";
      for (size_t i = 0; i < 32; i++) {
        OutHex[i * 2] = hexchars[(Prk[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[Prk[i] & 0x0F];
      }
      OutHex[64] = '\0';
    }
    ValFree(&SaltVal);
    ValFree(&IkmVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoHkdfExpandC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: CryptoHkdfExpandC() expects 3 arguments "
                      "(prk_hex, info, length_bytes)\n");
      return -1;
    }
    AKSValue LenVal = POP(), InfoVal = POP(), PrkVal = POP();
    char *OutHex = NULL;
    if (ValKind(PrkVal) == VALSTR) {
      AKSString *P = ValToStrPtr(PrkVal);
      const char *InfoData =
          (ValKind(InfoVal) == VALSTR) ? ValToStrPtr(InfoVal)->data : "";
      size_t InfoLen =
          (ValKind(InfoVal) == VALSTR) ? ValToStrPtr(InfoVal)->len : 0;
      size_t OkmLen = (size_t)ValToInt(LenVal);
      if (OkmLen > 8192)
        OkmLen = 8192;

      uint8_t Prk[32] = {0};
      for (size_t i = 0; i + 1 < (size_t)P->len && (i / 2) < 32; i += 2) {
        char c1 = P->data[i], c2 = P->data[i + 1];
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
        Prk[i / 2] = (b1 << 4) | b2;
      }

      uint8_t *Okm = malloc(OkmLen > 0 ? OkmLen : 1);
      CryptoHkdfExpand(Prk, (const uint8_t *)InfoData, InfoLen, Okm, OkmLen);

      static const char hexchars[] = "0123456789abcdef";
      OutHex = malloc(OkmLen * 2 + 1);
      for (size_t i = 0; i < OkmLen; i++) {
        OutHex[i * 2] = hexchars[(Okm[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[Okm[i] & 0x0F];
      }
      OutHex[OkmLen * 2] = '\0';
      free(Okm);
    }
    ValFree(&PrkVal);
    ValFree(&InfoVal);
    ValFree(&LenVal);
    PUSH(ValStr(OutHex ? OutHex : ""));
    if (OutHex)
      free(OutHex);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoAes128CmacAuthenticateC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoAes128CmacAuthenticateC() expects "
                      "2 arguments (key_hex, data)\n");
      return -1;
    }
    AKSValue DataVal = POP(), KeyVal = POP();
    char OutHex[33] = {0};
    if (ValKind(KeyVal) == VALSTR && ValKind(DataVal) == VALSTR) {
      AKSString *K = ValToStrPtr(KeyVal);
      AKSString *D = ValToStrPtr(DataVal);
      uint8_t KeyBytes[16] = {0};
      for (size_t i = 0; i + 1 < (size_t)K->len && (i / 2) < 16; i += 2) {
        char c1 = K->data[i], c2 = K->data[i + 1];
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
        KeyBytes[i / 2] = (b1 << 4) | b2;
      }
      uint8_t Mac[16] = {0};
      CryptoAes128Cmac(KeyBytes, (const uint8_t *)D->data, D->len, Mac);
      static const char hexchars[] = "0123456789abcdef";
      for (size_t i = 0; i < 16; i++) {
        OutHex[i * 2] = hexchars[(Mac[i] >> 4) & 0x0F];
        OutHex[i * 2 + 1] = hexchars[Mac[i] & 0x0F];
      }
      OutHex[32] = '\0';
    }
    ValFree(&KeyVal);
    ValFree(&DataVal);
    PUSH(ValStr(OutHex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Base32EncodeC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Base32EncodeC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }
    static const char b32alph[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    const unsigned char *inp = (const unsigned char *)ValToStrPtr(SV)->data;
    int inlen = ValToStrPtr(SV)->len;
    int outlen = ((inlen + 4) / 5) * 8;
    char *out = malloc(outlen + 1);
    int oi = 0;
    for (int i = 0; i < inlen; i += 5) {
      uint64_t buf = 0;
      int nb = inlen - i < 5 ? inlen - i : 5;
      for (int j = 0; j < nb; j++)
        buf = (buf << 8) | inp[i + j];
      buf <<= (5 - nb) * 8;
      int nchars = (nb * 8 + 4) / 5;
      for (int j = 0; j < 8; j++) {
        out[oi++] = (j < nchars) ? b32alph[(buf >> (35 - j * 5)) & 0x1f] : '=';
      }
    }
    out[oi] = 0;
    ValFree(&SV);
    PUSH(ValStr(out));
    free(out);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Base32DecodeC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Base32DecodeC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }
    const unsigned char *inp2 = (const unsigned char *)ValToStrPtr(SV)->data;
    int inlen2 = ValToStrPtr(SV)->len;
    char *out2 = malloc(inlen2 + 1);
    int oi2 = 0;
    static const signed char b32dec[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29,
        30, 31, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
        7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
        11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1};
    for (int i = 0; i + 7 < inlen2; i += 8) {
      uint64_t buf2 = 0;
      int pad = 0;
      for (int j = 0; j < 8; j++) {
        if (inp2[i + j] == '=') {
          pad++;
          buf2 <<= 5;
        } else
          buf2 = (buf2 << 5) | (uint64_t)b32dec[inp2[i + j]];
      }
      int nb2 = 5 - (pad * 5 + 7) / 8;
      uint64_t shifted = buf2 << (pad * 5);
      for (int j = 0; j < nb2; j++)
        out2[oi2++] = (char)((shifted >> ((4 - j) * 8)) & 0xff);
    }
    out2[oi2] = 0;
    ValFree(&SV);
    PUSH(ValStr(out2));
    free(out2);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Base85EncodeC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Base85EncodeC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }
    const unsigned char *b85in = (const unsigned char *)ValToStrPtr(SV)->data;
    int b85len = ValToStrPtr(SV)->len;
    char *b85out = malloc(((b85len + 3) / 4) * 5 + 10);
    int b85oi = 0;
    for (int i = 0; i < b85len; i += 4) {
      uint32_t val2 = 0;
      int nb = b85len - i < 4 ? b85len - i : 4;
      for (int j = 0; j < nb; j++)
        val2 = (val2 << 8) | b85in[i + j];
      val2 <<= (4 - nb) * 8;
      if (val2 == 0 && nb == 4) {
        b85out[b85oi++] = 'z';
      } else {
        char tmp[5];
        for (int j = 4; j >= 0; j--) {
          tmp[j] = (char)('!' + val2 % 85);
          val2 /= 85;
        }
        for (int j = 0; j <= nb; j++)
          b85out[b85oi++] = tmp[j];
      }
    }
    b85out[b85oi] = 0;
    ValFree(&SV);
    PUSH(ValStr(b85out));
    free(b85out);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Base85DecodeC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Base85DecodeC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *b85di = ValToStrPtr(SV)->data;
    int b85dl = ValToStrPtr(SV)->len;
    char *b85do = malloc(b85dl + 8);
    int b85doi = 0;
    for (int i = 0; i < b85dl;) {
      if (b85di[i] == 'z') {
        b85do[b85doi++] = 0;
        b85do[b85doi++] = 0;
        b85do[b85doi++] = 0;
        b85do[b85doi++] = 0;
        i++;
        continue;
      }
      uint32_t acc = 0;
      int cnt = 0;
      while (i < b85dl && cnt < 5) {
        acc = acc * 85 + (uint32_t)(b85di[i++] - '!');
        cnt++;
      }
      if (cnt <= 1)
        break;
      int pad = 5 - cnt;
      for (int p = 0; p < pad; p++)
        acc = acc * 85 + 84;
      char tmp[4];
      for (int j = 3; j >= 0; j--) {
        tmp[j] = (char)(acc & 0xff);
        acc >>= 8;
      }
      for (int j = 0; j < cnt - 1; j++)
        b85do[b85doi++] = tmp[j];
    }
    b85do[b85doi] = 0;
    ValFree(&SV);
    PUSH(ValStr(b85do));
    free(b85do);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoCrc32C") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CryptoCrc32C() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValInt(0));
      *stacktopptr = stacktop;
      return 1;
    }
    static uint32_t crc32_tbl[256];
    static int crc32_inited = 0;
    if (!crc32_inited) {
      for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
          c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_tbl[i] = c;
      }
      crc32_inited = 1;
    }
    const unsigned char *crc_data =
        (const unsigned char *)ValToStrPtr(SV)->data;
    int crc_len = ValToStrPtr(SV)->len;
    uint32_t crc32_val = 0xFFFFFFFFu;
    for (int i = 0; i < crc_len; i++)
      crc32_val =
          (crc32_val >> 8) ^ crc32_tbl[(crc32_val ^ crc_data[i]) & 0xFF];
    crc32_val ^= 0xFFFFFFFFu;
    ValFree(&SV);
    PUSH(ValInt((int64_t)crc32_val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoXxHashC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: CryptoXxHashC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValInt(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const uint8_t *xxh_d = (const uint8_t *)ValToStrPtr(SV)->data;
    int xxh_len = ValToStrPtr(SV)->len;
    static const uint64_t XXH_P1 = 11400714785074694791ULL,
                          XXH_P2 = 14029467366897019727ULL;
    static const uint64_t XXH_P3 = 1609587929392839161ULL,
                          XXH_P4 = 9650029242287828579ULL;
    static const uint64_t XXH_P5 = 2870177450012600261ULL;
    uint64_t xxh_h = (uint64_t)xxh_len + XXH_P5;
    int xxh_i = 0;
    while (xxh_i + 8 <= xxh_len) {
      uint64_t k;
      memcpy(&k, xxh_d + xxh_i, 8);
      k *= XXH_P2;
      k = (k << 31) | (k >> 33);
      k *= XXH_P1;
      xxh_h ^= k;
      xxh_h = ((xxh_h << 27) | (xxh_h >> 37)) * XXH_P1 + XXH_P4;
      xxh_i += 8;
    }
    while (xxh_i + 4 <= xxh_len) {
      uint32_t k4;
      memcpy(&k4, xxh_d + xxh_i, 4);
      xxh_h ^= (uint64_t)k4 * XXH_P1;
      xxh_h = ((xxh_h << 23) | (xxh_h >> 41)) * XXH_P2 + XXH_P3;
      xxh_i += 4;
    }
    while (xxh_i < xxh_len) {
      xxh_h ^= (uint64_t)xxh_d[xxh_i] * XXH_P5;
      xxh_h = ((xxh_h << 11) | (xxh_h >> 53)) * XXH_P1;
      xxh_i++;
    }
    xxh_h ^= xxh_h >> 33;
    xxh_h *= XXH_P2;
    xxh_h ^= xxh_h >> 29;
    xxh_h *= XXH_P3;
    xxh_h ^= xxh_h >> 32;
    ValFree(&SV);
    PUSH(ValInt((int64_t)xxh_h));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoTotpC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoTotpC() expects 2 args "
                      "(base32_secret, timestamp_sec)\n");
      return -1;
    }
    AKSValue TV4 = POP(), SV2 = POP();
    int64_t ts = ValToInt(TV4);
    uint64_t totp_t = (uint64_t)(ts / 30);
    const char *b32s = (ValKind(SV2) == VALSTR) ? ValToStrPtr(SV2)->data : "";
    static const signed char totp_dec[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29,
        30, 31, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
        7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
        11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1};
    uint8_t totp_key[64];
    int totp_klen = 0;
    for (int i = 0; b32s[i] && totp_klen < 64; i += 8) {
      uint64_t buf2 = 0;
      int pad = 0;
      for (int j = 0; j < 8 && b32s[i + j]; j++) {
        if (b32s[i + j] == '=') {
          pad++;
          buf2 <<= 5;
        } else
          buf2 = (buf2 << 5) | (uint64_t)totp_dec[(unsigned char)b32s[i + j]];
      }
      int nb2 = 5 - (pad * 5 + 7) / 8;
      uint64_t shifted2 = buf2 << (pad * 5);
      for (int j = 0; j < nb2 && totp_klen < 64; j++)
        totp_key[totp_klen++] = (uint8_t)((shifted2 >> ((4 - j) * 8)) & 0xff);
    }
    uint8_t totp_msg[8];
    for (int i = 7; i >= 0; i--) {
      totp_msg[i] = (uint8_t)(totp_t & 0xff);
      totp_t >>= 8;
    }
    uint8_t totp_hmac[32] = {0};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE totp_alg = NULL;
    BCRYPT_HASH_HANDLE totp_hh = NULL;
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&totp_alg, BCRYPT_SHA1_ALGORITHM,
                                               NULL,
                                               BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
      if (NT_SUCCESS(BCryptCreateHash(totp_alg, &totp_hh, NULL, 0, totp_key,
                                      (ULONG)totp_klen, 0))) {
        BCryptHashData(totp_hh, totp_msg, 8, 0);
        BCryptFinishHash(totp_hh, totp_hmac, 20, 0);
        BCryptDestroyHash(totp_hh);
      }
      BCryptCloseAlgorithmProvider(totp_alg, 0);
    }
#else
    CryptoHmacSha1C(totp_key, (size_t)totp_klen, totp_msg, 8, totp_hmac);
#endif
    int totp_off = totp_hmac[19] & 0xf;
    uint32_t totp_code = (((uint32_t)totp_hmac[totp_off] & 0x7f) << 24) |
                         ((uint32_t)totp_hmac[totp_off + 1] << 16) |
                         ((uint32_t)totp_hmac[totp_off + 2] << 8) |
                         (uint32_t)totp_hmac[totp_off + 3];
    totp_code %= 1000000;
    ValFree(&TV4);
    ValFree(&SV2);
    PUSH(ValInt((int64_t)totp_code));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoPbkdf2C") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: CryptoPbkdf2C() expects 4 args (pass, "
                      "salt, iters, keylen)\n");
      return -1;
    }
    AKSValue kl_v = POP(), it_v = POP(), sa_v = POP(), pa_v = POP();
    int pb_klen = (int)ValToInt(kl_v), pb_iters = (int)ValToInt(it_v);
    if (pb_klen <= 0 || pb_klen > 64)
      pb_klen = 32;
    if (pb_iters <= 0)
      pb_iters = 10000;
    const char *pb_pass =
        (ValKind(pa_v) == VALSTR) ? ValToStrPtr(pa_v)->data : "";
    int pb_plen = (ValKind(pa_v) == VALSTR) ? ValToStrPtr(pa_v)->len : 0;
    const char *pb_salt =
        (ValKind(sa_v) == VALSTR) ? ValToStrPtr(sa_v)->data : "";
    int pb_slen = (ValKind(sa_v) == VALSTR) ? ValToStrPtr(sa_v)->len : 0;
    uint8_t pb_key[64] = {0};
    CryptoPbkdf2Sha256C((const uint8_t *)pb_pass, (size_t)pb_plen,
                        (const uint8_t *)pb_salt, (size_t)pb_slen,
                        (uint32_t)pb_iters, pb_key, (size_t)pb_klen);
    char pb_hex[130] = {0};
    for (int i = 0; i < pb_klen && i < 64; i++)
      sprintf(pb_hex + i * 2, "%02x", (unsigned int)pb_key[i]);
    ValFree(&kl_v);
    ValFree(&it_v);
    ValFree(&sa_v);
    ValFree(&pa_v);
    PUSH(ValStr(pb_hex));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoAesCbcEncryptC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: CryptoAesCbcEncryptC() expects 3 args "
                      "(key16or32, iv16, plaintext)\n");
      return -1;
    }
    AKSValue pt_v = POP(), iv_v = POP(), ky_v = POP();
    if (ValKind(ky_v) != VALSTR || ValKind(iv_v) != VALSTR ||
        ValKind(pt_v) != VALSTR) {
      ValFree(&pt_v);
      ValFree(&iv_v);
      ValFree(&ky_v);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const uint8_t *aes_key = (const uint8_t *)ValToStrPtr(ky_v)->data;
    int aes_klen = ValToStrPtr(ky_v)->len;
    const uint8_t *aes_iv = (const uint8_t *)ValToStrPtr(iv_v)->data;
    const uint8_t *aes_pt = (const uint8_t *)ValToStrPtr(pt_v)->data;
    int aes_ptlen = ValToStrPtr(pt_v)->len;
    int aes_pad = 16 - (aes_ptlen % 16);
    int aes_ctlen = aes_ptlen + aes_pad;
    uint8_t *aes_ct = malloc((size_t)aes_ctlen);
    memcpy(aes_ct, aes_pt, aes_ptlen);
    memset(aes_ct + aes_ptlen, aes_pad, aes_pad);
    AKSValue aes_res = ValNull();
#ifdef _WIN32
    BCRYPT_ALG_HANDLE aes_alg = NULL;
    BCRYPT_KEY_HANDLE aes_kh = NULL;
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&aes_alg, BCRYPT_AES_ALGORITHM,
                                               NULL, 0))) {
      BCryptSetProperty(aes_alg, BCRYPT_CHAINING_MODE,
                        (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                        sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
      ULONG aes_ks = 0;
      ULONG aes_tmp = 0;
      BCryptGetProperty(aes_alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&aes_ks,
                        sizeof(aes_ks), &aes_tmp, 0);
      uint8_t *aes_obj = malloc(aes_ks);
      if (NT_SUCCESS(BCryptGenerateSymmetricKey(aes_alg, &aes_kh, aes_obj,
                                                aes_ks, (PUCHAR)aes_key,
                                                (ULONG)aes_klen, 0))) {
        uint8_t aes_iv2[16];
        memcpy(aes_iv2, aes_iv, 16);
        ULONG aes_out = 0;
        if (NT_SUCCESS(BCryptEncrypt(aes_kh, aes_ct, (ULONG)aes_ctlen, NULL,
                                     aes_iv2, 16, aes_ct, (ULONG)aes_ctlen,
                                     &aes_out, 0))) {
          char *aes_hex = malloc(aes_ctlen * 2 + 1);
          for (int i = 0; i < aes_ctlen; i++)
            sprintf(aes_hex + i * 2, "%02x", (unsigned int)aes_ct[i]);
          aes_hex[aes_ctlen * 2] = 0;
          aes_res = ValStr(aes_hex);
          free(aes_hex);
        }
        BCryptDestroyKey(aes_kh);
      }
      free(aes_obj);
      BCryptCloseAlgorithmProvider(aes_alg, 0);
    }
#else
    uint8_t aes_iv2[16] = {0};
    int iv_cpy = ValToStrPtr(iv_v)->len > 16 ? 16 : ValToStrPtr(iv_v)->len;
    memcpy(aes_iv2, aes_iv, (size_t)iv_cpy);
    if ((aes_klen == 16 || aes_klen == 24 || aes_klen == 32) &&
        CryptoAesCbcC(1, aes_key, (size_t)aes_klen, aes_iv2, aes_ct,
                      (size_t)aes_ctlen, aes_ct)) {
      char *aes_hex = malloc((size_t)aes_ctlen * 2 + 1);
      for (int i = 0; i < aes_ctlen; i++)
        sprintf(aes_hex + i * 2, "%02x", (unsigned int)aes_ct[i]);
      aes_hex[aes_ctlen * 2] = 0;
      aes_res = ValStr(aes_hex);
      free(aes_hex);
    }
#endif
    free(aes_ct);
    ValFree(&pt_v);
    ValFree(&iv_v);
    ValFree(&ky_v);
    PUSH(aes_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoAesCbcDecryptC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: CryptoAesCbcDecryptC() expects 3 args "
                      "(key, iv, ciphertext_hex)\n");
      return -1;
    }
    AKSValue ct_v = POP(), iv_v = POP(), ky_v = POP();
    if (ValKind(ky_v) != VALSTR || ValKind(iv_v) != VALSTR ||
        ValKind(ct_v) != VALSTR) {
      ValFree(&ct_v);
      ValFree(&iv_v);
      ValFree(&ky_v);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const uint8_t *aes_key = (const uint8_t *)ValToStrPtr(ky_v)->data;
    int aes_klen = ValToStrPtr(ky_v)->len;
    const uint8_t *aes_iv = (const uint8_t *)ValToStrPtr(iv_v)->data;
    const char *ct_hex = ValToStrPtr(ct_v)->data;
    int ct_hlen = ValToStrPtr(ct_v)->len;
    int ct_len = ct_hlen / 2;
    uint8_t *ct_bytes = (uint8_t *)malloc(ct_len > 0 ? (size_t)ct_len : 1);
    for (int i = 0; i < ct_len; i++) {
      unsigned int b = 0;
      sscanf(ct_hex + i * 2, "%02x", &b);
      ct_bytes[i] = (uint8_t)b;
    }
    AKSValue aes_res = ValNull();
#ifdef _WIN32
    BCRYPT_ALG_HANDLE aes_alg = NULL;
    BCRYPT_KEY_HANDLE aes_kh = NULL;
    if (ct_len > 0 && NT_SUCCESS(BCryptOpenAlgorithmProvider(
                          &aes_alg, BCRYPT_AES_ALGORITHM, NULL, 0))) {
      BCryptSetProperty(aes_alg, BCRYPT_CHAINING_MODE,
                        (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                        sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
      ULONG aes_ks = 0;
      ULONG aes_tmp = 0;
      BCryptGetProperty(aes_alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&aes_ks,
                        sizeof(aes_ks), &aes_tmp, 0);
      uint8_t *aes_obj = malloc(aes_ks);
      if (NT_SUCCESS(BCryptGenerateSymmetricKey(aes_alg, &aes_kh, aes_obj,
                                                aes_ks, (PUCHAR)aes_key,
                                                (ULONG)aes_klen, 0))) {
        uint8_t aes_iv2[16];
        memset(aes_iv2, 0, 16);
        int iv_cpy = ValToStrPtr(iv_v)->len > 16 ? 16 : ValToStrPtr(iv_v)->len;
        memcpy(aes_iv2, aes_iv, iv_cpy);
        ULONG aes_out = 0;
        uint8_t *pt_bytes = (uint8_t *)malloc((size_t)ct_len + 16);
        if (NT_SUCCESS(BCryptDecrypt(aes_kh, ct_bytes, (ULONG)ct_len, NULL,
                                     aes_iv2, 16, pt_bytes, (ULONG)ct_len,
                                     &aes_out, 0))) {
          int pad = (aes_out > 0) ? pt_bytes[aes_out - 1] : 0;
          int final_len = (int)aes_out;
          if (pad >= 1 && pad <= 16 && pad <= final_len)
            final_len -= pad;
          char *res_str = malloc((size_t)final_len + 1);
          memcpy(res_str, pt_bytes, final_len);
          res_str[final_len] = 0;
          aes_res = ValStr(res_str);
          free(res_str);
        }
        free(pt_bytes);
        BCryptDestroyKey(aes_kh);
      }
      free(aes_obj);
      BCryptCloseAlgorithmProvider(aes_alg, 0);
    }
#else
    if (ct_len > 0 && (ct_len % 16) == 0 &&
        (aes_klen == 16 || aes_klen == 24 || aes_klen == 32)) {
      uint8_t aes_iv2[16] = {0};
      int iv_cpy = ValToStrPtr(iv_v)->len > 16 ? 16 : ValToStrPtr(iv_v)->len;
      memcpy(aes_iv2, aes_iv, (size_t)iv_cpy);
      uint8_t *pt_bytes = (uint8_t *)malloc((size_t)ct_len + 16);
      if (pt_bytes && CryptoAesCbcC(0, aes_key, (size_t)aes_klen, aes_iv2,
                                    ct_bytes, (size_t)ct_len, pt_bytes)) {
        int pad = pt_bytes[ct_len - 1];
        int final_len = ct_len;
        if (pad >= 1 && pad <= 16 && pad <= final_len)
          final_len -= pad;
        char *res_str = (char *)malloc((size_t)final_len + 1);
        memcpy(res_str, pt_bytes, (size_t)final_len);
        res_str[final_len] = 0;
        aes_res = ValStr(res_str);
        free(res_str);
      }
      free(pt_bytes);
    }
#endif
    free(ct_bytes);
    ValFree(&ct_v);
    ValFree(&iv_v);
    ValFree(&ky_v);
    PUSH(aes_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptoX25519C") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: CryptoX25519C() expects 2 args "
                      "(my_privkey32hex, their_pubkey32hex)\n");
      return -1;
    }
    AKSValue pk_v = POP(), sk_v = POP();
    AKSValue x25_res = ValNull();
#ifdef _WIN32
    BCRYPT_ALG_HANDLE x25_alg = NULL;
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(
            &x25_alg, BCRYPT_ECDH_P256_ALGORITHM, NULL, 0))) {
      BCRYPT_KEY_HANDLE x25_kh = NULL;
      if (NT_SUCCESS(BCryptGenerateKeyPair(x25_alg, &x25_kh, 256, 0))) {
        BCryptFinalizeKeyPair(x25_kh, 0);
        ULONG x25_bloblen = 0;
        ULONG x25_tmp2 = 0;
        BCryptExportKey(x25_kh, NULL, BCRYPT_ECCPUBLIC_BLOB, NULL, 0,
                        &x25_bloblen, 0);
        uint8_t *x25_blob = malloc(x25_bloblen);
        if (NT_SUCCESS(BCryptExportKey(x25_kh, NULL, BCRYPT_ECCPUBLIC_BLOB,
                                       x25_blob, x25_bloblen, &x25_tmp2, 0))) {
          char x25_hex[128] = {0};
          int x25_copy = x25_bloblen > 32 ? 32 : (int)x25_bloblen;
          for (int i = 0; i < x25_copy; i++)
            sprintf(x25_hex + i * 2, "%02x", (unsigned int)x25_blob[i]);
          x25_res = ValStr(x25_hex);
        }
        free(x25_blob);
        BCryptDestroyKey(x25_kh);
      }
      BCryptCloseAlgorithmProvider(x25_alg, 0);
    }
#else
    char secret_hex[128] = {0}, pub_hex[128] = {0}, priv_hex[128] = {0};
    if (ValKind(sk_v) == VALSTR && ValKind(pk_v) == VALSTR &&
        ValToStrPtr(sk_v)->len == 64 && ValToStrPtr(pk_v)->len == 64 &&
        X25519SharedSecret(ValToStrPtr(sk_v)->data, ValToStrPtr(pk_v)->data,
                           secret_hex)) {
      x25_res = ValStr(secret_hex);
    } else if (Curve25519GenerateHex(BCRYPT_ECDH_ALGORITHM, pub_hex,
                                     priv_hex)) {
      x25_res = ValStr(pub_hex);
    }
#endif
    ValFree(&pk_v);
    ValFree(&sk_v);
    PUSH(x25_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SimdCrc32cC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SimdCrc32cC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValInt(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const uint8_t *sc_d = (const uint8_t *)ValToStrPtr(SV)->data;
    int sc_len = ValToStrPtr(SV)->len;
    uint32_t sc_crc = 0xFFFFFFFFu;
    int sc_i = 0;
#if defined(__SSE4_2__) || defined(_M_X64) || defined(_M_IX86)
    while (sc_i + 8 <= sc_len) {
      uint64_t sc_v;
      memcpy(&sc_v, sc_d + sc_i, 8);
      sc_crc = (uint32_t)_mm_crc32_u64(sc_crc, sc_v);
      sc_i += 8;
    }
    while (sc_i + 4 <= sc_len) {
      sc_crc = _mm_crc32_u32(sc_crc, *(const uint32_t *)(sc_d + sc_i));
      sc_i += 4;
    }
    while (sc_i < sc_len) {
      sc_crc = _mm_crc32_u8(sc_crc, sc_d[sc_i++]);
    }
#else
    (void)sc_i;
    sc_crc = AksCrc32cSw(sc_crc, sc_d, (size_t)(sc_len > 0 ? sc_len : 0));
#endif
    sc_crc ^= 0xFFFFFFFFu;
    ValFree(&SV);
    PUSH(ValInt((int64_t)sc_crc));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ZipArchiveCreateC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ZipArchiveCreateC() expects 2 args "
                      "(zip_path, files_array)\n");
      return -1;
    }
    AKSValue fv = POP(), pv = POP();
    const char *zpath =
        (ValKind(pv) == VALSTR) ? ValToStrPtr(pv)->data : "output.zip";
    AKSArray *farr = ValToArr(fv);
    FILE *zf = fopen(zpath, "wb");
    int ok = 0;
    if (zf && farr) {
      typedef struct {
        char name[256];
        uint32_t offset;
        uint32_t crc;
        uint32_t size;
      } ZipEntryInfo;
      ZipEntryInfo *entries = calloc(farr->count + 1, sizeof(ZipEntryInfo));
      uint32_t cd_offset = 0;
      for (int i = 0; i < farr->count; i++) {
        const char *item_name = "";
        const char *item_data = "";
        size_t item_len = 0;
        char *allocated_data = NULL;
        if (ValKind(farr->items[i]) == VALSTR) {
          item_name = ValToStrPtr(farr->items[i])->data;
          FILE *inf = fopen(item_name, "rb");
          if (inf) {
            fseek(inf, 0, SEEK_END);
            item_len = ftell(inf);
            fseek(inf, 0, SEEK_SET);
            allocated_data = malloc(item_len + 1);
            if (allocated_data) {
              size_t rd = fread(allocated_data, 1, item_len, inf);
              allocated_data[rd] = '\0';
              item_len = (long)rd;
              item_data = allocated_data;
            }
            fclose(inf);
          }
        } else if (ValKind(farr->items[i]) == VALSTRUCT) {
          AKSStruct *st = ValToObj(farr->items[i]);
          for (int f = 0; f < st->fieldcount; f++) {
            if (strcmp(st->fieldnames[f], "name") == 0 &&
                ValKind(st->fieldvals[f]) == VALSTR)
              item_name = ValToStrPtr(st->fieldvals[f])->data;
            if (strcmp(st->fieldnames[f], "data") == 0 &&
                ValKind(st->fieldvals[f]) == VALSTR) {
              item_data = ValToStrPtr(st->fieldvals[f])->data;
              item_len = (size_t)ValToStrPtr(st->fieldvals[f])->len;
            }
          }
        }
        strncpy(entries[i].name, item_name, 255);
        entries[i].offset = (uint32_t)ftell(zf);
        entries[i].size = (uint32_t)item_len;
        uint32_t crc = 0xFFFFFFFF;
        for (size_t b = 0; b < item_len; b++) {
          crc ^= (uint8_t)item_data[b];
          for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
        }
        entries[i].crc = ~crc;

        uint16_t nlen = (uint16_t)strlen(item_name);
        uint8_t lfh[30] = {0x50, 0x4B, 0x03, 0x04, 20, 0, 0, 0, 0, 0,
                           0,    0,    0,    0,    0,  0, 0, 0, 0, 0,
                           0,    0,    0,    0,    0,  0, 0, 0, 0, 0};
        memcpy(lfh + 14, &entries[i].crc, 4);
        memcpy(lfh + 18, &entries[i].size, 4);
        memcpy(lfh + 22, &entries[i].size, 4);
        memcpy(lfh + 26, &nlen, 2);
        fwrite(lfh, 1, 30, zf);
        fwrite(item_name, 1, nlen, zf);
        if (item_len > 0) {
          fwrite(item_data, 1, item_len, zf);
        }
        if (allocated_data)
          free(allocated_data);
      }

      cd_offset = (uint32_t)ftell(zf);
      for (int i = 0; i < farr->count; i++) {
        uint16_t nlen = (uint16_t)strlen(entries[i].name);
        uint8_t cdh[46] = {0x50, 0x4B, 0x01, 0x02, 20, 0, 20, 0, 0, 0, 0, 0,
                           0,    0,    0,    0,    0,  0, 0,  0, 0, 0, 0, 0,
                           0,    0,    0,    0,    0,  0, 0,  0, 0, 0, 0, 0,
                           0,    0,    0,    0,    0,  0, 0,  0, 0, 0};
        memcpy(cdh + 16, &entries[i].crc, 4);
        memcpy(cdh + 20, &entries[i].size, 4);
        memcpy(cdh + 24, &entries[i].size, 4);
        memcpy(cdh + 28, &nlen, 2);
        memcpy(cdh + 42, &entries[i].offset, 4);
        fwrite(cdh, 1, 46, zf);
        fwrite(entries[i].name, 1, nlen, zf);
      }
      uint32_t cd_size = (uint32_t)ftell(zf) - cd_offset;
      uint16_t total_ent = (uint16_t)farr->count;
      uint8_t eocd[22] = {0x50, 0x4B, 0x05, 0x06, 0, 0, 0, 0, 0, 0, 0,
                          0,    0,    0,    0,    0, 0, 0, 0, 0, 0, 0};
      memcpy(eocd + 8, &total_ent, 2);
      memcpy(eocd + 10, &total_ent, 2);
      memcpy(eocd + 12, &cd_size, 4);
      memcpy(eocd + 16, &cd_offset, 4);
      fwrite(eocd, 1, 22, zf);
      fclose(zf);
      free(entries);
      ok = 1;
    }
    ValFree(&fv);
    ValFree(&pv);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ZipArchiveListC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ZipArchiveListC() expects 1 arg (zip_path)\n");
      return -1;
    }
    AKSValue pv = POP();
    const char *zpath = (ValKind(pv) == VALSTR) ? ValToStrPtr(pv)->data : "";
    FILE *zf = fopen(zpath, "rb");
    AKSValue res = ValArr(8);
    AKSArray *ra = ValToArr(res);
    if (zf) {
      uint8_t sig[4];
      while (fread(sig, 1, 4, zf) == 4) {
        if (sig[0] == 0x50 && sig[1] == 0x4B && sig[2] == 0x03 &&
            sig[3] == 0x04) {
          uint8_t hdr[26];
          if (fread(hdr, 1, 26, zf) != 26)
            break;
          uint16_t nlen = *(uint16_t *)(hdr + 22);
          uint16_t elen = *(uint16_t *)(hdr + 24);
          uint32_t csize = *(uint32_t *)(hdr + 14);
          char *ename = malloc(nlen + 1);
          if (ename) {
            size_t rd = fread(ename, 1, nlen, zf);
            ename[rd] = '\0';
            AksArrPush(ra, ValStr(ename));
            free(ename);
          }
          if (elen > 0)
            fseek(zf, elen, SEEK_CUR);
          if (csize > 0)
            fseek(zf, csize, SEEK_CUR);
        } else {
          break;
        }
      }
      fclose(zf);
    }
    ValFree(&pv);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ZipArchiveExtractC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ZipArchiveExtractC() expects 2 args "
                      "(zip_path, dest_dir)\n");
      return -1;
    }
    AKSValue dv = POP(), pv = POP();
    const char *zpath = (ValKind(pv) == VALSTR) ? ValToStrPtr(pv)->data : "";
    const char *dest = (ValKind(dv) == VALSTR) ? ValToStrPtr(dv)->data : ".";
#ifdef _WIN32
    CreateDirectoryA(dest, NULL);
#else
    mkdir(dest, 0755);
#endif
    FILE *zf = fopen(zpath, "rb");
    int extracted = 0;
    if (zf) {
      uint8_t sig[4];
      while (fread(sig, 1, 4, zf) == 4) {
        if (sig[0] == 0x50 && sig[1] == 0x4B && sig[2] == 0x03 &&
            sig[3] == 0x04) {
          uint8_t hdr[26];
          if (fread(hdr, 1, 26, zf) != 26)
            break;
          uint16_t nlen = *(uint16_t *)(hdr + 22);
          uint16_t elen = *(uint16_t *)(hdr + 24);
          uint32_t csize = *(uint32_t *)(hdr + 14);
          char *ename = malloc(nlen + 1);
          if (ename) {
            size_t rd_name = fread(ename, 1, nlen, zf);
            ename[rd_name] = '\0';
            if (elen > 0)
              fseek(zf, elen, SEEK_CUR);
            char outpath[MAX_PATH];
            snprintf(outpath, sizeof(outpath), "%s/%s", dest, ename);
            FILE *outf = fopen(outpath, "wb");
            if (outf) {
              if (csize > 0) {
                uint8_t *dbuf = malloc(csize);
                if (dbuf) {
                  size_t rd_data = fread(dbuf, 1, csize, zf);
                  if (rd_data > 0)
                    fwrite(dbuf, 1, rd_data, outf);
                  free(dbuf);
                }
              }
              fclose(outf);
              extracted++;
            } else {
              if (csize > 0)
                fseek(zf, csize, SEEK_CUR);
            }
            free(ename);
          }
        } else {
          break;
        }
      }
      fclose(zf);
    }
    ValFree(&dv);
    ValFree(&pv);
    PUSH(ValInt(extracted));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HuffmanDecode") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: HuffmanDecode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    if (ValKind(strval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: HuffmanDecode() argument must be a string\n");
      ValFree(&strval);
      return -1;
    }
    const char *data = ValToStrPtr(strval)->data;
    int len = ValToStrPtr(strval)->len;
    int dec_len = 0;
    char *dec_bytes = HuffmanDecodeStr(data, len, &dec_len);
    if (!dec_bytes) {
      PUSH(ValStr(""));
    } else {
      AKSValue res = ValStrLen(dec_bytes, dec_len);
      PUSH(res);
      free(dec_bytes);
    }
    ValFree(&strval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SecureString") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SecureString() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr,
              "Runtime error: SecureString() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    AKSValue ss = ValStruct("SecureString");
    ValToObj(ss)->fieldcount = 1;
    ValToObj(ss)->fieldnames = malloc(sizeof(char *));
    ValToObj(ss)->fieldvals = malloc(sizeof(AKSValue));
    ValToObj(ss)->fieldnames[0] = strdup("data");
    ValToObj(ss)->fieldvals[0] = arg;
    PUSH(ss);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CryptCompare") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: CryptCompare() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) != VALSTR || ValKind(b) != VALSTR) {
      fprintf(stderr,
              "Runtime error: CryptCompare() arguments must be strings\n");
      ValFree(&a);
      ValFree(&b);
      return -1;
    }
    int alen = ValToStrPtr(a)->len;
    int blen = ValToStrPtr(b)->len;
    int result = 0;
    if (alen != blen) {
      result = 1;
    }
    int len = alen < blen ? alen : blen;
    for (int i = 0; i < len; i++) {
      result |= ValToStrPtr(a)->data[i] ^ ValToStrPtr(b)->data[i];
    }
    PUSH(ValBool(result == 0 && alen == blen));
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConstantTimeCompare") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: ConstantTimeCompare() expects 2 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) != VALSTR || ValKind(b) != VALSTR) {
      fprintf(
          stderr,
          "Runtime error: ConstantTimeCompare() arguments must be strings\n");
      ValFree(&a);
      ValFree(&b);
      return -1;
    }
    int alen = ValToStrPtr(a)->len;
    int blen = ValToStrPtr(b)->len;
    int result = 0;
    if (alen != blen) {
      result = 1;
    }
    int len = alen < blen ? alen : blen;
    for (int i = 0; i < len; i++) {
      result |= ValToStrPtr(a)->data[i] ^ ValToStrPtr(b)->data[i];
    }
    PUSH(ValBool(result == 0 && alen == blen));
    ValFree(&a);
    ValFree(&b);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MD5") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MD5() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: MD5() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    AKSMD5CTX ctx;
    uint8_t digest[16];
    MD5Init(&ctx);
    MD5Update(&ctx, (const uint8_t *)ValToStrPtr(arg)->data,
              ValToStrPtr(arg)->len);
    MD5Final(&ctx, digest);

    char hashstr[33];
    for (int i = 0; i < 16; i++) {
      sprintf(&hashstr[i * 2], "%02x", digest[i]);
    }
    hashstr[32] = '\0';
    PUSH(ValStr(hashstr));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SHA256") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SHA256() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: SHA256() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    AKSSHA256CTX ctx;
    uint8_t digest[32];
    Sha256Init(&ctx);
    Sha256Update(&ctx, (const uint8_t *)ValToStrPtr(arg)->data,
                 ValToStrPtr(arg)->len);
    Sha256Final(&ctx, digest);

    char hashstr[65];
    for (int i = 0; i < 32; i++) {
      sprintf(&hashstr[i * 2], "%02x", digest[i]);
    }
    hashstr[64] = '\0';
    PUSH(ValStr(hashstr));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SHA512") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SHA512() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: SHA512() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    uint8_t digest[64];
    CryptoSha512((const uint8_t *)ValToStrPtr(arg)->data, ValToStrPtr(arg)->len,
                 digest);
    char hashstr[129];
    HexEncodeC(digest, 64, hashstr);
    PUSH(ValStr(hashstr));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SHA3256") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SHA3256() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: SHA3256() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    uint8_t digest[32];
    KeccakHash((const uint8_t *)ValToStrPtr(arg)->data, ValToStrPtr(arg)->len,
               digest, 32, 136);
    char hashstr[65];
    HexEncodeC(digest, 32, hashstr);
    PUSH(ValStr(hashstr));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SHA3512") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SHA3512() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: SHA3512() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    uint8_t digest[64];
    KeccakHash((const uint8_t *)ValToStrPtr(arg)->data, ValToStrPtr(arg)->len,
               digest, 64, 72);
    char hashstr[129];
    HexEncodeC(digest, 64, hashstr);
    PUSH(ValStr(hashstr));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AES256GCMEncrypt") == 0) {
    if (argc != 4) {
      fprintf(stderr,
              "Runtime error: AES256GCMEncrypt() expects 4 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue aad = POP();
    AKSValue pt = POP();
    AKSValue iv = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(iv) != VALSTR ||
        ValKind(pt) != VALSTR || ValKind(aad) != VALSTR) {
      fprintf(stderr,
              "Runtime error: AES256GCMEncrypt() invalid argument types\n");
      ValFree(&aad);
      ValFree(&pt);
      ValFree(&iv);
      ValFree(&key);
      return -1;
    }
    uint8_t key_b[32], iv_b[12];
    HexDecodeC(ValToStrPtr(key)->data, key_b);
    HexDecodeC(ValToStrPtr(iv)->data, iv_b);
    size_t pt_len = ValToStrPtr(pt)->len;
    uint8_t *ct = malloc(pt_len > 0 ? pt_len : 1);
    uint8_t tag[16];
    int res = AesGcmEncrypt(
        key_b, 32, iv_b, 12, (const uint8_t *)ValToStrPtr(aad)->data,
        ValToStrPtr(aad)->len, (const uint8_t *)ValToStrPtr(pt)->data, pt_len,
        ct, tag, 16);
    if (res) {
      char *ct_hex = malloc(pt_len * 2 + 1);
      HexEncodeC(ct, pt_len, ct_hex);
      char tag_hex[33];
      HexEncodeC(tag, 16, tag_hex);
      AKSValue struct_val = ValStruct("AesGcmResult");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("ciphertext");
      s->fieldnames[1] = strdup("tag");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(ct_hex);
      s->fieldvals[1] = ValStr(tag_hex);
      PUSH(struct_val);
      free(ct_hex);
    } else {
      PUSH(ValNull());
    }
    free(ct);
    ValFree(&aad);
    ValFree(&pt);
    ValFree(&iv);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AES256GCMDecrypt") == 0) {
    if (argc != 5) {
      fprintf(stderr,
              "Runtime error: AES256GCMDecrypt() expects 5 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue aad = POP();
    AKSValue tag = POP();
    AKSValue ct = POP();
    AKSValue iv = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(iv) != VALSTR ||
        ValKind(ct) != VALSTR || ValKind(tag) != VALSTR ||
        ValKind(aad) != VALSTR) {
      fprintf(stderr,
              "Runtime error: AES256GCMDecrypt() invalid argument types\n");
      ValFree(&aad);
      ValFree(&tag);
      ValFree(&ct);
      ValFree(&iv);
      ValFree(&key);
      return -1;
    }
    uint8_t key_b[32], iv_b[12], tag_b[16];
    HexDecodeC(ValToStrPtr(key)->data, key_b);
    HexDecodeC(ValToStrPtr(iv)->data, iv_b);
    HexDecodeC(ValToStrPtr(tag)->data, tag_b);
    size_t ct_len = strlen(ValToStrPtr(ct)->data) / 2;
    uint8_t *ct_b = malloc(ct_len > 0 ? ct_len : 1);
    HexDecodeC(ValToStrPtr(ct)->data, ct_b);
    uint8_t *pt = malloc(ct_len + 1);
    int res = AesGcmDecrypt(key_b, 32, iv_b, 12,
                            (const uint8_t *)ValToStrPtr(aad)->data,
                            ValToStrPtr(aad)->len, ct_b, ct_len, tag_b, 16, pt);
    if (res) {
      pt[ct_len] = '\0';
      PUSH(ValStr((char *)pt));
    } else {
      PUSH(ValNull());
    }
    free(ct_b);
    free(pt);
    ValFree(&aad);
    ValFree(&tag);
    ValFree(&ct);
    ValFree(&iv);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Chacha20Poly1305Encrypt") == 0) {
    if (argc != 4) {
      fprintf(stderr,
              "Runtime error: Chacha20Poly1305Encrypt() expects 4 arguments, "
              "got %d\n",
              argc);
      return -1;
    }
    AKSValue aad = POP();
    AKSValue pt = POP();
    AKSValue iv = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(iv) != VALSTR ||
        ValKind(pt) != VALSTR || ValKind(aad) != VALSTR) {
      fprintf(
          stderr,
          "Runtime error: Chacha20Poly1305Encrypt() invalid argument types\n");
      ValFree(&aad);
      ValFree(&pt);
      ValFree(&iv);
      ValFree(&key);
      return -1;
    }
    uint8_t key_b[32], iv_b[12];
    HexDecodeC(ValToStrPtr(key)->data, key_b);
    HexDecodeC(ValToStrPtr(iv)->data, iv_b);
    size_t pt_len = ValToStrPtr(pt)->len;
    uint8_t *ct = malloc(pt_len > 0 ? pt_len : 1);
    uint8_t tag[16];
    int res = Chacha20Poly1305EncryptC(
        key_b, 32, iv_b, 12, (const uint8_t *)ValToStrPtr(aad)->data,
        ValToStrPtr(aad)->len, (const uint8_t *)ValToStrPtr(pt)->data, pt_len,
        ct, tag, 16);
    if (res) {
      char *ct_hex = malloc(pt_len * 2 + 1);
      HexEncodeC(ct, pt_len, ct_hex);
      char tag_hex[33];
      HexEncodeC(tag, 16, tag_hex);
      AKSValue struct_val = ValStruct("ChaChaPolyResult");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("ciphertext");
      s->fieldnames[1] = strdup("tag");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(ct_hex);
      s->fieldvals[1] = ValStr(tag_hex);
      PUSH(struct_val);
      free(ct_hex);
    } else {
      PUSH(ValNull());
    }
    free(ct);
    ValFree(&aad);
    ValFree(&pt);
    ValFree(&iv);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Chacha20Poly1305Decrypt") == 0) {
    if (argc != 5) {
      fprintf(stderr,
              "Runtime error: Chacha20Poly1305Decrypt() expects 5 arguments, "
              "got %d\n",
              argc);
      return -1;
    }
    AKSValue aad = POP();
    AKSValue tag = POP();
    AKSValue ct = POP();
    AKSValue iv = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(iv) != VALSTR ||
        ValKind(ct) != VALSTR || ValKind(tag) != VALSTR ||
        ValKind(aad) != VALSTR) {
      fprintf(
          stderr,
          "Runtime error: Chacha20Poly1305Decrypt() invalid argument types\n");
      ValFree(&aad);
      ValFree(&tag);
      ValFree(&ct);
      ValFree(&iv);
      ValFree(&key);
      return -1;
    }
    uint8_t key_b[32], iv_b[12], tag_b[16];
    HexDecodeC(ValToStrPtr(key)->data, key_b);
    HexDecodeC(ValToStrPtr(iv)->data, iv_b);
    HexDecodeC(ValToStrPtr(tag)->data, tag_b);
    size_t ct_len = strlen(ValToStrPtr(ct)->data) / 2;
    uint8_t *ct_b = malloc(ct_len > 0 ? ct_len : 1);
    HexDecodeC(ValToStrPtr(ct)->data, ct_b);
    uint8_t *pt = malloc(ct_len + 1);
    int res = Chacha20Poly1305DecryptC(
        key_b, 32, iv_b, 12, (const uint8_t *)ValToStrPtr(aad)->data,
        ValToStrPtr(aad)->len, ct_b, ct_len, tag_b, 16, pt);
    if (res) {
      pt[ct_len] = '\0';
      PUSH(ValStr((char *)pt));
    } else {
      PUSH(ValNull());
    }
    free(ct_b);
    free(pt);
    ValFree(&aad);
    ValFree(&tag);
    ValFree(&ct);
    ValFree(&iv);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HMACSHA256") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: HMACSHA256() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue data = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(data) != VALSTR) {
      fprintf(stderr, "Runtime error: HMACSHA256() invalid argument types\n");
      ValFree(&data);
      ValFree(&key);
      return -1;
    }
    uint8_t digest[32];
    CryptoHMAC(BCRYPT_SHA256_ALGORITHM, (const uint8_t *)ValToStrPtr(key)->data,
               ValToStrPtr(key)->len, (const uint8_t *)ValToStrPtr(data)->data,
               ValToStrPtr(data)->len, digest, 32);
    char hashstr[65];
    HexEncodeC(digest, 32, hashstr);
    PUSH(ValStr(hashstr));
    ValFree(&data);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HMACSHA512") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: HMACSHA512() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue data = POP();
    AKSValue key = POP();
    if (ValKind(key) != VALSTR || ValKind(data) != VALSTR) {
      fprintf(stderr, "Runtime error: HMACSHA512() invalid argument types\n");
      ValFree(&data);
      ValFree(&key);
      return -1;
    }
    uint8_t digest[64];
    CryptoHMAC(BCRYPT_SHA512_ALGORITHM, (const uint8_t *)ValToStrPtr(key)->data,
               ValToStrPtr(key)->len, (const uint8_t *)ValToStrPtr(data)->data,
               ValToStrPtr(data)->len, digest, 64);
    char hashstr[129];
    HexEncodeC(digest, 64, hashstr);
    PUSH(ValStr(hashstr));
    ValFree(&data);
    ValFree(&key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "PBKDF2") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: PBKDF2() expects 4 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue key_len_val = POP();
    AKSValue iter_val = POP();
    AKSValue salt = POP();
    AKSValue pwd = POP();
    if (ValKind(pwd) != VALSTR || ValKind(salt) != VALSTR ||
        ValKind(iter_val) != VALINT || ValKind(key_len_val) != VALINT) {
      fprintf(stderr, "Runtime error: PBKDF2() invalid argument types\n");
      ValFree(&key_len_val);
      ValFree(&iter_val);
      ValFree(&salt);
      ValFree(&pwd);
      return -1;
    }
    int iterations = (int)ValToInt(iter_val);
    int key_len = (int)ValToInt(key_len_val);
    uint8_t *dk = (uint8_t *)malloc(key_len > 0 ? (size_t)key_len : 1);
    int ok =
        (key_len > 0 && iterations > 0 && dk)
            ? CryptoPbkdf2Sha256C((const uint8_t *)ValToStrPtr(pwd)->data,
                                  (size_t)ValToStrPtr(pwd)->len,
                                  (const uint8_t *)ValToStrPtr(salt)->data,
                                  (size_t)ValToStrPtr(salt)->len,
                                  (uint32_t)iterations, dk, (size_t)key_len)
            : 0;
    if (ok) {
      char *dk_hex = malloc((size_t)key_len * 2 + 1);
      HexEncodeC(dk, key_len, dk_hex);
      PUSH(ValStr(dk_hex));
      free(dk_hex);
    } else {
      PUSH(ValNull());
    }
    free(dk);
    ValFree(&key_len_val);
    ValFree(&iter_val);
    ValFree(&salt);
    ValFree(&pwd);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Scrypt") == 0) {
    if (argc != 6) {
      fprintf(stderr, "Runtime error: Scrypt() expects 6 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue key_len_val = POP();
    AKSValue p_val = POP();
    AKSValue r_val = POP();
    AKSValue n_val = POP();
    AKSValue salt = POP();
    AKSValue pwd = POP();
    if (ValKind(pwd) != VALSTR || ValKind(salt) != VALSTR ||
        ValKind(n_val) != VALINT || ValKind(r_val) != VALINT ||
        ValKind(p_val) != VALINT || ValKind(key_len_val) != VALINT) {
      fprintf(stderr, "Runtime error: Scrypt() invalid argument types\n");
      ValFree(&key_len_val);
      ValFree(&p_val);
      ValFree(&r_val);
      ValFree(&n_val);
      ValFree(&salt);
      ValFree(&pwd);
      return -1;
    }
    uint64_t N = (uint64_t)ValToInt(n_val);
    uint32_t r = (uint32_t)ValToInt(r_val);
    uint32_t p = (uint32_t)ValToInt(p_val);
    size_t key_len = (size_t)ValToInt(key_len_val);
    uint8_t *dk = malloc(key_len);
    int res = CryptoScrypt((const uint8_t *)ValToStrPtr(pwd)->data,
                           ValToStrPtr(pwd)->len,
                           (const uint8_t *)ValToStrPtr(salt)->data,
                           ValToStrPtr(salt)->len, N, r, p, dk, key_len);
    if (res) {
      char *dk_hex = malloc(key_len * 2 + 1);
      HexEncodeC(dk, key_len, dk_hex);
      PUSH(ValStr(dk_hex));
      free(dk_hex);
    } else {
      PUSH(ValNull());
    }
    free(dk);
    ValFree(&key_len_val);
    ValFree(&p_val);
    ValFree(&r_val);
    ValFree(&n_val);
    ValFree(&salt);
    ValFree(&pwd);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Argon2") == 0) {
    if (argc != 7) {
      fprintf(stderr, "Runtime error: Argon2() expects 7 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue type_val = POP();
    AKSValue key_len_val = POP();
    AKSValue p_val = POP();
    AKSValue t_val = POP();
    AKSValue m_val = POP();
    AKSValue salt = POP();
    AKSValue pwd = POP();
    if (ValKind(pwd) != VALSTR || ValKind(salt) != VALSTR ||
        ValKind(m_val) != VALINT || ValKind(t_val) != VALINT ||
        ValKind(p_val) != VALINT || ValKind(key_len_val) != VALINT ||
        ValKind(type_val) != VALSTR) {
      fprintf(stderr, "Runtime error: Argon2() invalid argument types\n");
      ValFree(&type_val);
      ValFree(&key_len_val);
      ValFree(&p_val);
      ValFree(&t_val);
      ValFree(&m_val);
      ValFree(&salt);
      ValFree(&pwd);
      return -1;
    }
    uint32_t m_cost = (uint32_t)ValToInt(m_val);
    uint32_t t_cost = (uint32_t)ValToInt(t_val);
    uint32_t parallelism = (uint32_t)ValToInt(p_val);
    size_t key_len = (size_t)ValToInt(key_len_val);
    const char *type = ValToStrPtr(type_val)->data;
    uint8_t *dk = malloc(key_len);
    int res = CryptoArgon2(
        (const uint8_t *)ValToStrPtr(pwd)->data, ValToStrPtr(pwd)->len,
        (const uint8_t *)ValToStrPtr(salt)->data, ValToStrPtr(salt)->len,
        m_cost, t_cost, parallelism, dk, key_len, type);
    if (res) {
      char *dk_hex = malloc(key_len * 2 + 1);
      HexEncodeC(dk, key_len, dk_hex);
      PUSH(ValStr(dk_hex));
      free(dk_hex);
    } else {
      PUSH(ValNull());
    }
    free(dk);
    ValFree(&type_val);
    ValFree(&key_len_val);
    ValFree(&p_val);
    ValFree(&t_val);
    ValFree(&m_val);
    ValFree(&salt);
    ValFree(&pwd);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RSAKeygen") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: RSAKeygen() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue key_size_val = POP();
    if (ValKind(key_size_val) != VALINT) {
      fprintf(stderr,
              "Runtime error: RSAKeygen() argument must be an integer\n");
      ValFree(&key_size_val);
      return -1;
    }
    int key_size = (int)ValToInt(key_size_val);
    char *pub_pem = NULL;
    char *priv_pem = NULL;
    int res = RSAGenerateKeys(key_size, &pub_pem, &priv_pem);
    if (res) {
      AKSValue struct_val = ValStruct("RsaKeyPair");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("public_key");
      s->fieldnames[1] = strdup("private_key");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(pub_pem);
      s->fieldvals[1] = ValStr(priv_pem);
      PUSH(struct_val);
      free(pub_pem);
      free(priv_pem);
    } else {
      PUSH(ValNull());
    }
    ValFree(&key_size_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RSASign") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RSASign() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue msg = POP();
    AKSValue priv_key = POP();
    if (ValKind(priv_key) != VALSTR || ValKind(msg) != VALSTR) {
      fprintf(stderr, "Runtime error: RSASign() invalid argument types\n");
      ValFree(&msg);
      ValFree(&priv_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = 1024;
    uint8_t *sig = malloc(sig_len);
    int res =
        RSASignData(ValToStrPtr(priv_key)->data, digest, 32, sig, &sig_len);
    if (res) {
      char *sig_hex = malloc(sig_len * 2 + 1);
      HexEncodeC(sig, sig_len, sig_hex);
      PUSH(ValStr(sig_hex));
      free(sig_hex);
    } else {
      PUSH(ValNull());
    }
    free(sig);
    ValFree(&msg);
    ValFree(&priv_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RSAVerify") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: RSAVerify() expects 3 arguments, got %d\n", argc);
      return -1;
    }
    AKSValue sig_hex = POP();
    AKSValue msg = POP();
    AKSValue pub_key = POP();
    if (ValKind(pub_key) != VALSTR || ValKind(msg) != VALSTR ||
        ValKind(sig_hex) != VALSTR) {
      fprintf(stderr, "Runtime error: RSAVerify() invalid argument types\n");
      ValFree(&sig_hex);
      ValFree(&msg);
      ValFree(&pub_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = strlen(ValToStrPtr(sig_hex)->data) / 2;
    uint8_t *sig = malloc(sig_len > 0 ? sig_len : 1);
    HexDecodeC(ValToStrPtr(sig_hex)->data, sig);
    int res =
        RSAVerifyData(ValToStrPtr(pub_key)->data, digest, 32, sig, sig_len);
    PUSH(ValBool(res));
    free(sig);
    ValFree(&sig_hex);
    ValFree(&msg);
    ValFree(&pub_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ECDSAKeygen") == 0) {
    char *pub_pem = NULL;
    char *priv_pem = NULL;
    int res = ECCGenerateKeys(BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0, 0, 256,
                              &pub_pem, &priv_pem);
    if (res) {
      AKSValue struct_val = ValStruct("EcdsaKeyPair");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("public_key");
      s->fieldnames[1] = strdup("private_key");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(pub_pem);
      s->fieldvals[1] = ValStr(priv_pem);
      PUSH(struct_val);
      free(pub_pem);
      free(priv_pem);
    } else {
      PUSH(ValNull());
    }
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ECDSASign") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ECDSASign() expects 2 arguments, got %d\n", argc);
      return -1;
    }
    AKSValue msg = POP();
    AKSValue priv_key = POP();
    if (ValKind(priv_key) != VALSTR || ValKind(msg) != VALSTR) {
      fprintf(stderr, "Runtime error: ECDSASign() invalid argument types\n");
      ValFree(&msg);
      ValFree(&priv_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = 1024;
    uint8_t *sig = malloc(sig_len);
    int res =
        ECCSignData(BCRYPT_ECDSA_P256_ALGORITHM, NULL,
                    ValToStrPtr(priv_key)->data, digest, 32, sig, &sig_len);
    if (res) {
      char *sig_hex = malloc(sig_len * 2 + 1);
      HexEncodeC(sig, sig_len, sig_hex);
      PUSH(ValStr(sig_hex));
      free(sig_hex);
    } else {
      PUSH(ValNull());
    }
    free(sig);
    ValFree(&msg);
    ValFree(&priv_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ECDSAVerify") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: ECDSAVerify() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sig_hex = POP();
    AKSValue msg = POP();
    AKSValue pub_key = POP();
    if (ValKind(pub_key) != VALSTR || ValKind(msg) != VALSTR ||
        ValKind(sig_hex) != VALSTR) {
      fprintf(stderr, "Runtime error: ECDSAVerify() invalid argument types\n");
      ValFree(&sig_hex);
      ValFree(&msg);
      ValFree(&pub_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = strlen(ValToStrPtr(sig_hex)->data) / 2;
    uint8_t *sig = malloc(sig_len > 0 ? sig_len : 1);
    HexDecodeC(ValToStrPtr(sig_hex)->data, sig);
    int res =
        ECCVerifyData(BCRYPT_ECDSA_P256_ALGORITHM, NULL,
                      ValToStrPtr(pub_key)->data, digest, 32, sig, sig_len);
    PUSH(ValBool(res));
    free(sig);
    ValFree(&sig_hex);
    ValFree(&msg);
    ValFree(&pub_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Ed25519Keygen") == 0) {
    char *pub_pem = NULL;
    char *priv_pem = NULL;
    int res = ECCGenerateKeys(BCRYPT_ECDSA_ALGORITHM, BCRYPT_ECC_CURVE_25519, 0,
                              0, 255, &pub_pem, &priv_pem);
    if (res) {
      AKSValue struct_val = ValStruct("Ed25519KeyPair");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("public_key");
      s->fieldnames[1] = strdup("private_key");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(pub_pem);
      s->fieldvals[1] = ValStr(priv_pem);
      PUSH(struct_val);
      free(pub_pem);
      free(priv_pem);
    } else {
      PUSH(ValNull());
    }
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Ed25519Sign") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: Ed25519Sign() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue msg = POP();
    AKSValue priv_key = POP();
    if (ValKind(priv_key) != VALSTR || ValKind(msg) != VALSTR) {
      fprintf(stderr, "Runtime error: Ed25519Sign() invalid argument types\n");
      ValFree(&msg);
      ValFree(&priv_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = 1024;
    uint8_t *sig = malloc(sig_len);
    int res =
        ECCSignData(BCRYPT_ECDSA_ALGORITHM, BCRYPT_ECC_CURVE_25519,
                    ValToStrPtr(priv_key)->data, digest, 32, sig, &sig_len);
    if (res) {
      char *sig_hex = malloc(sig_len * 2 + 1);
      HexEncodeC(sig, sig_len, sig_hex);
      PUSH(ValStr(sig_hex));
      free(sig_hex);
    } else {
      PUSH(ValNull());
    }
    free(sig);
    ValFree(&msg);
    ValFree(&priv_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Ed25519Verify") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: Ed25519Verify() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sig_hex = POP();
    AKSValue msg = POP();
    AKSValue pub_key = POP();
    if (ValKind(pub_key) != VALSTR || ValKind(msg) != VALSTR ||
        ValKind(sig_hex) != VALSTR) {
      fprintf(stderr,
              "Runtime error: Ed25519Verify() invalid argument types\n");
      ValFree(&sig_hex);
      ValFree(&msg);
      ValFree(&pub_key);
      return -1;
    }
    AKSSHA256CTX sha_ctx;
    uint8_t digest[32];
    Sha256Init(&sha_ctx);
    Sha256Update(&sha_ctx, (const uint8_t *)ValToStrPtr(msg)->data,
                 ValToStrPtr(msg)->len);
    Sha256Final(&sha_ctx, digest);
    size_t sig_len = strlen(ValToStrPtr(sig_hex)->data) / 2;
    uint8_t *sig = malloc(sig_len > 0 ? sig_len : 1);
    HexDecodeC(ValToStrPtr(sig_hex)->data, sig);
    int res =
        ECCVerifyData(BCRYPT_ECDSA_ALGORITHM, BCRYPT_ECC_CURVE_25519,
                      ValToStrPtr(pub_key)->data, digest, 32, sig, sig_len);
    PUSH(ValBool(res));
    free(sig);
    ValFree(&sig_hex);
    ValFree(&msg);
    ValFree(&pub_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "X25519Keygen") == 0) {
    char pub_hex[65], priv_hex[65];
    int res = Curve25519GenerateHex(BCRYPT_ECDH_ALGORITHM, pub_hex, priv_hex);
    if (res) {
      AKSValue struct_val = ValStruct("X25519KeyPair");
      AKSStruct *s = ValToObj(struct_val);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldnames[0] = strdup("public_key");
      s->fieldnames[1] = strdup("private_key");
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldvals[0] = ValStr(pub_hex);
      s->fieldvals[1] = ValStr(priv_hex);
      PUSH(struct_val);
    } else {
      PUSH(ValNull());
    }
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "X25519DH") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: X25519DH() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue peer_pub = POP();
    AKSValue priv_key = POP();
    if (ValKind(priv_key) != VALSTR || ValKind(peer_pub) != VALSTR) {
      fprintf(stderr, "Runtime error: X25519DH() invalid argument types\n");
      ValFree(&peer_pub);
      ValFree(&priv_key);
      return -1;
    }
    char secret_hex[65];
    int res = X25519SharedSecret(ValToStrPtr(priv_key)->data,
                                 ValToStrPtr(peer_pub)->data, secret_hex);
    if (res) {
      PUSH(ValStr(secret_hex));
    } else {
      PUSH(ValNull());
    }
    ValFree(&peer_pub);
    ValFree(&priv_key);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SecureRandom") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SecureRandom() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue size_val = POP();
    if (ValKind(size_val) != VALINT) {
      fprintf(stderr,
              "Runtime error: SecureRandom() argument must be an integer\n");
      ValFree(&size_val);
      return -1;
    }
    int size = (int)ValToInt(size_val);
    uint8_t *buf = malloc(size > 0 ? size : 1);
    int res = CryptoSecureRandom(buf, size);
    if (res) {
      char *hex = malloc(size * 2 + 1);
      HexEncodeC(buf, size, hex);
      PUSH(ValStr(hex));
      free(hex);
    } else {
      PUSH(ValNull());
    }
    free(buf);
    ValFree(&size_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HexEncode") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: HexEncode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: HexEncode() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    size_t len = ValToStrPtr(arg)->len;
    char *hex = malloc(len * 2 + 1);
    HexEncodeC((const uint8_t *)ValToStrPtr(arg)->data, len, hex);
    PUSH(ValStr(hex));
    free(hex);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HexDecode") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: HexDecode() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: HexDecode() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    const char *hex = ValToStrPtr(arg)->data;
    size_t hex_len = strlen(hex);
    size_t bin_len = hex_len / 2;
    uint8_t *bin = malloc(bin_len + 1);
    int res = HexDecodeC(hex, bin);
    if (res) {
      bin[bin_len] = '\0';
      PUSH(ValStrLen((const char *)bin, (int)bin_len));
      free(bin);
    } else {
      free(bin);
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "B64Enc") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: B64Enc() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: B64Enc() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    char *enc = Base64EncodeStr((const uint8_t *)ValToStrPtr(arg)->data,
                                ValToStrPtr(arg)->len);
    if (enc) {
      PUSH(ValStr(enc));
      free(enc);
    } else {
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "B64Dec") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: B64Dec() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: B64Dec() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    size_t outlen = 0;
    uint8_t *dec = Base64DecodeStr(ValToStrPtr(arg)->data, &outlen);
    if (dec) {
      PUSH(ValStrLen((const char *)dec, (int)outlen));
      free(dec);
    } else {
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UrlEnc") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: UrlEnc() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: UrlEnc() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    char *enc = UrlEncodeStr(ValToStrPtr(arg)->data);
    if (enc) {
      PUSH(ValStr(enc));
      free(enc);
    } else {
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UrlDec") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: UrlDec() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: UrlDec() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    char *dec = UrlDecodeStr(ValToStrPtr(arg)->data);
    if (dec) {
      PUSH(ValStr(dec));
      free(dec);
    } else {
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }

#undef PUSH
#undef POP
  return 0;
}

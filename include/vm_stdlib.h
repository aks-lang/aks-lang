// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef VM_STDLIB_H
#define VM_STDLIB_H

#ifdef _WIN32
/* clang-format off */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <compressapi.h>
#include <dbghelp.h>
#include <direct.h>
#include <evntprov.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <intrin.h>
#include <mmsystem.h>
#include <pdh.h>
#include <psapi.h>
#include <rpc.h>
#include <shlobj.h>
#include <timeapi.h>
#include <tlhelp32.h>
#include <windns.h>
/* clang-format on */

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#endif
#ifndef SleepMs
#define SleepMs(ms) Sleep((DWORD)(ms))
#endif
#ifndef NewDir
#define NewDir(path) (_mkdir(path) == 0)
#endif
#else
#include "aks_plat.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#endif
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef AKS_SOCKET_TYPEDEF
#define AKS_SOCKET_TYPEDEF
typedef int SOCKET;
#endif
#ifndef SleepMs
#define SleepMs(ms) usleep((ms) * 1000)
#endif
#ifndef NewDir
#define NewDir(path) (mkdir(path, 0777) == 0)
#endif
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#include <immintrin.h>
#endif

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ast.h"
#include "cross.h"
#include "jitx64.h"
#include "opcode.h"
#include "parser.h"
#include "pkg.h"
#include "value.h"
#include "vm.h"

/* Shared VM / Stdlib Internal Types */
typedef struct {
  const char *s;
  int pos;
} AksJP;

typedef struct {
#ifdef _WIN32
  OVERLAPPED overlapped;
#endif
  VMTask *task;
  HANDLE handle;
  char *buffer;
  int op_type; /* 0=read, 1=write, 2=socket recv, 3=socket send, 4=accept,
                  5=connect */
  void *extra;
  size_t io_len;
  int is_udp;
} AsyncIOContext;

typedef struct {
  uint8_t data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
} AKSSHA256CTX;

typedef struct {
  uint32_t count[2];
  uint32_t state[4];
  uint8_t buffer[64];
} AKSMD5CTX;

typedef enum {
  ORDER_SEQ_CST,
  ORDER_ACQUIRE,
  ORDER_RELEASE,
  ORDER_RELAXED
} MemOrder;

typedef enum {
  RE_CHAR,
  RE_ANY,
  RE_CLASS,
  RE_SPLIT,
  RE_JUMP,
  RE_SAVE,
  RE_MATCH,
  RE_ANCHOR_START,
  RE_ANCHOR_END
} ReOp;

typedef struct {
  int start;
  int end;
} ReClassRange;

typedef struct {
  ReOp op;
  int val;
  int target1;
  int target2;
  uint8_t class_bits[32];
  ReClassRange *class_ranges;
  int class_range_count;
  int class_negated;
} ReInst;

typedef struct {
  ReInst *insts;
  int count;
  int cap;
  int save_count;
} ReProg;

/* Stack push/pop macros shared across modular stdlib handlers */
#define AKS_STDLIB_PUSH(val)                                                   \
  do {                                                                         \
    if (stacktop < stacklimit) {                                               \
      *stacktop++ = (val);                                                     \
    } else {                                                                   \
      fprintf(stderr, "Runtime error: stack overflow\n");                      \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#define AKS_STDLIB_POP() StdlibPop(&stacktop)

/* Portable software CRC-32C (Castagnoli) fallback */
static inline uint32_t AksCrc32cSw(uint32_t init_crc, const uint8_t *buf,
                                   size_t len) {
  uint32_t crc = init_crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0x82F63B78u) : (crc >> 1);
    }
  }
  return crc;
}

static inline AKSValue *AksStructFindField(AKSStruct *s, const char *name) {
  if (!s || !name)
    return NULL;
  for (int i = 0; i < s->fieldcount; i++) {
    if (s->fieldnames[i] && strcmp(s->fieldnames[i], name) == 0) {
      return &s->fieldvals[i];
    }
  }
  return NULL;
}

/* External custom cryptography primitives (src/crypto_custom.c) */
void HexEncodeC(const uint8_t *in, size_t in_len, char *out);
int HexDecodeC(const char *in, uint8_t *out);
int CryptoSha512(const uint8_t *data, size_t len, uint8_t *digest);
void KeccakHash(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen,
                int rate);
int AesGcmEncrypt(const uint8_t *key, size_t key_len, const uint8_t *iv,
                  size_t iv_len, const uint8_t *aad, size_t aad_len,
                  const uint8_t *pt, size_t pt_len, uint8_t *ct, uint8_t *tag,
                  size_t tag_len);
int AesGcmDecrypt(const uint8_t *key, size_t key_len, const uint8_t *iv,
                  size_t iv_len, const uint8_t *aad, size_t aad_len,
                  const uint8_t *ct, size_t ct_len, const uint8_t *tag,
                  size_t tag_len, uint8_t *pt);
int Chacha20Poly1305EncryptC(const uint8_t *key, size_t key_len,
                             const uint8_t *iv, size_t iv_len,
                             const uint8_t *aad, size_t aad_len,
                             const uint8_t *pt, size_t pt_len, uint8_t *ct,
                             uint8_t *tag, size_t tag_len);
int Chacha20Poly1305DecryptC(const uint8_t *key, size_t key_len,
                             const uint8_t *iv, size_t iv_len,
                             const uint8_t *aad, size_t aad_len,
                             const uint8_t *ct, size_t ct_len,
                             const uint8_t *tag, size_t tag_len, uint8_t *pt);
int CryptoHMAC(LPCWSTR alg_id, const uint8_t *key, size_t key_len,
               const uint8_t *data, size_t data_len, uint8_t *digest,
               size_t digest_len);
int CryptoScrypt(const uint8_t *pwd, size_t pwd_len, const uint8_t *salt,
                 size_t salt_len, uint64_t N, uint32_t r, uint32_t p,
                 uint8_t *dk, size_t dk_len);
int CryptoArgon2(const uint8_t *pwd, size_t pwd_len, const uint8_t *salt,
                 size_t salt_len, uint32_t m_cost, uint32_t t_cost,
                 uint32_t parallelism, uint8_t *dk, size_t dk_len,
                 const char *type);
int RSAGenerateKeys(uint32_t key_size, char **pub_pem, char **priv_pem);
int RSASignData(const char *priv_pem, const uint8_t *hash, size_t hash_len,
                uint8_t *sig, size_t *sig_len);
int RSAVerifyData(const char *pub_pem, const uint8_t *hash, size_t hash_len,
                  const uint8_t *sig, size_t sig_len);
int ECCGenerateKeys(LPCWSTR alg_id, LPCWSTR curve_name, DWORD magic_priv,
                    DWORD magic_pub, size_t key_bits, char **pub_pem,
                    char **priv_pem);
int ECCSignData(LPCWSTR alg_id, LPCWSTR curve_name, const char *priv_pem,
                const uint8_t *hash, size_t hash_len, uint8_t *sig,
                size_t *sig_len);
int ECCVerifyData(LPCWSTR alg_id, LPCWSTR curve_name, const char *pub_pem,
                  const uint8_t *hash, size_t hash_len, const uint8_t *sig,
                  size_t sig_len);
int Curve25519GenerateHex(LPCWSTR alg_id, char *pub_hex, char *priv_hex);
int X25519SharedSecret(const char *priv_hex, const char *pub_hex,
                       char *secret_hex);
int CryptoSecureRandom(uint8_t *buf, size_t len);
int CryptoPbkdf2Sha256C(const uint8_t *pwd, size_t pwd_len, const uint8_t *salt,
                        size_t salt_len, uint32_t iters, uint8_t *dk,
                        size_t dk_len);
int CryptoHmacSha1C(const uint8_t *key, size_t key_len, const uint8_t *data,
                    size_t data_len, uint8_t out[20]);
int CryptoAesCbcC(int encrypt, const uint8_t *key, size_t key_len,
                  const uint8_t iv[16], const uint8_t *in, size_t len,
                  uint8_t *out);
int CryptoBlake2b(const uint8_t *data, size_t len, uint8_t *out,
                  size_t out_len);
int CryptoBlake2s(const uint8_t *data, size_t len, uint8_t *out,
                  size_t out_len);
int CryptoPoly1305(const uint8_t *key, const uint8_t *msg, size_t msg_len,
                   uint8_t tag[16]);
uint64_t CryptoSipHash64(const uint8_t key[16], const uint8_t *in,
                         size_t inlen);
void CryptoSipHash128(const uint8_t key[16], const uint8_t *in, size_t inlen,
                      uint8_t out[16]);
int CryptoXChaCha20Poly1305Encrypt(const uint8_t key[32],
                                   const uint8_t nonce[24], const uint8_t *aad,
                                   size_t aad_len, const uint8_t *in,
                                   size_t in_len, uint8_t *out,
                                   uint8_t tag[16]);
int CryptoXChaCha20Poly1305Decrypt(const uint8_t key[32],
                                   const uint8_t nonce[24], const uint8_t *aad,
                                   size_t aad_len, const uint8_t *in,
                                   size_t in_len, const uint8_t tag[16],
                                   uint8_t *out);
void CryptoSha512_256Hash(const uint8_t *in, size_t inlen, uint8_t out[32]);
void CryptoHkdfExtract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm,
                       size_t ikm_len, uint8_t prk[32]);
int CryptoHkdfExpand(const uint8_t prk[32], const uint8_t *info,
                     size_t info_len, uint8_t *okm, size_t okm_len);
void CryptoAes128Cmac(const uint8_t key[16], const uint8_t *data, size_t len,
                      uint8_t mac[16]);

/* Shared VM helpers exported to modular stdlib files */
#ifdef _WIN32
VOID CALLBACK AksThreadPoolWorkCallback(PTP_CALLBACK_INSTANCE Instance,
                                        PVOID Context, PTP_WORK Work);
VOID CALLBACK AksThreadPoolTimerCallback(PTP_CALLBACK_INSTANCE Instance,
                                         PVOID Context, PTP_TIMER Timer);
#else
int PosixRegisterAsyncIo(VM *vm, int fd, AsyncIOContext *ctx, int is_write);
int PosixSetConsoleRawMode(int enable);
int AksPosixOpen(const char *path, int flags, int mode);
#endif

int AksRxMatchLen(const char *p, const char *t, int *out_len);
int AksRxMatchHere(const char *p, const char *t);
AKSValue AksJPParseValue(AksJP *p);
void AksArrPush(AKSArray *a, AKSValue v);
void *MutexNewNative(void);
void MutexFreeNative(void *m);
void MutexLockNative(void *m);
void MutexUnlockNative(void *m);
void WorkerPushTail(VMWorker *w, VMTask *t);
void WakeBlockedTaskOnChannel(VM *vm, VMTask *t, VMChannel *c);
VMTask *CreateAsyncPendingTask(VM *vm);
int FileExists(const char *path);
int SetEnvironmentVar(const char *name, const char *val);
int64_t MathGcd(int64_t a, int64_t b);
int64_t MathLcm(int64_t a, int64_t b);
int MathIsPrime(int64_t n);
double MathFact(double n);
double MathNpr(double n, double r);
double MathNcr(double n, double r);
void Sha256Init(AKSSHA256CTX *ctx);
void Sha256Update(AKSSHA256CTX *ctx, const uint8_t *data, size_t len);
void Sha256Final(AKSSHA256CTX *ctx, uint8_t *hash);
void MD5Init(AKSMD5CTX *ctx);
void MD5Update(AKSMD5CTX *ctx, const uint8_t *input, size_t inputLen);
void MD5Final(AKSMD5CTX *ctx, uint8_t digest[16]);
char *Base64EncodeStr(const uint8_t *src, size_t len);
uint8_t *Base64DecodeStr(const char *src, size_t *outlen);
char *UrlEncodeStr(const char *str);
char *UrlDecodeStr(const char *str);
char *GetCwdStr(void);
int CompareAKSVals(const void *a, const void *b);
AKSValue ParseJSONVal(const char **ptr, int *Error);
void StrIfyVal(AKSValue v, char **buf, size_t *len, size_t *cap);
int IsPathSafe(VM *vm, const char *path);
int ValidateCap(VM *vm, AKSValue cap, const char *expectedres,
                const char *expectedperm, const char *path);
AKSValue StdlibPop(AKSValue **stacktopptr);
void TaskCancel(VM *vm, VMTask *t);
char *HuffmanDecodeStr(const char *in_data, int in_len, int *out_len);
int CreateTLSContext(SOCKET sock, int is_server);
void TLSCloseContext(int tls_id);
int PerformTLSHandshake(int tls_id, const char *hostname);
int TLSSendData(int tls_id, const char *data, int len);
int TLSRecvData(int tls_id, char *out_buf, int max_len);
MemOrder ParseMemOrder(AKSValue val);
void CBOREncodeVal(AKSValue v, char **buf, size_t *len, size_t *cap);
AKSValue CBORDecodeVal(const uint8_t **ptr, const uint8_t *end, int *Error);
void MsgpackEncodeVal(AKSValue v, char **buf, size_t *len, size_t *cap);
AKSValue MsgpackDecodeVal(const uint8_t **ptr, const uint8_t *end, int *Error);
AKSValue ParseCSVStr(const char *csv);
char *StringifyCSV(AKSValue arr);
AKSValue ParseTOMLStr(const char *toml);
char *StringifyTOML(AKSValue val);
AKSValue ParseXMLStr(const char *xml);
char *StringifyXML(AKSValue val);
char *ProtobufEncodeObjFunc(AKSValue obj, AKSValue mapping, size_t *out_len);
AKSValue ProtobufDecodeObjFunc(const char *bytes, int bytes_len,
                               AKSValue mapping, const char *typename);
int Utf8NextCodepoint(const uint8_t *s, int len, int *bytes_consumed);
int Utf8LenFunc(const char *str);
char *Utf8CharAtFunc(const char *str, int target_idx);
int Utf8CodepointAtFunc(const char *str, int target_idx);
ReProg CompileRegex(const char *pattern);
void FreeRegex(ReProg *prog);
int MatchNFA(ReInst *program, int pc, const int *input, int input_len, int idx,
             int *saves, int save_count);
int *StrToCodepoints(const char *str, int *out_len);
char *CodepointsToStr(const int *cps, int count);
char *UnicodeNormalizeFunc(const char *str, const char *form);
int LocaleCompareFunc(const char *str1, const char *str2, const char *locale);
void SimdOpArrays(AKSArray *a1, AKSArray *a2, AKSArray *res_arr, int count,
                  int op);
void SimdFmaArrays(AKSArray *a1, AKSArray *a2, AKSArray *a3, AKSArray *res_arr,
                   int count);
void MatrixMulC(int r1, int c1, double *d1, int r2, int c2, double *d2,
                double *res);
int MatrixInverseC(int n, double *a, double *res);
int MatrixLuC(int n, double *a, double *L, double *U, double *P);
int MatrixCholeskyC(int n, double *a, double *L);
void FFTC(double *real, double *imag, int n, int inverse);
char *BigIntAddC(const char *s1, const char *s2);
int BigIntCmpC(const char *s1, const char *s2);
char *BigIntSubC(const char *s1, const char *s2);
char *BigIntMulC(const char *s1, const char *s2);
char *BigIntDivC(const char *s1, const char *s2, char **rem);
void StructSetFieldNative(AKSStruct *s, const char *fieldname, AKSValue val);
void TriggerPanic(VM *vm, VMTask *task, const char *msg, uint8_t **ipptr);
void AksCloseAllFileHandles(void);
VMChannel *ChanNew(int capacity);
VMMutex *MutexNew(void);
VMSemaphore *SemNew(int initialcount);

/* Top-level & domain-module stdlib dispatchers */
int AksExecuteStdlibFn(VM *vm, VMTask *task, const char *name, int argc,
                       AKSValue **stacktopptr);
int ExecuteStdlibCore(VM *vm, VMTask *task, const char *name, int argc,
                      AKSValue **stacktopptr);
int ExecuteStdlibCollections(VM *vm, VMTask *task, const char *name, int argc,
                             AKSValue **stacktopptr);
int ExecuteStdlibMathSimd(VM *vm, VMTask *task, const char *name, int argc,
                          AKSValue **stacktopptr);
int ExecuteStdlibCryptoDb(VM *vm, VMTask *task, const char *name, int argc,
                          AKSValue **stacktopptr);
int ExecuteStdlibAutodiff(VM *vm, VMTask *task, const char *name, int argc,
                          AKSValue **stacktopptr);
int ExecuteStdlibOsNet(VM *vm, VMTask *task, const char *name, int argc,
                       AKSValue **stacktopptr);

/* Runtime AddressSanitizer & ThreadSanitizer hooks (src/vm_sanitizer.c) */
extern volatile int gAksAsanActive;
void AksSanQuarantinePtr(void *ptr);
void AksSanUnquarantinePtr(void *ptr);
int AksSanIsQuarantined(const void *ptr);
void AksSanInitVm(VM *vm);
void AksSanFreeVm(VM *vm);
void AksSanReportAsanError(VM *vm, const char *kind, const char *detail,
                           int line);
int AksSanCheckObjAsan(VM *vm, AKSValue v, const char *opname, int line);
void AksSanOnSpawn(VM *vm, VMTask *parent, VMTask *child);
void AksSanOnAwait(VM *vm, VMTask *waiter, VMTask *target);
void AksSanOnMutexLock(VM *vm, VMTask *task, void *mutex_ptr);
void AksSanOnMutexUnlock(VM *vm, VMTask *task, void *mutex_ptr);
void AksSanOnChannelSend(VM *vm, VMTask *sender, void *chan_ptr);
void AksSanOnChannelRecv(VM *vm, VMTask *receiver, void *chan_ptr);
void AksSanRecordAccess(VM *vm, VMTask *task, uintptr_t obj_addr,
                        const char *kind, int is_write, int line);

#endif

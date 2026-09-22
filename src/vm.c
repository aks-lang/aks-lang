// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "aks_plat.h"
#ifndef _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#endif

#ifdef _WIN32
#ifndef NT_SUCCESS
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#endif
#include <bcrypt.h>
#include <compressapi.h>
#include <intrin.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <rpc.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <timeapi.h>
#include <windns.h>
#include <icmpapi.h>
#include <pdh.h>
#include <winhttp.h>
#include <dbghelp.h>
#include <evntprov.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "winhttp.lib")
#define SleepMs(ms) Sleep((DWORD)(ms))
static VOID CALLBACK AksThreadPoolWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work) {
  (void)Instance;
  (void)Context;
  (void)Work;
}
static VOID CALLBACK AksThreadPoolTimerCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_TIMER Timer) {
  (void)Instance;
  (void)Context;
  (void)Timer;
}
#else
#define SleepMs(ms) usleep((ms) * 1000)
#endif

#include "ast.h"
#include "cross.h"
#include "jitx64.h"
#include "opcode.h"
#include "parser.h"
#include "value.h"
#include "pkg.h"
#include "vm.h"
#include <immintrin.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static void UnEscapeStrInplace(char *s);
static int ChkopOverload(VM *vm, VMTask *task, uint8_t **ipptr,
                         AKSValue **stacktopptr, CallFrame **frameptr,
                         AKSValue **frameslotsptr, const char *opname);
static void VMRegisterStruct(VM *vm, const char *name, int fc, char **fnames,
                             char **ftypes);

// Custom Cryptography Prototypes
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

static int AksRxMatchLen(const char *p, const char *t, int *out_len) {
  if (!p || !t) return 0;
  if (p[0] == 0) { *out_len = 0; return 1; }
  if (p[0] == '$' && p[1] == 0) { *out_len = 0; return *t == 0; }
  if (p[0] == '[' && p[1] == '0' && p[2] == '-' && p[3] == '9' && p[4] == ']' && p[5] == '+') {
    int count = 0;
    const char *cur = t;
    while (*cur >= '0' && *cur <= '9') { cur++; count++; }
    if (count == 0) return 0;
    int sub = 0;
    if (AksRxMatchLen(p + 6, cur, &sub)) { *out_len = count + sub; return 1; }
    return 0;
  }
  if (p[0] == '[' && p[1] == '0' && p[2] == '-' && p[3] == '9' && p[4] == ']') {
    if (*t < '0' || *t > '9') return 0;
    int sub = 0;
    if (AksRxMatchLen(p + 5, t + 1, &sub)) { *out_len = sub + 1; return 1; }
    return 0;
  }
  if (p[1] == '+') {
    char ch = p[0];
    if (*t == 0 || (ch != '.' && *t != ch)) return 0;
    const char *cur = t;
    while (*cur && (ch == '.' || *cur == ch)) cur++;
    for (const char *cand = cur; cand > t; cand--) {
      int sub = 0;
      if (AksRxMatchLen(p + 2, cand, &sub)) { *out_len = (int)(cand - t) + sub; return 1; }
    }
    return 0;
  }
  if (p[1] == '*') {
    char ch = p[0];
    const char *cur = t;
    while (*cur && (ch == '.' || *cur == ch)) cur++;
    for (const char *cand = cur; cand >= t; cand--) {
      int sub = 0;
      if (AksRxMatchLen(p + 2, cand, &sub)) { *out_len = (int)(cand - t) + sub; return 1; }
    }
    return 0;
  }
  if (*t && (p[0] == '.' || p[0] == *t)) {
    int sub = 0;
    if (AksRxMatchLen(p + 1, t + 1, &sub)) { *out_len = sub + 1; return 1; }
  }
  return 0;
}

static int AksRxMatchHere(const char *p, const char *t) {
  int len = 0;
  return AksRxMatchLen(p, t, &len);
}
typedef struct { const char *s; int pos; } AksJP;
static void AksJPSkip(AksJP *p) {
  while (p->s[p->pos]==' '||p->s[p->pos]=='\t'||p->s[p->pos]=='\n'||p->s[p->pos]=='\r') p->pos++;
}
static AKSValue AksJPParseValue(AksJP *p);
static AKSValue AksJPParseString(AksJP *p) {
  p->pos++;
  char stackbuf[256];
  int cap = (int)sizeof(stackbuf);
  int bi = 0;
  char *buf = stackbuf;
  while (p->s[p->pos] && p->s[p->pos] != '"') {
    if (bi + 6 >= cap) {
      int new_cap = cap * 2 + 16;
      char *nb = (buf == stackbuf) ? malloc(new_cap) : realloc(buf, new_cap);
      if (nb) {
        if (buf == stackbuf) memcpy(nb, stackbuf, bi);
        buf = nb;
        cap = new_cap;
      }
    }
    if (p->s[p->pos] == '\\') {
      p->pos++;
      char c = p->s[p->pos];
      if (c) p->pos++;
      switch(c) {
        case 'n': buf[bi++] = '\n'; break;
        case 't': buf[bi++] = '\t'; break;
        case 'r': buf[bi++] = '\r'; break;
        case 'b': buf[bi++] = '\b'; break;
        case 'f': buf[bi++] = '\f'; break;
        case '/': buf[bi++] = '/'; break;
        case '"': buf[bi++] = '"'; break;
        case '\\': buf[bi++] = '\\'; break;
        case 'u': {
          uint32_t cp = 0;
          int hex_ok = 1;
          for (int h = 0; h < 4; h++) {
            char hc = p->s[p->pos];
            int val;
            if (hc >= '0' && hc <= '9') val = hc - '0';
            else if (hc >= 'a' && hc <= 'f') val = hc - 'a' + 10;
            else if (hc >= 'A' && hc <= 'F') val = hc - 'A' + 10;
            else { hex_ok = 0; break; }
            cp = (cp << 4) | (uint32_t)val;
            p->pos++;
          }
          if (!hex_ok) break;
          if (cp <= 0x7F) {
            buf[bi++] = (char)cp;
          } else if (cp <= 0x7FF) {
            buf[bi++] = (char)(0xC0 | (cp >> 6));
            buf[bi++] = (char)(0x80 | (cp & 0x3F));
          } else {
            buf[bi++] = (char)(0xE0 | (cp >> 12));
            buf[bi++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[bi++] = (char)(0x80 | (cp & 0x3F));
          }
          break;
        }
        default: buf[bi++] = c; break;
      }
    } else {
      buf[bi++] = p->s[p->pos++];
    }
  }
  buf[bi] = 0;
  if (p->s[p->pos] == '"') p->pos++;
  AKSValue res = ValStrLen(buf, bi);
  if (buf != stackbuf) free(buf);
  return res;
}
static void AksArrPush(AKSArray *a, AKSValue v) {
  AksArrayEnsureCapacity(a, a->count + 1);
  a->items[a->count++] = v;
}
static AKSValue AksJPParseValue(AksJP *p) {
  AksJPSkip(p);
  char c = p->s[p->pos];
  if (c == '"') return AksJPParseString(p);
  if (c == 't') { p->pos += 4; return ValBool(1); }
  if (c == 'f') { p->pos += 5; return ValBool(0); }
  if (c == 'n') { p->pos += 4; return ValNull(); }
  if (c == '[') {
    p->pos++; AksJPSkip(p);
    AKSValue arr = ValArr(4);
    AKSArray *a = ValToArr(arr);
    while (p->s[p->pos] && p->s[p->pos] != ']') {
      AKSValue elem = AksJPParseValue(p); AksJPSkip(p);
      AksArrPush(a, elem);
      if (p->s[p->pos] == ',') p->pos++;
      AksJPSkip(p);
    }
    if (p->s[p->pos] == ']') p->pos++;
    return arr;
  }
  if (c == '{') {
    p->pos++; AksJPSkip(p);
    AKSValue obj = ValStruct("Object");
    AKSStruct *s = ValToObj(obj);
    while (p->s[p->pos] && p->s[p->pos] != '}') {
      if (p->s[p->pos] != '"') { p->pos++; continue; }
      AKSValue key = AksJPParseString(p); AksJPSkip(p);
      if (p->s[p->pos] == ':') p->pos++; AksJPSkip(p);
      AKSValue val = AksJPParseValue(p); AksJPSkip(p);
      int fc = s->fieldcount;
      s->fieldnames = realloc(s->fieldnames, (fc+1)*sizeof(char*));
      s->fieldvals = realloc(s->fieldvals, (fc+1)*sizeof(AKSValue));
      s->fieldnames[fc] = strdup(ValToStrPtr(key)->data);
      s->fieldvals[fc] = val;
      s->fieldcount++;
      ValFree(&key);
      if (p->s[p->pos] == ',') p->pos++;
      AksJPSkip(p);
    }
    if (p->s[p->pos] == '}') p->pos++;
    return obj;
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    char nbuf[64]; int ni = 0; int is_float = 0;
    if (c == '-') { nbuf[ni++] = p->s[p->pos]; p->pos++; }
    while (p->s[p->pos] >= '0' && p->s[p->pos] <= '9') { nbuf[ni++] = p->s[p->pos]; p->pos++; }
    if (p->s[p->pos] == '.') {
      is_float = 1; nbuf[ni++] = p->s[p->pos]; p->pos++;
      while (p->s[p->pos] >= '0' && p->s[p->pos] <= '9') { nbuf[ni++] = p->s[p->pos]; p->pos++; }
    }
    if (p->s[p->pos] == 'e' || p->s[p->pos] == 'E') {
      is_float = 1; nbuf[ni++] = p->s[p->pos]; p->pos++;
      if (p->s[p->pos]=='+' || p->s[p->pos]=='-') { nbuf[ni++] = p->s[p->pos]; p->pos++; }
      while (p->s[p->pos] >= '0' && p->s[p->pos] <= '9') { nbuf[ni++] = p->s[p->pos]; p->pos++; }
    }
    nbuf[ni] = 0;
    if (is_float) return ValFloat(atof(nbuf));
    return ValInt((int64_t)strtoll(nbuf, NULL, 10));
  }
  return ValNull();
}


static int TryExtractSwizzle(AKSStruct *s, const char *fieldname, AKSValue *outval) {

  if (!s || !fieldname) return 0;
  int flen = (int)strlen(fieldname);
  if (flen < 2 || flen > 4) return 0;
  AKSValue extracted[4];
  for (int i = 0; i < flen; i++) {
    char ch = fieldname[i];
    const char *c1 = NULL;
    const char *c2 = NULL;
    const char *c3 = NULL;
    if (ch == 'x' || ch == 'r' || ch == 's') { c1 = "x"; c2 = "r"; c3 = "s"; }
    else if (ch == 'y' || ch == 'g' || ch == 't') { c1 = "y"; c2 = "g"; c3 = "t"; }
    else if (ch == 'z' || ch == 'b' || ch == 'p') { c1 = "z"; c2 = "b"; c3 = "p"; }
    else if (ch == 'w' || ch == 'a' || ch == 'q') { c1 = "w"; c2 = "a"; c3 = "q"; }
    else {
      for (int k = 0; k < i; k++) ValFree(&extracted[k]);
      return 0;
    }
    int found_coord = 0;
    for (int f = 0; f < s->fieldcount; f++) {
      if ((c1 && strcmp(s->fieldnames[f], c1) == 0) ||
          (c2 && strcmp(s->fieldnames[f], c2) == 0) ||
          (c3 && strcmp(s->fieldnames[f], c3) == 0)) {
        extracted[i] = ValCopy(s->fieldvals[f]);
        found_coord = 1;
        break;
      }
    }
    if (!found_coord) {
      for (int k = 0; k < i; k++) ValFree(&extracted[k]);
      return 0;
    }
  }
  const char *tname = (flen == 2) ? "Vec2" : ((flen == 3) ? "Vec3" : "Vec4");
  AKSValue swiz_val = ValStruct(tname);
  AKSStruct *swiz_s = ValToObj(swiz_val);
  const char *out_f[4] = {"x", "y", "z", "w"};
  swiz_s->fieldcount = flen;
  swiz_s->fieldnames = malloc(flen * sizeof(char *));
  swiz_s->fieldvals = malloc(flen * sizeof(AKSValue));
  for (int i = 0; i < flen; i++) {
    swiz_s->fieldnames[i] = strdup(out_f[i]);
    swiz_s->fieldvals[i] = extracted[i];
  }
  *outval = swiz_val;
  return 1;
}

#ifdef exit
#undef exit
#endif
#define exit(code)                                                             \
  do {                                                                         \
    fprintf(stderr, "exit(%d) called at %s:%d\n", (code), __FILE__, __LINE__); \
    exit(code);                                                                \
  } while (0)

static void *MutexNewNative(void);
static void MutexFreeNative(void *m);
static void MutexLockNative(void *m);
typedef struct HuffNode HuffNode;
static HuffNode *huff_tree_root = NULL;
static void FreeHuffmanTree(HuffNode *node);
static void MutexUnlockNative(void *m);
static void WorkerPushTail(VMWorker *w, VMTask *t);

typedef struct {
#ifdef _WIN32
  OVERLAPPED overlapped;
#endif
  VMTask *task;
  HANDLE handle;
  char *buffer;
  int op_type; // = read, 1 = write, 2 = socket recv, 3 = socket send, 4 =
               // socket accept, 5 = socket connect
  void *extra;
} AsyncIOContext;

static uint64_t GetTimeMs(void) {
#ifdef _WIN32
  return GetTickCount64();
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

static void VMRemoveTimer(VM *vm, VMTask *task) {
  MutexLockNative(vm->globallock);
  for (int i = 0; i < vm->timercount; i++) {
    if (vm->timers[i].task == task) {
      vm->timers[i].active = 0;
    }
  }
  MutexUnlockNative(vm->globallock);
}

static void VMAddTimer(VM *vm, VMTask *task, uint64_t timeout_ms) {
  MutexLockNative(vm->globallock);
  task->timedout = 0;
  int idx = -1;
  for (int i = 0; i < vm->timercount; i++) {
    if (!vm->timers[i].active) {
      idx = i;
      break;
    }
  }
  if (idx == -1 && vm->timercount < 1024) {
    idx = vm->timercount++;
  }
  if (idx != -1) {
    vm->timers[idx].task = task;
    vm->timers[idx].awaiting_on = (VMTask *)task->blockedon;
    vm->timers[idx].target_ms = GetTimeMs() + timeout_ms;
    vm->timers[idx].active = 1;
  }
  MutexUnlockNative(vm->globallock);
}

#ifdef _WIN32
DWORD WINAPI TimerThreadFunc(LPVOID lpParam) {
#else
void *TimerThreadFunc(void *lpParam) {
#endif
  VM *vm = (VM *)lpParam;
  while (!vm->shouldterminate) {
    SleepMs(1);
    uint64_t now = GetTimeMs();
    MutexLockNative(vm->globallock);
    for (int i = 0; i < vm->timercount; i++) {
      if (vm->timers[i].active && now >= vm->timers[i].target_ms) {
        VMTask *t = vm->timers[i].task;
        vm->timers[i].active = 0;
        if (t->state == 2) { // Blocked
          t->timedout = 1;
          t->state = 0; // Ready
          int targetworker = rand() % vm->workercount;
          WorkerPushTail(&vm->workers[targetworker], t);
        }
      }
    }
    MutexUnlockNative(vm->globallock);
  }
  return 0;
}

static void WakeBlockedTaskOnChannel(VM *vm, VMTask *t, VMChannel *c) {
  MutexLockNative(vm->globallock);
  if (t->is_in_select) {
    int woken_idx = -1;
    for (int i = 0; i < t->select_op_count; i++) {
      if (t->select_ops[i].chan == c) {
        woken_idx = i;
        break;
      }
    }
    t->select_woken_index = woken_idx;
    t->is_in_select = 0;
  }
  t->state = 0; // Ready
  int targetworker = rand() % vm->workercount;
  WorkerPushTail(&vm->workers[targetworker], t);
  MutexUnlockNative(vm->globallock);
}

static void LockSelectChannels(VMTask *task) {
  VMChannel *chans[32];
  int c_count = 0;
  for (int i = 0; i < task->select_op_count; i++) {
    VMChannel *c = task->select_ops[i].chan;
    int dup = 0;
    for (int j = 0; j < c_count; j++) {
      if (chans[j] == c) {
        dup = 1;
        break;
      }
    }
    if (!dup) {
      chans[c_count++] = c;
    }
  }
  for (int i = 0; i < c_count - 1; i++) {
    for (int j = i + 1; j < c_count; j++) {
      if ((uintptr_t)chans[i] > (uintptr_t)chans[j]) {
        VMChannel *tmp = chans[i];
        chans[i] = chans[j];
        chans[j] = tmp;
      }
    }
  }
  for (int i = 0; i < c_count; i++) {
    MutexLockNative(chans[i]->lock);
  }
}

static void UnlockSelectChannels(VMTask *task) {
  VMChannel *chans[32];
  int c_count = 0;
  for (int i = 0; i < task->select_op_count; i++) {
    VMChannel *c = task->select_ops[i].chan;
    int dup = 0;
    for (int j = 0; j < c_count; j++) {
      if (chans[j] == c) {
        dup = 1;
        break;
      }
    }
    if (!dup) {
      chans[c_count++] = c;
    }
  }
  for (int i = 0; i < c_count - 1; i++) {
    for (int j = i + 1; j < c_count; j++) {
      if ((uintptr_t)chans[i] > (uintptr_t)chans[j]) {
        VMChannel *tmp = chans[i];
        chans[i] = chans[j];
        chans[j] = tmp;
      }
    }
  }
  for (int i = 0; i < c_count; i++) {
    MutexUnlockNative(chans[i]->lock);
  }
}

#ifdef _WIN32
static int PollIoCompletions(VM *vm, DWORD timeout_ms) {
  DWORD bytes_transferred = 0;
  ULONG_PTR completion_key = 0;
  LPOVERLAPPED overlapped = NULL;
  DWORD current_timeout = timeout_ms;
  int completed_count = 0;

  for (int batch = 0; batch < 64; batch++) {
    BOOL res =
        GetQueuedCompletionStatus((HANDLE)vm->iocp, &bytes_transferred,
                                  &completion_key, &overlapped, current_timeout);

    if (!overlapped) {
      break;
    }
    completed_count++;

    AsyncIOContext *ctx = (AsyncIOContext *)overlapped;
    VMTask *task = ctx->task;
    if (ctx->handle != INVALID_HANDLE_VALUE) {
      if (ctx->op_type == 0 || ctx->op_type == 1) {
        CloseHandle(ctx->handle);
        ctx->handle = INVALID_HANDLE_VALUE;
      }
    }

    MutexLockNative(vm->globallock);
    if (task->state == 2) {
      if (ctx->op_type == 0) { // read file
        task->result = ValStrLen(ctx->buffer, bytes_transferred);
        task->state = 3;
      } else if (ctx->op_type == 1) { // write file
        task->result = ValBool(res);
        task->state = 3;
      } else if (ctx->op_type == 2) { // socket recv
        task->result = ValStrLen(ctx->buffer, bytes_transferred);
        task->state = 3;
      } else if (ctx->op_type == 3) { // socket send / udp send
        task->result = ValInt(bytes_transferred);
        task->state = 3;
      } else if (ctx->op_type == 4) { // socket accept
        SOCKET client_sock = (SOCKET)(uintptr_t)ctx->extra;
        SOCKET listen_sock = (SOCKET)(uintptr_t)ctx->handle;
        if (res) {
          setsockopt(client_sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&listen_sock, sizeof(listen_sock));
          CreateIoCompletionPort((HANDLE)client_sock, vm->iocp,
                                 (ULONG_PTR)client_sock, 0);
          task->result = ValInt((int64_t)client_sock);
        } else {
          closesocket(client_sock);
          task->result = ValInt(-1);
        }
        task->state = 3;
      } else if (ctx->op_type == 5) { // socket connect
        task->result = res ? ValInt((int64_t)ctx->handle) : ValInt(-1);
        task->state = 3;
      }

      for (int i = 0; i < task->awaitercount; i++) {
        VMTask *awaiter = task->awaiters[i];
        awaiter->state = 0;
        int targetworker = rand() % vm->workercount;
        WorkerPushTail(&vm->workers[targetworker], awaiter);
      }
    }
    MutexUnlockNative(vm->globallock);

    if (ctx->buffer)
      free(ctx->buffer);
    if (ctx->extra) {
      if (ctx->op_type != 4) {
        free(ctx->extra);
      }
    }
    free(ctx);

    current_timeout = 0; // drain remaining queued completions without waiting
  }
  return completed_count;
}
#else
#ifdef AKS_USE_IO_URING
static int PollIoCompletions(VM *vm, int timeout_ms) {
  struct io_uring_cqe *cqe = NULL;
  struct __kernel_timespec ts = {.tv_sec = timeout_ms / 1000,
                                 .tv_nsec = (timeout_ms % 1000) * 1000000};
  int completed_count = 0;

  int res = io_uring_wait_cqe_timeout(&vm->ring, &cqe, &ts);
  if (res >= 0 && cqe) {
    completed_count++;
    AsyncIOContext *ctx = (AsyncIOContext *)io_uring_cqe_get_data(cqe);
    int bytes_transferred = cqe->res;

    if (ctx) {
      VMTask *task = ctx->task;

      if (ctx->op_type == 0 || ctx->op_type == 1) {
        close(ctx->handle);
      }

      MutexLockNative(vm->globallock);
      if (task->state == 2) {
        if (bytes_transferred >= 0) {
          if (ctx->op_type == 0 || ctx->op_type == 2) {
            task->result = ValStrLen(ctx->buffer, bytes_transferred);
          } else if (ctx->op_type == 1) {
            task->result = ValBool(1);
          } else if (ctx->op_type == 3) {
            task->result = ValInt(bytes_transferred);
          } else if (ctx->op_type == 4) {
            int client_fd = bytes_transferred;
            task->result = ValInt(client_fd);
          } else if (ctx->op_type == 5) {
            task->result = ValInt((int64_t)ctx->handle);
          }
        } else {
          task->result = ValNull();
        }
        task->state = 3;

        for (int i = 0; i < task->awaitercount; i++) {
          VMTask *awaiter = task->awaiters[i];
          awaiter->state = 0;
          int targetworker = rand() % vm->workercount;
          WorkerPushTail(&vm->workers[targetworker], awaiter);
        }
      }
      MutexUnlockNative(vm->globallock);

      if (ctx->buffer)
        free(ctx->buffer);
      if (ctx->extra)
        free(ctx->extra);
      free(ctx);
    }
    io_uring_cqe_seen(&vm->ring, cqe);
  }
  return completed_count;
}
#else
static int PollIoCompletions(VM *vm, int timeout_ms) {
  (void)vm;
  (void)timeout_ms;
  return 0;
}
#endif
#endif

static VMTask *CreateAsyncPendingTask(VM *vm) {
  VMTask *t = calloc(1, sizeof(VMTask));
  t->refcount = 1;
  t->state = 2; // Blocked
  t->result = ValNull();
  MutexLockNative(vm->globallock);
  t->id = vm->nexttaskid++;
  if (vm->alltaskscount < 4096) {
    vm->alltasks[vm->alltaskscount++] = t;
    t->refcount++;
  }
  MutexUnlockNative(vm->globallock);
  return t;
}

int gSandboxEnabled = 0;
int gCapMode = 0;
uint32_t gPermissions = 0xFFFFFFFF;
int gCustomPermissions = 0;

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define NewDir(path) (_mkdir(path) == 0)
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define NewDir(path) (mkdir(path, 0777) == 0)
#endif

static void RecordTraceStep(VMTask *task, uint8_t op, uint8_t *ip,
                            AKSValue *stacktop, FnProto *fn) {
  if (task->tracestepcount >= 256) {
    task->tracingmode = 0; // stop tracing if it gets too long
    return;
  }
  TraceStep *trace = (TraceStep *)task->recordedtrace;
  if (!trace) {
    trace = malloc(256 * sizeof(TraceStep));
    task->recordedtrace = trace;
  }
  TraceStep *step = &trace[task->tracestepcount++];
  step->op = op;
  step->ipoffset = (int)(ip - fn->chunk.code);
  step->jumpoffset = 0;
  step->guardoutcome = 0;
  step->pushival = 0;
  step->pushfval = 0;
  step->slot = 0;

  switch (op) {
  case OPPUSHINT: {
    int64_t val = 0;
    for (int k = 0; k < 8; k++) {
      val = (val << 8) | ip[1 + k];
    }
    step->pushival = val;
    break;
  }
  case OPPUSHFLOAT: {
    uint64_t bits = 0;
    for (int k = 0; k < 8; k++) {
      bits = (bits << 8) | ip[1 + k];
    }
    double dval;
    memcpy(&dval, &bits, 8);
    step->pushfval = dval;
    break;
  }
  case OPLOAD:
  case OPSTORE: {
    step->slot = (ip[1] << 8) | ip[2];
    break;
  }
  case OPJUMP: {
    int32_t offset =
        (int32_t)((ip[1] << 24) | (ip[2] << 16) | (ip[3] << 8) | ip[4]);
    step->jumpoffset = offset;
    break;
  }
  case OPJUMPIFFALSE: {
    int32_t offset =
        (int32_t)((ip[1] << 24) | (ip[2] << 16) | (ip[3] << 8) | ip[4]);
    step->jumpoffset = offset;
    int truthy = ValIsTruthy(stacktop[-1]);
    step->guardoutcome = !truthy; // if falsy jump taken, 0 if truthy
    break;
  }
  }
}

// native mutex wrappers
static void *MutexNewNative(void) {
#ifdef _WIN32
  CRITICAL_SECTION *cs = malloc(sizeof(CRITICAL_SECTION));
  InitializeCriticalSection(cs);
  return cs;
#else
  pthread_mutex_t *m = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(m, NULL);
  return m;
#endif
}
static void MutexFreeNative(void *m) {
  if (m) {
#ifdef _WIN32
    DeleteCriticalSection((CRITICAL_SECTION *)m);
#else
    pthread_mutex_destroy((pthread_mutex_t *)m);
#endif
    free(m);
  }
}
static void MutexLockNative(void *m) {
  if (m) {
#ifdef _WIN32
    EnterCriticalSection((CRITICAL_SECTION *)m);
#else
    pthread_mutex_lock((pthread_mutex_t *)m);
#endif
  }
}
static void MutexUnlockNative(void *m) {
  if (m) {
#ifdef _WIN32
    LeaveCriticalSection((CRITICAL_SECTION *)m);
#else
    pthread_mutex_unlock((pthread_mutex_t *)m);
#endif
  }
}

void *AllocStackArenaWithGuard(size_t size, size_t *allocated_size) {
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  size_t page_size = si.dwPageSize;
  size = (size + page_size - 1) & ~(page_size - 1);

  char *base =
      (char *)VirtualAlloc(NULL, size + page_size, MEM_RESERVE, PAGE_NOACCESS);
  if (!base)
    return NULL;

  if (!VirtualAlloc(base, size, MEM_COMMIT, PAGE_READWRITE)) {
    VirtualFree(base, 0, MEM_RELEASE);
    return NULL;
  }

  char *guard_page = base + size;
  if (!VirtualAlloc(guard_page, page_size, MEM_COMMIT, PAGE_READWRITE)) {
    VirtualFree(base, 0, MEM_RELEASE);
    return NULL;
  }
  DWORD old_protect;
  if (!VirtualProtect(guard_page, page_size, PAGE_READWRITE | PAGE_GUARD,
                      &old_protect)) {
    VirtualFree(base, 0, MEM_RELEASE);
    return NULL;
  }

  *allocated_size = size;
  return base;
#else
  size_t page_size = sysconf(_SC_PAGESIZE);
  size = (size + page_size - 1) & ~(page_size - 1);

  char *base = mmap(NULL, size + page_size, PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (base == MAP_FAILED)
    return NULL;

  if (mprotect(base, size, PROT_READ | PROT_WRITE) != 0) {
    munmap(base, size + page_size);
    return NULL;
  }

  *allocated_size = size;
  return base;
#endif
}

#include "vm_tasks.inc.c"


VM *VMNew(void) {
  srand((unsigned int)time(NULL));
  VM *vm = calloc(1, sizeof(VM));
  if (vm) {
    vm->panic_hook = ValNull();
    vm->permissions = gPermissions;
    vm->sandboxenabled = gSandboxEnabled;
    vm->capmode = gCapMode;
    vm->inrestrictedeval = 0;
    vm->no_bounds_check = gNoBoundsCheck;
    vm->strict_ovf = gStrictOvf;
    vm->stackcanary =
        (((uint64_t)rand() << 16) | (uint64_t)rand()) & 0x00007FFFFFFFFFFFULL;
    vm->instructionlimit = 10000000;
    vm->instructioncount = 0;
    vm->hot_loop_threshold = 50;
    vm->hot_func_threshold = 50;
    vm->flame_node_count = 0;
    vm->flame_profiling_enabled = 0;
    vm->flame_current_node = 0;
    vm->flame_output_path[0] = '\0';
    memset(vm->flame_nodes, 0, sizeof(vm->flame_nodes));

    vm->globalcount = 0;
    vm->fncount = 0;
    vm->strcount = 0;
    vm->isrepl = 0;
    vm->nexttaskid = 1;
    vm->registered_natives_count = 0;
    vm->globallock = MutexNewNative();
    vm->shouldterminate = 0;
    memset(vm->globals, 0, sizeof(vm->globals));

    vm->timercount = 0;
    memset(vm->timers, 0, sizeof(vm->timers));

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    vm->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    vm->timerthread = CreateThread(NULL, 0, TimerThreadFunc, vm, 0, NULL);
#else
#ifdef AKS_USE_IO_URING
    io_uring_queue_init(256, &vm->ring, 0);
#endif
    vm->timerthread = CreateThread(NULL, 0, TimerThreadFunc, vm, 0, NULL);
#endif

    // spawn worker threads
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    vm->workercount = sysinfo.dwNumberOfProcessors;
    if (vm->workercount < 1)
      vm->workercount = 1;
    if (vm->workercount > 16)
      vm->workercount = 16;
    for (int i = 0; i < vm->workercount; i++) {
      vm->workers[i].id = i;
      vm->workers[i].lock = MutexNewNative();
      vm->workers[i].head = 0;
      vm->workers[i].tail = 0;
      WorkerContext *ctx = malloc(sizeof(WorkerContext));
      ctx->vm = vm;
      ctx->workerid = i;
      vm->workers[i].threadhandle =
          CreateThread(NULL, 0, WorkerThreadFunc, ctx, 0, NULL);
    }
  }
  return vm;
}

void VMFree(VM *vm) {
  if (!vm)
    return;
  if (vm->flame_profiling_enabled && vm->flame_output_path[0] != '\0') {
    VMGenerateFlamegraphSVG(vm, vm->flame_output_path);
  }
  if (vm->pgoprofile) {
    FILE *pf = fopen(vm->pgoprofile, "w");
    if (pf) {
      for (int i = 0; i < vm->fncount; i++) {
        if (vm->fns[i] && vm->fns[i]->chunk.loopcounters) {
          for (int offset = 0; offset < vm->fns[i]->chunk.count; offset++) {
            int c = vm->fns[i]->chunk.loopcounters[offset];
            if (c > 0) {
              fprintf(pf, "loop %d %d %d\n", i, offset, c);
            }
          }
        }
        if (i < 4096 && vm->fncallcounts[i] > 0) {
          fprintf(pf, "call %d %d\n", i, vm->fncallcounts[i]);
        }
      }
      fclose(pf);
    }
  }
  vm->shouldterminate = 1;
  if (vm->timerthread) {
    WaitForSingleObject((HANDLE)vm->timerthread, INFINITE);
    CloseHandle((HANDLE)vm->timerthread);
  }
  for (int i = 0; i < vm->workercount; i++) {
    if (vm->workers[i].threadhandle) {
      WaitForSingleObject((HANDLE)vm->workers[i].threadhandle, INFINITE);
      CloseHandle((HANDLE)vm->workers[i].threadhandle);
    }
    MutexFreeNative(vm->workers[i].lock);
  }

#ifdef _WIN32
  if (vm->iocp) {
    CloseHandle((HANDLE)vm->iocp);
  }
  WSACleanup();
#else
#ifdef AKS_USE_IO_URING
  io_uring_queue_exit(&vm->ring);
#endif
#endif

  MutexFreeNative(vm->globallock);
  ValFree(&vm->panic_hook);

  for (int i = 0; i < vm->globalcount; i++) {
    free(vm->globals[i].name);
    ValFree(&vm->globals[i].val);
  }
  for (int i = 0; i < vm->alltaskscount; i++) {
    TaskFree(vm->alltasks[i]);
  }
  for (int i = 0; i < vm->strcount; i++) {
    free(vm->strpool[i]);
  }
  free(vm->strpool);
  for (int i = 0; i < vm->fncount; i++) {
    if (vm->fns[i]) {
      free(vm->fns[i]->chunk.code);
      free(vm->fns[i]->chunk.lines);
      if (vm->fns[i]->chunk.globalcache)
        free(vm->fns[i]->chunk.globalcache);
      if (vm->fns[i]->chunk.fieldcache)
        free(vm->fns[i]->chunk.fieldcache);
      if (vm->fns[i]->chunk.loopcounters)
        free(vm->fns[i]->chunk.loopcounters);
      if (vm->fns[i]->chunk.executed)
        free(vm->fns[i]->chunk.executed);
      FreeRegIfNotNull(vm->fns[i]);
      free(vm->fns[i]);
    }
  }
  for (int i = 0; i < vm->jitloopcount; i++) {
    if (vm->jitloops[i].libhandle) {
      FreeLibrary((HMODULE)vm->jitloops[i].libhandle);
      char dllfile[128];
      snprintf(dllfile, sizeof(dllfile), "jitloop%d%d.dll",
               vm->jitloops[i].fnid, vm->jitloops[i].targetoffset);
      DeleteFileA(dllfile);
      snprintf(dllfile, sizeof(dllfile), "jittrace%d%d.dll",
               vm->jitloops[i].fnid, vm->jitloops[i].targetoffset);
      DeleteFileA(dllfile);
    }
  }
  for (int i = 0; i < vm->registered_natives_count; i++) {
    free(vm->registered_natives[i].name);
  }
  for (int i = 0; i < vm->structcount; i++) {
    for (int f = 0; f < vm->structs[i].fc; f++) {
      free(vm->structs[i].fnames[f]);
      free(vm->structs[i].ftypes[f]);
    }
    free(vm->structs[i].fnames);
    free(vm->structs[i].ftypes);
  }
  FreeHuffmanTree(huff_tree_root);
  huff_tree_root = NULL;
  free(vm);
  if (gActiveStrs != 0 || gactivearrs != 0 || gactivestructs != 0) {
    fprintf(stderr,
            "\n[AKS ALLOC TRACKER ERROR] Active allocations remaining:\n");
    fprintf(stderr, "  Strings: %lld, Arrays: %lld, Structs: %lld\n",
            (long long)gActiveStrs, (long long)gactivearrs,
            (long long)gactivestructs);
  } else {
    printf("Memory diagnostics: all runtime references clean. 0 leaks.\n");
  }
}

static inline uint16_t FindTypeMethod(VM *vm, const char *typename, const char *methodname) {
  if (!vm || !typename || !methodname) return 65535;

  uint32_t h = 2166136261u;
  for (const char *p = typename; *p; p++) {
    h ^= (uint8_t)*p;
    h *= 16777619u;
  }
  h ^= (uint8_t)'.';
  h *= 16777619u;
  for (const char *p = methodname; *p; p++) {
    h ^= (uint8_t)*p;
    h *= 16777619u;
  }

  int idx = (int)(h & (VM_METHOD_CACHE_SIZE - 1));
  VMMethodCacheEntry *entry = &vm->method_cache[idx];
  if (entry->valid && strcmp(entry->typename, typename) == 0 && strcmp(entry->methodname, methodname) == 0) {
    return entry->fnid;
  }

  char fullfnname[256];
  snprintf(fullfnname, sizeof(fullfnname), "%s.%s", typename, methodname);
  uint16_t found = 65535;
  for (uint16_t k = 0; k < vm->fncount; k++) {
    if (vm->fns[k] && vm->fns[k]->name && strcmp(vm->fns[k]->name, fullfnname) == 0) {
      found = k;
      break;
    }
  }

  strncpy(entry->typename, typename, sizeof(entry->typename) - 1);
  entry->typename[sizeof(entry->typename) - 1] = '\0';
  strncpy(entry->methodname, methodname, sizeof(entry->methodname) - 1);
  entry->methodname[sizeof(entry->methodname) - 1] = '\0';
  entry->fnid = found;
  entry->valid = 1;

  return found;
}

int VMGenerateFlamegraphSVG(VM *vm, const char *filepath) {
  if (!vm || !filepath) return 0;
  FILE *f = fopen(filepath, "w");
  if (!f) return 0;

  if (vm->flame_node_count == 0) {
    uint64_t total_calls = 0;
    for (int i = 0; i < vm->fncount; i++) {
      if (i < 4096 && vm->fncallcounts[i] > 0) {
        total_calls += vm->fncallcounts[i];
      }
    }
    if (total_calls == 0) total_calls = 100;
    
    strncpy(vm->flame_nodes[0].name, "root", 127);
    vm->flame_nodes[0].samples = total_calls;
    vm->flame_nodes[0].self_samples = 0;
    vm->flame_nodes[0].parent_idx = -1;
    vm->flame_nodes[0].depth = 0;
    vm->flame_node_count = 1;

    for (int i = 0; i < vm->fncount && vm->flame_node_count < 4090; i++) {
      if (vm->fns[i]) {
        uint64_t count = (i < 4096 && vm->fncallcounts[i] > 0) ? vm->fncallcounts[i] : 1;
        int idx = vm->flame_node_count++;
        const char *fname = vm->fns[i]->name[0] ? vm->fns[i]->name : "anonymous";
        strncpy(vm->flame_nodes[idx].name, fname, 127);
        vm->flame_nodes[idx].samples = count;
        vm->flame_nodes[idx].self_samples = count;
        vm->flame_nodes[idx].parent_idx = 0;
        vm->flame_nodes[idx].depth = 1;
      }
    }
  }

  int total_nodes = vm->flame_node_count;
  uint64_t total_samples = 0;
  int max_depth = 0;
  for (int i = 0; i < total_nodes; i++) {
    if (vm->flame_nodes[i].depth == 0) {
      total_samples += vm->flame_nodes[i].samples;
    }
    if (vm->flame_nodes[i].depth > max_depth) {
      max_depth = vm->flame_nodes[i].depth;
    }
  }
  if (total_samples == 0) total_samples = 1;

  int width = 1200;
  int row_height = 24;
  int padding = 40;
  int height = (max_depth + 2) * row_height + padding * 2;

  fprintf(f, "<?xml version=\"1.0\" standalone=\"no\"?>\n");
  fprintf(f, "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n");
  fprintf(f, "<svg version=\"1.1\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\" xmlns=\"http://www.w3.org/2000/svg\">\n", width, height, width, height);
  fprintf(f, "<style type=\"text/css\">\n");
  fprintf(f, "  text { font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif; font-size: 12px; fill: #ffffff; pointer-events: none; }\n");
  fprintf(f, "  rect { stroke: #1e1e2e; stroke-width: 0.5; cursor: pointer; transition: opacity 0.2s; }\n");
  fprintf(f, "  rect:hover { opacity: 0.82; stroke: #ffffff; stroke-width: 1.2; }\n");
  fprintf(f, "</style>\n");
  fprintf(f, "<rect x=\"0\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"#181825\" rx=\"8\"/>\n", width, height);
  fprintf(f, "<text x=\"%d\" y=\"25\" font-size=\"16\" font-weight=\"bold\" fill=\"#cdd6f4\">AKS VM Execution Flamegraph Profile</text>\n", padding);

  const char *colors[] = {
      "#f38ba8", "#fab387", "#f9e2af", "#a6e3a1", "#89dceb", "#89b4fa", "#cba6f7", "#eba0ac"
  };
  int num_colors = sizeof(colors) / sizeof(colors[0]);

  double avail_width = (double)(width - padding * 2);

  for (int d = 0; d <= max_depth; d++) {
    double current_x = (double)padding;
    for (int i = 0; i < total_nodes; i++) {
      if (vm->flame_nodes[i].depth == d) {
        double nw = ((double)vm->flame_nodes[i].samples / (double)total_samples) * avail_width;
        if (nw < 2.0) nw = 2.0;
        double nx = current_x;
        current_x += nw;

        int y = height - padding - (d + 1) * row_height;
        const char *col = colors[i % num_colors];
        double pct = ((double)vm->flame_nodes[i].samples / (double)total_samples) * 100.0;
        
        fprintf(f, "<g>\n");
        fprintf(f, "  <title>%s (%llu samples, %.2f%%, self: %llu)</title>\n",
                vm->flame_nodes[i].name,
                (unsigned long long)vm->flame_nodes[i].samples,
                pct,
                (unsigned long long)vm->flame_nodes[i].self_samples);
        fprintf(f, "  <rect x=\"%.2f\" y=\"%d\" width=\"%.2f\" height=\"%d\" fill=\"%s\" rx=\"3\"/>\n",
                nx, y, nw, row_height - 2, col);
        if (nw > 35.0) {
          fprintf(f, "  <clipPath id=\"cp%d\"><rect x=\"%.2f\" y=\"%d\" width=\"%.2f\" height=\"%d\"/></clipPath>\n",
                  i, nx + 3, y, nw - 6, row_height - 2);
          fprintf(f, "  <text x=\"%.2f\" y=\"%d\" clip-path=\"url(#cp%d)\">%s (%.1f%%)</text>\n",
                  nx + 5, y + 15, i, vm->flame_nodes[i].name, pct);
        }
        fprintf(f, "</g>\n");
      }
    }
  }

  fprintf(f, "</svg>\n");
  fclose(f);
  return 1;
}

static void VMInjectNatives(VM *vm) {
  for (int i = 0; i < vm->registered_natives_count; i++) {
    const char *name = vm->registered_natives[i].name;
    // Check if function already exists in vm->fns
    int fnid = -1;
    for (int f = 0; f < vm->fncount; f++) {
      if (vm->fns[f] && strcmp(vm->fns[f]->name, name) == 0) {
        fnid = f;
        break;
      }
    }
    if (fnid < 0) {
      if (vm->fncount >= MAXFNS) {
        fprintf(stderr,
                "VM warning: Cannot inject native %s, fn limit reached\n",
                name);
        continue;
      }
      FnProto *destfn = calloc(1, sizeof(FnProto));
      if (!destfn)
        continue;
      strncpy(destfn->name, name, 127);
      destfn->id = vm->fncount;
      destfn->paramcount = 0;
      destfn->localcount = 0;
      destfn->scopedepth = 0;
      vm->fns[vm->fncount] = destfn;
      fnid = vm->fncount;
      vm->fncount++;
    }

    // Check if global variable already exists in vm->globals
    int gidx = -1;
    for (int g = 0; g < vm->globalcount; g++) {
      if (strcmp(vm->globals[g].name, name) == 0) {
        gidx = g;
        break;
      }
    }
    if (gidx >= 0) {
      ValFree(&vm->globals[gidx].val);
      vm->globals[gidx].val = ValFn(fnid);
    } else {
      if (vm->globalcount < MAXGLOBALS) {
        vm->globals[vm->globalcount].name = strdup(name);
        vm->globals[vm->globalcount].val = ValFn(fnid);
        vm->globalcount++;
      } else {
        fprintf(stderr,
                "VM warning: Cannot inject native global %s, global limit "
                "reached\n",
                name);
      }
    }
  }
}

int VMLoad(VM *vm, const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    perror("Error opening bytecode file");
    return 0;
  }

  // read bytecode header
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint32_t fncount;
  uint32_t strcount;

  if (fread(&magic, 4, 1, file) != 1 || fread(&version, 2, 1, file) != 1 ||
      fread(&flags, 2, 1, file) != 1 || fread(&fncount, 4, 1, file) != 1 ||
      fread(&strcount, 4, 1, file) != 1) {
    fprintf(stderr, "Error: failed to read bytecode header in file %s\n", path);
    fclose(file);
    return 0;
  }

  if (magic == 0x42534B41) {
    /* File has AKSBCA01 cache header; seek past it to read bytecode payload */
    fseek(file, sizeof(BcaHeader), SEEK_SET);
    if (fread(&magic, 4, 1, file) != 1 || fread(&version, 2, 1, file) != 1 ||
        fread(&flags, 2, 1, file) != 1 || fread(&fncount, 4, 1, file) != 1 ||
        fread(&strcount, 4, 1, file) != 1) {
      fprintf(stderr, "Error: failed to read bytecode header in cached file %s\n", path);
      fclose(file);
      return 0;
    }
  }

  if (magic != AKSMAGIC) {
    fprintf(stderr,
            "Error: invalid magic number: expected 0x%08X, got 0x%08X\n",
            AKSMAGIC, magic);
    fclose(file);
    return 0;
  }

  if (version != AKSVERSION) {
    fprintf(stderr, "Error: version mismatch: expected %d, got %d\n",
            AKSVERSION, version);
    fclose(file);
    return 0;
  }

  // load string pool
  vm->strcount = (int)strcount;
  vm->strpool = calloc(strcount, sizeof(char *));
  for (uint32_t i = 0; i < strcount; i++) {
    uint32_t len;
    if (fread(&len, 4, 1, file) != 1) {
      fprintf(stderr, "Error: failed to read string length\n");
      fclose(file);
      return 0;
    }
    char *str = malloc(len + 1);
    if (fread(str, 1, len, file) != len) {
      fprintf(stderr, "Error: failed to read string content\n");
      free(str);
      fclose(file);
      return 0;
    }
    str[len] = '\0';
    vm->strpool[i] = str;
  }

  // load function chunks
  vm->fncount = (int)fncount;
  for (uint32_t i = 0; i < fncount; i++) {
    uint32_t namelen;
    if (fread(&namelen, 4, 1, file) != 1) {
      fprintf(stderr, "Error: failed to read function name length\n");
      fclose(file);
      return 0;
    }
    char *fnname = malloc(namelen + 1);
    if (fread(fnname, 1, namelen, file) != namelen) {
      fprintf(stderr, "Error: failed to read function name\n");
      free(fnname);
      fclose(file);
      return 0;
    }
    fnname[namelen] = '\0';

    uint32_t paramcount;
    if (fread(&paramcount, 4, 1, file) != 1) {
      fprintf(stderr, "Error: failed to read parameter count\n");
      free(fnname);
      fclose(file);
      return 0;
    }

    uint32_t localcount;
    if (fread(&localcount, 4, 1, file) != 1) {
      fprintf(stderr, "Error: failed to read local count\n");
      free(fnname);
      fclose(file);
      return 0;
    }

    uint32_t codesize;
    if (fread(&codesize, 4, 1, file) != 1) {
      fprintf(stderr, "Error: failed to read code size\n");
      free(fnname);
      fclose(file);
      return 0;
    }

    uint8_t *code = malloc(codesize);
    if (fread(code, 1, codesize, file) != codesize) {
      fprintf(stderr, "Error: failed to read code bytecode\n");
      free(fnname);
      free(code);
      fclose(file);
      return 0;
    }

    FnProto *fn = calloc(1, sizeof(FnProto));
    strncpy(fn->name, fnname, 127);
    free(fnname);
    fn->id = (int)i;
    fn->paramcount = (int)paramcount;
    fn->localcount = (int)localcount;
    fn->chunk.code = code;
    fn->chunk.count = (int)codesize;
    fn->chunk.cap = (int)codesize;
    fn->chunk.lines =
        calloc(codesize, sizeof(int)); // zero-fill line metadata for now
    fn->chunk.globalcache = malloc(codesize * sizeof(int));
    for (int k = 0; k < (int)codesize; k++)
      fn->chunk.globalcache[k] = -1;
    fn->chunk.fieldcache = malloc(codesize * sizeof(int));
    for (int k = 0; k < (int)codesize; k++)
      fn->chunk.fieldcache[k] = -1;
    fn->chunk.loopcounters = calloc(codesize, sizeof(int));

    uint8_t isasync = 0;
    if (fread(&isasync, 1, 1, file) != 1) {
      fprintf(stderr, "Error: failed to read isasync\n");
      free(code);
      fclose(file);
      return 0;
    }
    fn->isasync = (int)isasync;

    for (int p = 0; p < (int)paramcount; p++) {
      uint32_t plen = 0;
      if (fread(&plen, 4, 1, file) != 1) {
        fprintf(stderr, "Error: failed to read parameter name length\n");
        free(code);
        fclose(file);
        return 0;
      }
      char *pname = malloc(plen + 1);
      if (fread(pname, 1, plen, file) != plen) {
        fprintf(stderr, "Error: failed to read parameter name\n");
        free(pname);
        free(code);
        fclose(file);
        return 0;
      }
      pname[plen] = '\0';
      strncpy(fn->locals[p].name, pname, 127);
      fn->locals[p].slot = p;
      free(pname);
    }

    uint32_t flen = 0;
    if (fread(&flen, 4, 1, file) != 1) {
      fprintf(stderr, "Error: failed to read file_name length\n");
      free(code);
      free(fn);
      fclose(file);
      return 0;
    }
    if (flen > 0) {
      char *fname = malloc(flen + 1);
      if (fread(fname, 1, flen, file) != flen) {
        fprintf(stderr, "Error: failed to read file_name\n");
        free(fname);
        free(code);
        free(fn);
        fclose(file);
        return 0;
      }
      fname[flen] = '\0';
      strncpy(fn->file_name, fname, sizeof(fn->file_name) - 1);
      fn->file_name[sizeof(fn->file_name) - 1] = '\0';
      free(fname);
    } else {
      fn->file_name[0] = '\0';
    }

    if (codesize > 0) {
      free(fn->chunk.lines); // free the calloced dummy lines array
      fn->chunk.lines = malloc(codesize * sizeof(int));
      if (fread(fn->chunk.lines, sizeof(int), codesize, file) != codesize) {
        fprintf(stderr, "Error: failed to read lines array\n");
        free(code);
        free(fn);
        fclose(file);
        return 0;
      }
    }

    vm->fns[i] = fn;
    TranslateToRegister(vm, fn);
  }

  uint32_t structcount = 0;
  if (fread(&structcount, 4, 1, file) == 1) {
    for (uint32_t i = 0; i < structcount; i++) {
      uint32_t namelen = 0;
      if (fread(&namelen, 4, 1, file) != 1)
        break;
      char *sname = malloc(namelen + 1);
      if (fread(sname, 1, namelen, file) != namelen) {
        free(sname);
        break;
      }
      sname[namelen] = '\0';

      uint32_t fc = 0;
      if (fread(&fc, 4, 1, file) != 1) {
        free(sname);
        break;
      }

      char **fnames = malloc(fc * sizeof(char *));
      char **ftypes = malloc(fc * sizeof(char *));
      int success = 1;
      for (uint32_t j = 0; j < fc; j++) {
        uint32_t fnlen = 0;
        if (fread(&fnlen, 4, 1, file) != 1) {
          success = 0;
          break;
        }
        fnames[j] = malloc(fnlen + 1);
        if (fread(fnames[j], 1, fnlen, file) != fnlen) {
          free(fnames[j]);
          success = 0;
          break;
        }
        fnames[j][fnlen] = '\0';

        uint32_t ftlen = 0;
        if (fread(&ftlen, 4, 1, file) != 1) {
          success = 0;
          break;
        }
        ftypes[j] = malloc(ftlen + 1);
        if (fread(ftypes[j], 1, ftlen, file) != ftlen) {
          free(ftypes[j]);
          success = 0;
          break;
        }
        ftypes[j][ftlen] = '\0';
      }

      uint32_t ic = 0;
      if (fread(&ic, 4, 1, file) != 1) {
        success = 0;
      }
      for (uint32_t j = 0; j < ic; j++) {
        uint32_t inlen = 0;
        if (fread(&inlen, 4, 1, file) != 1) {
          success = 0;
          break;
        }
        char *iname = malloc(inlen + 1);
        if (fread(iname, 1, inlen, file) != inlen) {
          free(iname);
          success = 0;
          break;
        }
        free(iname);
      }

      if (success) {
        VMRegisterStruct(vm, sname, (int)fc, fnames, ftypes);
      }

      for (uint32_t j = 0; j < fc; j++) {
        free(fnames[j]);
        free(ftypes[j]);
      }
      free(fnames);
      free(ftypes);
      free(sname);
    }
  }

  fclose(file);
  VMInjectNatives(vm);
  return 1;
}

typedef struct {
  const uint8_t *data;
  size_t size;
  size_t offset;
} buf_reader_t;

static int BufRead(buf_reader_t *r, void *dest, size_t size) {
  if (r->offset + size > r->size)
    return 0;
  memcpy(dest, r->data + r->offset, size);
  r->offset += size;
  return 1;
}

int VMLoadBuffer(VM *vm, const uint8_t *buffer, size_t size) {
  buf_reader_t r = {buffer, size, 0};
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint32_t fncount;
  uint32_t strcount;

  if (!BufRead(&r, &magic, 4) || !BufRead(&r, &version, 2) ||
      !BufRead(&r, &flags, 2) || !BufRead(&r, &fncount, 4) ||
      !BufRead(&r, &strcount, 4)) {
    fprintf(stderr, "Error: failed to read bytecode header in buffer\n");
    return 0;
  }

  if (magic == 0x42534B41 && size >= sizeof(BcaHeader) + 16) {
    /* Buffer has AKSBCA01 cache header; skip past it to read bytecode payload */
    r.offset = sizeof(BcaHeader);
    if (!BufRead(&r, &magic, 4) || !BufRead(&r, &version, 2) ||
        !BufRead(&r, &flags, 2) || !BufRead(&r, &fncount, 4) ||
        !BufRead(&r, &strcount, 4)) {
      fprintf(stderr, "Error: failed to read bytecode header after BCA header in buffer\n");
      return 0;
    }
  }

  if (magic != AKSMAGIC) {
    fprintf(stderr,
            "Error: invalid magic number: expected 0x%08X, got 0x%08X\n",
            AKSMAGIC, magic);
    return 0;
  }

  if (version != AKSVERSION) {
    fprintf(stderr, "Error: version mismatch: expected %d, got %d\n",
            AKSVERSION, version);
    return 0;
  }

  // load string pool
  vm->strcount = (int)strcount;
  vm->strpool = calloc(strcount, sizeof(char *));
  for (uint32_t i = 0; i < strcount; i++) {
    uint32_t len;
    if (!BufRead(&r, &len, 4)) {
      fprintf(stderr, "Error: failed to read string length from buffer\n");
      return 0;
    }
    char *str = malloc(len + 1);
    if (!BufRead(&r, str, len)) {
      fprintf(stderr, "Error: failed to read string content from buffer\n");
      free(str);
      return 0;
    }
    str[len] = '\0';
    vm->strpool[i] = str;
  }

  // load function chunks
  vm->fncount = (int)fncount;
  for (uint32_t i = 0; i < fncount; i++) {
    uint32_t namelen;
    if (!BufRead(&r, &namelen, 4)) {
      fprintf(stderr,
              "Error: failed to read function name length from buffer\n");
      return 0;
    }
    char *fnname = malloc(namelen + 1);
    if (!BufRead(&r, fnname, namelen)) {
      fprintf(stderr, "Error: failed to read function name from buffer\n");
      free(fnname);
      return 0;
    }
    fnname[namelen] = '\0';

    uint32_t paramcount;
    if (!BufRead(&r, &paramcount, 4)) {
      fprintf(stderr, "Error: failed to read parameter count from buffer\n");
      free(fnname);
      return 0;
    }

    uint32_t localcount;
    if (!BufRead(&r, &localcount, 4)) {
      fprintf(stderr, "Error: failed to read local count from buffer\n");
      free(fnname);
      return 0;
    }

    uint32_t codesize;
    if (!BufRead(&r, &codesize, 4)) {
      fprintf(stderr, "Error: failed to read code size from buffer\n");
      free(fnname);
      return 0;
    }

    uint8_t *code = malloc(codesize);
    if (!BufRead(&r, code, codesize)) {
      fprintf(stderr, "Error: failed to read code bytecode from buffer\n");
      free(fnname);
      free(code);
      return 0;
    }

    FnProto *fn = calloc(1, sizeof(FnProto));
    strncpy(fn->name, fnname, 127);
    free(fnname);
    fn->id = (int)i;
    fn->paramcount = (int)paramcount;
    fn->localcount = (int)localcount;
    fn->chunk.code = code;
    fn->chunk.count = (int)codesize;
    fn->chunk.cap = (int)codesize;
    fn->chunk.lines = calloc(codesize, sizeof(int));
    fn->chunk.globalcache = malloc(codesize * sizeof(int));
    for (int k = 0; k < (int)codesize; k++)
      fn->chunk.globalcache[k] = -1;
    fn->chunk.fieldcache = malloc(codesize * sizeof(int));
    for (int k = 0; k < (int)codesize; k++)
      fn->chunk.fieldcache[k] = -1;
    fn->chunk.loopcounters = calloc(codesize, sizeof(int));

    uint8_t isasync = 0;
    if (!BufRead(&r, &isasync, 1)) {
      fprintf(stderr, "Error: failed to read isasync from buffer\n");
      free(code);
      free(fn);
      return 0;
    }
    fn->isasync = (int)isasync;

    for (int p = 0; p < (int)paramcount; p++) {
      uint32_t plen = 0;
      if (!BufRead(&r, &plen, 4)) {
        fprintf(stderr,
                "Error: failed to read parameter name length from buffer\n");
        free(code);
        free(fn);
        return 0;
      }
      char *pname = malloc(plen + 1);
      if (!BufRead(&r, pname, plen)) {
        fprintf(stderr, "Error: failed to read parameter name from buffer\n");
        free(pname);
        free(code);
        free(fn);
        return 0;
      }
      pname[plen] = '\0';
      strncpy(fn->locals[p].name, pname, 127);
      fn->locals[p].slot = p;
      free(pname);
    }

    uint32_t flen = 0;
    if (!BufRead(&r, &flen, 4)) {
      fprintf(stderr, "Error: failed to read file_name length from buffer\n");
      free(code);
      free(fn);
      return 0;
    }
    if (flen > 0) {
      char *fname = malloc(flen + 1);
      if (!BufRead(&r, fname, flen)) {
        fprintf(stderr, "Error: failed to read file_name from buffer\n");
        free(fname);
        free(code);
        free(fn);
        return 0;
      }
      fname[flen] = '\0';
      strncpy(fn->file_name, fname, sizeof(fn->file_name) - 1);
      fn->file_name[sizeof(fn->file_name) - 1] = '\0';
      free(fname);
    } else {
      fn->file_name[0] = '\0';
    }

    if (codesize > 0) {
      free(fn->chunk.lines); // free the calloced dummy lines array
      fn->chunk.lines = malloc(codesize * sizeof(int));
      if (!BufRead(&r, fn->chunk.lines, codesize * sizeof(int))) {
        fprintf(stderr, "Error: failed to read lines array from buffer\n");
        free(code);
        free(fn);
        return 0;
      }
    }

    vm->fns[i] = fn;
    TranslateToRegister(vm, fn);
  }

  uint32_t structcount = 0;
  if (BufRead(&r, &structcount, 4)) {
    for (uint32_t i = 0; i < structcount; i++) {
      uint32_t namelen = 0;
      if (!BufRead(&r, &namelen, 4))
        break;
      char *sname = malloc(namelen + 1);
      if (!BufRead(&r, sname, namelen)) {
        free(sname);
        break;
      }
      sname[namelen] = '\0';

      uint32_t fc = 0;
      if (!BufRead(&r, &fc, 4)) {
        free(sname);
        break;
      }

      char **fnames = malloc(fc * sizeof(char *));
      char **ftypes = malloc(fc * sizeof(char *));
      int success = 1;
      for (uint32_t j = 0; j < fc; j++) {
        uint32_t fnlen = 0;
        if (!BufRead(&r, &fnlen, 4)) {
          success = 0;
          break;
        }
        fnames[j] = malloc(fnlen + 1);
        if (!BufRead(&r, fnames[j], fnlen)) {
          free(fnames[j]);
          success = 0;
          break;
        }
        fnames[j][fnlen] = '\0';

        uint32_t ftlen = 0;
        if (!BufRead(&r, &ftlen, 4)) {
          success = 0;
          break;
        }
        ftypes[j] = malloc(ftlen + 1);
        if (!BufRead(&r, ftypes[j], ftlen)) {
          free(ftypes[j]);
          success = 0;
          break;
        }
        ftypes[j][ftlen] = '\0';
      }

      uint32_t ic = 0;
      if (!BufRead(&r, &ic, 4)) {
        success = 0;
      }
      for (uint32_t j = 0; j < ic; j++) {
        uint32_t inlen = 0;
        if (!BufRead(&r, &inlen, 4)) {
          success = 0;
          break;
        }
        char *iname = malloc(inlen + 1);
        if (!BufRead(&r, iname, inlen)) {
          free(iname);
          success = 0;
          break;
        }
        free(iname);
      }

      if (success) {
        VMRegisterStruct(vm, sname, (int)fc, fnames, ftypes);
      }

      for (uint32_t j = 0; j < fc; j++) {
        free(fnames[j]);
        free(ftypes[j]);
      }
      free(fnames);
      free(ftypes);
      free(sname);
    }
  }

  VMInjectNatives(vm);
  return 1;
}

static void VMRegisterStruct(VM *vm, const char *name, int fc, char **fnames,
                             char **ftypes) {
  AKSStruct *meta = calloc(1, sizeof(AKSStruct));
  AtomicIncrement(&gactivestructs);
  meta->typename = strdup("StructType");
  meta->refcount = 1;
  meta->fieldcount = 3;
  meta->fieldnames = malloc(3 * sizeof(char *));
  meta->fieldnames[0] = strdup("name");
  meta->fieldnames[1] = strdup("fields");
  meta->fieldnames[2] = strdup("types");

  meta->fieldvals = malloc(3 * sizeof(AKSValue));
  meta->fieldvals[0] = ValStr(name);

  AKSValue fields_arr = ValArr(fc);
  ValToArr(fields_arr)->count = fc;
  for (int j = 0; j < fc; j++) {
    ValToArr(fields_arr)->items[j] = ValStr(fnames[j]);
  }
  meta->fieldvals[1] = fields_arr;

  AKSValue types_arr = ValArr(fc);
  ValToArr(types_arr)->count = fc;
  for (int j = 0; j < fc; j++) {
    ValToArr(types_arr)->items[j] = ValStr(ftypes[j]);
  }
  meta->fieldvals[2] = types_arr;

  AKSValue metaval =
      ((uint64_t)0xFFF6 << 48) | ((uintptr_t)meta & 0x0000FFFFFFFFFFFFULL);

  int found = -1;
  for (int g = 0; g < vm->globalcount; g++) {
    if (strcmp(vm->globals[g].name, name) == 0) {
      found = g;
      break;
    }
  }
  if (found >= 0) {
    ValFree(&vm->globals[found].val);
    vm->globals[found].val = metaval;
  } else if (vm->globalcount < MAXGLOBALS) {
    vm->globals[vm->globalcount].name = strdup(name);
    vm->globals[vm->globalcount].val = metaval;
    vm->globalcount++;
  } else {
    fprintf(stderr, "VM warning: Cannot register metadata global '%s', global limit reached (%d)\n", name, MAXGLOBALS);
    ValFree(&metaval);
  }
}

int VMLoadMem(VM *vm, Compiler *c) {
  // copy string pool
  vm->strcount = c->strcount;
  vm->strpool = calloc(vm->strcount, sizeof(char *));
  for (int i = 0; i < vm->strcount; i++) {
    vm->strpool[i] = strdup(c->strpool[i]);
  }

  // copy function prototypes
  vm->fncount = c->fncount;
  for (int i = 0; i < vm->fncount; i++) {
    FnProto *srcfn = c->fns[i];
    FnProto *destfn = calloc(1, sizeof(FnProto));
    strncpy(destfn->name, srcfn->name, 127);
    strncpy(destfn->file_name, srcfn->file_name, sizeof(destfn->file_name) - 1);
    destfn->file_name[sizeof(destfn->file_name) - 1] = '\0';
    destfn->id = srcfn->id;
    destfn->paramcount = srcfn->paramcount;
    destfn->localcount = srcfn->localcount;
    destfn->scopedepth = srcfn->scopedepth;

    destfn->chunk.count = srcfn->chunk.count;
    destfn->chunk.cap = srcfn->chunk.count;
    destfn->chunk.code = malloc(srcfn->chunk.count);
    memcpy(destfn->chunk.code, srcfn->chunk.code, srcfn->chunk.count);

    destfn->chunk.lines = malloc(srcfn->chunk.count * sizeof(int));
    memcpy(destfn->chunk.lines, srcfn->chunk.lines,
           srcfn->chunk.count * sizeof(int));

    destfn->chunk.globalcache = malloc(srcfn->chunk.count * sizeof(int));
    for (int k = 0; k < srcfn->chunk.count; k++)
      destfn->chunk.globalcache[k] = -1;
    destfn->chunk.fieldcache = malloc(srcfn->chunk.count * sizeof(int));
    for (int k = 0; k < srcfn->chunk.count; k++)
      destfn->chunk.fieldcache[k] = -1;

    destfn->chunk.loopcounters = calloc(srcfn->chunk.count, sizeof(int));
    destfn->chunk.executed = calloc(srcfn->chunk.count, sizeof(uint8_t));

    destfn->isasync = srcfn->isasync;
    for (int p = 0; p < srcfn->localcount; p++) {
      strncpy(destfn->locals[p].name, srcfn->locals[p].name, 127);
      destfn->locals[p].slot = srcfn->locals[p].slot;
      strncpy(destfn->locals[p].typename, srcfn->locals[p].typename, 63);
    }

    vm->fns[i] = destfn;
    TranslateToRegister(vm, destfn);
  }

  vm->structcount = c->structcount;
  for (int i = 0; i < c->structcount && i < 512; i++) {
    CompilerStruct *srcs = &c->structs[i];
    VMRegisterStruct(vm, srcs->name, srcs->fc, srcs->fnames, srcs->ftypes);
    strncpy(vm->structs[i].name, srcs->name, sizeof(vm->structs[i].name) - 1);
    vm->structs[i].name[sizeof(vm->structs[i].name) - 1] = '\0';
    vm->structs[i].fc = srcs->fc;
    vm->structs[i].fnames = malloc((srcs->fc + 1) * sizeof(char *));
    vm->structs[i].ftypes = malloc((srcs->fc + 1) * sizeof(char *));
    for (int f = 0; f < srcs->fc; f++) {
      vm->structs[i].fnames[f] = strdup(srcs->fnames[f]);
      vm->structs[i].ftypes[f] = strdup(srcs->ftypes[f]);
    }
  }

  VMInjectNatives(vm);
  return 1;
}

// stdlib helpers for system, string, array, and json

#include <sys/stat.h>
static int FileExists(const char *path) {
  struct stat buffer;
  return (stat(path, &buffer) == 0);
}

static int SetEnvironmentVar(const char *name, const char *val) {
#ifdef _WIN32
  return _putenv_s(name, val) == 0;
#else
  return setenv(name, val, 1) == 0;
#endif
}

static void UnEscapeStrInplace(char *s) {
  char *src = s;
  char *dst = s;
  while (*src) {
    if (*src == '\\') {
      src++;
      if (*src == 'n') {
        *dst++ = '\n';
        src++;
      } else if (*src == 't') {
        *dst++ = '\t';
        src++;
      } else if (*src == 'r') {
        *dst++ = '\r';
        src++;
      } else if (*src == 'b') {
        *dst++ = '\b';
        src++;
      } else if (*src == 'f') {
        *dst++ = '\f';
        src++;
      } else if (*src == '"') {
        *dst++ = '"';
        src++;
      } else if (*src == '\\') {
        *dst++ = '\\';
        src++;
      } else if (*src == '/') {
        *dst++ = '/';
        src++;
      } else {
        *dst++ = '\\';
      } // keep backslash if not escape
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

static int64_t MathGcd(int64_t a, int64_t b) {
  a = a < 0 ? -a : a;
  b = b < 0 ? -b : b;
  while (b != 0) {
    int64_t t = b;
    b = a % b;
    a = t;
  }
  return a;
}

static int64_t MathLcm(int64_t a, int64_t b) {
  if (a == 0 || b == 0)
    return 0;
  a = a < 0 ? -a : a;
  b = b < 0 ? -b : b;
  return (a / MathGcd(a, b)) * b;
}

static int MathIsPrime(int64_t n) {
  if (n <= 1)
    return 0;
  if (n <= 3)
    return 1;
  if (n % 2 == 0 || n % 3 == 0)
    return 0;
  for (int64_t i = 5; i * i <= n; i += 6) {
    if (n % i == 0 || n % (i + 2) == 0)
      return 0;
  }
  return 1;
}

static double MathFact(double n) {
  if (n < 0.0)
    return 0.0;
  double res = 1.0;
  int64_t lim = (int64_t)n;
  for (int64_t i = 2; i <= lim; i++) {
    res *= i;
  }
  return res;
}

static double MathNpr(double n, double r) {
  if (n < 0 || r < 0 || n < r)
    return 0.0;
  double res = 1.0;
  int64_t Ln = (int64_t)n;
  int64_t lr = (int64_t)r;
  for (int64_t i = Ln - lr + 1; i <= Ln; i++) {
    res *= i;
  }
  return res;
}

static double MathNcr(double n, double r) {
  if (n < 0 || r < 0 || n < r)
    return 0.0;
  int64_t Ln = (int64_t)n;
  int64_t lr = (int64_t)r;
  if (lr > Ln - lr)
    lr = Ln - lr;
  double res = 1.0;
  for (int64_t i = 1; i <= lr; i++) {
    res *= (double)(Ln - lr + i) / i;
  }
  return round(res);
}

#define ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x, 2) ^ ROTRIGHT(x, 13) ^ ROTRIGHT(x, 22))
#define EP1(x) (ROTRIGHT(x, 6) ^ ROTRIGHT(x, 11) ^ ROTRIGHT(x, 25))
#define SIG0(x) (ROTRIGHT(x, 7) ^ ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x, 17) ^ ROTRIGHT(x, 19) ^ ((x) >> 10))

static const uint32_t sha256k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

typedef struct {
  uint8_t data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
} AKSSHA256CTX;

static void Sha256Transform(AKSSHA256CTX *ctx, const uint8_t *data) {
  uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];
  for (i = 0, j = 0; i < 16; ++i, j += 4)
    m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) |
           (data[j + 3]);
  for (; i < 64; ++i)
    m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];
  for (i = 0; i < 64; ++i) {
    t1 = h + EP1(e) + CH(e, f, g) + sha256k[i] + m[i];
    t2 = EP0(a) + MAJ(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void Sha256Init(AKSSHA256CTX *ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
}

static void Sha256Update(AKSSHA256CTX *ctx, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    ctx->data[ctx->datalen] = data[i];
    ctx->datalen++;
    if (ctx->datalen == 64) {
      Sha256Transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

static void Sha256Final(AKSSHA256CTX *ctx, uint8_t *hash) {
  uint32_t i = ctx->datalen;
  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56)
      ctx->data[i++] = 0x00;
  } else {
    ctx->data[i++] = 0x80;
    while (i < 64)
      ctx->data[i++] = 0x00;
    Sha256Transform(ctx, ctx->data);
    memset(ctx->data, 0, 56);
  }
  ctx->bitlen += ctx->datalen * 8;
  ctx->data[63] = (uint8_t)(ctx->bitlen);
  ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
  ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
  ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
  ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
  ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
  ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
  ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
  Sha256Transform(ctx, ctx->data);
  for (i = 0; i < 4; ++i) {
    hash[i] = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 4] = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 8] = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
  }
}

#undef ROTRIGHT
#undef CH
#undef MAJ
#undef EP0
#undef EP1
#undef SIG0
#undef SIG1

typedef struct {
  uint32_t count[2];
  uint32_t state[4];
  uint8_t buffer[64];
} AKSMD5CTX;

#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

static uint8_t PADDING[64] = {
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

#define ROTATELEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a, b, c, d, x, s, ac)                                               \
  {                                                                            \
    (a) += F((b), (c), (d)) + (x) + (uint32_t)(ac);                            \
    (a) = ROTATELEFT((a), (s));                                                \
    (a) += (b);                                                                \
  }
#define GG(a, b, c, d, x, s, ac)                                               \
  {                                                                            \
    (a) += G((b), (c), (d)) + (x) + (uint32_t)(ac);                            \
    (a) = ROTATELEFT((a), (s));                                                \
    (a) += (b);                                                                \
  }
#define HH(a, b, c, d, x, s, ac)                                               \
  {                                                                            \
    (a) += H((b), (c), (d)) + (x) + (uint32_t)(ac);                            \
    (a) = ROTATELEFT((a), (s));                                                \
    (a) += (b);                                                                \
  }
#define II(a, b, c, d, x, s, ac)                                               \
  {                                                                            \
    (a) += I((b), (c), (d)) + (x) + (uint32_t)(ac);                            \
    (a) = ROTATELEFT((a), (s));                                                \
    (a) += (b);                                                                \
  }

static void MD5Encode(uint8_t *output, const uint32_t *input, size_t len) {
  size_t i, j;
  for (i = 0, j = 0; j < len; i++, j += 4) {
    output[j] = (uint8_t)(input[i] & 0xff);
    output[j + 1] = (uint8_t)((input[i] >> 8) & 0xff);
    output[j + 2] = (uint8_t)((input[i] >> 16) & 0xff);
    output[j + 3] = (uint8_t)((input[i] >> 24) & 0xff);
  }
}

static void MD5Decode(uint32_t *output, const uint8_t *input, size_t len) {
  size_t i, j;
  for (i = 0, j = 0; j < len; i++, j += 4)
    output[i] = ((uint32_t)input[j]) | (((uint32_t)input[j + 1]) << 8) |
                (((uint32_t)input[j + 2]) << 16) |
                (((uint32_t)input[j + 3]) << 24);
}

static void MD5Transform(uint32_t state[4], const uint8_t block[64]) {
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
  MD5Decode(x, block, 64);
  /* Round 1 */
  FF(a, b, c, d, x[0], S11, 0xd76aa478);
  FF(d, a, b, c, x[1], S12, 0xe8c7b756);
  FF(c, d, a, b, x[2], S13, 0x242070db);
  FF(b, c, d, a, x[3], S14, 0xc1bdceee);
  FF(a, b, c, d, x[4], S11, 0xf57c0faf);
  FF(d, a, b, c, x[5], S12, 0x4787c62a);
  FF(c, d, a, b, x[6], S13, 0xa8304613);
  FF(b, c, d, a, x[7], S14, 0xfd469501);
  FF(a, b, c, d, x[8], S11, 0x698098d8);
  FF(d, a, b, c, x[9], S12, 0x8b44f7af);
  FF(c, d, a, b, x[10], S13, 0xffff5bb1);
  FF(b, c, d, a, x[11], S14, 0x895cd7be);
  FF(a, b, c, d, x[12], S11, 0x6b901122);
  FF(d, a, b, c, x[13], S12, 0xfd987193);
  FF(c, d, a, b, x[14], S13, 0xa679438e);
  FF(b, c, d, a, x[15], S14, 0x49b40821);
  /* Round 2 */
  GG(a, b, c, d, x[1], S21, 0xf61e2562);
  GG(d, a, b, c, x[6], S22, 0xc040b340);
  GG(c, d, a, b, x[11], S23, 0x265e5a51);
  GG(b, c, d, a, x[0], S24, 0xe9b6c7aa);
  GG(a, b, c, d, x[5], S21, 0xd62f105d);
  GG(d, a, b, c, x[10], S22, 0x2441453);
  GG(c, d, a, b, x[15], S23, 0xd8a1e681);
  GG(b, c, d, a, x[4], S24, 0xe7d3fbc8);
  GG(a, b, c, d, x[9], S21, 0x21e1cde6);
  GG(d, a, b, c, x[14], S22, 0xc33707d6);
  GG(c, d, a, b, x[3], S23, 0xf4d50d87);
  GG(b, c, d, a, x[8], S24, 0x455a14ed);
  GG(a, b, c, d, x[13], S21, 0xa9e3e905);
  GG(d, a, b, c, x[2], S22, 0xfcefa3f8);
  GG(c, d, a, b, x[7], S23, 0x676f02d9);
  GG(b, c, d, a, x[12], S24, 0x8d2a4c8a);
  /* Round 3 */
  HH(a, b, c, d, x[5], S31, 0xfffa3942);
  HH(d, a, b, c, x[8], S32, 0x8771f681);
  HH(c, d, a, b, x[11], S33, 0x6d9d6122);
  HH(b, c, d, a, x[14], S34, 0xfde5380c);
  HH(a, b, c, d, x[1], S31, 0xa4beea44);
  HH(d, a, b, c, x[4], S32, 0x4bdecfa9);
  HH(c, d, a, b, x[7], S33, 0xf6bb4b60);
  HH(b, c, d, a, x[10], S34, 0xbebfbc70);
  HH(a, b, c, d, x[13], S31, 0x289b7ec6);
  HH(d, a, b, c, x[0], S32, 0xeaa127fa);
  HH(c, d, a, b, x[3], S33, 0xd4ef3085);
  HH(b, c, d, a, x[6], S34, 0x4881d05);
  HH(a, b, c, d, x[9], S31, 0xd9d4d039);
  HH(d, a, b, c, x[12], S32, 0xe6db99e5);
  HH(c, d, a, b, x[15], S33, 0x1fa27cf8);
  HH(b, c, d, a, x[2], S34, 0xc4ac5665);
  /* Round 4 */
  II(a, b, c, d, x[0], S41, 0xf4292244);
  II(d, a, b, c, x[7], S42, 0x432aff97);
  II(c, d, a, b, x[14], S43, 0xab9423a7);
  II(b, c, d, a, x[5], S44, 0xfc93a039);
  II(a, b, c, d, x[12], S41, 0x655b59c3);
  II(d, a, b, c, x[3], S42, 0x8f0ccc92);
  II(c, d, a, b, x[10], S43, 0xffeff47d);
  II(b, c, d, a, x[1], S44, 0x85845dd1);
  II(a, b, c, d, x[8], S41, 0x6fa87e4f);
  II(d, a, b, c, x[15], S42, 0xfe2ce6e0);
  II(c, d, a, b, x[6], S43, 0xa3014314);
  II(b, c, d, a, x[13], S44, 0x4e0811a1);
  II(a, b, c, d, x[4], S41, 0xf7537e82);
  II(d, a, b, c, x[11], S42, 0xbd3af235);
  II(c, d, a, b, x[2], S43, 0x2ad7d2bb);
  II(b, c, d, a, x[9], S44, 0xeb86d391);

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  memset((void *)x, 0, sizeof(x));
}

static void MD5Init(AKSMD5CTX *ctx) {
  ctx->count[0] = ctx->count[1] = 0;
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xefcdab89;
  ctx->state[2] = 0x98badcfe;
  ctx->state[3] = 0x10325476;
}

static void MD5Update(AKSMD5CTX *ctx, const uint8_t *input, size_t inputLen) {
  size_t i, index, partLen;
  index = (size_t)((ctx->count[0] >> 3) & 0x3F);
  if ((ctx->count[0] += ((uint32_t)inputLen << 3)) < ((uint32_t)inputLen << 3))
    ctx->count[1]++;
  ctx->count[1] += ((uint32_t)inputLen >> 29);
  partLen = 64 - index;
  if (inputLen >= partLen) {
    memcpy((void *)&ctx->buffer[index], (const void *)input, partLen);
    MD5Transform(ctx->state, ctx->buffer);
    for (i = partLen; i + 63 < inputLen; i += 64)
      MD5Transform(ctx->state, &input[i]);
    index = 0;
  } else {
    i = 0;
  }
  memcpy((void *)&ctx->buffer[index], (const void *)&input[i], inputLen - i);
}

static void MD5Final(AKSMD5CTX *ctx, uint8_t digest[16]) {
  uint8_t bits[8];
  size_t index, padLen;
  MD5Encode(bits, ctx->count, 8);
  index = (size_t)((ctx->count[0] >> 3) & 0x3f);
  padLen = (index < 56) ? (56 - index) : (120 - index);
  MD5Update(ctx, PADDING, padLen);
  MD5Update(ctx, bits, 8);
  MD5Encode(digest, ctx->state, 16);
  memset((void *)ctx, 0, sizeof(*ctx));
}

#undef S11
#undef S12
#undef S13
#undef S14
#undef S21
#undef S22
#undef S23
#undef S24
#undef S31
#undef S32
#undef S33
#undef S34
#undef S41
#undef S42
#undef S43
#undef S44
#undef F
#undef G
#undef H
#undef I
#undef ROTATELEFT
#undef FF
#undef GG
#undef HH
#undef II

static const char b64table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *Base64EncodeStr(const uint8_t *src, size_t len) {
  size_t outlen = 4 * ((len + 2) / 3);
  char *out = malloc(outlen + 1);
  if (!out)
    return NULL;
  size_t i, j;
  for (i = 0, j = 0; i < len;) {
    uint32_t octeta = i < len ? src[i++] : 0;
    uint32_t octetb = i < len ? src[i++] : 0;
    uint32_t octetc = i < len ? src[i++] : 0;
    uint32_t triple = (octeta << 16) + (octetb << 8) + octetc;
    out[j++] = b64table[(triple >> 18) & 0x3F];
    out[j++] = b64table[(triple >> 12) & 0x3F];
    out[j++] = b64table[(triple >> 6) & 0x3F];
    out[j++] = b64table[triple & 0x3F];
  }
  size_t mod = len % 3;
  if (mod == 1) {
    out[outlen - 1] = '=';
    out[outlen - 2] = '=';
  } else if (mod == 2) {
    out[outlen - 1] = '=';
  }
  out[outlen] = '\0';
  return out;
}

static uint8_t *Base64DecodeStr(const char *src, size_t *outlen) {
  size_t len = strlen(src);
  if (len % 4 != 0)
    return NULL;
  size_t plen = len;
  if (len > 0 && src[len - 1] == '=')
    plen--;
  if (len > 1 && src[len - 2] == '=')
    plen--;
  size_t reslen = (plen * 3) / 4;
  uint8_t *out = malloc(reslen + 1);
  if (!out)
    return NULL;

  static char revtable[256];
  static int initialized = 0;
  if (!initialized) {
    memset(revtable, 0, 256);
    for (int i = 0; i < 64; i++)
      revtable[(unsigned char)b64table[i]] = i;
    initialized = 1;
  }

  size_t i, j;
  for (i = 0, j = 0; i < len;) {
    uint32_t sexa = src[i] == '=' ? 0 : revtable[(unsigned char)src[i]];
    i++;
    uint32_t sexb = src[i] == '=' ? 0 : revtable[(unsigned char)src[i]];
    i++;
    uint32_t sexc = src[i] == '=' ? 0 : revtable[(unsigned char)src[i]];
    i++;
    uint32_t sexd = src[i] == '=' ? 0 : revtable[(unsigned char)src[i]];
    i++;
    uint32_t triple = (sexa << 18) + (sexb << 12) + (sexc << 6) + sexd;
    if (j < reslen)
      out[j++] = (triple >> 16) & 0xFF;
    if (j < reslen)
      out[j++] = (triple >> 8) & 0xFF;
    if (j < reslen)
      out[j++] = triple & 0xFF;
  }
  out[reslen] = '\0';
  *outlen = reslen;
  return out;
}

static char *UrlEncodeStr(const char *str) {
  size_t len = strlen(str);
  size_t cap = len * 3 + 1;
  char *buf = malloc(cap);
  char *dst = buf;
  for (size_t i = 0; i < len; i++) {
    char c = str[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      *dst++ = c;
    } else {
      sprintf(dst, "%%%02X", (unsigned char)c);
      dst += 3;
    }
  }
  *dst = '\0';
  return buf;
}

static char *UrlDecodeStr(const char *str) {
  size_t len = strlen(str);
  char *buf = malloc(len + 1);
  char *dst = buf;
  for (size_t i = 0; i < len; i++) {
    if (str[i] == '%' && i + 2 < len) {
      char hex[3] = {str[i + 1], str[i + 2], '\0'};
      *dst++ = (char)strtol(hex, NULL, 16);
      i += 2;
    } else if (str[i] == '+') {
      *dst++ = ' ';
    } else {
      *dst++ = str[i];
    }
  }
  *dst = '\0';
  return buf;
}

#ifdef _WIN32
#include <direct.h>
#define GetCurrentDir(buf, size) _getcwd(buf, size)
#else
#include <unistd.h>
#define GetCurrentDir(buf, size) getcwd(buf, size)
#endif

static char *GetCwdStr(void) {
  char buf[1024];
  if (GetCurrentDir(buf, sizeof(buf))) {
    return strdup(buf);
  }
  return strdup("");
}

static int CompareAKSVals(const void *a, const void *b) {
  AKSValue va = *(const AKSValue *)a;
  AKSValue vb = *(const AKSValue *)b;
  ValType ta = ValKind(va);
  ValType tb = ValKind(vb);
  if ((ta == VALINT || ta == VALFLOAT) && (tb == VALINT || tb == VALFLOAT)) {
    double da = (ta == VALINT) ? (double)ValToInt(va) : ValToFloat(va);
    double db = (tb == VALINT) ? (double)ValToInt(vb) : ValToFloat(vb);
    return (da > db) - (da < db);
  }
  if (ta != tb) {
    return (int)ta - (int)tb;
  }
  if (ta == VALINT) {
    return (ValToInt(va) > ValToInt(vb)) - (ValToInt(va) < ValToInt(vb));
  }
  if (ta == VALFLOAT) {
    return (ValToFloat(va) > ValToFloat(vb)) -
           (ValToFloat(va) < ValToFloat(vb));
  }
  if (ta == VALSTR) {
    return strcmp(ValToStrPtr(va)->data, ValToStrPtr(vb)->data);
  }
  if (ta == VALBOOL) {
    return ValToBool(va) - ValToBool(vb);
  }
  return 0;
}

static void SkipJSONWhitespace(const char **ptr) {
  while (**ptr &&
         (**ptr == ' ' || **ptr == '\t' || **ptr == '\r' || **ptr == '\n')) {
    (*ptr)++;
  }
}

static AKSValue ParseJSONVal(const char **ptr, int *Error);

static AKSValue ParseJSONStr(const char **ptr, int *Error) {
  (*ptr)++; // skip opening '"'
  size_t cap = 32;
  size_t len = 0;
  char *buf = malloc(cap);
  while (**ptr && **ptr != '"') {
    char c = **ptr;
    if (c == '\\') {
      (*ptr)++;
      if (!**ptr) {
        *Error = 1;
        break;
      }
      c = **ptr;
      if (c == 'n')
        c = '\n';
      else if (c == 't')
        c = '\t';
      else if (c == 'r')
        c = '\r';
      else if (c == 'b')
        c = '\b';
      else if (c == 'f')
        c = '\f';
      else if (c == '"')
        c = '"';
      else if (c == '\\')
        c = '\\';
      else if (c == '/')
        c = '/';
      else if (c == 'u') {
        uint32_t cp = 0;
        int hex_ok = 1;
        for (int h = 0; h < 4; h++) {
          (*ptr)++;
          char hc = **ptr;
          int val;
          if (hc >= '0' && hc <= '9') val = hc - '0';
          else if (hc >= 'a' && hc <= 'f') val = hc - 'a' + 10;
          else if (hc >= 'A' && hc <= 'F') val = hc - 'A' + 10;
          else { hex_ok = 0; break; }
          cp = (cp << 4) | (uint32_t)val;
        }
        if (!hex_ok) { *Error = 1; break; }
        if (cp <= 0x7F) {
          c = (char)cp;
        } else if (cp <= 0x7FF) {
          if (len + 2 >= cap) { cap = cap * 2 + 8; buf = realloc(buf, cap); }
          buf[len++] = (char)(0xC0 | (cp >> 6));
          buf[len++] = (char)(0x80 | (cp & 0x3F));
          (*ptr)++;
          continue;
        } else {
          if (len + 3 >= cap) { cap = cap * 2 + 8; buf = realloc(buf, cap); }
          buf[len++] = (char)(0xE0 | (cp >> 12));
          buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
          buf[len++] = (char)(0x80 | (cp & 0x3F));
          (*ptr)++;
          continue;
        }
      }
    }
    if (len + 1 >= cap) {
      cap *= 2;
      buf = realloc(buf, cap);
    }
    buf[len++] = c;
    (*ptr)++;
  }
  if (**ptr == '"') {
    (*ptr)++;
  } else {
    *Error = 1;
  }
  buf[len] = '\0';
  AKSValue v = ValStrLen(buf, (int)len);
  free(buf);
  return v;
}

static AKSValue ParseJSONNumber(const char **ptr, int *Error) {
  const char *start = *ptr;
  int isfloat = 0;
  if (**ptr == '-')
    (*ptr)++;
  while (**ptr >= '0' && **ptr <= '9')
    (*ptr)++;
  if (**ptr == '.') {
    isfloat = 1;
    (*ptr)++;
    while (**ptr >= '0' && **ptr <= '9')
      (*ptr)++;
  }
  if (**ptr == 'e' || **ptr == 'E') {
    isfloat = 1;
    (*ptr)++;
    if (**ptr == '+' || **ptr == '-')
      (*ptr)++;
    while (**ptr >= '0' && **ptr <= '9')
      (*ptr)++;
  }
  size_t len = *ptr - start;
  char stackbuf[64];
  char *numbuf = (len < sizeof(stackbuf)) ? stackbuf : malloc(len + 1);
  memcpy(numbuf, start, len);
  numbuf[len] = '\0';
  AKSValue v;
  if (isfloat) {
    v = ValFloat(strtod(numbuf, NULL));
  } else {
    v = ValInt((int64_t)strtoll(numbuf, NULL, 10));
  }
  if (numbuf != stackbuf) free(numbuf);
  return v;
}

static AKSValue ParseJSONArr(const char **ptr, int *Error) {
  (*ptr)++; // skip '['
  AKSValue arrval = ValArr(4);
  SkipJSONWhitespace(ptr);
  if (**ptr == ']') {
    (*ptr)++;
    return arrval;
  }
  for (;;) {
    AKSValue item = ParseJSONVal(ptr, Error);
    if (*Error) {
      ValFree(&arrval);
      return ValNull();
    }
    // append item
    AKSArray *a = ValToArr(arrval);
    AksArrayEnsureCapacity(a, a->count + 1);
    a->items[a->count++] = item;

    SkipJSONWhitespace(ptr);
    if (**ptr == ']') {
      (*ptr)++;
      break;
    }
    if (**ptr == ',') {
      (*ptr)++;
    } else {
      *Error = 1;
      ValFree(&arrval);
      return ValNull();
    }
  }
  return arrval;
}

static AKSValue ParseJSONObj(const char **ptr, int *Error) {
  (*ptr)++; // skip '{'
  AKSValue objval = ValStruct("Object");
  SkipJSONWhitespace(ptr);
  if (**ptr == '}') {
    (*ptr)++;
    return objval;
  }
  for (;;) {
    SkipJSONWhitespace(ptr);
    if (**ptr != '"') {
      *Error = 1;
      ValFree(&objval);
      return ValNull();
    }
    AKSValue keyval = ParseJSONStr(ptr, Error);
    if (*Error) {
      ValFree(&objval);
      return ValNull();
    }
    SkipJSONWhitespace(ptr);
    if (**ptr != ':') {
      *Error = 1;
      ValFree(&keyval);
      ValFree(&objval);
      return ValNull();
    }
    (*ptr)++; // skip ':'
    AKSValue val = ParseJSONVal(ptr, Error);
    if (*Error) {
      ValFree(&keyval);
      ValFree(&objval);
      return ValNull();
    }

    // store in object
    AKSStruct *s = ValToObj(objval);
    int found = -1;
    for (int i = 0; i < s->fieldcount; i++) {
      const char *kstr = ValToStrPtr(keyval)->data;
      if (s->fieldnames[i] == kstr || strcmp(s->fieldnames[i], kstr) == 0) {
        found = i;
        break;
      }
    }
    if (found >= 0) {
      ValFree(&s->fieldvals[found]);
      s->fieldvals[found] = val;
    } else {
      s->fieldcount++;
      s->fieldnames = realloc(s->fieldnames, s->fieldcount * sizeof(char *));
      s->fieldvals = realloc(s->fieldvals, s->fieldcount * sizeof(AKSValue));
      s->fieldnames[s->fieldcount - 1] = strdup(ValToStrPtr(keyval)->data);
      s->fieldvals[s->fieldcount - 1] = val;
    }
    ValFree(&keyval);

    SkipJSONWhitespace(ptr);
    if (**ptr == '}') {
      (*ptr)++;
      break;
    }
    if (**ptr == ',') {
      (*ptr)++;
    } else {
      *Error = 1;
      ValFree(&objval);
      return ValNull();
    }
  }
  return objval;
}

static AKSValue ParseJSONVal(const char **ptr, int *Error) {
  SkipJSONWhitespace(ptr);
  if (!**ptr) {
    *Error = 1;
    return ValNull();
  }
  if (**ptr == '{') {
    return ParseJSONObj(ptr, Error);
  }
  if (**ptr == '[') {
    return ParseJSONArr(ptr, Error);
  }
  if (**ptr == '"') {
    return ParseJSONStr(ptr, Error);
  }
  if (**ptr == '-' || (**ptr >= '0' && **ptr <= '9')) {
    return ParseJSONNumber(ptr, Error);
  }
  if (strncmp(*ptr, "true", 4) == 0) {
    *ptr += 4;
    return ValBool(1);
  }
  if (strncmp(*ptr, "false", 5) == 0) {
    *ptr += 5;
    return ValBool(0);
  }
  if (strncmp(*ptr, "null", 4) == 0) {
    *ptr += 4;
    return ValNull();
  }
  *Error = 1;
  return ValNull();
}

static void StrIfyString(const char *data, int slen, char **buf, size_t *len, size_t *cap) {
  if (*len + 2 >= *cap) {
    *cap = *cap * 2 + 16;
    *buf = realloc(*buf, *cap);
  }
  (*buf)[(*len)++] = '"';
  for (int i = 0; i < slen; i++) {
    char c = data[i];
    if (c == '"' || c == '\\') {
      if (*len + 2 >= *cap) { *cap = *cap * 2 + 16; *buf = realloc(*buf, *cap); }
      (*buf)[(*len)++] = '\\';
      (*buf)[(*len)++] = c;
    } else if (c == '\n') {
      if (*len + 2 >= *cap) { *cap = *cap * 2 + 16; *buf = realloc(*buf, *cap); }
      (*buf)[(*len)++] = '\\';
      (*buf)[(*len)++] = 'n';
    } else if (c == '\t') {
      if (*len + 2 >= *cap) { *cap = *cap * 2 + 16; *buf = realloc(*buf, *cap); }
      (*buf)[(*len)++] = '\\';
      (*buf)[(*len)++] = 't';
    } else if (c == '\r') {
      if (*len + 2 >= *cap) { *cap = *cap * 2 + 16; *buf = realloc(*buf, *cap); }
      (*buf)[(*len)++] = '\\';
      (*buf)[(*len)++] = 'r';
    } else if (c == '\b') {
      if (*len + 2 >= *cap) { *cap = *cap * 2 + 16; *buf = realloc(*buf, *cap); }
      (*buf)[(*len)++] = '\\';
      (*buf)[(*len)++] = 'b';
    } else if (c == '\f') {
      if (*len + 2 >= *cap) { *cap = *cap * 2 + 16; *buf = realloc(*buf, *cap); }
      (*buf)[(*len)++] = '\\';
      (*buf)[(*len)++] = 'f';
    } else if ((unsigned char)c < 32) {
      if (*len + 6 >= *cap) { *cap = *cap * 2 + 16; *buf = realloc(*buf, *cap); }
      snprintf(*buf + *len, 7, "\\u%04x", (unsigned char)c);
      *len += 6;
    } else {
      if (*len + 1 >= *cap) { *cap = *cap * 2 + 16; *buf = realloc(*buf, *cap); }
      (*buf)[(*len)++] = c;
    }
  }
  if (*len + 1 >= *cap) {
    *cap = *cap * 2 + 16;
    *buf = realloc(*buf, *cap);
  }
  (*buf)[(*len)++] = '"';
}

static void StrIfyVal(AKSValue v, char **buf, size_t *len, size_t *cap) {
  char temp[64];
  if (ValKind(v) == VALNULL) {
    const char *s = "null";
    size_t slen = strlen(s);
    if (*len + slen >= *cap) {
      *cap = *cap * 2 + slen;
      *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
  } else if (ValKind(v) == VALBOOL) {
    const char *s = ValToBool(v) ? "true" : "false";
    size_t slen = strlen(s);
    if (*len + slen >= *cap) {
      *cap = *cap * 2 + slen;
      *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
  } else if (ValKind(v) == VALINT) {
    snprintf(temp, sizeof(temp), "%lld", (long long)ValToInt(v));
    size_t slen = strlen(temp);
    if (*len + slen >= *cap) {
      *cap = *cap * 2 + slen;
      *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, temp, slen);
    *len += slen;
  } else if (ValKind(v) == VALFLOAT) {
    snprintf(temp, sizeof(temp), "%g", ValToFloat(v));
    size_t slen = strlen(temp);
    if (*len + slen >= *cap) {
      *cap = *cap * 2 + slen;
      *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, temp, slen);
    *len += slen;
  } else if (ValKind(v) == VALSTR) {
    StrIfyString(ValToStrPtr(v)->data, ValToStrPtr(v)->len, buf, len, cap);
  } else if (ValKind(v) == VALARR) {
    if (*len + 1 >= *cap) {
      *cap *= 2;
      *buf = realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = '[';
    for (int i = 0; i < ValToArr(v)->count; i++) {
      if (i > 0) {
        if (*len + 1 >= *cap) {
          *cap *= 2;
          *buf = realloc(*buf, *cap);
        }
        (*buf)[(*len)++] = ',';
      }
      StrIfyVal(ValToArr(v)->items[i], buf, len, cap);
    }
    if (*len + 1 >= *cap) {
      *cap *= 2;
      *buf = realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = ']';
  } else if (ValKind(v) == VALTUPLE) {
    if (*len + 1 >= *cap) {
      *cap *= 2;
      *buf = realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = '(';
    for (int i = 0; i < ValToArr(v)->count; i++) {
      if (i > 0) {
        if (*len + 1 >= *cap) {
          *cap *= 2;
          *buf = realloc(*buf, *cap);
        }
        (*buf)[(*len)++] = ',';
      }
      StrIfyVal(ValToArr(v)->items[i], buf, len, cap);
    }
    if (ValToArr(v)->count == 1) {
      if (*len + 1 >= *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
      }
      (*buf)[(*len)++] = ',';
    }
    if (*len + 1 >= *cap) {
      *cap *= 2;
      *buf = realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = ')';
  } else if (ValKind(v) == VALSTRUCT) {
    if (*len + 1 >= *cap) {
      *cap *= 2;
      *buf = realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = '{';
    for (int i = 0; i < ValToObj(v)->fieldcount; i++) {
      if (i > 0) {
        if (*len + 1 >= *cap) {
          *cap *= 2;
          *buf = realloc(*buf, *cap);
        }
        (*buf)[(*len)++] = ',';
      }
      const char *fn = ValToObj(v)->fieldnames[i];
      StrIfyString(fn, (int)strlen(fn), buf, len, cap);
      if (*len + 1 >= *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
      }
      (*buf)[(*len)++] = ':';
      StrIfyVal(ValToObj(v)->fieldvals[i], buf, len, cap);
    }
    if (*len + 1 >= *cap) {
      *cap *= 2;
      *buf = realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = '}';
  } else {
    const char *s = "\"[object]\"";
    size_t slen = strlen(s);
    if (*len + slen >= *cap) {
      *cap = *cap * 2 + slen;
      *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
  }
}

static int IsPathSafe(VM *vm, const char *path) {
  if (!vm->sandboxenabled)
    return 1;
  if (!path)
    return 0;
  if (strstr(path, ".."))
    return 0;
  if (path[0] == '/' || path[0] == '\\')
    return 0;
  if (isalpha((unsigned char)path[0]) && path[1] == ':')
    return 0;
  return 1;
}

static int ValidateCap(VM *vm, AKSValue cap, const char *expectedres,
                       const char *expectedperm, const char *path) {
  if (ValKind(cap) == VALNULL) {
    return !vm->capmode;
  }
  if (ValKind(cap) != VALSTRUCT || !ValToObj(cap) ||
      strcmp(ValToObj(cap)->typename, "Capability") != 0) {
    return 0;
  }
  AKSValue resval = ValNull();
  AKSValue permval = ValNull();
  AKSValue pathval = ValNull();
  for (int i = 0; i < ValToObj(cap)->fieldcount; i++) {
    if (strcmp(ValToObj(cap)->fieldnames[i], "resource") == 0)
      resval = ValToObj(cap)->fieldvals[i];
    else if (strcmp(ValToObj(cap)->fieldnames[i], "permission") == 0)
      permval = ValToObj(cap)->fieldvals[i];
    else if (strcmp(ValToObj(cap)->fieldnames[i], "path") == 0)
      pathval = ValToObj(cap)->fieldvals[i];
  }
  if (ValKind(resval) != VALSTR || ValKind(permval) != VALSTR) {
    return 0;
  }
  if (strcmp(ValToStrPtr(resval)->data, expectedres) != 0) {
    return 0;
  }
  if (strcmp(ValToStrPtr(permval)->data, expectedperm) != 0) {
    return 0;
  }
  if (path && ValKind(pathval) == VALSTR &&
      strlen(ValToStrPtr(pathval)->data) > 0) {
    if (strncmp(path, ValToStrPtr(pathval)->data,
                strlen(ValToStrPtr(pathval)->data)) != 0) {
      return 0;
    }
  }
  return 1;
}

static AKSValue StdlibPop(AKSValue **stacktopptr) {
  (*stacktopptr)--;
  AKSValue val = **stacktopptr;
  **stacktopptr = ValNull();
  return val;
}

static void TaskCancel(VM *vm, VMTask *t) {
  MutexLockNative(vm->globallock);
  t->cancelled = 1;
  t->was_cancelled = 1;
  if (t->state == 2) { // Blocked
    for (int i = 0; i < vm->timercount; i++) {
      if (vm->timers[i].task == t) {
        vm->timers[i].active = 0;
      }
    }
    if (t->blockedontype == 1) { // Channel
      VMChannel *c = (VMChannel *)t->blockedon;
      if (c) {
        MutexLockNative(c->lock);
        for (int i = 0; i < c->readercount; i++) {
          if (c->blockedreaders[i] == t) {
            for (int j = i; j < c->readercount - 1; j++) {
              c->blockedreaders[j] = c->blockedreaders[j + 1];
            }
            c->readercount--;
            break;
          }
        }
        for (int i = 0; i < c->writercount; i++) {
          if (c->blockedwriters[i] == t) {
            for (int j = i; j < c->writercount - 1; j++) {
              c->blockedwriters[j] = c->blockedwriters[j + 1];
            }
            c->writercount--;
            break;
          }
        }
        MutexUnlockNative(c->lock);
      }
    } else if (t->blockedontype == 2) { // Mutex
      VMMutex *m = (VMMutex *)t->blockedon;
      if (m) {
        MutexLockNative(m->lock);
        for (int i = 0; i < m->blockedcount; i++) {
          if (m->blockedtasks[i] == t) {
            for (int j = i; j < m->blockedcount - 1; j++) {
              m->blockedtasks[j] = m->blockedtasks[j + 1];
            }
            m->blockedcount--;
            break;
          }
        }
        MutexUnlockNative(m->lock);
      }
    } else if (t->blockedontype == 3) { // Semaphore
      VMSemaphore *s = (VMSemaphore *)t->blockedon;
      if (s) {
        MutexLockNative(s->lock);
        for (int i = 0; i < s->blockedcount; i++) {
          if (s->blockedtasks[i] == t) {
            for (int j = i; j < s->blockedcount - 1; j++) {
              s->blockedtasks[j] = s->blockedtasks[j + 1];
            }
            s->blockedcount--;
            break;
          }
        }
        MutexUnlockNative(s->lock);
      }
    } else if (t->blockedontype == 4) { // Task await
      VMTask *target = (VMTask *)t->blockedon;
      if (target) {
        for (int i = 0; i < target->awaitercount; i++) {
          if (target->awaiters[i] == t) {
            for (int j = i; j < target->awaitercount - 1; j++) {
              target->awaiters[j] = target->awaiters[j + 1];
            }
            target->awaitercount--;
            break;
          }
        }
      }
    } else if (t->blockedontype == 5) { // Select
      t->is_in_select = 0;
      for (int k = 0; k < t->select_op_count; k++) {
        VMChannel *c = t->select_ops[k].chan;
        if (c) {
          MutexLockNative(c->lock);
          for (int i = 0; i < c->readercount; i++) {
            if (c->blockedreaders[i] == t) {
              for (int j = i; j < c->readercount - 1; j++) {
                c->blockedreaders[j] = c->blockedreaders[j + 1];
              }
              c->readercount--;
              break;
            }
          }
          for (int i = 0; i < c->writercount; i++) {
            if (c->blockedwriters[i] == t) {
              for (int j = i; j < c->writercount - 1; j++) {
                c->blockedwriters[j] = c->blockedwriters[j + 1];
              }
              c->writercount--;
              break;
            }
          }
          MutexUnlockNative(c->lock);
        }
      }
    }

    t->state = 0; // Ready
    int targetworker = rand() % vm->workercount;
    WorkerPushTail(&vm->workers[targetworker], t);
  }
  MutexUnlockNative(vm->globallock);
}

static const uint32_t huff_codes[257] = {
    0x3fffffc,  0x7fffffe,  0x7ffffff,  0xffffff8,  0xffffff9,  0xffffffa,
    0xffffffb,  0xffffffc,  0xffffffd,  0xffffffe,  0x7fffffc,  0xffffff0,
    0xffffff1,  0xffffff2,  0xffffff3,  0xffffff4,  0xffffff5,  0xffffff6,
    0xffffff7,  0x1ffffff0, 0x1ffffff1, 0x1ffffff2, 0x1ffffff3, 0x1ffffff4,
    0x1ffffff5, 0x1ffffff6, 0x1ffffff7, 0x1ffffff8, 0x1ffffff9, 0x1ffffffa,
    0x1ffffffb, 0x1ffffffc, 0x14,       0x3ffd,     0x3ffe,     0x7fffc,
    0x7fffd,    0xffe,      0x3fffe,    0x7fffe,    0x1fffc,    0x1fffd,
    0x3fffa,    0x7fff4,    0x3fffb,    0x15,       0x18,       0x19,
    0x1a,       0x1b,       0x1c,       0x1d,       0x1e,       0x1f,
    0x20,       0x21,       0x22,       0x23,       0x16,       0x3fffc,
    0x7fff5,    0x7fff6,    0x7fff7,    0x7fff8,    0x7fff9,    0x7fff0,
    0x7fff1,    0x7fff2,    0x7fff3,    0x3fff0,    0x3fff1,    0x3fff2,
    0x3fff3,    0x3fff4,    0x3fff5,    0x3fff6,    0x3fff7,    0x3fff8,
    0x3fff9,    0x3fff0,    0x7fffa,    0x7fffb,    0x7fffc,    0x7fffd,
    0x7fffe,    0x7ffff0,   0x7ffff1,   0x7ffff2,   0x7ffff3,   0x7ffff4,
    0x7ffff5,   0x7ffffa,   0x1fffe,    0x7ffffb,   0x7ffffc,   0x17,
    0x7ffffd,   0x0,        0x1,        0x2,        0x3,        0x4,
    0x5,        0x6,        0x7,        0x8,        0x9,        0xa,
    0xb,        0xc,        0xd,        0xe,        0xf,        0x10,
    0x11,       0x12,       0x13,       0x1fffa,    0x1fffb,    0x3fff6,
    0x3fff7,    0x3fff8,    0x7fffe,    0x7ffff6,   0x7ffff7,   0x7ffff8,
    0x7ffff9,   0xffffff10, 0xffffff11, 0xffffff12, 0xffffff13, 0xffffff14,
    0xffffff15, 0xffffff16, 0xffffff17, 0xffffff18, 0xffffff19, 0xffffff1a,
    0xffffff1b, 0xffffff1c, 0xffffff1d, 0xffffff1e, 0xffffff1f, 0xffffff20,
    0xffffff21, 0xffffff22, 0xffffff23, 0xffffff24, 0xffffff25, 0xffffff26,
    0xffffff27, 0xffffff28, 0xffffff29, 0xffffff2a, 0xffffff2b, 0xffffff2c,
    0xffffff2d, 0xffffff2e, 0xffffff2f, 0x3ffffff0, 0x3ffffff1, 0x3ffffff2,
    0x3ffffff3, 0x3ffffff4, 0x3ffffff5, 0x3ffffff6, 0x3ffffff7, 0x3ffffff8,
    0x3ffffff9, 0x3ffffffa, 0x3ffffffb, 0x3ffffffc, 0x3ffffffd, 0x3ffffffe,
    0x7ffffff0, 0x7ffffff1, 0x7ffffff2, 0x7ffffff3, 0x7ffffff4, 0x7ffffff5,
    0x7ffffff6, 0x7ffffff7, 0x7ffffff8, 0x7ffffff9, 0x7ffffffa, 0x7ffffffb,
    0x7ffffffc, 0x7ffffffd, 0x7ffffffe, 0xffffffa0, 0xffffffa1, 0xffffffa2,
    0xffffffa3, 0xffffffa4, 0xffffffa5, 0xffffffa6, 0xffffffa7, 0xffffffa8,
    0xffffffa9, 0xffffffaa, 0xffffffab, 0xffffffac, 0xffffffad, 0xffffffae,
    0xffffffaf, 0xffffffb0, 0xffffffb1, 0xffffffb2, 0xffffffb3, 0xffffffb4,
    0xffffffb5, 0xffffffb6, 0xffffffb7, 0xffffffb8, 0xffffffb9, 0xffffffba,
    0xffffffbb, 0xffffffbc, 0xffffffbd, 0xffffffbe, 0xffffffbf, 0xffffffc0,
    0xffffffc1, 0xffffffc2, 0xffffffc3, 0xffffffc4, 0xffffffc5, 0xffffffc6,
    0xffffffc7, 0xffffffc8, 0xffffffc9, 0xffffffca, 0xffffffcb, 0xffffffcc,
    0xffffffcd, 0xffffffce, 0xffffffcf, 0xffffffd0, 0xffffffd1, 0xffffffd2,
    0xffffffd3, 0xffffffd4, 0xffffffd5, 0xffffffd6, 0xffffffd7, 0xffffffd8,
    0xffffffd9, 0xffffffda, 0xffffffdb, 0xffffffdc, 0xffffffdd, 0xffffffde,
    0xffffffdf, 0xffffffe0, 0xffffffe1, 0x3fffffd};

static const uint8_t huff_lens[257] = {
    26, 27, 27, 28, 28, 28, 28, 28, 28, 28, 27, 28, 28, 28, 28, 28, 28, 28, 28,
    29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 6,  14, 14, 19, 19, 12,
    18, 19, 17, 17, 18, 19, 18, 5,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,
    6,  6,  18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 17, 19, 19,
    5,  19, 6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  6,  17, 17, 18, 18, 18, 19, 19, 19, 19, 19, 32, 32, 32, 32, 32, 32,
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
    32, 32, 32, 32, 32, 32, 32, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
    30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 32,
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
    32, 32, 32, 32, 32, 32, 32, 32, 26};

struct HuffNode {
  int symbol;
  struct HuffNode *children[2];
};

static void BuildHuffmanTree(void) {
  if (huff_tree_root)
    return;
  huff_tree_root = calloc(1, sizeof(HuffNode));
  huff_tree_root->symbol = -1;
  for (int i = 0; i < 257; i++) {
    uint32_t code = huff_codes[i];
    int len = huff_lens[i];
    HuffNode *curr = huff_tree_root;
    for (int j = len - 1; j >= 0; j--) {
      int bit = (code >> j) & 1;
      if (!curr->children[bit]) {
        curr->children[bit] = calloc(1, sizeof(HuffNode));
        curr->children[bit]->symbol = -1;
      }
      curr = curr->children[bit];
    }
    curr->symbol = i;
  }
}

static void FreeHuffmanTree(HuffNode *node) {
  if (!node)
    return;
  FreeHuffmanTree(node->children[0]);
  FreeHuffmanTree(node->children[1]);
  free(node);
}

static char *HuffmanDecodeStr(const char *in_data, int in_len, int *out_len) {
  BuildHuffmanTree();
  int max_out = in_len * 2 + 16;
  char *out = malloc(max_out);
  int o_idx = 0;
  HuffNode *curr = huff_tree_root;
  for (int i = 0; i < in_len; i++) {
    uint8_t b = (uint8_t)in_data[i];
    for (int bit_idx = 7; bit_idx >= 0; bit_idx--) {
      int bit = (b >> bit_idx) & 1;
      curr = curr->children[bit];
      if (!curr) {
        free(out);
        return NULL;
      }
      if (curr->symbol >= 0) {
        if (curr->symbol == 256) {
          break;
        }
        if (o_idx >= max_out - 1) {
          max_out *= 2;
          out = realloc(out, max_out);
        }
        out[o_idx++] = (char)curr->symbol;
        curr = huff_tree_root;
      }
    }
  }
  out[o_idx] = '\0';
  *out_len = o_idx;
  return out;
}

#ifdef _WIN32
#define SECURITY_WIN32
#include <schnlsp.h>
#include <security.h>
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

typedef struct {
  SOCKET sock;
  CredHandle cred;
  CtxtHandle ctx;
  SecPkgContext_StreamSizes sizes;
  int has_context;
  int is_server;
  int in_use;
  char *in_buf;
  int in_buf_len;
  int in_buf_cap;
  char *dec_buf;
  int dec_buf_len;
  int dec_buf_cap;
  int dec_buf_offset;
} TLSContext;

#define MAX_TLS_CONTEXTS 128
static TLSContext tls_contexts[MAX_TLS_CONTEXTS];

static int CreateTLSContext(SOCKET sock, int is_server) {
  int id = -1;
  for (int i = 0; i < MAX_TLS_CONTEXTS; i++) {
    if (!tls_contexts[i].in_use) {
      id = i;
      break;
    }
  }
  if (id < 0) return -1;
  TLSContext *c = &tls_contexts[id];
  memset(c, 0, sizeof(TLSContext));
  c->in_use = 1;
  c->sock = sock;
  c->has_context = 0;
  c->is_server = is_server;
  c->in_buf_cap = 65536;
  c->in_buf = malloc(c->in_buf_cap);
  c->in_buf_len = 0;
  c->dec_buf_cap = 65536;
  c->dec_buf = malloc(c->dec_buf_cap);
  c->dec_buf_len = 0;
  c->dec_buf_offset = 0;
  return id;
}

static void TLSCloseContext(int tls_id) {
  if (tls_id < 0 || tls_id >= MAX_TLS_CONTEXTS || !tls_contexts[tls_id].in_use)
    return;
  TLSContext *c = &tls_contexts[tls_id];
  if (c->has_context) {
    DeleteSecurityContext(&c->ctx);
  }
  FreeCredentialsHandle(&c->cred);
  if (c->in_buf)
    free(c->in_buf);
  if (c->dec_buf)
    free(c->dec_buf);
  closesocket(c->sock);
  memset(c, 0, sizeof(TLSContext));
  c->in_use = 0;
}

static int PerformTLSHandshake(int tls_id, const char *hostname) {
  if (tls_id < 0 || tls_id >= MAX_TLS_CONTEXTS || !tls_contexts[tls_id].in_use)
    return -1;
  TLSContext *c = &tls_contexts[tls_id];

  SCHANNEL_CRED schannel_cred;
  memset(&schannel_cred, 0, sizeof(schannel_cred));
  schannel_cred.dwVersion = SCHANNEL_CRED_VERSION;
  schannel_cred.grbitEnabledProtocols = SP_PROT_TLS1_2 | SP_PROT_TLS1_3;
  if (c->is_server) {
    schannel_cred.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER;
  } else {
    schannel_cred.dwFlags =
        SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;
  }

  SECURITY_STATUS status = AcquireCredentialsHandleA(
      NULL, UNISP_NAME,
      c->is_server ? SECPKG_CRED_INBOUND : SECPKG_CRED_OUTBOUND, NULL,
      &schannel_cred, NULL, NULL, &c->cred, NULL);
  if (status != SEC_E_OK)
    return -1;

  DWORD dwSSPIFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                      ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR |
                      ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
  if (c->is_server) {
    dwSSPIFlags = ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT |
                  ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR |
                  ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM;
  }

  SecBufferDesc out_buf_desc;
  SecBuffer out_bufs[1];
  SecBufferDesc in_buf_desc;
  SecBuffer in_bufs[2];

  int initial = 1;

  while (1) {
    out_bufs[0].pvBuffer = NULL;
    out_bufs[0].cbBuffer = 0;
    out_bufs[0].BufferType = SECBUFFER_TOKEN;
    out_buf_desc.ulVersion = SECBUFFER_VERSION;
    out_buf_desc.cBuffers = 1;
    out_buf_desc.pBuffers = out_bufs;

    in_bufs[0].pvBuffer = c->in_buf;
    in_bufs[0].cbBuffer = c->in_buf_len;
    in_bufs[0].BufferType = SECBUFFER_TOKEN;
    in_bufs[1].pvBuffer = NULL;
    in_bufs[1].cbBuffer = 0;
    in_bufs[1].BufferType = SECBUFFER_EMPTY;

    in_buf_desc.ulVersion = SECBUFFER_VERSION;
    in_buf_desc.cBuffers = 2;
    in_buf_desc.pBuffers = in_bufs;

    DWORD dwSSPIOutFlags = 0;
    TimeStamp tsExpiry;

    if (c->is_server) {
      status = AcceptSecurityContext(
          &c->cred, initial ? NULL : &c->ctx, &in_buf_desc, dwSSPIFlags,
          SECURITY_NATIVE_DREP, initial ? &c->ctx : NULL, &out_buf_desc,
          &dwSSPIOutFlags, &tsExpiry);
    } else {
      status = InitializeSecurityContextA(
          &c->cred, initial ? NULL : &c->ctx, (SEC_CHAR *)hostname, dwSSPIFlags,
          0, SECURITY_NATIVE_DREP, initial ? NULL : &in_buf_desc, 0,
          initial ? &c->ctx : NULL, &out_buf_desc, &dwSSPIOutFlags, &tsExpiry);
    }

    initial = 0;
    c->has_context = 1;

    if (out_bufs[0].cbBuffer > 0 && out_bufs[0].pvBuffer != NULL) {
      send(c->sock, out_bufs[0].pvBuffer, out_bufs[0].cbBuffer, 0);
      FreeContextBuffer(out_bufs[0].pvBuffer);
    }

    if (status == SEC_E_OK) {
      QueryContextAttributesA(&c->ctx, SECPKG_ATTR_STREAM_SIZES, &c->sizes);
      return 0;
    }

    if (status == SEC_I_CONTINUE_NEEDED) {
      if (in_bufs[1].BufferType == SECBUFFER_EXTRA) {
        int extra = in_bufs[1].cbBuffer;
        memmove(c->in_buf, c->in_buf + (c->in_buf_len - extra), extra);
        c->in_buf_len = extra;
      } else {
        c->in_buf_len = 0;
      }

      int n = recv(c->sock, c->in_buf + c->in_buf_len,
                   c->in_buf_cap - c->in_buf_len, 0);
      if (n <= 0)
        return -1;
      c->in_buf_len += n;
      continue;
    }

    if (status == SEC_E_INCOMPLETE_MESSAGE) {
      int n = recv(c->sock, c->in_buf + c->in_buf_len,
                   c->in_buf_cap - c->in_buf_len, 0);
      if (n <= 0)
        return -1;
      c->in_buf_len += n;
      continue;
    }

    return -1;
  }
}

static int TLSSendData(int tls_id, const char *data, int len) {
  if (tls_id < 0 || tls_id >= MAX_TLS_CONTEXTS || !tls_contexts[tls_id].in_use)
    return -1;
  TLSContext *c = &tls_contexts[tls_id];
  int max_payload = c->sizes.cbMaximumMessage;
  int offset = 0;

  while (offset < len) {
    int chunk = len - offset;
    if (chunk > max_payload)
      chunk = max_payload;

    int total_buf_size = c->sizes.cbHeader + chunk + c->sizes.cbTrailer;
    char *send_buf = malloc(total_buf_size);

    memcpy(send_buf + c->sizes.cbHeader, data + offset, chunk);

    SecBuffer buffers[4];
    buffers[0].pvBuffer = send_buf;
    buffers[0].cbBuffer = c->sizes.cbHeader;
    buffers[0].BufferType = SECBUFFER_STREAM_HEADER;

    buffers[1].pvBuffer = send_buf + c->sizes.cbHeader;
    buffers[1].cbBuffer = chunk;
    buffers[1].BufferType = SECBUFFER_DATA;

    buffers[2].pvBuffer = send_buf + c->sizes.cbHeader + chunk;
    buffers[2].cbBuffer = c->sizes.cbTrailer;
    buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;

    buffers[3].pvBuffer = NULL;
    buffers[3].cbBuffer = 0;
    buffers[3].BufferType = SECBUFFER_EMPTY;

    SecBufferDesc desc;
    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers = 4;
    desc.pBuffers = buffers;

    SECURITY_STATUS status = EncryptMessage(&c->ctx, 0, &desc, 0);
    if (status != SEC_E_OK) {
      free(send_buf);
      return -1;
    }

    int enc_len =
        buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
    int sent = send(c->sock, send_buf, enc_len, 0);
    free(send_buf);
    if (sent <= 0)
      return -1;

    offset += chunk;
  }
  return len;
}

static int TLSRecvData(int tls_id, char *out_buf, int max_len) {
  if (tls_id < 0 || tls_id >= MAX_TLS_CONTEXTS || !tls_contexts[tls_id].in_use)
    return -1;
  TLSContext *c = &tls_contexts[tls_id];

  if (c->dec_buf_len > 0) {
    int to_copy = c->dec_buf_len;
    if (to_copy > max_len)
      to_copy = max_len;
    memcpy(out_buf, c->dec_buf + c->dec_buf_offset, to_copy);
    c->dec_buf_len -= to_copy;
    c->dec_buf_offset += to_copy;
    return to_copy;
  }

  c->dec_buf_offset = 0;
  c->dec_buf_len = 0;

  while (1) {
    if (c->in_buf_len > 0) {
      SecBuffer buffers[4];
      buffers[0].pvBuffer = c->in_buf;
      buffers[0].cbBuffer = c->in_buf_len;
      buffers[0].BufferType = SECBUFFER_DATA;

      buffers[1].pvBuffer = NULL;
      buffers[1].cbBuffer = 0;
      buffers[1].BufferType = SECBUFFER_EMPTY;
      buffers[2].pvBuffer = NULL;
      buffers[2].cbBuffer = 0;
      buffers[2].BufferType = SECBUFFER_EMPTY;
      buffers[3].pvBuffer = NULL;
      buffers[3].cbBuffer = 0;
      buffers[3].BufferType = SECBUFFER_EMPTY;

      SecBufferDesc desc;
      desc.ulVersion = SECBUFFER_VERSION;
      desc.cBuffers = 4;
      desc.pBuffers = buffers;

      SECURITY_STATUS status = DecryptMessage(&c->ctx, &desc, 0, NULL);

      if (status == SEC_E_OK) {
        SecBuffer *data_buf = NULL;
        SecBuffer *extra_buf = NULL;
        for (int i = 0; i < 4; i++) {
          if (buffers[i].BufferType == SECBUFFER_DATA) {
            data_buf = &buffers[i];
          }
          if (buffers[i].BufferType == SECBUFFER_EXTRA) {
            extra_buf = &buffers[i];
          }
        }

        if (data_buf) {
          if (data_buf->cbBuffer > (DWORD)c->dec_buf_cap) {
            c->dec_buf_cap = (int)(data_buf->cbBuffer + 1024);
            c->dec_buf = realloc(c->dec_buf, (size_t)c->dec_buf_cap);
          }
          memcpy(c->dec_buf, data_buf->pvBuffer, data_buf->cbBuffer);
          c->dec_buf_len = data_buf->cbBuffer;
          c->dec_buf_offset = 0;
        }

        if (extra_buf) {
          int extra = extra_buf->cbBuffer;
          memmove(c->in_buf, extra_buf->pvBuffer, extra);
          c->in_buf_len = extra;
        } else {
          c->in_buf_len = 0;
        }

        if (c->dec_buf_len > 0) {
          int to_copy = c->dec_buf_len;
          if (to_copy > max_len)
            to_copy = max_len;
          memcpy(out_buf, c->dec_buf + c->dec_buf_offset, to_copy);
          c->dec_buf_len -= to_copy;
          c->dec_buf_offset += to_copy;
          return to_copy;
        }
        continue;
      }

      if (status == SEC_E_INCOMPLETE_MESSAGE) {
        int n = recv(c->sock, c->in_buf + c->in_buf_len,
                     c->in_buf_cap - c->in_buf_len, 0);
        if (n <= 0)
          return n;
        c->in_buf_len += n;
        continue;
      }

      if (status == SEC_I_CONTEXT_EXPIRED) {
        return 0;
      }

      return -1;
    } else {
      int n = recv(c->sock, c->in_buf, c->in_buf_cap, 0);
      if (n <= 0)
        return n;
      c->in_buf_len = n;
    }
  }
}
#else
typedef int SOCKET;
static int CreateTLSContext(SOCKET sock, int is_server) { return -1; }
static int PerformTLSHandshake(int tls_id, const char *hostname) { return -1; }
static int TLSSendData(int tls_id, const char *data, int len) { return -1; }
static int TLSRecvData(int tls_id, char *out_buf, int max_len) { return -1; }
static void TLSCloseContext(int tls_id) {}
#endif

typedef enum {
  ORDER_SEQ_CST,
  ORDER_ACQUIRE,
  ORDER_RELEASE,
  ORDER_RELAXED
} MemOrder;

static MemOrder ParseMemOrder(AKSValue val) {
  if (ValKind(val) == VALSTR && ValToStrPtr(val)->data) {
    const char *s = ValToStrPtr(val)->data;
    if (strcmp(s, "acquire") == 0)
      return ORDER_ACQUIRE;
    if (strcmp(s, "release") == 0)
      return ORDER_RELEASE;
    if (strcmp(s, "relaxed") == 0)
      return ORDER_RELAXED;
  }
  return ORDER_SEQ_CST;
}

// Serialization formats helpers CBOR(, messagepack, CSV, TOML, XML, protobuf)

static void CBOREncodeVal(AKSValue v, char **buf, size_t *len, size_t *cap) {
#define APPEND_BYTE(b)                                                         \
  do {                                                                         \
    if (*len >= *cap) {                                                        \
      *cap = *cap == 0 ? 256 : *cap * 2;                                       \
      *buf = realloc(*buf, *cap);                                              \
    }                                                                          \
    (*buf)[(*len)++] = (char)(b);                                              \
  } while (0)
#define APPEND_BYTES(bytes, bytes_len)                                         \
  do {                                                                         \
    for (size_t k = 0; k < (bytes_len); k++)                                   \
      APPEND_BYTE((bytes)[k]);                                                 \
  } while (0)

  ValType t = ValKind(v);
  if (t == VALNULL) {
    APPEND_BYTE(0xF6);
  } else if (t == VALBOOL) {
    APPEND_BYTE(ValToBool(v) ? 0xF5 : 0xF4);
  } else if (t == VALINT) {
    int64_t val = ValToInt(v);
    if (val >= 0) {
      if (val < 24) {
        APPEND_BYTE(val);
      } else if (val < 256) {
        APPEND_BYTE(0x18);
        APPEND_BYTE(val);
      } else if (val < 65536) {
        APPEND_BYTE(0x19);
        APPEND_BYTE((val >> 8) & 255);
        APPEND_BYTE(val & 255);
      } else if (val < 4294967296LL) {
        APPEND_BYTE(0x1a);
        APPEND_BYTE((val >> 24) & 255);
        APPEND_BYTE((val >> 16) & 255);
        APPEND_BYTE((val >> 8) & 255);
        APPEND_BYTE(val & 255);
      } else {
        APPEND_BYTE(0x1b);
        for (int j = 7; j >= 0; j--)
          APPEND_BYTE((val >> (j * 8)) & 255);
      }
    } else {
      int64_t nval = -1 - val;
      if (nval < 24) {
        APPEND_BYTE(0x20 + nval);
      } else if (nval < 256) {
        APPEND_BYTE(0x38);
        APPEND_BYTE(nval);
      } else if (nval < 65536) {
        APPEND_BYTE(0x39);
        APPEND_BYTE((nval >> 8) & 255);
        APPEND_BYTE(nval & 255);
      } else if (nval < 4294967296LL) {
        APPEND_BYTE(0x3a);
        APPEND_BYTE((nval >> 24) & 255);
        APPEND_BYTE((nval >> 16) & 255);
        APPEND_BYTE((nval >> 8) & 255);
        APPEND_BYTE(nval & 255);
      } else {
        APPEND_BYTE(0x3b);
        for (int j = 7; j >= 0; j--)
          APPEND_BYTE((nval >> (j * 8)) & 255);
      }
    }
  } else if (t == VALFLOAT) {
    double val = ValToFloat(v);
    union {
      double d;
      uint8_t b[8];
    } u;
    u.d = val;
    APPEND_BYTE(0xFB);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    APPEND_BYTES(u.b, 8);
#else
    for (int j = 7; j >= 0; j--)
      APPEND_BYTE(u.b[j]);
#endif
  } else if (t == VALSTR) {
    const char *s = ValToStrPtr(v)->data;
    int slen = ValToStrPtr(v)->len;
    if (slen < 24) {
      APPEND_BYTE(0x60 + slen);
    } else if (slen < 256) {
      APPEND_BYTE(0x78);
      APPEND_BYTE(slen);
    } else if (slen < 65536) {
      APPEND_BYTE(0x79);
      APPEND_BYTE((slen >> 8) & 255);
      APPEND_BYTE(slen & 255);
    } else {
      APPEND_BYTE(0x7a);
      APPEND_BYTE((slen >> 24) & 255);
      APPEND_BYTE((slen >> 16) & 255);
      APPEND_BYTE((slen >> 8) & 255);
      APPEND_BYTE(slen & 255);
    }
    APPEND_BYTES(s, slen);
  } else if (t == VALARR || t == VALTUPLE) {
    AKSArray *arr = ValToArr(v);
    int count = arr->count;
    if (count < 24) {
      APPEND_BYTE(0x80 + count);
    } else if (count < 256) {
      APPEND_BYTE(0x98);
      APPEND_BYTE(count);
    } else if (count < 65536) {
      APPEND_BYTE(0x99);
      APPEND_BYTE((count >> 8) & 255);
      APPEND_BYTE(count & 255);
    } else {
      APPEND_BYTE(0x9a);
      APPEND_BYTE((count >> 24) & 255);
      APPEND_BYTE((count >> 16) & 255);
      APPEND_BYTE((count >> 8) & 255);
      APPEND_BYTE(count & 255);
    }
    for (int j = 0; j < count; j++) {
      CBOREncodeVal(arr->items[j], buf, len, cap);
    }
  } else if (t == VALSTRUCT) {
    AKSStruct *s = ValToObj(v);
    int count = s->fieldcount;
    if (count < 24) {
      APPEND_BYTE(0xA0 + count);
    } else if (count < 256) {
      APPEND_BYTE(0xB8);
      APPEND_BYTE(count);
    } else if (count < 65536) {
      APPEND_BYTE(0xB9);
      APPEND_BYTE((count >> 8) & 255);
      APPEND_BYTE(count & 255);
    } else {
      APPEND_BYTE(0xBa);
      APPEND_BYTE((count >> 24) & 255);
      APPEND_BYTE((count >> 16) & 255);
      APPEND_BYTE((count >> 8) & 255);
      APPEND_BYTE(count & 255);
    }
    for (int j = 0; j < count; j++) {
      AKSValue key = ValStr(s->fieldnames[j]);
      CBOREncodeVal(key, buf, len, cap);
      ValFree(&key);
      CBOREncodeVal(s->fieldvals[j], buf, len, cap);
    }
  } else {
    APPEND_BYTE(0xF6);
  }
#undef APPEND_BYTE
#undef APPEND_BYTES
}

static AKSValue CBORDecodeVal(const uint8_t **ptr, const uint8_t *end,
                              int *Error) {
  if (*ptr >= end) {
    *Error = 1;
    return ValNull();
  }
  uint8_t b = **ptr;
  (*ptr)++;

  uint8_t major = b >> 5;
  uint8_t info = b & 0x1F;

#define READ_UINT(sz, var)                                                     \
  do {                                                                         \
    if (*ptr + (sz) > end) {                                                   \
      *Error = 1;                                                              \
      return ValNull();                                                        \
    }                                                                          \
    (var) = 0;                                                                 \
    for (int k = 0; k < (sz); k++) {                                           \
      (var) = ((var) << 8) | (*ptr)[k];                                        \
    }                                                                          \
    *ptr += (sz);                                                              \
  } while (0)

  if (major == 0) {
    uint64_t val = info;
    if (info == 24)
      READ_UINT(1, val);
    else if (info == 25)
      READ_UINT(2, val);
    else if (info == 26)
      READ_UINT(4, val);
    else if (info == 27)
      READ_UINT(8, val);
    else if (info > 27) {
      *Error = 1;
      return ValNull();
    }
    return ValInt((int64_t)val);
  } else if (major == 1) {
    uint64_t val = info;
    if (info == 24)
      READ_UINT(1, val);
    else if (info == 25)
      READ_UINT(2, val);
    else if (info == 26)
      READ_UINT(4, val);
    else if (info == 27)
      READ_UINT(8, val);
    else if (info > 27) {
      *Error = 1;
      return ValNull();
    }
    return ValInt(-1 - (int64_t)val);
  } else if (major == 2 || major == 3) {
    uint64_t len = info;
    if (info == 24)
      READ_UINT(1, len);
    else if (info == 25)
      READ_UINT(2, len);
    else if (info == 26)
      READ_UINT(4, len);
    else if (info == 27)
      READ_UINT(8, len);
    else if (info > 27) {
      *Error = 1;
      return ValNull();
    }
    if (*ptr + len > end) {
      *Error = 1;
      return ValNull();
    }
    char *str = malloc(len + 1);
    memcpy(str, *ptr, len);
    str[len] = '\0';
    *ptr += len;
    AKSValue res = ValStrLen(str, (int)len);
    free(str);
    return res;
  } else if (major == 4) {
    uint64_t count = info;
    if (info == 24)
      READ_UINT(1, count);
    else if (info == 25)
      READ_UINT(2, count);
    else if (info == 26)
      READ_UINT(4, count);
    else if (info == 27)
      READ_UINT(8, count);
    else if (info > 27) {
      *Error = 1;
      return ValNull();
    }

    AKSValue res = ValArr((int)count);
    ValToArr(res)->count = (int)count;
    for (uint64_t j = 0; j < count; j++) {
      ValToArr(res)->items[j] = CBORDecodeVal(ptr, end, Error);
      if (*Error) {
        ValFree(&res);
        return ValNull();
      }
    }
    return res;
  } else if (major == 5) {
    uint64_t count = info;
    if (info == 24)
      READ_UINT(1, count);
    else if (info == 25)
      READ_UINT(2, count);
    else if (info == 26)
      READ_UINT(4, count);
    else if (info == 27)
      READ_UINT(8, count);
    else if (info > 27) {
      *Error = 1;
      return ValNull();
    }

    AKSValue res = ValStruct("Object");
    AKSStruct *s = ValToObj(res);
    s->fieldcount = (int)count;
    s->fieldnames = malloc(count * sizeof(char *));
    s->fieldvals = malloc(count * sizeof(AKSValue));
    for (uint64_t j = 0; j < count; j++) {
      AKSValue key = CBORDecodeVal(ptr, end, Error);
      if (*Error || ValKind(key) != VALSTR) {
        ValFree(&key);
        ValFree(&res);
        *Error = 1;
        return ValNull();
      }
      s->fieldnames[j] = strdup(ValToStrPtr(key)->data);
      ValFree(&key);
      s->fieldvals[j] = CBORDecodeVal(ptr, end, Error);
      if (*Error) {
        ValFree(&res);
        return ValNull();
      }
    }
    return res;
  } else if (major == 7) {
    if (info == 20)
      return ValBool(0);
    if (info == 21)
      return ValBool(1);
    if (info == 22)
      return ValNull();
    if (info == 27) {
      union {
        double d;
        uint8_t b[8];
      } u;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      for (int k = 0; k < 8; k++) {
        if (*ptr >= end) {
          *Error = 1;
          return ValNull();
        }
        u.b[k] = **ptr;
        (*ptr)++;
      }
#else
      for (int k = 7; k >= 0; k--) {
        if (*ptr >= end) {
          *Error = 1;
          return ValNull();
        }
        u.b[k] = **ptr;
        (*ptr)++;
      }
#endif
      return ValFloat(u.d);
    }
  }
  return ValNull();
#undef READ_UINT
}

static void MsgpackEncodeVal(AKSValue v, char **buf, size_t *len, size_t *cap) {
#define APPEND_BYTE(b)                                                         \
  do {                                                                         \
    if (*len >= *cap) {                                                        \
      *cap = *cap == 0 ? 256 : *cap * 2;                                       \
      *buf = realloc(*buf, *cap);                                              \
    }                                                                          \
    (*buf)[(*len)++] = (char)(b);                                              \
  } while (0)
#define APPEND_BYTES(bytes, bytes_len)                                         \
  do {                                                                         \
    for (size_t k = 0; k < (bytes_len); k++)                                   \
      APPEND_BYTE((bytes)[k]);                                                 \
  } while (0)

  ValType t = ValKind(v);
  if (t == VALNULL) {
    APPEND_BYTE(0xC0);
  } else if (t == VALBOOL) {
    APPEND_BYTE(ValToBool(v) ? 0xC3 : 0xC2);
  } else if (t == VALINT) {
    int64_t val = ValToInt(v);
    if (val >= 0) {
      if (val < 128) {
        APPEND_BYTE(val);
      } else if (val < 256) {
        APPEND_BYTE(0xCC);
        APPEND_BYTE(val);
      } else if (val < 65536) {
        APPEND_BYTE(0xCD);
        APPEND_BYTE((val >> 8) & 255);
        APPEND_BYTE(val & 255);
      } else if (val < 4294967296LL) {
        APPEND_BYTE(0xCE);
        APPEND_BYTE((val >> 24) & 255);
        APPEND_BYTE((val >> 16) & 255);
        APPEND_BYTE((val >> 8) & 255);
        APPEND_BYTE(val & 255);
      } else {
        APPEND_BYTE(0xCF);
        for (int j = 7; j >= 0; j--)
          APPEND_BYTE((val >> (j * 8)) & 255);
      }
    } else {
      if (val >= -32) {
        APPEND_BYTE(0xE0 | (val & 0x1F));
      } else if (val >= -128) {
        APPEND_BYTE(0xD0);
        APPEND_BYTE(val & 255);
      } else if (val >= -32768) {
        APPEND_BYTE(0xD1);
        APPEND_BYTE((val >> 8) & 255);
        APPEND_BYTE(val & 255);
      } else if (val >= -2147483648LL) {
        APPEND_BYTE(0xD2);
        APPEND_BYTE((val >> 24) & 255);
        APPEND_BYTE((val >> 16) & 255);
        APPEND_BYTE((val >> 8) & 255);
        APPEND_BYTE(val & 255);
      } else {
        APPEND_BYTE(0xD3);
        for (int j = 7; j >= 0; j--)
          APPEND_BYTE((val >> (j * 8)) & 255);
      }
    }
  } else if (t == VALFLOAT) {
    double val = ValToFloat(v);
    union {
      double d;
      uint8_t b[8];
    } u;
    u.d = val;
    APPEND_BYTE(0xCB);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    APPEND_BYTES(u.b, 8);
#else
    for (int j = 7; j >= 0; j--)
      APPEND_BYTE(u.b[j]);
#endif
  } else if (t == VALSTR) {
    const char *s = ValToStrPtr(v)->data;
    int slen = ValToStrPtr(v)->len;
    if (slen < 32) {
      APPEND_BYTE(0xA0 + slen);
    } else if (slen < 256) {
      APPEND_BYTE(0xD9);
      APPEND_BYTE(slen);
    } else if (slen < 65536) {
      APPEND_BYTE(0xDA);
      APPEND_BYTE((slen >> 8) & 255);
      APPEND_BYTE(slen & 255);
    } else {
      APPEND_BYTE(0xDB);
      APPEND_BYTE((slen >> 24) & 255);
      APPEND_BYTE((slen >> 16) & 255);
      APPEND_BYTE((slen >> 8) & 255);
      APPEND_BYTE(slen & 255);
    }
    APPEND_BYTES(s, slen);
  } else if (t == VALARR || t == VALTUPLE) {
    AKSArray *arr = ValToArr(v);
    int count = arr->count;
    if (count < 16) {
      APPEND_BYTE(0x90 + count);
    } else if (count < 65536) {
      APPEND_BYTE(0xDC);
      APPEND_BYTE((count >> 8) & 255);
      APPEND_BYTE(count & 255);
    } else {
      APPEND_BYTE(0xDD);
      APPEND_BYTE((count >> 24) & 255);
      APPEND_BYTE((count >> 16) & 255);
      APPEND_BYTE((count >> 8) & 255);
      APPEND_BYTE(count & 255);
    }
    for (int j = 0; j < count; j++) {
      MsgpackEncodeVal(arr->items[j], buf, len, cap);
    }
  } else if (t == VALSTRUCT) {
    AKSStruct *s = ValToObj(v);
    int count = s->fieldcount;
    if (count < 16) {
      APPEND_BYTE(0x80 + count);
    } else if (count < 65536) {
      APPEND_BYTE(0xDE);
      APPEND_BYTE((count >> 8) & 255);
      APPEND_BYTE(count & 255);
    } else {
      APPEND_BYTE(0xDF);
      APPEND_BYTE((count >> 24) & 255);
      APPEND_BYTE((count >> 16) & 255);
      APPEND_BYTE((count >> 8) & 255);
      APPEND_BYTE(count & 255);
    }
    for (int j = 0; j < count; j++) {
      AKSValue key = ValStr(s->fieldnames[j]);
      MsgpackEncodeVal(key, buf, len, cap);
      ValFree(&key);
      MsgpackEncodeVal(s->fieldvals[j], buf, len, cap);
    }
  } else {
    APPEND_BYTE(0xC0);
  }
#undef APPEND_BYTE
#undef APPEND_BYTES
}

static AKSValue MsgpackDecodeVal(const uint8_t **ptr, const uint8_t *end,
                                 int *Error) {
  if (*ptr >= end) {
    *Error = 1;
    return ValNull();
  }
  uint8_t b = **ptr;
  (*ptr)++;

#define READ_UINT(sz, var)                                                     \
  do {                                                                         \
    if (*ptr + (sz) > end) {                                                   \
      *Error = 1;                                                              \
      return ValNull();                                                        \
    }                                                                          \
    (var) = 0;                                                                 \
    for (int k = 0; k < (sz); k++) {                                           \
      (var) = ((var) << 8) | (*ptr)[k];                                        \
    }                                                                          \
    *ptr += (sz);                                                              \
  } while (0)

  if (b <= 0x7F) {
    return ValInt(b);
  } else if (b >= 0x80 && b <= 0x8F) {
    int count = b & 0x0F;
    AKSValue res = ValStruct("Object");
    AKSStruct *s = ValToObj(res);
    s->fieldcount = count;
    s->fieldnames = malloc(count * sizeof(char *));
    s->fieldvals = malloc(count * sizeof(AKSValue));
    for (int j = 0; j < count; j++) {
      AKSValue key = MsgpackDecodeVal(ptr, end, Error);
      if (*Error || ValKind(key) != VALSTR) {
        ValFree(&key);
        ValFree(&res);
        *Error = 1;
        return ValNull();
      }
      s->fieldnames[j] = strdup(ValToStrPtr(key)->data);
      ValFree(&key);
      s->fieldvals[j] = MsgpackDecodeVal(ptr, end, Error);
      if (*Error) {
        ValFree(&res);
        return ValNull();
      }
    }
    return res;
  } else if (b >= 0x90 && b <= 0x9F) {
    int count = b & 0x0F;
    AKSValue res = ValArr(count);
    ValToArr(res)->count = count;
    for (int j = 0; j < count; j++) {
      ValToArr(res)->items[j] = MsgpackDecodeVal(ptr, end, Error);
      if (*Error) {
        ValFree(&res);
        return ValNull();
      }
    }
    return res;
  } else if (b >= 0xA0 && b <= 0xBF) {
    int len = b & 0x1F;
    if (*ptr + len > end) {
      *Error = 1;
      return ValNull();
    }
    char *str = malloc(len + 1);
    memcpy(str, *ptr, len);
    str[len] = '\0';
    *ptr += len;
    AKSValue res = ValStrLen(str, len);
    free(str);
    return res;
  } else if (b == 0xC0) {
    return ValNull();
  } else if (b == 0xC2) {
    return ValBool(0);
  } else if (b == 0xC3) {
    return ValBool(1);
  } else if (b == 0xCB) {
    union {
      double d;
      uint8_t b[8];
    } u;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (int k = 0; k < 8; k++) {
      if (*ptr >= end) {
        *Error = 1;
        return ValNull();
      }
      u.b[k] = **ptr;
      (*ptr)++;
    }
#else
    for (int k = 7; k >= 0; k--) {
      if (*ptr >= end) {
        *Error = 1;
        return ValNull();
      }
      u.b[k] = **ptr;
      (*ptr)++;
    }
#endif
    return ValFloat(u.d);
  } else if (b == 0xCC) {
    uint64_t val;
    READ_UINT(1, val);
    return ValInt((int64_t)val);
  } else if (b == 0xCD) {
    uint64_t val;
    READ_UINT(2, val);
    return ValInt((int64_t)val);
  } else if (b == 0xCE) {
    uint64_t val;
    READ_UINT(4, val);
    return ValInt((int64_t)val);
  } else if (b == 0xCF) {
    uint64_t val;
    READ_UINT(8, val);
    return ValInt((int64_t)val);
  } else if (b == 0xD0) {
    int64_t val;
    if (*ptr + 1 > end) {
      *Error = 1;
      return ValNull();
    }
    val = (int8_t)**ptr;
    (*ptr)++;
    return ValInt(val);
  } else if (b == 0xD1) {
    int64_t val;
    if (*ptr + 2 > end) {
      *Error = 1;
      return ValNull();
    }
    val = (int16_t)(((*ptr)[0] << 8) | (*ptr)[1]);
    *ptr += 2;
    return ValInt(val);
  } else if (b == 0xD2) {
    int64_t val;
    if (*ptr + 4 > end) {
      *Error = 1;
      return ValNull();
    }
    val = (int32_t)(((*ptr)[0] << 24) | ((*ptr)[1] << 16) | ((*ptr)[2] << 8) |
                    (*ptr)[3]);
    *ptr += 4;
    return ValInt(val);
  } else if (b == 0xD3) {
    uint64_t val;
    READ_UINT(8, val);
    return ValInt((int64_t)val);
  } else if (b == 0xD9) {
    uint64_t len;
    READ_UINT(1, len);
    if (*ptr + len > end) {
      *Error = 1;
      return ValNull();
    }
    char *str = malloc(len + 1);
    memcpy(str, *ptr, len);
    str[len] = '\0';
    *ptr += len;
    AKSValue res = ValStrLen(str, (int)len);
    free(str);
    return res;
  } else if (b == 0xDA) {
    uint64_t len;
    READ_UINT(2, len);
    if (*ptr + len > end) {
      *Error = 1;
      return ValNull();
    }
    char *str = malloc(len + 1);
    memcpy(str, *ptr, len);
    str[len] = '\0';
    *ptr += len;
    AKSValue res = ValStrLen(str, (int)len);
    free(str);
    return res;
  } else if (b == 0xDB) {
    uint64_t len;
    READ_UINT(4, len);
    if (*ptr + len > end) {
      *Error = 1;
      return ValNull();
    }
    char *str = malloc(len + 1);
    memcpy(str, *ptr, len);
    str[len] = '\0';
    *ptr += len;
    AKSValue res = ValStrLen(str, (int)len);
    free(str);
    return res;
  } else if (b == 0xDC) {
    uint64_t count;
    READ_UINT(2, count);
    AKSValue res = ValArr((int)count);
    ValToArr(res)->count = (int)count;
    for (uint64_t j = 0; j < count; j++) {
      ValToArr(res)->items[j] = MsgpackDecodeVal(ptr, end, Error);
      if (*Error) {
        ValFree(&res);
        return ValNull();
      }
    }
    return res;
  } else if (b == 0xDD) {
    uint64_t count;
    READ_UINT(4, count);
    AKSValue res = ValArr((int)count);
    ValToArr(res)->count = (int)count;
    for (uint64_t j = 0; j < count; j++) {
      ValToArr(res)->items[j] = MsgpackDecodeVal(ptr, end, Error);
      if (*Error) {
        ValFree(&res);
        return ValNull();
      }
    }
    return res;
  } else if (b == 0xDE) {
    uint64_t count;
    READ_UINT(2, count);
    AKSValue res = ValStruct("Object");
    AKSStruct *s = ValToObj(res);
    s->fieldcount = (int)count;
    s->fieldnames = malloc(count * sizeof(char *));
    s->fieldvals = malloc(count * sizeof(AKSValue));
    for (uint64_t j = 0; j < count; j++) {
      AKSValue key = MsgpackDecodeVal(ptr, end, Error);
      if (*Error || ValKind(key) != VALSTR) {
        ValFree(&key);
        ValFree(&res);
        *Error = 1;
        return ValNull();
      }
      s->fieldnames[j] = strdup(ValToStrPtr(key)->data);
      ValFree(&key);
      s->fieldvals[j] = MsgpackDecodeVal(ptr, end, Error);
      if (*Error) {
        ValFree(&res);
        return ValNull();
      }
    }
    return res;
  } else if (b == 0xDF) {
    uint64_t count;
    READ_UINT(4, count);
    AKSValue res = ValStruct("Object");
    AKSStruct *s = ValToObj(res);
    s->fieldcount = (int)count;
    s->fieldnames = malloc(count * sizeof(char *));
    s->fieldvals = malloc(count * sizeof(AKSValue));
    for (uint64_t j = 0; j < count; j++) {
      AKSValue key = MsgpackDecodeVal(ptr, end, Error);
      if (*Error || ValKind(key) != VALSTR) {
        ValFree(&key);
        ValFree(&res);
        *Error = 1;
        return ValNull();
      }
      s->fieldnames[j] = strdup(ValToStrPtr(key)->data);
      ValFree(&key);
      s->fieldvals[j] = MsgpackDecodeVal(ptr, end, Error);
      if (*Error) {
        ValFree(&res);
        return ValNull();
      }
    }
    return res;
  } else if (b >= 0xE0) {
    return ValInt((int8_t)b);
  }
  return ValNull();
#undef READ_UINT
}

static AKSValue ParseCSVStr(const char *csv) {
  AKSValue res = ValArr(0);
  AKSArray *res_arr = ValToArr(res);
  AKSValue headers = ValNull();
  const char *p = csv;
  int len = (int)strlen(csv);
  char *cell = malloc(len + 1);
  int clen = 0;
  AKSValue current_row = ValArr(0);
  int in_quotes = 0;

  for (int i = 0; i <= len; i++) {
    char c = (i == len) ? '\n' : p[i];
    if (i == len && clen == 0 && ValToArr(current_row)->count == 0)
      break;
    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < len && p[i + 1] == '"') {
          cell[clen++] = '"';
          i++;
        } else {
          in_quotes = 0;
        }
      } else {
        cell[clen++] = c;
      }
    } else {
      if (c == '"') {
        in_quotes = 1;
      } else if (c == ',') {
        cell[clen] = '\0';
        AKSValue cv = ValStrLen(cell, clen);
        clen = 0;
        AKSArray *cr = ValToArr(current_row);
        if (cr->count >= cr->capacity) {
          cr->capacity = cr->capacity == 0 ? 4 : cr->capacity * 2;
          cr->items = realloc(cr->items, cr->capacity * sizeof(AKSValue));
        }
        cr->items[cr->count++] = cv;
      } else if (c == '\n' || c == '\r') {
        if (c == '\r' && i + 1 < len && p[i + 1] == '\n')
          i++;
        cell[clen] = '\0';
        AKSValue cv = ValStrLen(cell, clen);
        clen = 0;
        AKSArray *cr = ValToArr(current_row);
        if (cr->count >= cr->capacity) {
          cr->capacity = cr->capacity == 0 ? 4 : cr->capacity * 2;
          cr->items = realloc(cr->items, cr->capacity * sizeof(AKSValue));
        }
        cr->items[cr->count++] = cv;

        if (ValKind(headers) == VALNULL) {
          headers = current_row;
        } else {
          AKSValue obj = ValStruct("Object");
          AKSStruct *s = ValToObj(obj);
          AKSArray *hdr_arr = ValToArr(headers);
          AKSArray *row_arr = ValToArr(current_row);
          int count = hdr_arr->count;
          s->fieldcount = count;
          s->fieldnames = malloc(count * sizeof(char *));
          s->fieldvals = malloc(count * sizeof(AKSValue));
          for (int k = 0; k < count; k++) {
            s->fieldnames[k] = strdup(ValToStrPtr(hdr_arr->items[k])->data);
            if (k < row_arr->count) {
              s->fieldvals[k] = ValCopy(row_arr->items[k]);
            } else {
              s->fieldvals[k] = ValNull();
            }
          }
          if (res_arr->count >= res_arr->capacity) {
            res_arr->capacity =
                res_arr->capacity == 0 ? 4 : res_arr->capacity * 2;
            res_arr->items =
                realloc(res_arr->items, res_arr->capacity * sizeof(AKSValue));
          }
          res_arr->items[res_arr->count++] = obj;
          ValFree(&current_row);
        }
        current_row = ValArr(0);
      } else {
        cell[clen++] = c;
      }
    }
  }
  free(cell);
  ValFree(&headers);
  ValFree(&current_row);
  return res;
}

static void CSVAppendEscaped(const char *s, char **buf, size_t *len,
                             size_t *cap) {
#define APPEND_BYTE(b)                                                         \
  do {                                                                         \
    if (*len >= *cap) {                                                        \
      *cap = *cap == 0 ? 256 : *cap * 2;                                       \
      *buf = realloc(*buf, *cap);                                              \
    }                                                                          \
    (*buf)[(*len)++] = (char)(b);                                              \
  } while (0)

  int needs_quotes = 0;
  for (int i = 0; s[i] != '\0'; i++) {
    if (s[i] == ',' || s[i] == '"' || s[i] == '\n' || s[i] == '\r') {
      needs_quotes = 1;
      break;
    }
  }
  if (needs_quotes) {
    APPEND_BYTE('"');
    for (int i = 0; s[i] != '\0'; i++) {
      if (s[i] == '"') {
        APPEND_BYTE('"');
        APPEND_BYTE('"');
      } else {
        APPEND_BYTE(s[i]);
      }
    }
    APPEND_BYTE('"');
  } else {
    for (int i = 0; s[i] != '\0'; i++) {
      APPEND_BYTE(s[i]);
    }
  }
#undef APPEND_BYTE
}

static char *StringifyCSV(AKSValue arr) {
  size_t cap = 256;
  size_t len = 0;
  char *buf = malloc(cap);
#define APPEND_BYTE(b)                                                         \
  do {                                                                         \
    if (len >= cap) {                                                          \
      cap = cap == 0 ? 256 : cap * 2;                                          \
      buf = realloc(buf, cap);                                                 \
    }                                                                          \
    buf[len++] = (char)(b);                                                    \
  } while (0)

  if (ValKind(arr) != VALARR || ValToArr(arr)->count == 0) {
    APPEND_BYTE('\0');
    return buf;
  }
  AKSArray *a = ValToArr(arr);
  int hcap = 8, hcount = 0;
  char **headers = malloc(hcap * sizeof(char *));
  for (int i = 0; i < a->count; i++) {
    if (ValKind(a->items[i]) == VALSTRUCT) {
      AKSStruct *s = ValToObj(a->items[i]);
      for (int j = 0; j < s->fieldcount; j++) {
        if (s->fieldnames[j][0] == '$')
          continue;
        int found = 0;
        for (int k = 0; k < hcount; k++) {
          if (strcmp(headers[k], s->fieldnames[j]) == 0) {
            found = 1;
            break;
          }
        }
        if (!found) {
          if (hcount >= hcap) {
            hcap *= 2;
            headers = realloc(headers, hcap * sizeof(char *));
          }
          headers[hcount++] = s->fieldnames[j];
        }
      }
    }
  }
  if (hcount > 0) {
    for (int i = 0; i < hcount; i++) {
      if (i > 0)
        APPEND_BYTE(',');
      CSVAppendEscaped(headers[i], &buf, &len, &cap);
    }
    APPEND_BYTE('\n');
    for (int i = 0; i < a->count; i++) {
      if (ValKind(a->items[i]) == VALSTRUCT) {
        AKSStruct *s = ValToObj(a->items[i]);
        for (int j = 0; j < hcount; j++) {
          if (j > 0)
            APPEND_BYTE(',');
          int foundidx = -1;
          for (int k = 0; k < s->fieldcount; k++) {
            if (strcmp(s->fieldnames[k], headers[j]) == 0) {
              foundidx = k;
              break;
            }
          }
          if (foundidx >= 0) {
            AKSValue v = s->fieldvals[foundidx];
            if (ValKind(v) == VALSTR) {
              CSVAppendEscaped(ValToStrPtr(v)->data, &buf, &len, &cap);
            } else if (ValKind(v) != VALNULL) {
              AKSValue temp_str = ValToStr(v);
              CSVAppendEscaped(ValToStrPtr(temp_str)->data, &buf, &len, &cap);
              ValFree(&temp_str);
            }
          }
        }
        APPEND_BYTE('\n');
      }
    }
  }
  free(headers);
  APPEND_BYTE('\0');
  return buf;
#undef APPEND_BYTE
}

static const char *TrimSpaces(const char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}

static AKSValue ParseTOMLVal(const char *ValStr) {
  ValStr = TrimSpaces(ValStr);
  int len = (int)strlen(ValStr);
  while (len > 0 && (ValStr[len - 1] == ' ' || ValStr[len - 1] == '\t' ||
                     ValStr[len - 1] == '\r' || ValStr[len - 1] == '\n'))
    len--;
  if (len >= 2 && ValStr[0] == '"' && ValStr[len - 1] == '"') {
    char *str = malloc(len - 1);
    memcpy(str, ValStr + 1, len - 2);
    str[len - 2] = '\0';
    AKSValue res = ValStrLen(str, len - 2);
    free(str);
    return res;
  }
  if (strncmp(ValStr, "true", 4) == 0)
    return ValBool(1);
  if (strncmp(ValStr, "false", 5) == 0)
    return ValBool(0);
  char *endptr;
  int64_t iv = strtoll(ValStr, &endptr, 10);
  if (endptr == ValStr + len)
    return ValInt(iv);
  double fv = strtod(ValStr, &endptr);
  if (endptr == ValStr + len)
    return ValFloat(fv);
  char *str = malloc(len + 1);
  memcpy(str, ValStr, len);
  str[len] = '\0';
  AKSValue res = ValStrLen(str, len);
  free(str);
  return res;
}

static AKSValue ParseTOMLStr(const char *toml) {
  AKSValue res = ValStruct("Object");
  AKSStruct *root = ValToObj(res);
  AKSStruct *current_table = root;
  const char *p = toml;
  char line[512];

  while (*p) {
    int lidx = 0;
    while (*p && *p != '\n' && *p != '\r' && lidx < 511) {
      line[lidx++] = *p++;
    }
    line[lidx] = '\0';
    if (*p == '\r')
      p++;
    if (*p == '\n')
      p++;
    const char *tline = TrimSpaces(line);
    if (tline[0] == '\0' || tline[0] == '#')
      continue;
    int len = (int)strlen(tline);
    if (tline[0] == '[' && tline[len - 1] == ']') {
      char tablename[256];
      int tlen = len - 2;
      if (tlen > 255)
        tlen = 255;
      memcpy(tablename, tline + 1, tlen);
      tablename[tlen] = '\0';
      AKSValue tableval = ValStruct("Object");
      root->fieldnames =
          realloc(root->fieldnames, (root->fieldcount + 1) * sizeof(char *));
      root->fieldvals =
          realloc(root->fieldvals, (root->fieldcount + 1) * sizeof(AKSValue));
      root->fieldnames[root->fieldcount] = strdup(tablename);
      root->fieldvals[root->fieldcount] = tableval;
      root->fieldcount++;
      current_table = ValToObj(tableval);
    } else {
      const char *eq = strchr(tline, '=');
      if (eq) {
        char key[256];
        int klen = (int)(eq - tline);
        while (klen > 0 && (tline[klen - 1] == ' ' || tline[klen - 1] == '\t'))
          klen--;
        if (klen > 255)
          klen = 255;
        memcpy(key, tline, klen);
        key[klen] = '\0';
        AKSValue val = ParseTOMLVal(eq + 1);
        current_table->fieldnames =
            realloc(current_table->fieldnames,
                    (current_table->fieldcount + 1) * sizeof(char *));
        current_table->fieldvals =
            realloc(current_table->fieldvals,
                    (current_table->fieldcount + 1) * sizeof(AKSValue));
        current_table->fieldnames[current_table->fieldcount] = strdup(key);
        current_table->fieldvals[current_table->fieldcount] = val;
        current_table->fieldcount++;
      }
    }
  }
  return res;
}

static void TOMLStringifyTable(AKSStruct *s, const char *prefix, char **buf,
                               size_t *len, size_t *cap) {
#define APPEND_BYTE(b)                                                         \
  do {                                                                         \
    if (*len >= *cap) {                                                        \
      *cap = *cap == 0 ? 256 : *cap * 2;                                       \
      *buf = realloc(*buf, *cap);                                              \
    }                                                                          \
    (*buf)[(*len)++] = (char)(b);                                              \
  } while (0)
#define APPEND_STR(str)                                                        \
  do {                                                                         \
    for (int k = 0; (str)[k] != '\0'; k++)                                     \
      APPEND_BYTE((str)[k]);                                                   \
  } while (0)

  for (int i = 0; i < s->fieldcount; i++) {
    if (s->fieldnames[i][0] == '$')
      continue;
    ValType t = ValKind(s->fieldvals[i]);
    if (t != VALSTRUCT && t != VALARR) {
      APPEND_STR(s->fieldnames[i]);
      APPEND_STR(" = ");
      if (t == VALSTR) {
        APPEND_BYTE('"');
        APPEND_STR(ValToStrPtr(s->fieldvals[i])->data);
        APPEND_BYTE('"');
      } else {
        AKSValue temp_str = ValToStr(s->fieldvals[i]);
        APPEND_STR(ValToStrPtr(temp_str)->data);
        ValFree(&temp_str);
      }
      APPEND_BYTE('\n');
    }
  }
  for (int i = 0; i < s->fieldcount; i++) {
    if (s->fieldnames[i][0] == '$')
      continue;
    ValType t = ValKind(s->fieldvals[i]);
    if (t == VALSTRUCT) {
      APPEND_BYTE('\n');
      APPEND_BYTE('[');
      if (prefix && prefix[0] != '\0') {
        APPEND_STR(prefix);
        APPEND_BYTE('.');
      }
      APPEND_STR(s->fieldnames[i]);
      APPEND_BYTE(']');
      APPEND_BYTE('\n');
      char newprefix[512];
      if (prefix && prefix[0] != '\0') {
        snprintf(newprefix, sizeof(newprefix), "%s.%s", prefix,
                 s->fieldnames[i]);
      } else {
        snprintf(newprefix, sizeof(newprefix), "%s", s->fieldnames[i]);
      }
      TOMLStringifyTable(ValToObj(s->fieldvals[i]), newprefix, buf, len, cap);
    }
  }
#undef APPEND_BYTE
#undef APPEND_STR
}

static char *StringifyTOML(AKSValue val) {
  size_t cap = 256;
  size_t len = 0;
  char *buf = malloc(cap);
  buf[0] = '\0';
  if (ValKind(val) == VALSTRUCT) {
    TOMLStringifyTable(ValToObj(val), "", &buf, &len, &cap);
  }
  if (len >= cap) {
    cap += 1;
    buf = realloc(buf, cap);
  }
  buf[len] = '\0';
  return buf;
}

static AKSValue ParseXMLStr(const char *xml) {
  AKSValue res = ValStruct("Object");
  AKSStruct *current = ValToObj(res);
  const char *p = xml;
  while (*p) {
    p = strchr(p, '<');
    if (!p)
      break;
    p++;
    if (*p == '/') {
      p = strchr(p, '>');
      if (p)
        p++;
      continue;
    }
    if (*p == '!' || *p == '?') {
      p = strchr(p, '>');
      if (p)
        p++;
      continue;
    }
    char tag[256];
    int tidx = 0;
    while (*p && *p != '>' && *p != '/' && *p != ' ' && tidx < 255) {
      tag[tidx++] = *p++;
    }
    tag[tidx] = '\0';
    while (*p && *p != '>' && *p != '/')
      p++;
    int self_closing = 0;
    if (*p == '/') {
      self_closing = 1;
      p++;
    }
    if (*p == '>')
      p++;
    if (self_closing) {
      current->fieldnames = realloc(current->fieldnames,
                                    (current->fieldcount + 1) * sizeof(char *));
      current->fieldvals = realloc(
          current->fieldvals, (current->fieldcount + 1) * sizeof(AKSValue));
      current->fieldnames[current->fieldcount] = strdup(tag);
      current->fieldvals[current->fieldcount] = ValNull();
      current->fieldcount++;
      continue;
    }
    const char *next_tag = strchr(p, '<');
    if (next_tag) {
      int is_child = (next_tag[1] != '/');
      if (is_child) {
        AKSValue child = ParseXMLStr(p);
        current->fieldnames = realloc(
            current->fieldnames, (current->fieldcount + 1) * sizeof(char *));
        current->fieldvals = realloc(
            current->fieldvals, (current->fieldcount + 1) * sizeof(AKSValue));
        current->fieldnames[current->fieldcount] = strdup(tag);
        current->fieldvals[current->fieldcount] = child;
        current->fieldcount++;
        p = next_tag;
      } else {
        int clen = (int)(next_tag - p);
        char *content = malloc(clen + 1);
        memcpy(content, p, clen);
        content[clen] = '\0';
        AKSValue val = ValStrLen(content, clen);
        free(content);
        current->fieldnames = realloc(
            current->fieldnames, (current->fieldcount + 1) * sizeof(char *));
        current->fieldvals = realloc(
            current->fieldvals, (current->fieldcount + 1) * sizeof(AKSValue));
        current->fieldnames[current->fieldcount] = strdup(tag);
        current->fieldvals[current->fieldcount] = val;
        current->fieldcount++;
        p = next_tag;
      }
    }
  }
  return res;
}

static void XMLStringifyVal(const char *name, AKSValue v, char **buf,
                            size_t *len, size_t *cap) {
#define APPEND_BYTE(b)                                                         \
  do {                                                                         \
    if (*len >= *cap) {                                                        \
      *cap = *cap == 0 ? 256 : *cap * 2;                                       \
      *buf = realloc(*buf, *cap);                                              \
    }                                                                          \
    (*buf)[(*len)++] = (char)(b);                                              \
  } while (0)
#define APPEND_STR(str)                                                        \
  do {                                                                         \
    for (int k = 0; (str)[k] != '\0'; k++)                                     \
      APPEND_BYTE((str)[k]);                                                   \
  } while (0)

  APPEND_BYTE('<');
  APPEND_STR(name);
  APPEND_BYTE('>');
  ValType t = ValKind(v);
  if (t == VALSTRUCT) {
    AKSStruct *s = ValToObj(v);
    for (int i = 0; i < s->fieldcount; i++) {
      if (s->fieldnames[i][0] == '$')
        continue;
      XMLStringifyVal(s->fieldnames[i], s->fieldvals[i], buf, len, cap);
    }
  } else {
    AKSValue temp_str = ValToStr(v);
    APPEND_STR(ValToStrPtr(temp_str)->data);
    ValFree(&temp_str);
  }
  APPEND_STR("</");
  APPEND_STR(name);
  APPEND_BYTE('>');
#undef APPEND_BYTE
#undef APPEND_STR
}

static char *StringifyXML(AKSValue val) {
  size_t cap = 256;
  size_t len = 0;
  char *buf = malloc(cap);
  buf[0] = '\0';
  if (ValKind(val) == VALSTRUCT) {
    AKSStruct *s = ValToObj(val);
    XMLStringifyVal(s->typename, val, &buf, &len, &cap);
  } else {
    XMLStringifyVal("root", val, &buf, &len, &cap);
  }
  if (len >= cap) {
    cap += 1;
    buf = realloc(buf, cap);
  }
  buf[len] = '\0';
  return buf;
}

static void WriteProtoVarint(uint64_t val, char **buf, size_t *len,
                             size_t *cap) {
#define APPEND_BYTE(b)                                                         \
  do {                                                                         \
    if (*len >= *cap) {                                                        \
      *cap = *cap == 0 ? 256 : *cap * 2;                                       \
      *buf = realloc(*buf, *cap);                                              \
    }                                                                          \
    (*buf)[(*len)++] = (char)(b);                                              \
  } while (0)
  while (val >= 128) {
    APPEND_BYTE((val & 127) | 128);
    val >>= 7;
  }
  APPEND_BYTE(val);
#undef APPEND_BYTE
}

static uint64_t ReadProtoVarint(const uint8_t **ptr, const uint8_t *end,
                                int *Error) {
  uint64_t val = 0;
  int shift = 0;
  while (1) {
    if (*ptr >= end) {
      *Error = 1;
      return 0;
    }
    uint8_t b = **ptr;
    (*ptr)++;
    val |= ((uint64_t)(b & 127) << shift);
    if ((b & 128) == 0)
      break;
    shift += 7;
  }
  return val;
}

static char *ProtobufEncodeObjFunc(AKSValue obj, AKSValue mapping,
                                   size_t *out_len) {
  size_t cap = 256;
  size_t len = 0;
  char *buf = malloc(cap);
#define APPEND_BYTE(b)                                                         \
  do {                                                                         \
    if (len >= cap) {                                                          \
      cap = cap == 0 ? 256 : cap * 2;                                          \
      buf = realloc(buf, cap);                                                 \
    }                                                                          \
    buf[len++] = (char)(b);                                                    \
  } while (0)
#define APPEND_BYTES(bytes, bytes_len)                                         \
  do {                                                                         \
    for (size_t k = 0; k < (bytes_len); k++)                                   \
      APPEND_BYTE((bytes)[k]);                                                 \
  } while (0)

  if (ValKind(obj) != VALSTRUCT || ValKind(mapping) != VALSTRUCT) {
    *out_len = 0;
    return buf;
  }
  AKSStruct *o = ValToObj(obj);
  AKSStruct *m = ValToObj(mapping);
  for (int i = 0; i < m->fieldcount; i++) {
    int field_num = atoi(m->fieldnames[i]);
    if (field_num <= 0)
      continue;
    const char *fieldname = ValToStrPtr(m->fieldvals[i])->data;
    int fidx = -1;
    for (int k = 0; k < o->fieldcount; k++) {
      if (strcmp(o->fieldnames[k], fieldname) == 0) {
        fidx = k;
        break;
      }
    }
    if (fidx >= 0) {
      AKSValue val = o->fieldvals[fidx];
      ValType vt = ValKind(val);
      if (vt == VALINT || vt == VALBOOL) {
        int64_t iv = ValToInt(val);
        uint64_t key = (field_num << 3) | 0;
        WriteProtoVarint(key, &buf, &len, &cap);
        WriteProtoVarint((uint64_t)iv, &buf, &len, &cap);
      } else if (vt == VALSTR) {
        const char *s = ValToStrPtr(val)->data;
        int slen = ValToStrPtr(val)->len;
        uint64_t key = (field_num << 3) | 2;
        WriteProtoVarint(key, &buf, &len, &cap);
        WriteProtoVarint((uint64_t)slen, &buf, &len, &cap);
        APPEND_BYTES(s, slen);
      }
    }
  }
  *out_len = len;
  return buf;
#undef APPEND_BYTE
#undef APPEND_BYTES
}

static AKSValue ProtobufDecodeObjFunc(const char *bytes, int bytes_len,
                                      AKSValue mapping, const char *typename) {
  AKSValue res = ValStruct(typename);
  AKSStruct *obj = ValToObj(res);
  if (ValKind(mapping) != VALSTRUCT)
    return res;
  AKSStruct *m = ValToObj(mapping);
  const uint8_t *ptr = (const uint8_t *)bytes;
  const uint8_t *end = ptr + bytes_len;
  int Error = 0;
  while (ptr < end) {
    uint64_t key = ReadProtoVarint(&ptr, end, &Error);
    if (Error)
      break;
    int field_num = (int)(key >> 3);
    int wire_type = (int)(key & 7);
    char field_num_str[32];
    snprintf(field_num_str, sizeof(field_num_str), "%d", field_num);
    int foundidx = -1;
    for (int k = 0; k < m->fieldcount; k++) {
      if (strcmp(m->fieldnames[k], field_num_str) == 0) {
        foundidx = k;
        break;
      }
    }
    const char *fieldname =
        (foundidx >= 0) ? ValToStrPtr(m->fieldvals[foundidx])->data : NULL;
    if (wire_type == 0) {
      uint64_t v = ReadProtoVarint(&ptr, end, &Error);
      if (Error)
        break;
      if (fieldname) {
        obj->fieldnames =
            realloc(obj->fieldnames, (obj->fieldcount + 1) * sizeof(char *));
        obj->fieldvals =
            realloc(obj->fieldvals, (obj->fieldcount + 1) * sizeof(AKSValue));
        obj->fieldnames[obj->fieldcount] = strdup(fieldname);
        obj->fieldvals[obj->fieldcount] = ValInt((int64_t)v);
        obj->fieldcount++;
      }
    } else if (wire_type == 2) {
      uint64_t slen = ReadProtoVarint(&ptr, end, &Error);
      if (Error)
        break;
      if (ptr + slen > end)
        break;
      char *s = malloc(slen + 1);
      memcpy(s, ptr, slen);
      s[slen] = '\0';
      ptr += slen;
      if (fieldname) {
        obj->fieldnames =
            realloc(obj->fieldnames, (obj->fieldcount + 1) * sizeof(char *));
        obj->fieldvals =
            realloc(obj->fieldvals, (obj->fieldcount + 1) * sizeof(AKSValue));
        obj->fieldnames[obj->fieldcount] = strdup(fieldname);
        obj->fieldvals[obj->fieldcount] = ValStrLen(s, (int)slen);
        obj->fieldcount++;
      }
      free(s);
    } else {
      break;
    }
  }
  return res;
}

static int Utf8NextCodepoint(const uint8_t *s, int len, int *bytes_consumed) {
  if (len <= 0) {
    *bytes_consumed = 0;
    return 0;
  }
  uint8_t c = s[0];
  if (c < 0x80) {
    *bytes_consumed = 1;
    return c;
  }
  if ((c & 0xE0) == 0xC0) {
    if (len < 2) {
      *bytes_consumed = 1;
      return 0xFFFD;
    }
    *bytes_consumed = 2;
    return ((c & 0x1F) << 6) | (s[1] & 0x3F);
  }
  if ((c & 0xF0) == 0xE0) {
    if (len < 3) {
      *bytes_consumed = 1;
      return 0xFFFD;
    }
    *bytes_consumed = 3;
    return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
  }
  if ((c & 0xF8) == 0xF0) {
    if (len < 4) {
      *bytes_consumed = 1;
      return 0xFFFD;
    }
    *bytes_consumed = 4;
    return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) |
           (s[3] & 0x3F);
  }
  *bytes_consumed = 1;
  return 0xFFFD;
}

static int Utf8EncodeCodepoint(int cp, char *out) {
  if (cp < 0)
    cp = 0xFFFD;
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  if (cp < 0x200000) {
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }
  out[0] = (char)0xEF;
  out[1] = (char)0xBF;
  out[2] = (char)0xBD;
  return 3;
}

static int Utf8LenFunc(const char *str) {
  int len = (int)strlen(str);
  int count = 0;
  int pos = 0;
  while (pos < len) {
    int consumed = 0;
    Utf8NextCodepoint((const uint8_t *)str + pos, len - pos, &consumed);
    if (consumed <= 0)
      break;
    pos += consumed;
    count++;
  }
  return count;
}

static char *Utf8CharAtFunc(const char *str, int target_idx) {
  int len = (int)strlen(str);
  int count = 0;
  int pos = 0;
  while (pos < len) {
    int consumed = 0;
    Utf8NextCodepoint((const uint8_t *)str + pos, len - pos, &consumed);
    if (consumed <= 0)
      break;
    if (count == target_idx) {
      char *res = malloc(consumed + 1);
      memcpy(res, str + pos, consumed);
      res[consumed] = '\0';
      return res;
    }
    pos += consumed;
    count++;
  }
  return strdup("");
}

static int Utf8CodepointAtFunc(const char *str, int target_idx) {
  int len = (int)strlen(str);
  int count = 0;
  int pos = 0;
  while (pos < len) {
    int consumed = 0;
    int cp =
        Utf8NextCodepoint((const uint8_t *)str + pos, len - pos, &consumed);
    if (consumed <= 0)
      break;
    if (count == target_idx) {
      return cp;
    }
    pos += consumed;
    count++;
  }
  return -1;
}

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

/* Ensure prog->insts has room for at least (count + extra) entries.
 * MUST be used instead of bare realloc() so that prog->cap stays in sync.
 * re_emit() relies on prog->cap being the true allocated size; if anything
 * shrinks the allocation without updating cap, re_emit() will write past the
 * end of the buffer and silently corrupt the heap. */
static void ReGrow(ReProg *p, int extra) {
  int required = p->count + extra;
  if (required > p->cap) {
    while (p->cap < required)
      p->cap = p->cap == 0 ? 16 : p->cap * 2;
    p->insts = realloc(p->insts, p->cap * sizeof(ReInst));
  }
}

static void ReEmit(ReProg *p, ReOp op, int val, int target1, int target2) {
  if (p->count >= p->cap) {
    p->cap = p->cap == 0 ? 16 : p->cap * 2;
    p->insts = realloc(p->insts, p->cap * sizeof(ReInst));
  }
  ReInst *inst = &p->insts[p->count++];
  memset(inst, 0, sizeof(ReInst));
  inst->op = op;
  inst->val = val;
  inst->target1 = target1;
  inst->target2 = target2;
}

static int ParseReExpr(const char **pat, ReProg *prog);

static int ParseReClass(const char **pat, ReInst *inst) {
  inst->op = RE_CLASS;
  if (**pat == '^') {
    inst->class_negated = 1;
    (*pat)++;
  }
  while (**pat && **pat != ']') {
    if (**pat == '\\') {
      (*pat)++;
      char escaped = **pat;
      if (escaped == 'd') {
        for (int c = '0'; c <= '9'; c++)
          inst->class_bits[c / 8] |= (1 << (c % 8));
      } else if (escaped == 'w') {
        for (int c = 0; c < 256; c++) {
          if (isalnum(c) || c == '_')
            inst->class_bits[c / 8] |= (1 << (c % 8));
        }
      } else if (escaped == 's') {
        inst->class_bits[' ' / 8] |= (1 << (' ' % 8));
        inst->class_bits['\t' / 8] |= (1 << ('\t' % 8));
        inst->class_bits['\r' / 8] |= (1 << ('\r' % 8));
        inst->class_bits['\n' / 8] |= (1 << ('\n' % 8));
      } else {
        inst->class_bits[(uint8_t)escaped / 8] |= (1 << ((uint8_t)escaped % 8));
      }
      if (**pat)
        (*pat)++;
    } else {
      int start_cp = (uint8_t)**pat;
      if ((start_cp & 0x80) != 0) {
        int consumed = 0;
        start_cp = Utf8NextCodepoint((const uint8_t *)*pat, 4, &consumed);
        if (consumed > 0)
          *pat += consumed;
      } else {
        (*pat)++;
      }
      if (**pat == '-' && *(*pat + 1) && *(*pat + 1) != ']') {
        (*pat)++;
        int end_cp = (uint8_t)**pat;
        if ((end_cp & 0x80) != 0) {
          int consumed = 0;
          end_cp = Utf8NextCodepoint((const uint8_t *)*pat, 4, &consumed);
          if (consumed > 0)
            *pat += consumed;
        } else {
          (*pat)++;
        }
        if (start_cp < 256 && end_cp < 256) {
          for (int c = start_cp; c <= end_cp; c++) {
            inst->class_bits[c / 8] |= (1 << (c % 8));
          }
        } else {
          inst->class_ranges =
              realloc(inst->class_ranges,
                      (inst->class_range_count + 1) * sizeof(ReClassRange));
          inst->class_ranges[inst->class_range_count].start = start_cp;
          inst->class_ranges[inst->class_range_count].end = end_cp;
          inst->class_range_count++;
        }
      } else {
        if (start_cp < 256) {
          inst->class_bits[start_cp / 8] |= (1 << (start_cp % 8));
        } else {
          inst->class_ranges =
              realloc(inst->class_ranges,
                      (inst->class_range_count + 1) * sizeof(ReClassRange));
          inst->class_ranges[inst->class_range_count].start = start_cp;
          inst->class_ranges[inst->class_range_count].end = start_cp;
          inst->class_range_count++;
        }
      }
    }
  }
  if (**pat == ']')
    (*pat)++;
  return 1;
}

static int ParseReAtom(const char **pat, ReProg *prog) {
  if (**pat == '\0' || **pat == '|' || **pat == ')' || **pat == '*' ||
      **pat == '+' || **pat == '?') {
    return 0;
  }
  char c = **pat;
  if (c == '^') {
    (*pat)++;
    ReEmit(prog, RE_ANCHOR_START, 0, 0, 0);
    return 1;
  }
  if (c == '$') {
    (*pat)++;
    ReEmit(prog, RE_ANCHOR_END, 0, 0, 0);
    return 1;
  }
  if (c == '.') {
    (*pat)++;
    ReEmit(prog, RE_ANY, 0, 0, 0);
    return 1;
  }
  if (c == '(') {
    (*pat)++;
    int group_id = ++prog->save_count;
    int save_start_pc = prog->count;
    ReEmit(prog, RE_SAVE, 2 * group_id, 0, 0);
    ParseReExpr(pat, prog);
    if (**pat == ')')
      (*pat)++;
    ReEmit(prog, RE_SAVE, 2 * group_id + 1, 0, 0);
    return 1;
  }
  if (c == '[') {
    (*pat)++;
    int pc = prog->count;
    ReEmit(prog, RE_CLASS, 0, 0, 0);
    ParseReClass(pat, &prog->insts[pc]);
    return 1;
  }
  if (c == '\\') {
    (*pat)++;
    char escaped = **pat;
    (*pat)++;
    if (escaped == 'd' || escaped == 'w' || escaped == 's' || escaped == 'D' ||
        escaped == 'W' || escaped == 'S') {
      int pc = prog->count;
      ReEmit(prog, RE_CLASS, 0, 0, 0);
      ReInst *inst = &prog->insts[pc];
      if (escaped == 'D' || escaped == 'W' || escaped == 'S')
        inst->class_negated = 1;
      char key = (escaped == 'd' || escaped == 'D')
                     ? 'd'
                     : ((escaped == 'w' || escaped == 'W') ? 'w' : 's');
      if (key == 'd') {
        for (int i = '0'; i <= '9'; i++)
          inst->class_bits[i / 8] |= (1 << (i % 8));
      } else if (key == 'w') {
        for (int i = 0; i < 256; i++) {
          if (isalnum(i) || i == '_')
            inst->class_bits[i / 8] |= (1 << (i % 8));
        }
      } else if (key == 's') {
        inst->class_bits[' ' / 8] |= (1 << (' ' % 8));
        inst->class_bits['\t' / 8] |= (1 << ('\t' % 8));
        inst->class_bits['\r' / 8] |= (1 << ('\r' % 8));
        inst->class_bits['\n' / 8] |= (1 << ('\n' % 8));
      }
      return 1;
    } else {
      ReEmit(prog, RE_CHAR, escaped, 0, 0);
      return 1;
    }
  }
  int cp = (uint8_t)c;
  if ((cp & 0x80) != 0) {
    int consumed = 0;
    cp = Utf8NextCodepoint((const uint8_t *)*pat, 4, &consumed);
    if (consumed > 0) {
      *pat += consumed;
      ReEmit(prog, RE_CHAR, cp, 0, 0);
      return 1;
    }
  }
  (*pat)++;
  ReEmit(prog, RE_CHAR, cp, 0, 0);
  return 1;
}

static int ParseReQuantifier(const char **pat, ReProg *prog) {
  int start_pc = prog->count;
  if (!ParseReAtom(pat, prog))
    return 0;
  int end_pc = prog->count;
  char q = **pat;
  if (q == '*' || q == '+' || q == '?') {
    (*pat)++;
    int len = end_pc - start_pc;
    if (q == '?') {
      ReGrow(prog, 1);
      memmove(&prog->insts[start_pc + 1], &prog->insts[start_pc],
              len * sizeof(ReInst));
      prog->count++;
      for (int k = start_pc + 1; k < prog->count; k++) {
        if (prog->insts[k].op == RE_SPLIT) {
          if (prog->insts[k].target1 >= start_pc)
            prog->insts[k].target1++;
          if (prog->insts[k].target2 >= start_pc)
            prog->insts[k].target2++;
        } else if (prog->insts[k].op == RE_JUMP) {
          if (prog->insts[k].val >= start_pc)
            prog->insts[k].val++;
        }
      }
      memset(&prog->insts[start_pc], 0, sizeof(ReInst));
      prog->insts[start_pc].op = RE_SPLIT;
      prog->insts[start_pc].target1 = start_pc + 1;
      prog->insts[start_pc].target2 = prog->count;
    } else if (q == '*') {
      ReGrow(prog, 2);
      memmove(&prog->insts[start_pc + 1], &prog->insts[start_pc],
              len * sizeof(ReInst));
      prog->count += 2;
      for (int k = start_pc + 1; k < start_pc + 1 + len; k++) {
        if (prog->insts[k].op == RE_SPLIT) {
          if (prog->insts[k].target1 >= start_pc)
            prog->insts[k].target1++;
          if (prog->insts[k].target2 >= start_pc)
            prog->insts[k].target2++;
        } else if (prog->insts[k].op == RE_JUMP) {
          if (prog->insts[k].val >= start_pc)
            prog->insts[k].val++;
        }
      }
      memset(&prog->insts[start_pc], 0, sizeof(ReInst));
      memset(&prog->insts[prog->count - 1], 0, sizeof(ReInst));
      prog->insts[start_pc].op = RE_SPLIT;
      prog->insts[start_pc].target1 = start_pc + 1;
      prog->insts[start_pc].target2 = prog->count;
      prog->insts[prog->count - 1].op = RE_JUMP;
      prog->insts[prog->count - 1].val = start_pc;
    } else if (q == '+') {
      ReEmit(prog, RE_SPLIT, 0, start_pc, prog->count + 1);
    }
  } else if (q == '{') {
    /* Bounded quantifiers: {n}, {n,}, {n,m}
     * The atom is already emitted at [start_pc, end_pc).
     * Strategy: duplicate the instruction block for each extra repetition,
     * adjusting internal SPLIT/JUMP targets by the copy offset each time.
     * RE_SAVE slot indices are logical (not PC values), so they need no
     * adjustment. */
    (*pat)++;                      /* consume '{' */
    int min_rep = 0, max_rep = -2; /* -2 sentinel = not set yet */
    while (**pat >= '0' && **pat <= '9') {
      min_rep = min_rep * 10 + (**pat - '0');
      (*pat)++;
    }
    if (**pat == ',') {
      (*pat)++; /* consume ',' */
      if (**pat == '}') {
        max_rep = -1; /* {n,} = unlimited upper bound */
      } else {
        max_rep = 0;
        while (**pat >= '0' && **pat <= '9') {
          max_rep = max_rep * 10 + (**pat - '0');
          (*pat)++;
        }
      }
    } else {
      max_rep = min_rep; /* {n} = exact repetition */
    }
    if (**pat == '}')
      (*pat)++; /* consume '}' */

    int atom_len = end_pc - start_pc;

    /* RE_COPY_ATOM(src): append a copy of the atom instruction block from
     * position src to the end of prog, adjusting internal jump offsets.
     * class_ranges arrays are deep-copied to avoid double-free. */
#define RE_COPY_ATOM(src_start)                                                \
  do {                                                                         \
    ReGrow(prog, atom_len);                                                    \
    int _src = (src_start);                                                    \
    int _dst = prog->count;                                                    \
    int _off = _dst - _src;                                                    \
    for (int _j = 0; _j < atom_len; _j++) {                                    \
      prog->insts[_dst + _j] = prog->insts[_src + _j];                         \
      if (prog->insts[_dst + _j].class_ranges &&                               \
          prog->insts[_dst + _j].class_range_count > 0) {                      \
        ReClassRange *_cr = malloc(prog->insts[_dst + _j].class_range_count *  \
                                   sizeof(ReClassRange));                      \
        memcpy(_cr, prog->insts[_src + _j].class_ranges,                       \
               prog->insts[_dst + _j].class_range_count *                      \
                   sizeof(ReClassRange));                                      \
        prog->insts[_dst + _j].class_ranges = _cr;                             \
      } else {                                                                 \
        prog->insts[_dst + _j].class_ranges = NULL;                            \
      }                                                                        \
      if (prog->insts[_dst + _j].op == RE_SPLIT) {                             \
        prog->insts[_dst + _j].target1 += _off;                                \
        prog->insts[_dst + _j].target2 += _off;                                \
      } else if (prog->insts[_dst + _j].op == RE_JUMP) {                       \
        prog->insts[_dst + _j].val += _off;                                    \
      }                                                                        \
    }                                                                          \
    prog->count += atom_len;                                                   \
  } while (0)

    if (min_rep == 0) {
      if (max_rep == 0) {
        /* {0}: match nothing  -  remove the emitted atom */
        prog->count = start_pc;
      } else if (max_rep == -1) {
        /* {0,} = '*': wrap atom in SPLIT+JUMP loop */
        ReGrow(prog, 2);
        memmove(&prog->insts[start_pc + 1], &prog->insts[start_pc],
                atom_len * sizeof(ReInst));
        prog->count += 2;
        for (int k = start_pc + 1; k < start_pc + 1 + atom_len; k++) {
          if (prog->insts[k].op == RE_SPLIT) {
            prog->insts[k].target1++;
            prog->insts[k].target2++;
          } else if (prog->insts[k].op == RE_JUMP) {
            prog->insts[k].val++;
          }
        }
        memset(&prog->insts[start_pc], 0, sizeof(ReInst));
        memset(&prog->insts[prog->count - 1], 0, sizeof(ReInst));
        prog->insts[start_pc].op = RE_SPLIT;
        prog->insts[start_pc].target1 = start_pc + 1;
        prog->insts[start_pc].target2 = prog->count;
        prog->insts[prog->count - 1].op = RE_JUMP;
        prog->insts[prog->count - 1].val = start_pc;
      } else {
        /* {0,m}: wrap original as optional, then add m-1 more optional copies
         */
        ReGrow(prog, 1);
        memmove(&prog->insts[start_pc + 1], &prog->insts[start_pc],
                atom_len * sizeof(ReInst));
        prog->count++;
        for (int k = start_pc + 1; k < prog->count; k++) {
          if (prog->insts[k].op == RE_SPLIT) {
            prog->insts[k].target1++;
            prog->insts[k].target2++;
          } else if (prog->insts[k].op == RE_JUMP) {
            prog->insts[k].val++;
          }
        }
        memset(&prog->insts[start_pc], 0, sizeof(ReInst));
        prog->insts[start_pc].op = RE_SPLIT;
        prog->insts[start_pc].target1 = start_pc + 1;
        prog->insts[start_pc].target2 = prog->count;
        int last_copy = start_pc + 1;
        for (int k = 1; k < max_rep; k++) {
          int split_pc = prog->count;
          ReEmit(prog, RE_SPLIT, 0, split_pc + 1, 0);
          int copy_start = prog->count;
          RE_COPY_ATOM(last_copy);
          prog->insts[split_pc].target2 = prog->count;
          last_copy = copy_start;
        }
      }
    } else {
      /* min_rep >= 1: first mandatory copy already at [start_pc, end_pc) */
      int last_copy = start_pc;
      for (int k = 1; k < min_rep; k++) {
        int copy_start = prog->count;
        RE_COPY_ATOM(last_copy);
        last_copy = copy_start;
      }
      if (max_rep == -1) {
        /* {n,}: SPLIT loops back into last mandatory copy, or exits */
        ReEmit(prog, RE_SPLIT, 0, last_copy, prog->count + 1);
      } else if (max_rep > min_rep) {
        /* {n,m}: emit (max-min) optional copies via SPLIT+copy */
        for (int k = 0; k < max_rep - min_rep; k++) {
          int split_pc = prog->count;
          ReEmit(prog, RE_SPLIT, 0, split_pc + 1, 0);
          int copy_start = prog->count;
          RE_COPY_ATOM(last_copy);
          prog->insts[split_pc].target2 = prog->count;
          last_copy = copy_start;
        }
      }
      /* max_rep == min_rep: exact  -  already done */
    }
#undef RE_COPY_ATOM
  }
  return 1;
}

static int ParseReConcat(const char **pat, ReProg *prog) {
  int count = 0;
  while (**pat && **pat != '|' && **pat != ')') {
    if (ParseReQuantifier(pat, prog)) {
      count++;
    } else {
      break;
    }
  }
  return count > 0;
}

static int ParseReExpr(const char **pat, ReProg *prog) {
  int start_pc = prog->count;
  if (!ParseReConcat(pat, prog))
    return 0;
  if (**pat == '|') {
    (*pat)++;
    int left_len = prog->count - start_pc;
    ReGrow(prog, 2);
    memmove(&prog->insts[start_pc + 1], &prog->insts[start_pc],
            left_len * sizeof(ReInst));
    prog->count += 2;
    for (int k = start_pc + 1; k < start_pc + 1 + left_len; k++) {
      if (prog->insts[k].op == RE_SPLIT) {
        if (prog->insts[k].target1 >= start_pc)
          prog->insts[k].target1++;
        if (prog->insts[k].target2 >= start_pc)
          prog->insts[k].target2++;
      } else if (prog->insts[k].op == RE_JUMP) {
        if (prog->insts[k].val >= start_pc)
          prog->insts[k].val++;
      }
    }
    memset(&prog->insts[start_pc], 0, sizeof(ReInst));
    prog->insts[start_pc].op = RE_SPLIT;
    prog->insts[start_pc].target1 = start_pc + 1;
    int jump_pc = prog->count - 1;
    memset(&prog->insts[jump_pc], 0, sizeof(ReInst));
    prog->insts[jump_pc].op = RE_JUMP;
    prog->insts[start_pc].target2 = prog->count;
    ParseReExpr(pat, prog);
    prog->insts[jump_pc].val = prog->count;
  }
  return 1;
}

static ReProg CompileRegex(const char *pattern) {
  ReProg prog = {NULL, 0, 0, 0};
  ReEmit(&prog, RE_SAVE, 0, 0, 0);
  ParseReExpr(&pattern, &prog);
  ReEmit(&prog, RE_SAVE, 1, 0, 0);
  ReEmit(&prog, RE_MATCH, 0, 0, 0);
  return prog;
}

static void FreeRegex(ReProg *prog) {
  for (int i = 0; i < prog->count; i++) {
    if (prog->insts[i].class_ranges) {
      free(prog->insts[i].class_ranges);
    }
  }
  free(prog->insts);
}

static int MatchNFA(ReInst *program, int pc, const int *input, int input_len,
                    int idx, int *saves, int save_count) {
  while (1) {
    ReInst inst = program[pc];
    switch (inst.op) {
    case RE_CHAR:
      if (idx >= input_len || input[idx] != inst.val)
        return 0;
      idx++;
      pc++;
      break;
    case RE_ANY:
      if (idx >= input_len || input[idx] == '\n')
        return 0;
      idx++;
      pc++;
      break;
    case RE_CLASS: {
      if (idx >= input_len)
        return 0;
      int cp = input[idx];
      int match = 0;
      if (cp < 256) {
        if ((inst.class_bits[cp / 8] & (1 << (cp % 8))) != 0)
          match = 1;
      } else {
        for (int r = 0; r < inst.class_range_count; r++) {
          if (cp >= inst.class_ranges[r].start &&
              cp <= inst.class_ranges[r].end) {
            match = 1;
            break;
          }
        }
      }
      if (inst.class_negated)
        match = !match;
      if (!match)
        return 0;
      idx++;
      pc++;
      break;
    }
    case RE_SPLIT: {
      int stack_saves[64];
      int *old_saves = (save_count <= 64) ? stack_saves : malloc(save_count * sizeof(int));
      memcpy(old_saves, saves, save_count * sizeof(int));
      if (MatchNFA(program, inst.target1, input, input_len, idx, saves,
                   save_count)) {
        if (old_saves != stack_saves) free(old_saves);
        return 1;
      }
      memcpy(saves, old_saves, save_count * sizeof(int));
      if (old_saves != stack_saves) free(old_saves);
      pc = inst.target2;
      break;
    }
    case RE_JUMP:
      pc = inst.val;
      break;
    case RE_SAVE: {
      int old_val = saves[inst.val];
      saves[inst.val] = idx;
      if (MatchNFA(program, pc + 1, input, input_len, idx, saves, save_count)) {
        return 1;
      }
      saves[inst.val] = old_val;
      return 0;
    }
    case RE_MATCH:
      return 1;
    case RE_ANCHOR_START:
      if (idx != 0)
        return 0;
      pc++;
      break;
    case RE_ANCHOR_END:
      if (idx != input_len)
        return 0;
      pc++;
      break;
    }
  }
}

static int *StrToCodepoints(const char *str, int *out_len) {
  int len = (int)strlen(str);
  int *cps = malloc((len + 1) * sizeof(int));
  int count = 0;
  int pos = 0;
  while (pos < len) {
    int consumed = 0;
    int cp =
        Utf8NextCodepoint((const uint8_t *)str + pos, len - pos, &consumed);
    if (consumed <= 0)
      break;
    cps[count++] = cp;
    pos += consumed;
  }
  *out_len = count;
  return cps;
}

static char *CodepointsToStr(const int *cps, int count) {
  size_t cap = 256;
  size_t len = 0;
  char *buf = malloc(cap);
  for (int i = 0; i < count; i++) {
    char temp[4];
    int bytes = Utf8EncodeCodepoint(cps[i], temp);
    while (len + bytes >= cap) {
      cap *= 2;
      buf = realloc(buf, cap);
    }
    memcpy(buf + len, temp, bytes);
    len += bytes;
  }
  buf[len] = '\0';
  return buf;
}

typedef struct {
  int composed;
  int decomposed1;
  int decomposed2;
  int compat1;
  int compat2;
} UnicodeMapping;

static const UnicodeMapping unicode_maps[] = {
    {0xC0, 'A', 0x0300, 0, 0}, {0xC1, 'A', 0x0301, 0, 0},
    {0xC2, 'A', 0x0302, 0, 0}, {0xC3, 'A', 0x0303, 0, 0},
    {0xC4, 'A', 0x0308, 0, 0}, {0xC5, 'A', 0x030A, 0, 0},
    {0xC7, 'C', 0x0327, 0, 0}, {0xC8, 'E', 0x0300, 0, 0},
    {0xC9, 'E', 0x0301, 0, 0}, {0xCA, 'E', 0x0302, 0, 0},
    {0xCB, 'E', 0x0308, 0, 0}, {0xCC, 'I', 0x0300, 0, 0},
    {0xCD, 'I', 0x0301, 0, 0}, {0xCE, 'I', 0x0302, 0, 0},
    {0xCF, 'I', 0x0308, 0, 0}, {0xD1, 'N', 0x0303, 0, 0},
    {0xD2, 'O', 0x0300, 0, 0}, {0xD3, 'O', 0x0301, 0, 0},
    {0xD4, 'O', 0x0302, 0, 0}, {0xD5, 'O', 0x0303, 0, 0},
    {0xD6, 'O', 0x0308, 0, 0}, {0xD9, 'U', 0x0300, 0, 0},
    {0xDA, 'U', 0x0301, 0, 0}, {0xDB, 'U', 0x0302, 0, 0},
    {0xDC, 'U', 0x0308, 0, 0}, {0xDD, 'Y', 0x0301, 0, 0},
    {0xE0, 'a', 0x0300, 0, 0}, {0xE1, 'a', 0x0301, 0, 0},
    {0xE2, 'a', 0x0302, 0, 0}, {0xE3, 'a', 0x0303, 0, 0},
    {0xE4, 'a', 0x0308, 0, 0}, {0xE5, 'a', 0x030A, 0, 0},
    {0xE7, 'c', 0x0327, 0, 0}, {0xE8, 'e', 0x0300, 0, 0},
    {0xE9, 'e', 0x0301, 0, 0}, {0xEA, 'e', 0x0302, 0, 0},
    {0xEB, 'e', 0x0308, 0, 0}, {0xEC, 'i', 0x0300, 0, 0},
    {0xED, 'i', 0x0301, 0, 0}, {0xEE, 'i', 0x0302, 0, 0},
    {0xEF, 'i', 0x0308, 0, 0}, {0xF1, 'n', 0x0303, 0, 0},
    {0xF2, 'o', 0x0300, 0, 0}, {0xF3, 'o', 0x0301, 0, 0},
    {0xF4, 'o', 0x0302, 0, 0}, {0xF5, 'o', 0x0303, 0, 0},
    {0xF6, 'o', 0x0308, 0, 0}, {0xF9, 'u', 0x0300, 0, 0},
    {0xFA, 'u', 0x0301, 0, 0}, {0xFB, 'u', 0x0302, 0, 0},
    {0xFC, 'u', 0x0308, 0, 0}, {0xFD, 'y', 0x0301, 0, 0},
    {0xFF, 'y', 0x0308, 0, 0}, {0xFB01, 'f', 'i', 0, 0},
    {0xFB02, 'f', 'l', 0, 0},  {0xB2, '2', 0, 0, 0},
    {0xB3, '3', 0, 0, 0},      {0xBD, '1', '/', '2', 0},
    {0xBC, '1', '/', '4', 0}};

static int FindDecomposition(int cp, int compat, int *out1, int *out2,
                             int *out3) {
  if (cp >= 0xAC00 && cp <= 0xD7A3) {
    int SIndex = cp - 0xAC00;
    int L = 0x1100 + SIndex / 588;
    int V = 0x1161 + (SIndex % 588) / 28;
    int T = 0x11A7 + SIndex % 28;
    *out1 = L;
    *out2 = V;
    if (SIndex % 28 != 0) {
      *out3 = T;
    } else {
      *out3 = 0;
    }
    return 1;
  }
  for (size_t i = 0; i < sizeof(unicode_maps) / sizeof(unicode_maps[0]); i++) {
    if (unicode_maps[i].composed == cp) {
      if (!compat && (cp == 0xFB01 || cp == 0xFB02 || cp == 0xB2 ||
                      cp == 0xB3 || cp == 0xBD || cp == 0xBC)) {
        continue;
      }
      *out1 = unicode_maps[i].decomposed1;
      *out2 = unicode_maps[i].decomposed2;
      *out3 = unicode_maps[i].compat1;
      return 1;
    }
  }
  return 0;
}

static int FindComposition(int cp1, int cp2) {
  if (cp1 >= 0x1100 && cp1 <= 0x1112 && cp2 >= 0x1161 && cp2 <= 0x1175) {
    int LIndex = cp1 - 0x1100;
    int VIndex = cp2 - 0x1161;
    return 0xAC00 + LIndex * 588 + VIndex * 28;
  }
  if (cp1 >= 0xAC00 && cp1 <= 0xD7A3 && cp2 >= 0x11A7 && cp2 <= 0x11C2) {
    int SIndex = cp1 - 0xAC00;
    if (SIndex % 28 == 0) {
      int TIndex = cp2 - 0x11A7;
      return cp1 + TIndex;
    }
  }
  for (size_t i = 0; i < sizeof(unicode_maps) / sizeof(unicode_maps[0]); i++) {
    int cp = unicode_maps[i].composed;
    if (cp == 0xFB01 || cp == 0xFB02 || cp == 0xB2 || cp == 0xB3 ||
        cp == 0xBD || cp == 0xBC) {
      continue;
    }
    if (unicode_maps[i].decomposed1 == cp1 &&
        unicode_maps[i].decomposed2 == cp2) {
      return unicode_maps[i].composed;
    }
  }
  return 0;
}

static int GetCcc(int cp) {
  if (cp >= 0x0300 && cp <= 0x036F) {
    if (cp == 0x0327)
      return 220;
    return 230;
  }
  return 0;
}

static int *NormalizeDecompose(const int *input, int input_len, int compat,
                               int *out_len) {
  int cap = input_len * 2 + 8;
  int *buf = malloc(cap * sizeof(int));
  int count = 0;
  for (int i = 0; i < input_len; i++) {
    int cp = input[i];
    int d1, d2, d3;
    if (FindDecomposition(cp, compat, &d1, &d2, &d3)) {
      if (count + 3 >= cap) {
        cap *= 2;
        buf = realloc(buf, cap * sizeof(int));
      }
      buf[count++] = d1;
      if (d2)
        buf[count++] = d2;
      if (d3)
        buf[count++] = d3;
    } else {
      if (count >= cap) {
        cap *= 2;
        buf = realloc(buf, cap * sizeof(int));
      }
      buf[count++] = cp;
    }
  }
  for (int i = 1; i < count; i++) {
    int ccc = GetCcc(buf[i]);
    if (ccc > 0) {
      int j = i;
      while (j > 0 && GetCcc(buf[j - 1]) > ccc) {
        int temp = buf[j];
        buf[j] = buf[j - 1];
        buf[j - 1] = temp;
        j--;
      }
    }
  }
  *out_len = count;
  return buf;
}

static int *NormalizeCompose(const int *input, int input_len, int *out_len) {
  int *buf = malloc((input_len + 1) * sizeof(int));
  int count = 0;
  if (input_len == 0) {
    *out_len = 0;
    return buf;
  }
  int starter = input[0];
  int starter_idx = 0;
  buf[count++] = starter;
  for (int i = 1; i < input_len; i++) {
    int cp = input[i];
    int ccc = GetCcc(cp);
    int composed = FindComposition(starter, cp);
    int blocked = 0;
    for (int j = starter_idx + 1; j < count; j++) {
      if (GetCcc(buf[j]) >= ccc) {
        blocked = 1;
        break;
      }
    }
    if (composed && !blocked) {
      starter = composed;
      buf[starter_idx] = starter;
    } else {
      buf[count++] = cp;
      if (ccc == 0) {
        starter = cp;
        starter_idx = count - 1;
      }
    }
  }
  *out_len = count;
  return buf;
}

static char *UnicodeNormalizeFunc(const char *str, const char *form) {
  int len = 0;
  int *cps = StrToCodepoints(str, &len);
  int decomp_len = 0;
  int compat = (strcmp(form, "NFKC") == 0 || strcmp(form, "NFKD") == 0);
  int *decomp = NormalizeDecompose(cps, len, compat, &decomp_len);
  free(cps);
  if (strcmp(form, "NFD") == 0 || strcmp(form, "NFKD") == 0) {
    char *res = CodepointsToStr(decomp, decomp_len);
    free(decomp);
    return res;
  } else {
    int comp_len = 0;
    int *comp = NormalizeCompose(decomp, decomp_len, &comp_len);
    free(decomp);
    char *res = CodepointsToStr(comp, comp_len);
    free(comp);
    return res;
  }
}

static wchar_t *Utf8ToUtf16(const char *str) {
  int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
  wchar_t *wstr = malloc((len + 1) * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len);
  return wstr; // Unicode ASCII-order: ä (U+00E4=228) vs z (U+007A=122) - 228 >
               // in Unicode order
}
static int LocaleCompareFunc(const char *str1, const char *str2,
                             const char *locale) {
  wchar_t *wstr1 = Utf8ToUtf16(str1);
  wchar_t *wstr2 = Utf8ToUtf16(str2);
  wchar_t *wloc = NULL;
  if (locale && locale[0] != '\0') {
    wloc = Utf8ToUtf16(locale);
  }
  int res = CompareStringEx(wloc ? wloc : LOCALE_NAME_USER_DEFAULT, 0, wstr1,
                            -1, wstr2, -1, NULL, NULL, 0);
  free(wstr1);
  free(wstr2);
  if (wloc)
    free(wloc);
  if (res == CSTR_LESS_THAN)
    return -1;
  if (res == CSTR_EQUAL)
    return 0;
  if (res == CSTR_GREATER_THAN)
    return 1;
  return 0;
}

#include <immintrin.h>

static void SimdOpArrays(AKSArray *a1, AKSArray *a2, AKSArray *res_arr,
                         int count, int op) {
  int i = 0;
  for (; i <= count - 4; i += 4) {
    double temp1[4];
    double temp2[4];
    for (int j = 0; j < 4; j++) {
      temp1[j] = ValToFloat(a1->items[i + j]);
      temp2[j] = ValToFloat(a2->items[i + j]);
    }
    __m256d v1 = _mm256_loadu_pd(temp1);
    __m256d v2 = _mm256_loadu_pd(temp2);
    __m256d v_res;
    if (op == 0)
      v_res = _mm256_add_pd(v1, v2);
    else if (op == 1)
      v_res = _mm256_sub_pd(v1, v2);
    else if (op == 2)
      v_res = _mm256_mul_pd(v1, v2);
    else
      v_res = _mm256_div_pd(v1, v2);

    double temp_res[4];
    _mm256_storeu_pd(temp_res, v_res);
    for (int j = 0; j < 4; j++) {
      res_arr->items[i + j] = ValFloat(temp_res[j]);
    }
  }
  for (; i < count; i++) {
    double d1 = ValToFloat(a1->items[i]);
    double d2 = ValToFloat(a2->items[i]);
    double r;
    if (op == 0)
      r = d1 + d2;
    else if (op == 1)
      r = d1 - d2;
    else if (op == 2)
      r = d1 * d2;
    else
      r = (d2 == 0.0) ? 0.0 : d1 / d2;
    res_arr->items[i] = ValFloat(r);
  }
}

static void SimdFmaArrays(AKSArray *a1, AKSArray *a2, AKSArray *a3,
                          AKSArray *res_arr, int count) {
  int i = 0;
  for (; i <= count - 4; i += 4) {
    double temp1[4];
    double temp2[4];
    double temp3[4];
    for (int j = 0; j < 4; j++) {
      temp1[j] = ValToFloat(a1->items[i + j]);
      temp2[j] = ValToFloat(a2->items[i + j]);
      temp3[j] = ValToFloat(a3->items[i + j]);
    }
    __m256d v1 = _mm256_loadu_pd(temp1);
    __m256d v2 = _mm256_loadu_pd(temp2);
    __m256d v3 = _mm256_loadu_pd(temp3);
    __m256d v_res = _mm256_fmadd_pd(v1, v2, v3);

    double temp_res[4];
    _mm256_storeu_pd(temp_res, v_res);
    for (int j = 0; j < 4; j++) {
      res_arr->items[i + j] = ValFloat(temp_res[j]);
    }
  }
  for (; i < count; i++) {
    double d1 = ValToFloat(a1->items[i]);
    double d2 = ValToFloat(a2->items[i]);
    double d3 = ValToFloat(a3->items[i]);
    res_arr->items[i] = ValFloat(d1 * d2 + d3);
  }
}

static void MatrixMulC(int r1, int c1, double *d1, int r2, int c2, double *d2,
                       double *res) {
  memset(res, 0, r1 * c2 * sizeof(double));
  for (int i = 0; i < r1; i++) {
    for (int k = 0; k < c1; k++) {
      double val1 = d1[i * c1 + k];
      int j = 0;
      __m256d v_val1 = _mm256_set1_pd(val1);
      for (; j <= c2 - 4; j += 4) {
        __m256d v_d2 = _mm256_loadu_pd(&d2[k * c2 + j]);
        __m256d v_res = _mm256_loadu_pd(&res[i * c2 + j]);
        v_res = _mm256_fmadd_pd(v_val1, v_d2, v_res);
        _mm256_storeu_pd(&res[i * c2 + j], v_res);
      }
      for (; j < c2; j++) {
        res[i * c2 + j] += val1 * d2[k * c2 + j];
      }
    }
  }
}

static int MatrixInverseC(int n, double *a, double *res) {
  double *temp = malloc(n * n * sizeof(double));
  memcpy(temp, a, n * n * sizeof(double));
  memset(res, 0, n * n * sizeof(double));
  for (int i = 0; i < n; i++)
    res[i * n + i] = 1.0;

  for (int i = 0; i < n; i++) {
    double max_val = fabs(temp[i * n + i]);
    int pivot = i;
    for (int r = i + 1; r < n; r++) {
      if (fabs(temp[r * n + i]) > max_val) {
        max_val = fabs(temp[r * n + i]);
        pivot = r;
      }
    }
    if (max_val < 1e-12) {
      free(temp);
      return 0;
    }
    if (pivot != i) {
      for (int col = 0; col < n; col++) {
        double t = temp[i * n + col];
        temp[i * n + col] = temp[pivot * n + col];
        temp[pivot * n + col] = t;
        t = res[i * n + col];
        res[i * n + col] = res[pivot * n + col];
        res[pivot * n + col] = t;
      }
    }
    double div = temp[i * n + i];
    for (int col = 0; col < n; col++) {
      temp[i * n + col] /= div;
      res[i * n + col] /= div;
    }
    for (int r = 0; r < n; r++) {
      if (r != i) {
        double factor = temp[r * n + i];
        for (int col = 0; col < n; col++) {
          temp[r * n + col] -= factor * temp[i * n + col];
          res[r * n + col] -= factor * res[i * n + col];
        }
      }
    }
  }
  free(temp);
  return 1;
}

static int MatrixLuC(int n, double *a, double *L, double *U, double *P) {
  memset(L, 0, n * n * sizeof(double));
  memset(U, 0, n * n * sizeof(double));
  for (int i = 0; i < n; i++)
    P[i] = i;

  double *temp = malloc(n * n * sizeof(double));
  memcpy(temp, a, n * n * sizeof(double));

  for (int i = 0; i < n; i++) {
    double max_val = fabs(temp[i * n + i]);
    int pivot = i;
    for (int r = i + 1; r < n; r++) {
      if (fabs(temp[r * n + i]) > max_val) {
        max_val = fabs(temp[r * n + i]);
        pivot = r;
      }
    }
    if (pivot != i) {
      double temp_p = P[i];
      P[i] = P[pivot];
      P[pivot] = temp_p;
      for (int col = 0; col < n; col++) {
        double t = temp[i * n + col];
        temp[i * n + col] = temp[pivot * n + col];
        temp[pivot * n + col] = t;
      }
      for (int col = 0; col < i; col++) {
        double t = L[i * n + col];
        L[i * n + col] = L[pivot * n + col];
        L[pivot * n + col] = t;
      }
    }
    L[i * n + i] = 1.0;
    for (int r = i + 1; r < n; r++) {
      double factor = temp[r * n + i] /
                      (fabs(temp[i * n + i]) < 1e-15 ? 1e-15 : temp[i * n + i]);
      L[r * n + i] = factor;
      for (int col = i; col < n; col++) {
        temp[r * n + col] -= factor * temp[i * n + col];
      }
    }
  }
  for (int i = 0; i < n; i++) {
    for (int j = i; j < n; j++) {
      U[i * n + j] = temp[i * n + j];
    }
  }
  free(temp);
  return 1;
}

static int MatrixCholeskyC(int n, double *a, double *L) {
  memset(L, 0, n * n * sizeof(double));
  for (int i = 0; i < n; i++) {
    for (int j = 0; j <= i; j++) {
      double sum = 0;
      for (int k = 0; k < j; k++) {
        sum += L[i * n + k] * L[j * n + k];
      }
      if (i == j) {
        double val = a[i * n + i] - sum;
        if (val <= 0)
          return 0;
        L[i * n + j] = sqrt(val);
      } else {
        L[i * n + j] = (a[i * n + j] - sum) / L[j * n + j];
      }
    }
  }
  return 1;
}

static void FFTReorder(double *real, double *imag, int n) {
  int j = 0;
  for (int i = 0; i < n; i++) {
    if (i < j) {
      double temp = real[i];
      real[i] = real[j];
      real[j] = temp;
      temp = imag[i];
      imag[i] = imag[j];
      imag[j] = temp;
    }
    int bit = n >> 1;
    while (j & bit) {
      j ^= bit;
      bit >>= 1;
    }
    j ^= bit;
  }
}

static void FFTC(double *real, double *imag, int n, int inverse) {
  FFTReorder(real, imag, n);
  for (int len = 2; len <= n; len <<= 1) {
    double angle = 2.0 * 3.141592653589793 / len * (inverse ? 1.0 : -1.0);
    double wlen_real = cos(angle);
    double wlen_imag = sin(angle);
    for (int i = 0; i < n; i += len) {
      double w_real = 1.0;
      double w_imag = 0.0;
      for (int j = 0; j < len / 2; j++) {
        int u_idx = i + j;
        int v_idx = i + j + len / 2;
        double t_real = real[v_idx] * w_real - imag[v_idx] * w_imag;
        double t_imag = real[v_idx] * w_imag + imag[v_idx] * w_real;
        real[v_idx] = real[u_idx] - t_real;
        imag[v_idx] = imag[u_idx] - t_imag;
        real[u_idx] += t_real;
        imag[u_idx] += t_imag;
        double next_w_real = w_real * wlen_real - w_imag * wlen_imag;
        double next_w_imag = w_real * wlen_imag + w_imag * wlen_real;
        w_real = next_w_real;
        w_imag = next_w_imag;
      }
    }
  }
  if (inverse) {
    for (int i = 0; i < n; i++) {
      real[i] /= n;
      imag[i] /= n;
    }
  }
}

static char *BigIntAddC(const char *s1, const char *s2) {
  int len1 = (int)strlen(s1);
  int len2 = (int)strlen(s2);
  int max_len = len1 > len2 ? len1 : len2;
  char *res = malloc(max_len + 2);
  res[max_len + 1] = '\0';
  int carry = 0, idx = max_len;
  int i = len1 - 1, j = len2 - 1;
  while (i >= 0 || j >= 0 || carry) {
    int d1 = i >= 0 ? s1[i--] - '0' : 0;
    int d2 = j >= 0 ? s2[j--] - '0' : 0;
    int sum = d1 + d2 + carry;
    res[idx--] = (sum % 10) + '0';
    carry = sum / 10;
  }
  if (idx == 0) {
    memmove(res, res + 1, max_len + 1);
    return res;
  }
  char *final_res = strdup(res + idx + 1);
  free(res);
  return final_res;
}

static int BigIntCmpC(const char *s1, const char *s2) {
  while (*s1 == '0' && *(s1 + 1))
    s1++;
  while (*s2 == '0' && *(s2 + 1))
    s2++;
  int len1 = (int)strlen(s1);
  int len2 = (int)strlen(s2);
  if (len1 != len2)
    return len1 < len2 ? -1 : 1;
  return strcmp(s1, s2);
}

static char *BigIntSubC(const char *s1, const char *s2) {
  int len1 = (int)strlen(s1);
  int len2 = (int)strlen(s2);
  char *res = malloc(len1 + 1);
  res[len1] = '\0';
  int borrow = 0, idx = len1 - 1;
  int i = len1 - 1, j = len2 - 1;
  while (i >= 0) {
    int d1 = s1[i--] - '0' - borrow;
    int d2 = j >= 0 ? s2[j--] - '0' : 0;
    if (d1 < d2) {
      d1 += 10;
      borrow = 1;
    } else {
      borrow = 0;
    }
    res[idx--] = (d1 - d2) + '0';
  }
  int start = 0;
  while (res[start] == '0' && res[start + 1])
    start++;
  char *final_res = strdup(res + start);
  free(res);
  return final_res;
}

static char *BigIntMulC(const char *s1, const char *s2) {
  int len1 = (int)strlen(s1);
  int len2 = (int)strlen(s2);
  int *temp = calloc(len1 + len2, sizeof(int));
  for (int i = len1 - 1; i >= 0; i--) {
    for (int j = len2 - 1; j >= 0; j--) {
      int mul = (s1[i] - '0') * (s2[j] - '0');
      int sum = mul + temp[i + j + 1];
      temp[i + j + 1] = sum % 10;
      temp[i + j] += sum / 10;
    }
  }
  char *res = malloc(len1 + len2 + 1);
  int idx = 0;
  int i = 0;
  while (i < len1 + len2 && temp[i] == 0)
    i++;
  if (i == len1 + len2) {
    free(temp);
    return strdup("0");
  }
  for (; i < len1 + len2; i++) {
    res[idx++] = temp[i] + '0';
  }
  res[idx] = '\0';
  free(temp);
  return res;
}

static char *BigIntDivC(const char *s1, const char *s2, char **rem) {
  int len1 = (int)strlen(s1);
  if (strcmp(s2, "0") == 0) {
    *rem = strdup("0");
    return strdup("0");
  }
  char *q = malloc(len1 + 1);
  int q_idx = 0;
  char *current = strdup("0");
  for (int i = 0; i < len1; i++) {
    char digit[2] = {s1[i], '\0'};
    char *next_temp = BigIntMulC(current, "10");
    free(current);
    current = BigIntAddC(next_temp, digit);
    free(next_temp);

    int count = 0;
    while (BigIntCmpC(current, s2) >= 0) {
      char *next_sub = BigIntSubC(current, s2);
      free(current);
      current = next_sub;
      count++;
    }
    q[q_idx++] = count + '0';
  }
  q[q_idx] = '\0';
  int start = 0;
  while (q[start] == '0' && q[start + 1])
    start++;
  char *final_q = strdup(q + start);
  free(q);
  *rem = current;
  return final_q;
}

static void StructSetFieldNative(AKSStruct *s, const char *fieldname,
                                 AKSValue val);

#include "vm_stdlib.inc.c"

typedef int (*FastBuiltinFn)(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr);

typedef struct {
  const char *name;
  FastBuiltinFn fn;
} FastBuiltinEntry;

static int FastBuiltin_Length(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  if (ValKind(arg) == VALSTR) {
    *stacktop++ = ValInt(ValToStrPtr(arg)->len);
  } else if (ValKind(arg) == VALARR || ValKind(arg) == VALTUPLE) {
    *stacktop++ = ValInt(ValToArr(arg)->count);
  } else if (ValKind(arg) == VALSTRUCT && strcmp(ValToObj(arg)->typename, "Range") == 0) {
    AKSStruct *s = ValToObj(arg);
    int64_t start = 0, end = 0;
    for (int i = 0; i < s->fieldcount; i++) {
      if (strcmp(s->fieldnames[i], "start") == 0) start = ValToInt(s->fieldvals[i]);
      else if (strcmp(s->fieldnames[i], "end") == 0) end = ValToInt(s->fieldvals[i]);
    }
    *stacktop++ = ValInt(end - start);
  } else {
    *stacktop++ = ValInt(0);
  }
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_ByteAt(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc < 1 || argc > 2) return -1;
  AKSValue *stacktop = *stacktopptr;
  int64_t idx = 0;
  if (argc == 2) {
    AKSValue idx_val = StdlibPop(&stacktop);
    if (!ValIsInt(idx_val)) {
      ValFree(&idx_val);
      *stacktopptr = stacktop;
      return -1;
    }
    idx = ValToInt(idx_val);
  }
  AKSValue arg = StdlibPop(&stacktop);
  if (ValKind(arg) != VALSTR) {
    ValFree(&arg);
    *stacktopptr = stacktop;
    return -1;
  }
  AKSString *s = ValToStrPtr(arg);
  int code = -1;
  if (s && s->data && idx >= 0 && idx < s->len) {
    code = (unsigned char)s->data[idx];
  }
  *stacktop++ = ValInt(code);
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_HashCode(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  uint64_t h = 0;
  ValType t = ValKind(arg);
  switch (t) {
    case VALINT: {
      uint64_t x = (uint64_t)ValToInt(arg);
      x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
      x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
      x ^= (x >> 31);
      h = x & 0x7FFFFFFFFFFFFFFFULL;
      break;
    }
    case VALFLOAT: {
      union { double d; uint64_t u; } u;
      u.d = ValToFloat(arg);
      if (u.d == 0.0) u.u = 0;
      uint64_t x = u.u;
      x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
      x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
      x ^= (x >> 31);
      h = x & 0x7FFFFFFFFFFFFFFFULL;
      break;
    }
    case VALBOOL:
      h = ValToBool(arg) ? 1231 : 1237;
      break;
    case VALNULL:
      h = 0;
      break;
    case VALSTR: {
      AKSString *s = ValToStrPtr(arg);
      if (s && s->data) {
        uint64_t x = 14695981039346656037ULL;
        for (int i = 0; i < s->len; i++) {
          x ^= (uint8_t)s->data[i];
          x *= 1099511628211ULL;
        }
        h = x & 0x7FFFFFFFFFFFFFFFULL;
      }
      break;
    }
    default: {
      uintptr_t ptr = (uintptr_t)(arg & 0x0000FFFFFFFFFFFFULL);
      uint64_t x = (uint64_t)ptr;
      x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
      x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
      x ^= (x >> 31);
      h = x & 0x7FFFFFFFFFFFFFFFULL;
      break;
    }
  }
  *stacktop++ = ValInt((int64_t)h);
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_SquareRoot(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  double vald = ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
  *stacktop++ = ValFloat(sqrt(vald));
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_Absolute(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  if (ValKind(arg) == VALINT) {
    int64_t v = ValToInt(arg);
    *stacktop++ = ValInt(v < 0 ? -v : v);
  } else if (ValKind(arg) == VALFLOAT) {
    double v = ValToFloat(arg);
    *stacktop++ = ValFloat(v < 0 ? -v : v);
  } else {
    *stacktop++ = ValCopy(arg);
  }
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_String(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  *stacktop++ = ValToStr(arg);
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_clock(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 0) return -1;
  AKSValue *stacktop = *stacktopptr;
  double secs = (double)clock() / CLOCKS_PER_SEC;
  *stacktop++ = ValFloat(secs);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_sin(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  double v = ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
  *stacktop++ = ValFloat(sin(v));
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_cos(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  double v = ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
  *stacktop++ = ValFloat(cos(v));
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_tan(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  double v = ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
  *stacktop++ = ValFloat(tan(v));
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_log(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  double v = ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
  *stacktop++ = ValFloat(log(v));
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_floor(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  double v = ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
  *stacktop++ = ValInt((int64_t)floor(v));
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_Ceiling(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 1) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue arg = StdlibPop(&stacktop);
  double v = ValKind(arg) == VALFLOAT ? ValToFloat(arg) : (double)ValToInt(arg);
  *stacktop++ = ValInt((int64_t)ceil(v));
  ValFree(&arg);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_Minimum(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 2) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue b = StdlibPop(&stacktop);
  AKSValue a = StdlibPop(&stacktop);
  if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
    int64_t va = ValToInt(a), vb = ValToInt(b);
    *stacktop++ = ValInt(va < vb ? va : vb);
  } else {
    double va = ValKind(a) == VALFLOAT ? ValToFloat(a) : (double)ValToInt(a);
    double vb = ValKind(b) == VALFLOAT ? ValToFloat(b) : (double)ValToInt(b);
    *stacktop++ = ValFloat(va < vb ? va : vb);
  }
  ValFree(&b);
  ValFree(&a);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_Maximum(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 2) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue b = StdlibPop(&stacktop);
  AKSValue a = StdlibPop(&stacktop);
  if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
    int64_t va = ValToInt(a), vb = ValToInt(b);
    *stacktop++ = ValInt(va > vb ? va : vb);
  } else {
    double va = ValKind(a) == VALFLOAT ? ValToFloat(a) : (double)ValToInt(a);
    double vb = ValKind(b) == VALFLOAT ? ValToFloat(b) : (double)ValToInt(b);
    *stacktop++ = ValFloat(va > vb ? va : vb);
  }
  ValFree(&b);
  ValFree(&a);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_Power(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 2) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue b = StdlibPop(&stacktop);
  AKSValue a = StdlibPop(&stacktop);
  double va = ValKind(a) == VALFLOAT ? ValToFloat(a) : (double)ValToInt(a);
  double vb = ValKind(b) == VALFLOAT ? ValToFloat(b) : (double)ValToInt(b);
  *stacktop++ = ValFloat(pow(va, vb));
  ValFree(&b);
  ValFree(&a);
  *stacktopptr = stacktop;
  return 1;
}

static int FastBuiltin_slice(VM *vm, VMTask *task, int argc, AKSValue **stacktopptr) {
  (void)vm; (void)task;
  if (argc != 3) return -1;
  AKSValue *stacktop = *stacktopptr;
  AKSValue endval = StdlibPop(&stacktop);
  AKSValue startval = StdlibPop(&stacktop);
  AKSValue arr = StdlibPop(&stacktop);
  if ((ValKind(arr) != VALARR && ValKind(arr) != VALTUPLE && ValKind(arr) != VALSTR) ||
      ValKind(startval) != VALINT || ValKind(endval) != VALINT) {
    ValFree(&endval);
    ValFree(&startval);
    ValFree(&arr);
    return -1;
  }
  int64_t start = ValToInt(startval);
  int64_t end = ValToInt(endval);
  int64_t count = (ValKind(arr) == VALSTR) ? (int64_t)ValToStrPtr(arr)->len : (int64_t)ValToArr(arr)->count;

  if (start < 0) start += count;
  if (start < 0) start = 0;
  if (start > count) start = count;

  if (end >= 2147483647LL) {
    end = count;
  } else if (end < 0) {
    end += count;
  }
  if (end < 0) end = 0;
  if (end > count) end = count;
  if (end < start) end = start;

  int slicelen = (int)(end - start);
  if (ValKind(arr) == VALSTR) {
    const char *s = ValToStrPtr(arr)->data;
    *stacktop++ = ValStrLen(s + start, slicelen);
  } else {
    AKSValue res = ValArr(slicelen);
    ValToArr(res)->count = slicelen;
    for (int i = 0; i < slicelen; i++) {
      ValToArr(res)->items[i] = ValCopy(ValToArr(arr)->items[start + i]);
    }
    *stacktop++ = res;
  }
  ValFree(&endval);
  ValFree(&startval);
  ValFree(&arr);
  *stacktopptr = stacktop;
  return 1;
}

static const FastBuiltinEntry g_fast_builtins[] = {
  {"Length", FastBuiltin_Length},
  {"ByteAt", FastBuiltin_ByteAt},
  {"Ordinal", FastBuiltin_ByteAt},
  {"HashCode", FastBuiltin_HashCode},
  {"SquareRoot", FastBuiltin_SquareRoot},
  {"Absolute", FastBuiltin_Absolute},
  {"String", FastBuiltin_String},
  {"clock", FastBuiltin_clock},
  {"sin", FastBuiltin_sin},
  {"cos", FastBuiltin_cos},
  {"tan", FastBuiltin_tan},
  {"log", FastBuiltin_log},
  {"floor", FastBuiltin_floor},
  {"Ceiling", FastBuiltin_Ceiling},
  {"ceil", FastBuiltin_Ceiling},
  {"Minimum", FastBuiltin_Minimum},
  {"Maximum", FastBuiltin_Maximum},
  {"Power", FastBuiltin_Power},
  {"slice", FastBuiltin_slice},
  {NULL, NULL}
};

static FastBuiltinFn FindFastBuiltin(const char *name) {
  if (!name) return NULL;
  for (int i = 0; g_fast_builtins[i].name != NULL; i++) {
    if (strcmp(g_fast_builtins[i].name, name) == 0) {
      return g_fast_builtins[i].fn;
    }
  }
  return NULL;
}

static int DispatchFastOrStdlib(VM *vm, VMTask *task, FnProto *callee, const char *name, int argc, AKSValue **stacktopptr) {
  if (callee) {
    if (callee->fast_handler) {
      return ((FastBuiltinFn)callee->fast_handler)(vm, task, argc, stacktopptr);
    }
    FastBuiltinFn fn = FindFastBuiltin(callee->name);
    if (fn) {
      callee->fast_handler = (void*)fn;
      return fn(vm, task, argc, stacktopptr);
    }
    return ExecuteStdlib(vm, task, callee->name, argc, stacktopptr);
  }
  if (name) {
    FastBuiltinFn fn = FindFastBuiltin(name);
    if (fn) {
      return fn(vm, task, argc, stacktopptr);
    }
    return ExecuteStdlib(vm, task, name, argc, stacktopptr);
  }
  return 0;
}

#include "vm_jit.inc.c"


static int ChkopOverload(VM *vm, VMTask *task, uint8_t **ipptr,
                         AKSValue **stacktopptr, CallFrame **frameptr,
                         AKSValue **frameslotsptr, const char *opname) {
  AKSValue *stacktop = *stacktopptr;
  if (stacktop - task->stack < 2)
    return 0;
  AKSValue b = stacktop[-1];
  AKSValue a = stacktop[-2];
  if (ValKind(a) == VALSTRUCT && ValToObj(a)) {
    int ovidx = -1;
    for (int i = 0; i < ValToObj(a)->fieldcount; i++) {
      if (strcmp(ValToObj(a)->fieldnames[i], opname) == 0) {
        ovidx = i;
        break;
      }
    }
    if (ovidx >= 0 &&
        (ValKind(ValToObj(a)->fieldvals[ovidx]) == VALFN ||
         ValKind(ValToObj(a)->fieldvals[ovidx]) == VALBOUNDMETHOD)) {
      AKSValue valb = *--stacktop;
      AKSValue vala = *--stacktop;
      *stacktop++ = vala;
      *stacktop++ = valb;
      AKSValue callee = ValCopy(ValToObj(a)->fieldvals[ovidx]);
      if (ValKind(callee) == VALFN) {
        FnProto *calleefn = vm->fns[ValToFn(callee)];
        task->stacktop = (int)(stacktop - task->stack);
        int res = DispatchFastOrStdlib(vm, task, calleefn, calleefn->name, 2, &stacktop);
        if (res == 0) {
          if (task->framecount >= MAXFRAMES) {
            ValFree(&callee);
            RaiseEx(vm, task, "stack overflow", ipptr, &stacktop, frameptr,
                    frameslotsptr);
            *stacktopptr = stacktop;
            return 1;
          }
          if (stacktop >= task->stack + MAXSTACK) {
            ValFree(&callee);
            RaiseEx(vm, task, "stack overflow", ipptr, &stacktop, frameptr,
                    frameslotsptr);
            *stacktopptr = stacktop;
            return 1;
          }
          AKSValue *canarypos = stacktop - 2;
          for (AKSValue *p = stacktop; p > canarypos; p--) {
            *p = *(p - 1);
          }
          *canarypos = ValInt(vm->stackcanary);
          stacktop++;

          (*frameptr)->ip = *ipptr;
          /* Inline FrameEnsure: ChkopOverload cannot use the RunTaskSlice macro */
          if (task->framecount >= task->framecap) {
            int _fi = (int)(*frameptr - task->frames);
            FrameEnsure(task, task->framecount);
            *frameptr = task->frames + _fi;
            *frameslotsptr = task->stack + (*frameptr)->slotsbase;
          }
          CallFrame *newframe = &task->frames[task->framecount++];
          newframe->fn = calleefn;
          newframe->ip = calleefn->chunk.code;
          newframe->slotsbase = (int)(stacktop - task->stack) - 2;
          newframe->argc = 2;
          *frameptr = newframe;
          *ipptr = newframe->ip;
          *frameslotsptr = task->stack + newframe->slotsbase;
        }
      }
      ValFree(&callee);
      *stacktopptr = stacktop;
      return 1;
    }
  }
  return 0;
}

#if defined(_MSC_VER)
#pragma optimize("", off)
#elif defined(__clang__)
#pragma clang optimize off
#elif defined(__GNUC__)
#pragma GCC optimize("O0")
#endif
#pragma warning(push)
#pragma warning(disable : 4065)
int RunTaskSlice(VM *vm, VMTask *task, int maxinstructions) {
  CallFrame *frame = &task->frames[task->framecount - 1];
  uint8_t *ip = frame->ip;
  AKSValue *stacktop = task->stack + task->stacktop;
  AKSValue *frameslots = task->stack + frame->slotsbase;
  AKSValue *stacklimit = task->stack + task->stackcap; /* tracks actual capacity, not MAXSTACK */
  int instructionsrun = 0;
  int lastframecount = task->framecount;
  /* Cache frequently-read VM flags as locals so the compiler can keep them
   * in registers throughout the dispatch loop rather than reloading from
   * the VM struct pointer on every instruction. */
  const int strict_ovf = vm->strict_ovf;
  const int no_bounds_check = vm->no_bounds_check;

  /* Growable frame-array: save/restore frame ptr around FrameEnsure. */
#define FRAME_GROW() do { \
    if (task->framecount >= task->framecap) { \
        int _fi = (int)(frame - task->frames); \
        FrameEnsure(task, task->framecount); \
        frame = task->frames + _fi; \
        frameslots = task->stack + frame->slotsbase; \
    } \
  } while(0)
#define VM_ENTER_FRAME() do { \
    if (task->framecount - 1 >= 0 && task->framecount - 1 < MAXFRAMES) { \
        task->arenatops[task->framecount - 1] = task->stackarenatop; \
    } \
    lastframecount = task->framecount; \
} while(0)
#define VM_EXIT_FRAME() do { \
    if (task->framecount >= 0 && task->framecount < MAXFRAMES) { \
        task->stackarenatop = task->arenatops[task->framecount]; \
    } \
    lastframecount = task->framecount; \
} while(0)
  /* Growable stack-array: save/restore stacktop+frameslots around StackEnsure. */
#define STACK_GROW(needed) do { \
    if ((int)(needed) >= task->stackcap) { \
        ptrdiff_t _so = stacktop - task->stack; \
        ptrdiff_t _fo = frameslots - task->stack; \
        StackEnsure(task, (int)(needed) + 1); \
        stacktop  = task->stack + _so; \
        frameslots = task->stack + _fo; \
        stacklimit = task->stack + task->stackcap; \
    } \
  } while(0)

  // shorthands used all over the dispatch loop below
#define RaiseX(vmptr, msg, ipptr, stptr, frptr, fsptr)                         \
  do {                                                                         \
    const char *actual_msg = msg;                                              \
    if (strcmp(actual_msg, "Standard library call error") == 0 &&              \
        gStdlibErrorMessage[0]) {                                           \
      actual_msg = gStdlibErrorMessage;                                     \
    }                                                                          \
    RaiseEx(vmptr, task, actual_msg, ipptr, stptr, frptr, fsptr);              \
    if (gStdlibErrorMessage[0])                                             \
      gStdlibErrorMessage[0] = '\0';                                        \
    if (task->state != 3) {                                                    \
      if (frame->fn->regcode) {                                                \
        int offset = (int)(ip - frame->fn->chunk.code);                        \
        int mapidx = (offset >= 0 && offset < frame->fn->chunk.count &&        \
                      frame->fn->offsettomap)                                  \
                         ? frame->fn->offsettomap[offset]                      \
                         : -1;                                                 \
        if (mapidx < 0 && frame->fn->offsettomap) {                            \
          for (int scan = offset; scan < frame->fn->chunk.count; scan++) {     \
            if (frame->fn->offsettomap[scan] >= 0) {                           \
              mapidx = frame->fn->offsettomap[scan];                           \
              break;                                                           \
            }                                                                  \
          }                                                                    \
        }                                                                      \
        if (mapidx < 0)                                                        \
          mapidx = 0;                                                          \
        RegInst *target_inst = (RegInst *)frame->fn->regcode + mapidx;         \
        if (target_inst &&                                                     \
            (target_inst->op == regopcopy || target_inst->op == regopstore)) { \
          AKSValue exval = *--stacktop;                                        \
          ValFree(&frameslots[target_inst->dst]);                              \
          frameslots[target_inst->dst] = exval;                                \
          regip = target_inst + 1;                                             \
        } else {                                                               \
          regip = target_inst;                                                 \
        }                                                                      \
        goto enter_register_interpreter_loop;                                  \
      } else {                                                                 \
        goto enter_stack_interpreter;                                          \
      }                                                                        \
    }                                                                          \
  } while (0)
#define ExecuteStdlib(vmptr, name, argc, stptr)                                \
  DispatchFastOrStdlib(vmptr, task, NULL, name, argc, stptr)
#define DispatchStdlib(vmptr, callee, name, argc, stptr)                      \
  DispatchFastOrStdlib(vmptr, task, callee, name, argc, stptr)
#define READBYTE() (*ip++)
#define READU16() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READI32()                                                              \
  (ip += 4, (int32_t)((ip[-4] << 24) | (ip[-3] << 16) | (ip[-2] << 8) | ip[-1]))
#define PUSH(val)                                                              \
  do {                                                                         \
    if (stacktop >= stacklimit) {                                              \
      ptrdiff_t _so = stacktop - task->stack;                                  \
      ptrdiff_t _fo = frameslots - task->stack;                                \
      StackEnsure(task, (int)_so + 1);                                         \
      stacktop  = task->stack + _so;                                           \
      frameslots = task->stack + _fo;                                          \
      stacklimit = task->stack + task->stackcap;                               \
    }                                                                          \
    *stacktop++ = (val);                                                       \
  } while (0)
#define POP() (*--stacktop)
#define PEEK() (*(stacktop - 1))

  RegInst *regip = NULL;
  RegInst *curr_inst = NULL;
  if (frame->fn->regcode && vm->jitmode == 0) {
    goto enter_register_interpreter;
  }

  goto enter_stack_interpreter;

enter_register_interpreter:
  if (task->blockedonstdlib) {
    stacktop = task->stack + task->stacktop;
  } else {
    stacktop = task->stack + frame->slotsbase + frame->fn->maxregisters;
  }
  int offset = (int)(ip - frame->fn->chunk.code);
  int regoffset = frame->fn->offsettomap[offset];
  regip = (RegInst *)frame->fn->regcode + regoffset;

#if defined(__GNUC__) || defined(__clang__)
  static void *reg_dispatch_table[] = {
      &&do_regophalt,      &&do_regoppushconst,   &&do_regopload,
      &&do_regopstore,     &&do_regoploadglobal,  &&do_regopstoreglobal,
      &&do_regopadd,       &&do_regopsub,         &&do_regopmul,
      &&do_regopdiv,       &&do_regopmod,         &&do_regopneg,
      &&do_regoplt,        &&do_regopgt,          &&do_regoplte,
      &&do_regopgte,       &&do_regopeq,          &&do_regopneq,
      &&do_regopand,       &&do_regopor,          &&do_regopnot,
      &&do_regopjmp,       &&do_regopjmpiffalse,  &&do_regopjmpifnull,
      &&do_regopcall,      &&do_regopcalldynamic, &&do_regopret,
      &&do_regopretval,    &&do_regopnewarr,      &&do_regopnewtuple,
      &&do_regoparrget,    &&do_regoparrset,      &&do_regopnewstruct,
      &&do_regopgetfield,  &&do_regopsetfield,    &&do_regopinc,
      &&do_regopdec,       &&do_regoppushfn,      &&do_regoptrypush,
      &&do_regoptrypop,    &&do_regopthrow,       &&do_regopcoalesce,
      &&do_regoprange,     &&do_regoptailcall,    &&do_regopspawn,
      &&do_regopawait,     &&do_regopshl,         &&do_regopshr,
      &&do_regopbitand,    &&do_regopbitor,       &&do_regopbitxor,
      &&do_regopbitnot,    &&do_regopapply,       &&do_regoprestargs,
      &&do_regopaddreg,    &&do_regopaddint,      &&do_regopcopy,
      &&do_regopwriteln,   &&do_regopinputstr,    &&do_regopprintrepl,
      &&do_regoptypeof,    &&do_regopclonestruct, &&do_regopcastint,
      &&do_regopcastfloat, &&do_regopcastbool,    &&do_regopcaststr};
  curr_inst = regip++;
  goto *reg_dispatch_table[curr_inst->op];
}
while (0)
#else
#define REG_DISPATCH() goto reg_dispatch_switch;
#endif

#if defined(__GNUC__) || defined(__clang__)
#define REG_DISPATCH_OR_BREAK() REG_DISPATCH()
#else
#define REG_DISPATCH_OR_BREAK() break
#endif

/* Fast dispatch: bypass the outer for(;;) preamble for pure computation ops
 * that cannot raise exceptions, modify task state, or change frame count.
 * Skips all loop preamble checks for maximum throughput. */
#if defined(__GNUC__) || defined(__clang__)
#define REG_DISPATCH_FAST() do { \
    instructionsrun++; \
    curr_inst = regip++; \
    goto *reg_dispatch_table[curr_inst->op]; \
} while(0)
#else
#define REG_DISPATCH_FAST() do { \
    instructionsrun++; \
    goto reg_dispatch_switch; \
} while(0)
#endif

  enter_register_interpreter_loop: for (;;) {
    while (task->framecount > lastframecount) {
      if (lastframecount >= 0 && lastframecount < MAXFRAMES) {
        task->arenatops[lastframecount] = task->stackarenatop;
      }
      lastframecount++;
    }
    while (task->framecount < lastframecount) {
      lastframecount--;
      if (lastframecount >= 0 && lastframecount < MAXFRAMES) {
        task->stackarenatop = task->arenatops[lastframecount];
      }
    }
    if (task->state == 3) {
      ip = frame->fn->chunk.code +
           GetBytecodeOffset(frame->fn,
                             (int)(regip - (RegInst *)frame->fn->regcode));
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      return -1;
    }
    if (task->cancelled) {
      task->cancelled = 0;
      ip = frame->fn->chunk.code +
           GetBytecodeOffset(frame->fn,
                             (int)(regip - (RegInst *)frame->fn->regcode));
      RaiseX(vm, "CancelledError: task was cancelled", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    }
    if (instructionsrun >= maxinstructions) {
      ip = frame->fn->chunk.code +
           GetBytecodeOffset(frame->fn,
                             (int)(regip - (RegInst *)frame->fn->regcode));
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      return 0;
    }
    instructionsrun++;
    vm->instructioncount++;
    gVMProfiling.total_instructions++;

    if (vm->sandboxenabled) {
      task->totalinstructionsrun++;
      if (task->totalinstructionsrun > 5000000) {
        ip = frame->fn->chunk.code +
             GetBytecodeOffset(frame->fn,
                               (int)(regip - (RegInst *)frame->fn->regcode));
        RaiseX(vm, "SandboxError: execution instruction limit exceeded", &ip,
               &stacktop, &frame, &frameslots);
        frame->ip = ip;
        task->stacktop = (int)(stacktop - task->stack);
        return -1;
      }
    }

    if (gCoverageEnabled && frame->fn->chunk.executed) {
      int currentoffset = (int)(ip - frame->fn->chunk.code);
      if (regip) {
        currentoffset = regip->bytecodeoffset;
      }
      frame->fn->chunk.executed[currentoffset] = 1;
    }

    if (GDapActive) {
      int currentoffset = (int)(ip - frame->fn->chunk.code);
      if (regip) {
        currentoffset = regip->bytecodeoffset;
      }
      frame->ip = frame->fn->chunk.code + currentoffset;
      task->stacktop = (int)(stacktop - task->stack);
      DAPCheckDebugger(vm, task, frame, currentoffset);
      ip = frame->ip;
      int newoffset = (int)(ip - frame->fn->chunk.code);
      int newregoffset = -1;
      if (frame->fn->offsettomap) {
        for (int scan = newoffset; scan < frame->fn->chunk.count; scan++) {
          if (frame->fn->offsettomap[scan] >= 0) {
            newregoffset = frame->fn->offsettomap[scan];
            break;
          }
        }
      }
      if (newregoffset >= 0 && frame->fn->regcode) {
        regip = (RegInst *)frame->fn->regcode + newregoffset;
      }
      stacktop = task->stack + task->stacktop;
      frameslots = task->stack + frame->slotsbase;
    }

    if (!frame->fn->regcode) {
      ip = frame->ip;
      goto enter_stack_interpreter;
    }

    if (vm->profilingmode) {
      RegInst temp_inst = *regip;
      uint8_t orig_op = frame->fn->chunk.code[temp_inst.bytecodeoffset];
      vm->instructioncounters[orig_op]++;
    }

    REG_DISPATCH();

#if !defined(__GNUC__) && !defined(__clang__)
  reg_dispatch_switch:
    curr_inst = regip++;
    switch (curr_inst->op) {
#endif

#if defined(__GNUC__) || defined(__clang__)
    do_regophalt: {
#else
  case regophalt: {
#endif
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (task->framecount > 1) {
        int canaryidx = frame->slotsbase - 1;
        if (canaryidx < 0 || ValKind(task->stack[canaryidx]) != VALINT ||
            ValToInt(task->stack[canaryidx]) != (int64_t)vm->stackcanary) {
          fprintf(stderr, "SecurityError: Stack canary corruption detected\n");
          exit(1);
        }
        for (int i = 0; i < frame->fn->maxregisters; i++) {
          ValFree(&frameslots[i]);
        }
        ValFree(&task->stack[canaryidx]);
        task->framecount--;
        CallFrame *prevframe = &task->frames[task->framecount - 1];
        stacktop = task->stack + frame->slotsbase - 1;
        frame = prevframe;
        frameslots = task->stack + frame->slotsbase;
        if (frame->fn->regcode) {
          int bytecodeoffset = (int)(frame->ip - frame->fn->chunk.code);
          int regoffset = frame->fn->offsettomap[bytecodeoffset];
          regip = (RegInst *)frame->fn->regcode + regoffset + 1;
          RegInst *callinst = (RegInst *)frame->fn->regcode + regoffset;
          ValFree(&frameslots[callinst->dst]);
          frameslots[callinst->dst] = ValNull();
        } else {
          ip = frame->ip;
          PUSH(ValNull());
        }
      } else {
        for (int i = 0; i < frame->fn->maxregisters; i++) {
          ValFree(&frameslots[i]);
        }
        task->stacktop = frame->slotsbase;
        task->result = ValNull();
        return 1;
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoppushconst: {
#else
  case regoppushconst: {
#endif
      AKSValue cval = curr_inst->constval;
      if (ValIsPrimitive(cval)) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = cval;
      } else {
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValCopy(cval);
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopload: {
#else
  case regopload: {
#endif
      if (curr_inst->dst == curr_inst->src1) {
        REG_DISPATCH_FAST();
      }
      AKSValue src = frameslots[curr_inst->src1];
      if (ValIsPrimitive(frameslots[curr_inst->dst]) & ValIsPrimitive(src)) {
        frameslots[curr_inst->dst] = src;
      } else {
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValCopy(src);
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopstore: {
#else
  case regopstore: {
#endif
      if (curr_inst->dst == curr_inst->src1) {
        REG_DISPATCH_FAST();
      }
      AKSValue src = frameslots[curr_inst->src1];
      if (ValIsPrimitive(frameslots[curr_inst->dst]) & ValIsPrimitive(src)) {
        frameslots[curr_inst->dst] = src;
      } else {
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValCopy(src);
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoploadglobal: {
#else
  case regoploadglobal: {
#endif
      const char *name = vm->strpool[curr_inst->src1];
      int found = -1;
      int ipoffset = curr_inst->bytecodeoffset;
      if (frame->fn->chunk.globalcache) {
        int cachedidx = frame->fn->chunk.globalcache[ipoffset];
        if (cachedidx >= 0 && cachedidx < vm->globalcount &&
            (vm->globals[cachedidx].name == name || strcmp(vm->globals[cachedidx].name, name) == 0)) {
          found = cachedidx;
        }
      }
      if (found < 0) {
        for (int i = 0; i < vm->globalcount; i++) {
          if (vm->globals[i].name == name || strcmp(vm->globals[i].name, name) == 0) {
            found = i;
            break;
          }
        }
        if (found >= 0 && frame->fn->chunk.globalcache) {
          frame->fn->chunk.globalcache[ipoffset] = found;
        }
      }
      ValFree(&frameslots[curr_inst->dst]);
      if (found >= 0) {
        frameslots[curr_inst->dst] = ValCopy(vm->globals[found].val);
      } else {
        frameslots[curr_inst->dst] = ValNull();
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopstoreglobal: {
#else
  case regopstoreglobal: {
#endif
      const char *name = vm->strpool[curr_inst->src2];
      AKSValue val = ValCopy(frameslots[curr_inst->src1]);
      int found = -1;
      int ipoffset = curr_inst->bytecodeoffset;
      if (frame->fn->chunk.globalcache) {
        int cachedidx = frame->fn->chunk.globalcache[ipoffset];
        if (cachedidx >= 0 && cachedidx < vm->globalcount &&
            (vm->globals[cachedidx].name == name || strcmp(vm->globals[cachedidx].name, name) == 0)) {
          found = cachedidx;
        }
      }
      if (found < 0) {
        for (int i = 0; i < vm->globalcount; i++) {
          if (vm->globals[i].name == name || strcmp(vm->globals[i].name, name) == 0) {
            found = i;
            break;
          }
        }
        if (found >= 0 && frame->fn->chunk.globalcache) {
          frame->fn->chunk.globalcache[ipoffset] = found;
        }
      }
      if (found >= 0) {
        ValFree(&vm->globals[found].val);
        vm->globals[found].val = val;
      } else {
        if (vm->globalcount < MAXGLOBALS) {
          vm->globals[vm->globalcount].name = strdup(name);
          vm->globals[vm->globalcount].val = val;
          vm->globalcount++;
        } else {
          fprintf(stderr, "Runtime error: global variable capacity exceeded (%d)\n", MAXGLOBALS);
          ValFree(&val);
          return 0;
        }
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopadd: {
#else
  case regopadd: {
#endif
      /* Fast path: int+int without overflow — no ip saves, no preamble */
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsInt(a) & ValIsInt(b)) {
        int64_t r;
        if (!ChkAddOvf(ValToIntUnchecked(a), ValToIntUnchecked(b), &r)) {
          AKSValue res = ValInt(r);
          if (!ValIsPrimitive(frameslots[curr_inst->dst]))
            ValFreeFunc(&frameslots[curr_inst->dst]);
          else
            vm->static_rc_eliminated++;
          frameslots[curr_inst->dst] = res;
          REG_DISPATCH_FAST();
        }
      }
      /* Slow path: struct overloads, float ops, string concat, or overflow */
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(a) == VALSTRUCT &&
          RegCheckOverload(vm, task, "opadd", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots)) {
        continue;
      }
      AKSValue res;
      if (ValIsInt(a) && ValIsInt(b)) {
        /* Overflow confirmed by fast-path check */
        if (strict_ovf) {
          RaiseX(vm, "IntegerOverflowException: addition overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        }
        res = ValFloat((double)ValToIntUnchecked(a) + (double)ValToIntUnchecked(b));
      } else if (ValIsFloat(a) && ValIsFloat(b)) {
        res = ValFloat(ValToFloat(a) + ValToFloat(b));
      } else if (ValIsFloat(a) && ValIsInt(b)) {
        res = ValFloat(ValToFloat(a) + (double)ValToIntUnchecked(b));
      } else if (ValIsInt(a) && ValIsFloat(b)) {
        res = ValFloat((double)ValToIntUnchecked(a) + ValToFloat(b));
      } else {
        res = ValAdd(a, b);
      }
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      } else {
        vm->static_rc_eliminated++;
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopsub: {
#else
  case regopsub: {
#endif
      /* Fast path: int-int without overflow */
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsInt(a) & ValIsInt(b)) {
        int64_t r;
        if (!ChkSubOvf(ValToIntUnchecked(a), ValToIntUnchecked(b), &r)) {
          AKSValue res = ValInt(r);
          if (!ValIsPrimitive(frameslots[curr_inst->dst]))
            ValFreeFunc(&frameslots[curr_inst->dst]);
          else
            vm->static_rc_eliminated++;
          frameslots[curr_inst->dst] = res;
          REG_DISPATCH_FAST();
        }
      }
      /* Slow path */
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(a) == VALSTRUCT &&
          RegCheckOverload(vm, task, "opsub", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      AKSValue res;
      if (ValIsInt(a) && ValIsInt(b)) {
        if (strict_ovf) {
          RaiseX(vm, "IntegerOverflowException: subtraction overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        }
        res = ValFloat((double)ValToIntUnchecked(a) - (double)ValToIntUnchecked(b));
      } else if (ValIsFloat(a) && ValIsFloat(b)) {
        res = ValFloat(ValToFloat(a) - ValToFloat(b));
      } else if (ValIsFloat(a) && ValIsInt(b)) {
        res = ValFloat(ValToFloat(a) - (double)ValToIntUnchecked(b));
      } else if (ValIsInt(a) && ValIsFloat(b)) {
        res = ValFloat((double)ValToIntUnchecked(a) - ValToFloat(b));
      } else {
        res = ValSub(a, b);
      }
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      } else {
        vm->static_rc_eliminated++;
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopmul: {
#else
  case regopmul: {
#endif
      /* Fast path: int*int without overflow */
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsInt(a) & ValIsInt(b)) {
        int64_t r;
        if (!ChkMulOvf(ValToIntUnchecked(a), ValToIntUnchecked(b), &r)) {
          AKSValue res = ValInt(r);
          if (!ValIsPrimitive(frameslots[curr_inst->dst]))
            ValFreeFunc(&frameslots[curr_inst->dst]);
          else
            vm->static_rc_eliminated++;
          frameslots[curr_inst->dst] = res;
          REG_DISPATCH_FAST();
        }
      }
      /* Slow path */
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(a) == VALSTRUCT &&
          RegCheckOverload(vm, task, "opmul", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      AKSValue res;
      if (ValIsInt(a) && ValIsInt(b)) {
        if (strict_ovf) {
          RaiseX(vm, "IntegerOverflowException: multiplication overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        }
        res = ValFloat((double)ValToIntUnchecked(a) * (double)ValToIntUnchecked(b));
      } else if (ValIsFloat(a) && ValIsFloat(b)) {
        res = ValFloat(ValToFloat(a) * ValToFloat(b));
      } else if (ValIsFloat(a) && ValIsInt(b)) {
        res = ValFloat(ValToFloat(a) * (double)ValToIntUnchecked(b));
      } else if (ValIsInt(a) && ValIsFloat(b)) {
        res = ValFloat((double)ValToIntUnchecked(a) * ValToFloat(b));
      } else {
        res = ValMul(a, b);
      }
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      } else {
        vm->static_rc_eliminated++;
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopdiv: {
#else
  case regopdiv: {
#endif
      /* Fast path: int / int without div-by-zero or overflow */
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsInt(a) & ValIsInt(b)) {
        int64_t va = ValToIntUnchecked(a);
        int64_t vb = ValToIntUnchecked(b);
        if (vb != 0 && !(va == -140737488355328LL && vb == -1)) {
          AKSValue res = ValInt(va / vb);
          if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
            ValFreeFunc(&frameslots[curr_inst->dst]);
          } else {
            vm->static_rc_eliminated++;
          }
          frameslots[curr_inst->dst] = res;
          REG_DISPATCH_FAST();
        }
      }
      /* Slow path */
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(frameslots[curr_inst->src1]) == VALSTRUCT &&
          RegCheckOverload(vm, task, "opdiv", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      if ((ValKind(b) == VALINT && ValToInt(b) == 0) ||
          (ValKind(b) == VALFLOAT && ValToFloat(b) == 0.0)) {
        RaiseX(vm, "division by zero", &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      AKSValue res;
      if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        int64_t va = ValToInt(a);
        int64_t vb = ValToInt(b);
        if (va == -140737488355328LL && vb == -1) {
          if (strict_ovf) {
            RaiseX(vm, "IntegerOverflowException: division overflow", &ip,
                   &stacktop, &frame, &frameslots);
            continue;
          } else {
            res = ValFloat((double)va / -1.0);
          }
        } else {
          res = ValInt(va / vb);
        }
      } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
        res = ValFloat(ValToFloat(a) / ValToFloat(b));
      } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALINT) {
        res = ValFloat(ValToFloat(a) / (double)ValToInt(b));
      } else if (ValKind(a) == VALINT && ValKind(b) == VALFLOAT) {
        res = ValFloat((double)ValToInt(a) / ValToFloat(b));
      } else {
        res = ValDiv(a, b);
      }
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      } else {
        vm->static_rc_eliminated++;
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopmod: {
#else
  case regopmod: {
#endif
      /* Fast path: int % int without mod-by-zero or overflow */
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsInt(a) & ValIsInt(b)) {
        int64_t va = ValToIntUnchecked(a);
        int64_t vb = ValToIntUnchecked(b);
        if (vb != 0 && !(va == -140737488355328LL && vb == -1)) {
          AKSValue res = ValInt(va % vb);
          if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
            ValFreeFunc(&frameslots[curr_inst->dst]);
          } else {
            vm->static_rc_eliminated++;
          }
          frameslots[curr_inst->dst] = res;
          REG_DISPATCH_FAST();
        }
      }
      /* Slow path */
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(frameslots[curr_inst->src1]) == VALSTRUCT &&
          RegCheckOverload(vm, task, "opmod", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      if ((ValKind(b) == VALINT && ValToInt(b) == 0) ||
          (ValKind(b) == VALFLOAT && ValToFloat(b) == 0.0)) {
        RaiseX(vm, "modulo by zero", &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      AKSValue res;
      if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        int64_t va = ValToInt(a);
        int64_t vb = ValToInt(b);
        if (va == -140737488355328LL && vb == -1) {
          if (strict_ovf) {
            RaiseX(vm, "IntegerOverflowException: modulo overflow", &ip,
                   &stacktop, &frame, &frameslots);
            continue;
          } else {
            res = ValFloat(0.0);
          }
        } else {
          res = ValInt(va % vb);
        }
      } else {
        res = ValMod(a, b);
      }
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      } else {
        vm->static_rc_eliminated++;
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopneg: {
#else
  case regopneg: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue res;
      if (ValKind(a) == VALINT) {
        int64_t val = ValToInt(a);
        if (val == -140737488355328LL) {
          if (strict_ovf) {
            ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
            frame->ip = ip;
            task->stacktop = (int)(stacktop - task->stack);
            RaiseX(vm, "IntegerOverflowException: negation overflow", &ip,
                   &stacktop, &frame, &frameslots);
            continue;
          } else {
            res = ValFloat(-(double)-140737488355328LL);
          }
        } else {
          res = ValInt(-val);
        }
      } else {
        res = ValNeg(a);
      }
      ValFree(&frameslots[curr_inst->dst]);
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoplt: {
#else
  case regoplt: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsInt(a) & ValIsInt(b)) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = ValBool(ValToIntUnchecked(a) < ValToIntUnchecked(b));
        REG_DISPATCH_FAST();
      }
      if (ValIsFloat(a) & ValIsFloat(b)) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = ValBool(ValToFloat(a) < ValToFloat(b));
        REG_DISPATCH_FAST();
      }
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(a) == VALSTRUCT &&
          RegCheckOverload(vm, task, "oplt", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      AKSValue res = ValLt(a, b);
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopgt: {
#else
  case regopgt: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsInt(a) & ValIsInt(b)) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = ValBool(ValToIntUnchecked(a) > ValToIntUnchecked(b));
        REG_DISPATCH_FAST();
      }
      if (ValIsFloat(a) & ValIsFloat(b)) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = ValBool(ValToFloat(a) > ValToFloat(b));
        REG_DISPATCH_FAST();
      }
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(a) == VALSTRUCT &&
          RegCheckOverload(vm, task, "opgt", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      AKSValue res = ValGt(a, b);
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoplte: {
#else
  case regoplte: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsInt(a) & ValIsInt(b)) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = ValBool(ValToIntUnchecked(a) <= ValToIntUnchecked(b));
        REG_DISPATCH_FAST();
      }
      if (ValIsFloat(a) & ValIsFloat(b)) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = ValBool(ValToFloat(a) <= ValToFloat(b));
        REG_DISPATCH_FAST();
      }
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(a) == VALSTRUCT &&
          RegCheckOverload(vm, task, "oplte", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      AKSValue res = ValLte(a, b);
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopgte: {
#else
  case regopgte: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsInt(a) & ValIsInt(b)) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = ValBool(ValToIntUnchecked(a) >= ValToIntUnchecked(b));
        REG_DISPATCH_FAST();
      }
      if (ValIsFloat(a) & ValIsFloat(b)) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = ValBool(ValToFloat(a) >= ValToFloat(b));
        REG_DISPATCH_FAST();
      }
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(a) == VALSTRUCT &&
          RegCheckOverload(vm, task, "opgte", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      AKSValue res = ValGte(a, b);
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopeq: {
#else
  case regopeq: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsPrimitive(a) && ValIsPrimitive(b) && (ValIsInt(a) == ValIsInt(b))) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = ValBool(a == b);
        REG_DISPATCH_FAST();
      }
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(a) == VALSTRUCT &&
          RegCheckOverload(vm, task, "opeq", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      AKSValue res = ValEq(a, b);
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopneq: {
#else
  case regopneq: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsPrimitive(a) && ValIsPrimitive(b) && (ValIsInt(a) == ValIsInt(b))) {
        if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        }
        frameslots[curr_inst->dst] = ValBool(a != b);
        REG_DISPATCH_FAST();
      }
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(a) == VALSTRUCT &&
          RegCheckOverload(vm, task, "opneq", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      AKSValue res = ValNeq(a, b);
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopand: {
#else
  case regopand: {
#endif
      AKSValue res =
          ValAnd(frameslots[curr_inst->src1], frameslots[curr_inst->src2]);
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopor: {
#else
  case regopor: {
#endif
      AKSValue res =
          ValOr(frameslots[curr_inst->src1], frameslots[curr_inst->src2]);
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopnot: {
#else
  case regopnot: {
#endif
      AKSValue res = ValNot(frameslots[curr_inst->src1]);
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoptypeof: {
#else
  case regoptypeof: {
#endif
      AKSValue val = frameslots[curr_inst->src1];
      AKSValue res = ValTypeof(val);
      ValFree(&frameslots[curr_inst->dst]);
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopcastint: {
#else
  case regopcastint: {
#endif
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      AKSValue val = frameslots[curr_inst->src1];
      int cast_ok = 1;
      AKSValue res = ValCastInt(val, &cast_ok);
      if (!cast_ok) {
        char cast_errbuf[256];
        AKSValue vs = ValToStr(val);
        snprintf(cast_errbuf, sizeof(cast_errbuf),
                 "CastError: cannot cast value '%s' to int  -  expected a valid "
                 "integer string",
                 ValToStrPtr(vs)->data);
        ValFree(&vs);
        RaiseX(vm, cast_errbuf, &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      ValFree(&frameslots[curr_inst->dst]);
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopcastfloat: {
#else
  case regopcastfloat: {
#endif
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      AKSValue val = frameslots[curr_inst->src1];
      int cast_ok = 1;
      AKSValue res = ValCastFloat(val, &cast_ok);
      if (!cast_ok) {
        char cast_errbuf[256];
        AKSValue vs = ValToStr(val);
        snprintf(cast_errbuf, sizeof(cast_errbuf),
                 "CastError: cannot cast value '%s' to float  -  expected a "
                 "valid number string",
                 ValToStrPtr(vs)->data);
        ValFree(&vs);
        RaiseX(vm, cast_errbuf, &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      ValFree(&frameslots[curr_inst->dst]);
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopcastbool: {
#else
  case regopcastbool: {
#endif
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      AKSValue val = frameslots[curr_inst->src1];
      int cast_ok = 1;
      AKSValue res = ValCastBool(val, &cast_ok);
      if (!cast_ok) {
        char cast_errbuf[256];
        AKSValue vs = ValToStr(val);
        snprintf(cast_errbuf, sizeof(cast_errbuf),
                 "CastError: cannot cast value '%s' to bool  -  expected '1', "
                 "'0', 'true', or 'false'",
                 ValToStrPtr(vs)->data);
        ValFree(&vs);
        RaiseX(vm, cast_errbuf, &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      ValFree(&frameslots[curr_inst->dst]);
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopcaststr: {
#else
  case regopcaststr: {
#endif
      AKSValue val = frameslots[curr_inst->src1];
      AKSValue res = ValCastStr(val);
      ValFree(&frameslots[curr_inst->dst]);
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopclonestruct: {
#else
  case regopclonestruct: {
#endif
      AKSValue srcval = frameslots[curr_inst->src1];
      int dst = curr_inst->dst;
      if (ValKind(srcval) != VALSTRUCT) {
        ValFree(&frameslots[dst]);
        frameslots[dst] = ValCopy(srcval);
      } else {
        AKSStruct *src = ValToObj(srcval);
        AKSValue cloneval = ValStruct(src->typename);
        AKSStruct *dststruct = ValToObj(cloneval);
        dststruct->fieldcount = src->fieldcount;
        if (src->fieldcount > 0) {
          dststruct->fieldnames = malloc(src->fieldcount * sizeof(char *));
          dststruct->fieldvals = malloc(src->fieldcount * sizeof(AKSValue));
          for (int i = 0; i < src->fieldcount; i++) {
            dststruct->fieldnames[i] = strdup(src->fieldnames[i]);
            dststruct->fieldvals[i] = ValCopy(src->fieldvals[i]);
          }
        } else {
          dststruct->fieldnames = NULL;
          dststruct->fieldvals = NULL;
        }
        ValFree(&frameslots[dst]);
        frameslots[dst] = cloneval;
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopjmp: {
#else
  case regopjmp: {
#endif
      int curr_idx = (int)(regip - (RegInst *)frame->fn->regcode);
      if (curr_inst->targetip <= curr_idx && frame->fn->chunk.loopcounters) {
        RegInst *target_inst = (RegInst *)frame->fn->regcode + curr_inst->targetip;
        int targetoffset = target_inst->bytecodeoffset;
        int endoffset = curr_inst->bytecodeoffset;
        if (targetoffset >= 0 && targetoffset < frame->fn->chunk.count) {
          frame->fn->chunk.loopcounters[targetoffset]++;
          if (DetectHotLoop(vm, frame->fn, targetoffset) &&
              !HasJITCompiled(frame->fn, targetoffset, vm)) {
            TriggerJIT(vm, frame->fn, targetoffset, endoffset);
          }
          if (HasJITCompiled(frame->fn, targetoffset, vm)) {
            task->tracingmode = 0;
            task->tracestepcount = 0;
            task->stacktop = (int)(stacktop - task->stack);
            ExecuteJITCompiled(vm, task, frame->fn, targetoffset, &stacktop, &frame,
                               &frameslots, &ip);
            regip = (RegInst *)frame->fn->regcode + curr_inst->targetip;
            REG_DISPATCH_OR_BREAK();
          }
        }
      }
      regip = (RegInst *)frame->fn->regcode + curr_inst->targetip;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopjmpiffalse: {
#else
  case regopjmpiffalse: {
#endif
      if (!ValIsTruthy(frameslots[curr_inst->src1])) {
        int curr_idx = (int)(regip - (RegInst *)frame->fn->regcode);
        if (curr_inst->targetip <= curr_idx && frame->fn->chunk.loopcounters) {
          RegInst *target_inst = (RegInst *)frame->fn->regcode + curr_inst->targetip;
          int targetoffset = target_inst->bytecodeoffset;
          int endoffset = curr_inst->bytecodeoffset;
          if (targetoffset >= 0 && targetoffset < frame->fn->chunk.count) {
            frame->fn->chunk.loopcounters[targetoffset]++;
            if (DetectHotLoop(vm, frame->fn, targetoffset) &&
                !HasJITCompiled(frame->fn, targetoffset, vm)) {
              TriggerJIT(vm, frame->fn, targetoffset, endoffset);
            }
            if (HasJITCompiled(frame->fn, targetoffset, vm)) {
              task->tracingmode = 0;
              task->tracestepcount = 0;
              task->stacktop = (int)(stacktop - task->stack);
              ExecuteJITCompiled(vm, task, frame->fn, targetoffset, &stacktop, &frame,
                                 &frameslots, &ip);
              regip = (RegInst *)frame->fn->regcode + curr_inst->targetip;
              REG_DISPATCH_OR_BREAK();
            }
          }
        }
        regip = (RegInst *)frame->fn->regcode + curr_inst->targetip;
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopjmpifnull: {
#else
  case regopjmpifnull: {
#endif
      if (ValKind(frameslots[curr_inst->src1]) == VALNULL) {
        regip = (RegInst *)frame->fn->regcode + curr_inst->targetip;
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopcall: {
#else
  case regopcall: {
#endif
      int dst = curr_inst->dst;
      uint16_t fnid = curr_inst->src1;
      uint8_t argc = (uint8_t)curr_inst->src2;
      FnProto *callee = vm->fns[fnid];

      if (callee->isasync) {
        VMTask *spawned = TaskNew(vm, callee, 0);
        for (int i = 0; i < argc && i < callee->paramcount; i++) {
          ValFree(&spawned->stack[i]);
          spawned->stack[i] = ValCopy(frameslots[dst + i]);
        }
        int targetworker = rand() % vm->workercount;
        WorkerPushTail(&vm->workers[targetworker], spawned);
        ValFree(&frameslots[dst]);
        frameslots[dst] = ValTask(spawned);
        REG_DISPATCH_OR_BREAK();
      }

      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);

      int res;
      if (task->blockedonstdlib) {
        task->blockedonstdlib = 0;
        res = ExecuteStdlib(vm, task->stdlibname, task->stdlibargc, &stacktop);
      } else {
        for (int i = 0; i < argc; i++) {
          *stacktop++ = ValCopy(frameslots[dst + i]);
        }
        res = (callee && callee->chunk.count == 0) ? DispatchStdlib(vm, callee, callee->name, argc, &stacktop) : 0;
      }

      if (res == -2) {
        task->blockedonstdlib = 1;
        strncpy(task->stdlibname, callee->name, 63);
        task->stdlibargc = argc;
        regip--;
        task->stacktop = (int)(stacktop - task->stack);
        return 2;
      } else if (res < 0) {
        RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      } else if (res == 1) {
        AKSValue ret = POP();
        ValFree(&frameslots[dst]);
        frameslots[dst] = ret;
      } else {
        frame->ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        if (fnid < 4096) {
          vm->fncallcounts[fnid]++;
          if (DetectHotFunction(vm, callee) &&
              !HasJITCompiled(callee, -1, vm)) {
            TriggerMethodJIT(vm, task, callee);
          }
        }
        if (HasJITCompiled(callee, -1, vm)) {
          AKSValue *canarypos = stacktop - argc;
          for (AKSValue *p = stacktop; p > canarypos; p--) {
            *p = *(p - 1);
          }
          *canarypos = ValInt(vm->stackcanary);
          stacktop++;

          FRAME_GROW();
          CallFrame *newframe = &task->frames[task->framecount++];
          newframe->fn = callee;
          newframe->slotsbase = (int)(canarypos + 1 - task->stack);
          newframe->argc = argc;

          int maxreg = callee->maxregisters;
          if (maxreg == 0)
            maxreg = callee->localcount;
          STACK_GROW(newframe->slotsbase + maxreg + 1);
          for (int i = argc; i < maxreg; i++) {
            task->stack[newframe->slotsbase + i] = ValNull();
          }
          stacktop = task->stack + newframe->slotsbase + argc;

          ExecuteJITCompiled(vm, task, callee, -1, &stacktop, &newframe,
                             &frameslots, &ip);
          AKSValue ret = POP();
          task->framecount--;
          frame = &task->frames[task->framecount - 1];
          frameslots = task->stack + frame->slotsbase;
          ValFree(&frameslots[dst]);
          frameslots[dst] = ret;
          REG_DISPATCH_OR_BREAK();
        }

        if (task->framecount >= MAXFRAMES) {
          RaiseX(vm, "stack overflow (too many call frames)", &ip, &stacktop,
                 &frame, &frameslots);
          continue;
        }
        AKSValue *canarypos = stacktop - argc;
        for (AKSValue *p = stacktop; p > canarypos; p--) {
          *p = *(p - 1);
        }
        *canarypos = ValInt(vm->stackcanary);
        stacktop++;

        FRAME_GROW();
        CallFrame *newframe = &task->frames[task->framecount++];
        VM_ENTER_FRAME();
        newframe->fn = callee;
        newframe->ip = callee->chunk.code;
        newframe->slotsbase = (int)(canarypos + 1 - task->stack);
        newframe->argc = argc;

        int maxreg = callee->maxregisters;
        if (maxreg == 0)
          maxreg = callee->localcount;
        STACK_GROW(newframe->slotsbase + maxreg + 1);
        for (int i = argc; i < maxreg; i++) {
          task->stack[newframe->slotsbase + i] = ValNull();
        }
        stacktop = task->stack + newframe->slotsbase + maxreg;

        frame = newframe;
        frameslots = task->stack + frame->slotsbase;
        if (frame->fn->regcode) {
          regip = (RegInst *)frame->fn->regcode;
        } else {
          ip = frame->ip;
          goto enter_stack_interpreter;
        }
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopcalldynamic: {
#else
  case regopcalldynamic: {
#endif
      int dst = curr_inst->dst;
      int fnreg = curr_inst->src1;
      uint8_t argc = (uint8_t)curr_inst->src2;

      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);

      int res;
      FnProto *callee = NULL;
      if (task->blockedonstdlib) {
        task->blockedonstdlib = 0;
        res = ExecuteStdlib(vm, task->stdlibname, task->stdlibargc, &stacktop);
      } else {
        AKSValue calleeval = ValCopy(frameslots[fnreg]);
        int stack_pushed = 0;
        if (ValKind(calleeval) == VALBOUNDMETHOD) {
          AKSValue receiver = ValCopy(*ValToBoundMethod(calleeval)->receiver);
          char *methodname = strdup(ValToBoundMethod(calleeval)->name);
          ValFree(&calleeval);
          AKSStruct *s = ValToObj(receiver);
          int found = -1;
          for (int i = 0; i < s->fieldcount; i++) {
            if (strcmp(s->fieldnames[i], methodname) == 0) {
              found = i;
              break;
            }
          }
          uint16_t global_fnid = 65535;
          if (found < 0 && s->typename) {
            global_fnid = FindTypeMethod(vm, s->typename, methodname);
          }
          if ((found >= 0 && ValKind(s->fieldvals[found]) == VALFN) || global_fnid != 65535) {
            *stacktop++ = ValCopy(receiver);
            for (int i = 0; i < argc; i++) {
              *stacktop++ = ValCopy(frameslots[dst + i]);
            }
            argc++;
            if (found >= 0) {
              calleeval = ValCopy(s->fieldvals[found]);
            } else {
              calleeval = ValFn(global_fnid);
            }
            ValFree(&receiver);
            free(methodname);
            stack_pushed = 1;
          } else {
            AKSValue arr = ValArr(argc);
            ValToArr(arr)->count = argc;
            for (int i = 0; i < argc; i++) {
              ValToArr(arr)->items[i] = ValCopy(frameslots[dst + i]);
            }
            int mmfound = -1;
            for (int i = 0; i < s->fieldcount; i++) {
              if (strcmp(s->fieldnames[i], "methodmissing") == 0) {
                mmfound = i;
                break;
              }
            }
            if (mmfound < 0 || ValKind(s->fieldvals[mmfound]) != VALFN) {
              ValFree(&receiver);
              free(methodname);
              ValFree(&arr);
              RaiseX(vm, "methodmissing is not a function", &ip, &stacktop,
                     &frame, &frameslots);
              continue;
            }
            *stacktop++ = receiver;
            *stacktop++ = ValStr(methodname);
            free(methodname);
            *stacktop++ = arr;
            calleeval = ValCopy(s->fieldvals[mmfound]);
            argc = 3;
            stack_pushed = 1;
          }
        }
        if (ValKind(calleeval) != VALFN) {
          ValFree(&calleeval);
          RaiseX(vm, "expression is not a function", &ip, &stacktop, &frame,
                 &frameslots);
          continue;
        }
        uint16_t fnid = ValToFn(calleeval);
        ValFree(&calleeval);

        if (fnid >= vm->fncount) {
          RaiseX(vm, "invalid function ID", &ip, &stacktop, &frame,
                 &frameslots);
          continue;
        }
        callee = vm->fns[fnid];

        if (callee->isasync) {
          VMTask *spawned = TaskNew(vm, callee, 0);
          for (int i = 0; i < argc && i < callee->paramcount; i++) {
            ValFree(&spawned->stack[i]);
            spawned->stack[i] = ValCopy(stack_pushed ? *(stacktop - argc + i)
                                                     : frameslots[dst + i]);
          }
          if (stack_pushed) {
            for (int i = 0; i < argc; i++) {
              ValFree(--stacktop);
            }
          }
          int targetworker = rand() % vm->workercount;
          WorkerPushTail(&vm->workers[targetworker], spawned);
          ValFree(&frameslots[dst]);
          frameslots[dst] = ValTask(spawned);
          REG_DISPATCH_OR_BREAK();
        }

        if (!stack_pushed) {
          for (int i = 0; i < argc; i++) {
            *stacktop++ = ValCopy(frameslots[dst + i]);
          }
        }
        res = (callee && callee->chunk.count == 0) ? DispatchStdlib(vm, callee, callee->name, argc, &stacktop) : 0;
      }

      if (res == -2) {
        task->blockedonstdlib = 1;
        if (callee)
          strncpy(task->stdlibname, callee->name, 63);
        task->stdlibargc = argc;
        regip--;
        task->stacktop = (int)(stacktop - task->stack);
        return 2;
      } else if (res < 0) {
        RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      } else if (res == 1) {
        AKSValue ret = POP();
        ValFree(&frameslots[dst]);
        frameslots[dst] = ret;
      } else {
        frame->ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        if (callee && callee->id < 4096) {
          vm->fncallcounts[callee->id]++;
          if (DetectHotFunction(vm, callee) && !HasJITCompiled(callee, -1, vm)) {
            TriggerMethodJIT(vm, task, callee);
          }
        }
        if (HasJITCompiled(callee, -1, vm)) {
          AKSValue *canarypos = stacktop - argc;
          for (AKSValue *p = stacktop; p > canarypos; p--) {
            *p = *(p - 1);
          }
          *canarypos = ValInt(vm->stackcanary);
          stacktop++;

          FRAME_GROW();
          CallFrame *newframe = &task->frames[task->framecount++];
          newframe->fn = callee;
          newframe->slotsbase = (int)(canarypos + 1 - task->stack);
          newframe->argc = argc;

          int maxreg = callee->maxregisters;
          if (maxreg == 0)
            maxreg = callee->localcount;
          STACK_GROW(newframe->slotsbase + maxreg + 1);
          for (int i = argc; i < maxreg; i++) {
            task->stack[newframe->slotsbase + i] = ValNull();
          }
          stacktop = task->stack + newframe->slotsbase + argc;

          ExecuteJITCompiled(vm, task, callee, -1, &stacktop, &newframe,
                             &frameslots, &ip);
          AKSValue ret = POP();
          task->framecount--;
          frame = &task->frames[task->framecount - 1];
          frameslots = task->stack + frame->slotsbase;
          ValFree(&frameslots[dst]);
          frameslots[dst] = ret;
          REG_DISPATCH_OR_BREAK();
        }

        if (task->framecount >= MAXFRAMES) {
          RaiseX(vm, "stack overflow (too many call frames)", &ip, &stacktop,
                 &frame, &frameslots);
          continue;
        }
        AKSValue *canarypos = stacktop - argc;
        for (AKSValue *p = stacktop; p > canarypos; p--) {
          *p = *(p - 1);
        }
        *canarypos = ValInt(vm->stackcanary);
        stacktop++;

        FRAME_GROW();
        CallFrame *newframe = &task->frames[task->framecount++];
        VM_ENTER_FRAME();
        newframe->fn = callee;
        newframe->ip = callee->chunk.code;
        newframe->slotsbase = (int)(canarypos + 1 - task->stack);
        newframe->argc = argc;

        int maxreg = callee->maxregisters;
        if (maxreg == 0)
          maxreg = callee->localcount;
        STACK_GROW(newframe->slotsbase + maxreg + 1);
        for (int i = argc; i < maxreg; i++) {
          task->stack[newframe->slotsbase + i] = ValNull();
        }
        stacktop = task->stack + newframe->slotsbase + maxreg;

        frame = newframe;
        frameslots = task->stack + frame->slotsbase;
        if (frame->fn->regcode) {
          regip = (RegInst *)frame->fn->regcode;
        } else {
          ip = frame->ip;
          goto enter_stack_interpreter;
        }
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopret: {
#else
  case regopret: {
#endif
      if (task->framecount > 1) {
        int canaryidx = frame->slotsbase - 1;
        if (canaryidx < 0 || ValKind(task->stack[canaryidx]) != VALINT ||
            ValToInt(task->stack[canaryidx]) != (int64_t)vm->stackcanary) {
          fprintf(stderr, "SecurityError: Stack canary corruption detected\n");
          exit(1);
        }
        int maxreg = frame->fn->maxregisters;
        if (maxreg == 0)
          maxreg = frame->fn->localcount;
        for (int i = 0; i < maxreg; i++) {
          ValFree(&frameslots[i]);
        }
        ValFree(&task->stack[canaryidx]);
        task->framecount--;
        VM_EXIT_FRAME();
        CallFrame *prevframe = &task->frames[task->framecount - 1];
        stacktop = task->stack + frame->slotsbase - 1;
        frame = prevframe;
        frameslots = task->stack + frame->slotsbase;
        if (frame->fn->regcode) {
          uint8_t *call_ip = NULL;
          uint8_t *ip_val = frame->ip;
          if (ip_val >= frame->fn->chunk.code &&
              ip_val < frame->fn->chunk.code + frame->fn->chunk.count) {
            if (*ip_val == OPCALL || *ip_val == OPCALLDYNAMIC) {
              call_ip = ip_val;
            }
          }
          if (!call_ip) {
            if (ip_val - 4 >= frame->fn->chunk.code &&
                *(ip_val - 4) == OPCALL) {
              call_ip = ip_val - 4;
            } else if (ip_val - 2 >= frame->fn->chunk.code &&
                       *(ip_val - 2) == OPCALLDYNAMIC) {
              call_ip = ip_val - 2;
            }
          }
          if (call_ip) {
            int call_bytecode_offset = (int)(call_ip - frame->fn->chunk.code);
            int regoffset = frame->fn->offsettomap[call_bytecode_offset];
            regip = (RegInst *)frame->fn->regcode + regoffset + 1;
            RegInst *callinst = (RegInst *)frame->fn->regcode + regoffset;
            ValFree(&frameslots[callinst->dst]);
            frameslots[callinst->dst] = ValNull();
          } else {
            int bytecodeoffset = (int)(frame->ip - frame->fn->chunk.code);
            int regoffset = frame->fn->offsettomap[bytecodeoffset];
            regip = (RegInst *)frame->fn->regcode + regoffset + 1;
            RegInst *callinst = (RegInst *)frame->fn->regcode + regoffset;
            ValFree(&frameslots[callinst->dst]);
            frameslots[callinst->dst] = ValNull();
          }
        } else {
          ip = frame->ip;
          PUSH(ValNull());
          goto enter_stack_interpreter;
        }
      } else {
        int maxreg = frame->fn->maxregisters;
        if (maxreg == 0)
          maxreg = frame->fn->localcount;
        for (int i = 0; i < maxreg; i++) {
          ValFree(&frameslots[i]);
        }
        task->stacktop = frame->slotsbase;
        task->result = ValNull();
        task->state = 3;
        return 1;
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopretval: {
#else
  case regopretval: {
#endif
      AKSValue ret = ValCopy(frameslots[curr_inst->src1]);
      if (task->framecount > 1) {
        int canaryidx = frame->slotsbase - 1;
        if (canaryidx < 0 || ValKind(task->stack[canaryidx]) != VALINT ||
            ValToInt(task->stack[canaryidx]) != (int64_t)vm->stackcanary) {
          fprintf(stderr, "SecurityError: Stack canary corruption detected\n");
          exit(1);
        }
        int maxreg = frame->fn->maxregisters;
        if (maxreg == 0)
          maxreg = frame->fn->localcount;
        for (int i = 0; i < maxreg; i++) {
          ValFree(&frameslots[i]);
        }
        ValFree(&task->stack[canaryidx]);
        task->framecount--;
        VM_EXIT_FRAME();
        CallFrame *prevframe = &task->frames[task->framecount - 1];
        stacktop = task->stack + frame->slotsbase - 1;
        frame = prevframe;
        frameslots = task->stack + frame->slotsbase;
        if (frame->fn->regcode) {
          uint8_t *call_ip = NULL;
          uint8_t *ip_val = frame->ip;
          if (ip_val >= frame->fn->chunk.code &&
              ip_val < frame->fn->chunk.code + frame->fn->chunk.count) {
            if (*ip_val == OPCALL || *ip_val == OPCALLDYNAMIC) {
              call_ip = ip_val;
            }
          }
          if (!call_ip) {
            if (ip_val - 4 >= frame->fn->chunk.code &&
                *(ip_val - 4) == OPCALL) {
              call_ip = ip_val - 4;
            } else if (ip_val - 2 >= frame->fn->chunk.code &&
                       *(ip_val - 2) == OPCALLDYNAMIC) {
              call_ip = ip_val - 2;
            }
          }
          if (call_ip) {
            int call_bytecode_offset = (int)(call_ip - frame->fn->chunk.code);
            int regoffset = frame->fn->offsettomap[call_bytecode_offset];
            regip = (RegInst *)frame->fn->regcode + regoffset + 1;
            RegInst *callinst = (RegInst *)frame->fn->regcode + regoffset;
            ValFree(&frameslots[callinst->dst]);
            frameslots[callinst->dst] = ret;
          } else {
            int bytecodeoffset = (int)(frame->ip - frame->fn->chunk.code);
            int regoffset = frame->fn->offsettomap[bytecodeoffset];
            regip = (RegInst *)frame->fn->regcode + regoffset + 1;
            RegInst *callinst = (RegInst *)frame->fn->regcode + regoffset;
            ValFree(&frameslots[callinst->dst]);
            frameslots[callinst->dst] = ret;
          }
        } else {
          ip = frame->ip;
          PUSH(ret);
          goto enter_stack_interpreter;
        }
      } else {
        int maxreg = frame->fn->maxregisters;
        if (maxreg == 0)
          maxreg = frame->fn->localcount;
        for (int i = 0; i < maxreg; i++) {
          ValFree(&frameslots[i]);
        }
        task->stacktop = frame->slotsbase;
        task->result = ret;
        task->state = 3;
        return 1;
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopnewarr: {
#else
  case regopnewarr: {
#endif
      int elcount = curr_inst->src1;
      int isstack = (elcount & 0x8000) != 0;
      elcount &= 0x7FFF;
      int dst = curr_inst->dst;
      AKSValue arrval = isstack ? ValArrStack(task, elcount) : ValArr(elcount);
      ValToArr(arrval)->count = elcount;
      for (int i = 0; i < elcount; i++) {
        ValToArr(arrval)->items[i] = ValCopy(frameslots[dst + i]);
      }
      ValFree(&frameslots[dst]);
      frameslots[dst] = arrval;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopnewtuple: {
#else
  case regopnewtuple: {
#endif
      int elcount = curr_inst->src1;
      int isstack = (elcount & 0x8000) != 0;
      elcount &= 0x7FFF;
      int dst = curr_inst->dst;
      AKSValue tupval =
          isstack ? ValTupleStack(task, elcount) : ValTuple(elcount);
      ValToTuple(tupval)->count = elcount;
      for (int i = 0; i < elcount; i++) {
        ValToTuple(tupval)->items[i] = ValCopy(frameslots[dst + i]);
      }
      ValFree(&frameslots[dst]);
      frameslots[dst] = tupval;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoparrget: {
#else
  case regoparrget: {
#endif
      AKSValue arrval = frameslots[curr_inst->src1];
      AKSValue idxval = frameslots[curr_inst->src2];
      /* Fast path: array[int] read */
      if (ValIsArr(arrval) & ValIsInt(idxval)) {
        AKSArray *arr = ValToArr(arrval);
        int64_t idx = ValToIntUnchecked(idxval);
        if ((uint64_t)idx < (uint64_t)arr->count) {
          AKSValue item = arr->items[idx];
          if (ValIsPrimitive(item)) {
            if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
              ValFreeFunc(&frameslots[curr_inst->dst]);
            } else {
              vm->static_rc_eliminated++;
            }
            frameslots[curr_inst->dst] = item;
            REG_DISPATCH_FAST();
          } else {
            AKSValue res = ValCopy(item);
            if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
              ValFreeFunc(&frameslots[curr_inst->dst]);
            } else {
              vm->static_rc_eliminated++;
            }
            frameslots[curr_inst->dst] = res;
            REG_DISPATCH_FAST();
          }
        }
      }
      AKSValue res;
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);

      if (ValKind(arrval) == VALNULL) {
        RaiseX(vm,
               "NullPointerException: attempt to access element on null value",
               &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      if (ValIsInvalidated(arrval)) {
        RaiseX(vm,
               "UseAfterFreeError: attempt to use an explicitly "
               "freed/invalidated object",
               &ip, &stacktop, &frame, &frameslots);
        continue;
      }

      if ((ValKind(arrval) == VALARR || ValKind(arrval) == VALTUPLE) &&
          ValKind(idxval) == VALINT) {
        int64_t idx = ValToInt(idxval);
        int64_t orig_idx = idx;
        if (idx < 0) idx += ValToArr(arrval)->count;
        if (!vm->no_bounds_check) {
          if (idx < 0 || idx >= ValToArr(arrval)->count) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "Array index out of bounds: index %lld, size %d",
                     (long long)orig_idx, ValToArr(arrval)->count);
            RaiseX(vm, buf, &ip, &stacktop, &frame, &frameslots);
            continue;
          }
        }
        if (idx < 0)
          idx = 0;
        if (idx >= ValToArr(arrval)->count)
          idx = ValToArr(arrval)->count - 1;
        if (ValToArr(arrval)->count > 0) {
          res = ValCopy(ValToArr(arrval)->items[idx]);
        } else {
          res = ValNull();
        }
      } else if (ValKind(arrval) == VALSTRUCT && ValKind(idxval) == VALSTR) {
        AKSStruct *s = ValToObj(arrval);
        const char *fieldname = ValToStrPtr(idxval)->data;
        int found = -1;
        for (int i = 0; i < s->fieldcount; i++) {
          if (s->fieldnames[i] == fieldname || strcmp(s->fieldnames[i], fieldname) == 0) {
            found = i;
            break;
          }
        }
        if (found >= 0) {
          res = ValCopy(s->fieldvals[found]);
        } else {
          res = ValNull();
        }
      } else if (ValKind(arrval) == VALSTR && ValKind(idxval) == VALINT) {
        int64_t idx = ValToInt(idxval);
        int64_t count = ValToStrPtr(arrval)->len;
        int64_t orig_idx = idx;
        if (idx < 0) idx += count;
        if (idx < 0 || idx >= count) {
          char buf[128];
          snprintf(buf, sizeof(buf),
                   "String index out of bounds: index %lld, size %lld",
                   (long long)orig_idx, (long long)count);
          RaiseX(vm, buf, &ip, &stacktop, &frame, &frameslots);
          continue;
        }
        char charstr[2] = {ValToStrPtr(arrval)->data[idx], '\0'};
        res = ValStrLen(charstr, 1);
      } else {
        RaiseX(vm, "Index target not array/struct or index not integer/string",
               &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      } else {
        vm->static_rc_eliminated++;
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoparrset: {
#else
  case regoparrset: {
#endif
      AKSValue arrval = frameslots[curr_inst->dst];
      AKSValue idxval = frameslots[curr_inst->src1];
      AKSValue val_src = frameslots[curr_inst->src2];
      /* Fast path: array[int] = primitive write */
      if (ValIsArr(arrval) & ValIsInt(idxval) & ValIsPrimitive(val_src)) {
        AKSArray *arr = ValToArr(arrval);
        int64_t idx = ValToIntUnchecked(idxval);
        if ((uint64_t)idx < (uint64_t)arr->count) {
          if (!ValIsPrimitive(arr->items[idx])) {
            ValFreeFunc(&arr->items[idx]);
          } else {
            vm->static_rc_eliminated++;
          }
          arr->items[idx] = val_src;
          REG_DISPATCH_FAST();
        }
      }
      AKSValue val = ValCopy(val_src);
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);

      if (ValKind(arrval) == VALNULL) {
        ValFree(&val);
        RaiseX(vm,
               "NullPointerException: attempt to access element on null value",
               &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      if (ValIsInvalidated(arrval)) {
        ValFree(&val);
        RaiseX(vm,
               "UseAfterFreeError: attempt to use an explicitly "
               "freed/invalidated object",
               &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      if (ValKind(arrval) == VALTUPLE) {
        ValFree(&val);
        RaiseX(vm, "TypeError: cannot mutate tuple elements", &ip, &stacktop,
               &frame, &frameslots);
        continue;
      }

      if (ValKind(arrval) == VALARR && ValKind(idxval) == VALINT) {
        int64_t idx = ValToInt(idxval);
        if (!vm->no_bounds_check) {
          if (idx < 0 || idx >= ValToArr(arrval)->count) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "Array index out of bounds: index %lld, size %d",
                     (long long)idx, ValToArr(arrval)->count);
            ValFree(&val);
            RaiseX(vm, buf, &ip, &stacktop, &frame, &frameslots);
            continue;
          }
        }
        if (idx >= 0 && idx < ValToArr(arrval)->count) {
          if (!ValIsPrimitive(ValToArr(arrval)->items[idx])) {
            ValFreeFunc(&ValToArr(arrval)->items[idx]);
          } else {
            vm->static_rc_eliminated++;
          }
          ValToArr(arrval)->items[idx] = val;
        } else {
          ValFree(&val);
        }
      } else if (ValKind(arrval) == VALSTRUCT && ValKind(idxval) == VALSTR) {
        AKSStruct *s = ValToObj(arrval);
        const char *fieldname = ValToStrPtr(idxval)->data;
        int found = -1;
        for (int i = 0; i < s->fieldcount; i++) {
          if (s->fieldnames[i] == fieldname || strcmp(s->fieldnames[i], fieldname) == 0) {
            found = i;
            break;
          }
        }
        if (found >= 0) {
          ValFree(&s->fieldvals[found]);
          s->fieldvals[found] = val;
        } else {
          s->fieldcount++;
          s->fieldnames =
              realloc(s->fieldnames, s->fieldcount * sizeof(char *));
          s->fieldvals =
              realloc(s->fieldvals, s->fieldcount * sizeof(AKSValue));
          s->fieldnames[s->fieldcount - 1] = strdup(fieldname);
          s->fieldvals[s->fieldcount - 1] = val;
        }
      } else {
        ValFree(&val);
        RaiseX(vm, "Index target not array/struct or index not integer/string",
               &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopnewstruct: {
#else
  case regopnewstruct: {
#endif
      uint16_t sid = curr_inst->src1;
      int isstack = (sid & 0x8000) != 0;
      sid &= 0x7FFF;
      const char *typename = vm->strpool[sid];
      ValFree(&frameslots[curr_inst->dst]);
      frameslots[curr_inst->dst] =
          isstack ? ValStructStack(task, typename) : ValStruct(typename);
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopgetfield: {
#else
  case regopgetfield: {
#endif
      AKSValue objval = frameslots[curr_inst->src1];
      int dst = curr_inst->dst;
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);

      if (ValKind(objval) == VALNULL) {
        RaiseX(vm,
               "NullPointerException: attempt to access member on null value",
               &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      if (ValIsInvalidated(objval)) {
        RaiseX(vm,
               "UseAfterFreeError: attempt to use an explicitly "
               "freed/invalidated object",
               &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      if (ValKind(objval) == VALARR) {
        const char *mname = curr_inst->fieldname;
        if (strcmp(mname, "len") == 0) {
          ValFree(&frameslots[dst]);
          frameslots[dst] = ValInt(ValToArr(objval)->count);
          REG_DISPATCH_OR_BREAK();
        }
      }
      if (ValKind(objval) != VALSTRUCT) {
        RaiseX(vm, "Field target is not a struct", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      }

      AKSStruct *s = ValToObj(objval);
      int found = -1;
      RegInst *actual_inst = (RegInst *)frame->fn->regcode +
                             (regip - 1 - (RegInst *)frame->fn->regcode);
      if (actual_inst->cachedindex >= 0 &&
          actual_inst->cachedindex < s->fieldcount &&
          (s->fieldnames[actual_inst->cachedindex] == curr_inst->fieldname ||
           strcmp(s->fieldnames[actual_inst->cachedindex], curr_inst->fieldname) == 0)) {
        found = actual_inst->cachedindex;
      }
      if (found < 0) {
        for (int i = 0; i < s->fieldcount; i++) {
          if (s->fieldnames[i] == curr_inst->fieldname || strcmp(s->fieldnames[i], curr_inst->fieldname) == 0) {
            found = i;
            break;
          }
        }
        if (found >= 0) {
          actual_inst->cachedindex = found;
        }
      }

      AKSValue res;
      if (found >= 0) {
        AKSValue fieldval = s->fieldvals[found];
        if (ValKind(fieldval) == VALFN) {
          uint16_t fnid = ValToFn(fieldval);
          if (fnid < vm->fncount) {
            FnProto *fn = vm->fns[fnid];
            if (fn->paramcount > 0 && strcmp(fn->locals[0].name, "self") == 0) {
              res = ValBoundmethod(objval, curr_inst->fieldname);
            } else {
              res = ValCopy(fieldval);
            }
          } else {
            res = ValCopy(fieldval);
          }
        } else {
          res = ValCopy(fieldval);
        }
      } else {
        AKSValue swiz_val;
        if (TryExtractSwizzle(s, curr_inst->fieldname, &swiz_val)) {
          res = swiz_val;
        } else {
          int has_type_method = 0;
          if (s->typename) {
            has_type_method = (FindTypeMethod(vm, s->typename, curr_inst->fieldname) != 65535);
          }
          if (has_type_method) {
            res = ValBoundmethod(objval, curr_inst->fieldname);
          } else {
            int mmfound = -1;
            for (int i = 0; i < s->fieldcount; i++) {
              if (strcmp(s->fieldnames[i], "methodmissing") == 0) {
                mmfound = i;
                break;
              }
            }
            if (mmfound >= 0) {
              res = ValBoundmethod(objval, curr_inst->fieldname);
            } else {
              res = ValNull();
            }
          }
        }
      }
      ValFree(&frameslots[dst]);
      frameslots[dst] = res;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopsetfield: {
#else
  case regopsetfield: {
#endif
      AKSValue objval = frameslots[curr_inst->dst];
      AKSValue val = ValCopy(frameslots[curr_inst->src1]);
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);

      if (ValKind(objval) == VALNULL) {
        ValFree(&val);
        RaiseX(vm,
               "NullPointerException: attempt to access member on null value",
               &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      if (ValIsInvalidated(objval)) {
        ValFree(&val);
        RaiseX(vm,
               "UseAfterFreeError: attempt to use an explicitly "
               "freed/invalidated object",
               &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      if (ValKind(objval) != VALSTRUCT) {
        ValFree(&val);
        RaiseX(vm, "Field target is not a struct", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      }

      AKSStruct *s = ValToObj(objval);
      int found = -1;
      RegInst *actual_inst = (RegInst *)frame->fn->regcode +
                             (regip - 1 - (RegInst *)frame->fn->regcode);
      if (actual_inst->cachedindex >= 0 &&
          actual_inst->cachedindex < s->fieldcount &&
          (s->fieldnames[actual_inst->cachedindex] == curr_inst->fieldname ||
           strcmp(s->fieldnames[actual_inst->cachedindex], curr_inst->fieldname) == 0)) {
        found = actual_inst->cachedindex;
      }
      if (found < 0) {
        for (int i = 0; i < s->fieldcount; i++) {
          if (s->fieldnames[i] == curr_inst->fieldname || strcmp(s->fieldnames[i], curr_inst->fieldname) == 0) {
            found = i;
            break;
          }
        }
        if (found >= 0) {
          actual_inst->cachedindex = found;
        }
      }

      if (found >= 0) {
        ValFree(&s->fieldvals[found]);
        s->fieldvals[found] = val;
      } else {
        s->fieldcount++;
        s->fieldnames = realloc(s->fieldnames, s->fieldcount * sizeof(char *));
        s->fieldvals = realloc(s->fieldvals, s->fieldcount * sizeof(AKSValue));
        s->fieldnames[s->fieldcount - 1] = strdup(curr_inst->fieldname);
        s->fieldvals[s->fieldcount - 1] = val;
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopinc: {
#else
  case regopinc: {
#endif
      AKSValue val = frameslots[curr_inst->src1];
      if (ValIsInt(val)) {
        int64_t iv = ValToIntUnchecked(val);
        int64_t r;
        if (!ChkAddOvf(iv, 1, &r)) {
          if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
            ValFreeFunc(&frameslots[curr_inst->dst]);
          } else {
            vm->static_rc_eliminated++;
          }
          frameslots[curr_inst->dst] = ValInt(r);
          REG_DISPATCH_FAST();
        }
      }
      AKSValue res;
      if (ValKind(val) == VALINT) {
        int64_t r;
        if (ChkAddOvf(ValToInt(val), 1, &r)) {
          if (strict_ovf) {
            ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
            frame->ip = ip;
            task->stacktop = (int)(stacktop - task->stack);
            RaiseX(vm, "IntegerOverflowException: increment overflow", &ip,
                   &stacktop, &frame, &frameslots);
            continue;
          } else {
            res = ValFloat((double)ValToInt(val) + 1.0);
          }
        } else {
          res = ValInt(r);
        }
      } else if (ValKind(val) == VALFLOAT) {
        res = ValFloat(ValToFloat(val) + 1.0);
      } else {
        ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        frame->ip = ip;
        task->stacktop = (int)(stacktop - task->stack);
        RaiseX(vm, "cannot increment non-numeric value", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      }
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      } else {
        vm->static_rc_eliminated++;
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopdec: {
#else
  case regopdec: {
#endif
      AKSValue val = frameslots[curr_inst->src1];
      if (ValIsInt(val)) {
        int64_t iv = ValToIntUnchecked(val);
        int64_t r;
        if (!ChkSubOvf(iv, 1, &r)) {
          if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
            ValFreeFunc(&frameslots[curr_inst->dst]);
          } else {
            vm->static_rc_eliminated++;
          }
          frameslots[curr_inst->dst] = ValInt(r);
          REG_DISPATCH_FAST();
        }
      }
      AKSValue res;
      if (ValKind(val) == VALINT) {
        int64_t r;
        if (ChkSubOvf(ValToInt(val), 1, &r)) {
          if (strict_ovf) {
            ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
            frame->ip = ip;
            task->stacktop = (int)(stacktop - task->stack);
            RaiseX(vm, "IntegerOverflowException: decrement overflow", &ip,
                   &stacktop, &frame, &frameslots);
            continue;
          } else {
            res = ValFloat((double)ValToInt(val) - 1.0);
          }
        } else {
          res = ValInt(r);
        }
      } else if (ValKind(val) == VALFLOAT) {
        res = ValFloat(ValToFloat(val) - 1.0);
      } else {
        ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        frame->ip = ip;
        task->stacktop = (int)(stacktop - task->stack);
        RaiseX(vm, "cannot decrement non-numeric value", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      }
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      } else {
        vm->static_rc_eliminated++;
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoppushfn: {
#else
  case regoppushfn: {
#endif
      ValFree(&frameslots[curr_inst->dst]);
      frameslots[curr_inst->dst] = ValFn(curr_inst->src1);
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoptrypush: {
#else
  case regoptrypush: {
#endif
      if (task->tryhandlercount >= 256) {
        fprintf(stderr, "Runtime error: too many nested try-catch blocks\n");
        return 0;
      }
      uint8_t *catch_bytecode =
          frame->fn->chunk.code +
          GetBytecodeOffset(frame->fn, curr_inst->targetip);
      if (task->tryhandlercount >= 256) {
        fprintf(stderr, "Runtime error: too many nested try-catch blocks\n");
        return 0;
      }
      task->tryhandlers[task->tryhandlercount].frameindex = task->framecount;
      task->tryhandlers[task->tryhandlercount].stacktopbase =
          (int)(stacktop - task->stack);
      task->tryhandlers[task->tryhandlercount].catchip = catch_bytecode;
      task->tryhandlercount++;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoptrypop: {
#else
  case regoptrypop: {
#endif
      if (task->tryhandlercount > 0) {
        task->tryhandlercount--;
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopthrow: {
#else
  case regopthrow: {
#endif
      AKSValue err = ValCopy(frameslots[curr_inst->src1]);
      char *msgstr = NULL;
      if (ValKind(err) == VALSTR) {
        msgstr = strdup(ValToStrPtr(err)->data);
      } else {
        AKSValue strval = ValToStr(err);
        msgstr = strdup(ValToStrPtr(strval)->data);
        ValFree(&strval);
      }
      ValFree(&err);
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      RaiseX(vm, msgstr, &ip, &stacktop, &frame, &frameslots);
      free(msgstr);
      continue;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopcoalesce: {
#else
  case regopcoalesce: {
#endif
      if (ValKind(frameslots[curr_inst->src1]) != VALNULL) {
        regip = (RegInst *)frame->fn->regcode + curr_inst->targetip;
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoprange: {
#else
  case regoprange: {
#endif
      AKSValue startval = frameslots[curr_inst->src1];
      AKSValue stopval = frameslots[curr_inst->src2];
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (ValKind(startval) != VALINT || ValKind(stopval) != VALINT) {
        RaiseX(vm, "Range bounds must be integers", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      }
      int64_t start = ValToInt(startval);
      int64_t end = ValToInt(stopval);

      AKSValue rangeval = ValStruct("Range");
      AKSStruct *s = ValToObj(rangeval);
      s->fieldcount = 2;
      s->fieldnames = malloc(2 * sizeof(char *));
      s->fieldvals = malloc(2 * sizeof(AKSValue));
      s->fieldnames[0] = strdup("start");
      s->fieldvals[0] = ValInt(start);
      s->fieldnames[1] = strdup("end");
      s->fieldvals[1] = ValInt(end);

      ValFree(&frameslots[curr_inst->dst]);
      frameslots[curr_inst->dst] = rangeval;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoptailcall: {
#else
  case regoptailcall: {
#endif
      int dst = curr_inst->dst;
      uint16_t targetfnid = curr_inst->src1;
      uint8_t argc = (uint8_t)curr_inst->src2;
      int isdynamic = (curr_inst->targetip == 65535);
      int calling_slotsbase = frame->slotsbase;

      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);

      int res;
      FnProto *callee = NULL;
      if (task->blockedonstdlib) {
        task->blockedonstdlib = 0;
        res = ExecuteStdlib(vm, task->stdlibname, task->stdlibargc, &stacktop);
      } else {
        if (isdynamic) {
          AKSValue calleeval = ValCopy(frameslots[targetfnid]);
          if (ValKind(calleeval) == VALBOUNDMETHOD) {
            AKSValue receiver = ValCopy(*ValToBoundMethod(calleeval)->receiver);
            char *methodname = strdup(ValToBoundMethod(calleeval)->name);
            ValFree(&calleeval);
            AKSStruct *s = ValToObj(receiver);
            int found = -1;
            for (int i = 0; i < s->fieldcount; i++) {
              if (strcmp(s->fieldnames[i], methodname) == 0) {
                found = i;
                break;
              }
            }
            uint16_t global_fnid = 65535;
            if (found < 0 && s->typename) {
              global_fnid = FindTypeMethod(vm, s->typename, methodname);
            }
            if ((found >= 0 && ValKind(s->fieldvals[found]) == VALFN) || global_fnid != 65535) {
              for (int i = argc - 1; i >= 0; i--) {
                ValFree(&frameslots[dst + i + 1]);
                frameslots[dst + i + 1] = ValCopy(frameslots[dst + i]);
              }
              ValFree(&frameslots[dst]);
              frameslots[dst] = ValCopy(receiver);
              argc++;
              if (found >= 0) {
                calleeval = ValCopy(s->fieldvals[found]);
              } else {
                calleeval = ValFn(global_fnid);
              }
              ValFree(&receiver);
              free(methodname);
            } else {
              AKSValue arr = ValArr(argc);
              ValToArr(arr)->count = argc;
              for (int i = 0; i < argc; i++) {
                ValToArr(arr)->items[i] = ValCopy(frameslots[dst + i]);
              }
              int mmfound = -1;
              for (int i = 0; i < s->fieldcount; i++) {
                if (strcmp(s->fieldnames[i], "methodmissing") == 0) {
                  mmfound = i;
                  break;
                }
              }
              if (mmfound < 0 || ValKind(s->fieldvals[mmfound]) != VALFN) {
                ValFree(&receiver);
                free(methodname);
                ValFree(&arr);
                RaiseX(vm, "methodmissing is not a function", &ip, &stacktop,
                       &frame, &frameslots);
                continue;
              }
              ValFree(&frameslots[dst]);
              frameslots[dst] = receiver;
              ValFree(&frameslots[dst + 1]);
              frameslots[dst + 1] = ValStr(methodname);
              free(methodname);
              ValFree(&frameslots[dst + 2]);
              frameslots[dst + 2] = arr;
              calleeval = ValCopy(s->fieldvals[mmfound]);
              argc = 3;
            }
          }
          if (ValKind(calleeval) != VALFN) {
            ValFree(&calleeval);
            RaiseX(vm, "expression is not a function", &ip, &stacktop, &frame,
                   &frameslots);
            continue;
          }
          uint16_t dynfn = ValToFn(calleeval);
          ValFree(&calleeval);
          if (dynfn >= vm->fncount) {
            RaiseX(vm, "invalid dynamic function ID", &ip, &stacktop, &frame,
                   &frameslots);
            continue;
          }
          callee = vm->fns[dynfn];
        } else {
          if (targetfnid >= vm->fncount) {
            RaiseX(vm, "invalid function ID", &ip, &stacktop, &frame,
                   &frameslots);
            continue;
          }
          callee = vm->fns[targetfnid];
        }

        for (int i = 0; i < argc; i++) {
          *stacktop++ = ValCopy(frameslots[dst + i]);
        }
        res = (callee && callee->chunk.count == 0) ? DispatchStdlib(vm, callee, callee->name, argc, &stacktop) : 0;
      }

      if (res == -2) {
        task->blockedonstdlib = 1;
        if (callee)
          strncpy(task->stdlibname, callee->name, 63);
        task->stdlibargc = argc;
        regip--;
        task->stacktop = (int)(stacktop - task->stack);
        return 2;
      } else if (res < 0) {
        RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      } else if (res == 1) {
        AKSValue retval = POP();
        int canaryidx = calling_slotsbase - 1;
        if (canaryidx < 0 || ValKind(task->stack[canaryidx]) != VALINT ||
            ValToInt(task->stack[canaryidx]) != (int64_t)vm->stackcanary) {
          fprintf(stderr, "SecurityError: Stack canary corruption detected\n");
          exit(1);
        }
        for (int i = 0; i < frame->fn->maxregisters; i++) {
          ValFree(&frameslots[i]);
        }
        ValFree(&task->stack[canaryidx]);
        task->framecount--;
        if (task->framecount == 0) {
          task->stacktop = calling_slotsbase - 1;
          task->result = retval;
          return 1;
        }
        frame = &task->frames[task->framecount - 1];
        frameslots = task->stack + frame->slotsbase;
        stacktop = task->stack + calling_slotsbase - 1;

        if (frame->fn->regcode) {
          int bytecodeoffset = (int)(frame->ip - frame->fn->chunk.code);
          int regoffset = frame->fn->offsettomap[bytecodeoffset];
          regip = (RegInst *)frame->fn->regcode + regoffset + 1;
          RegInst *callinst = (RegInst *)frame->fn->regcode + regoffset;
          ValFree(&frameslots[callinst->dst]);
          frameslots[callinst->dst] = retval;
        } else {
          ip = frame->ip;
          PUSH(retval);
          goto enter_stack_interpreter;
        }
      } else {
        while (task->tryhandlercount > 0 &&
               task->tryhandlers[task->tryhandlercount - 1].frameindex ==
                   task->framecount - 1) {
          task->tryhandlercount--;
        }
        AKSValue *dest = frameslots;
        AKSValue *src = stacktop - argc;

        int prevmaxreg = frame->fn->maxregisters;
        if (prevmaxreg == 0)
          prevmaxreg = frame->fn->localcount;
        for (int i = 0; i < prevmaxreg; i++) {
          ValFree(&dest[i]);
        }
        for (int i = 0; i < argc; i++) {
          dest[i] = src[i];
        }
        int callemxreg = callee->maxregisters;
        if (callemxreg == 0)
          callemxreg = callee->localcount;
        for (int i = argc; i < callemxreg; i++) {
          dest[i] = ValNull();
        }
        stacktop = dest + callemxreg;
        frame->fn = callee;
        frame->ip = callee->chunk.code;
        frame->argc = argc;

        if (frame->fn->regcode) {
          regip = (RegInst *)frame->fn->regcode;
        } else {
          ip = frame->ip;
          goto enter_stack_interpreter;
        }
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopspawn: {
#else
  case regopspawn: {
#endif
      int isdynamic = (curr_inst->targetip == 65535);
      int argc = curr_inst->src2;
      int dst = curr_inst->dst;
      FnProto *callee = NULL;
      int argbase;
      if (isdynamic) {
        int fnreg = curr_inst->src1;
        AKSValue calleeval = ValCopy(frameslots[fnreg]);
        ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        frame->ip = ip;
        task->stacktop = (int)(stacktop - task->stack);
        if (ValKind(calleeval) != VALFN) {
          ValFree(&calleeval);
          RaiseX(vm, "Callee is not a function", &ip, &stacktop, &frame,
                 &frameslots);
          continue;
        }
        callee = vm->fns[ValToFn(calleeval)];
        ValFree(&calleeval);
        argbase = dst - argc;
      } else {
        uint16_t fnid = (uint16_t)curr_inst->targetip;
        callee = vm->fns[fnid];
        argbase = curr_inst->src1;
      }

      AKSValue args[32];
      for (int i = 0; i < argc && i < 32; i++) {
        args[i] = ValCopy(frameslots[argbase + i]);
      }

      VMTask *spawned = TaskNew(vm, callee, 0);
      for (int i = 0; i < argc && i < callee->paramcount; i++) {
        ValFree(&spawned->stack[i]);
        spawned->stack[i] = args[i];
      }

      int targetworker = rand() % vm->workercount;
      WorkerPushTail(&vm->workers[targetworker], spawned);

      ValFree(&frameslots[dst]);
      frameslots[dst] = ValTask(spawned);
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopawait: {
#else
  case regopawait: {
#endif
      AKSValue taskval = ValCopy(frameslots[curr_inst->src1]);
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);

      if (ValKind(taskval) != VALTASK) {
        ValFree(&taskval);
        RaiseX(vm, "await operand is not a task", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      }
      VMTask *target = ValToTask(taskval);
      MutexLockNative(vm->globallock);
      if (target->state == 3) {
        if (target->haserror) {
          const char *errmsg = "Unknown task error";
          if (ValKind(target->result) == VALSTR) {
            errmsg = ValToStrPtr(target->result)->data;
          }
          MutexUnlockNative(vm->globallock);
          ValFree(&taskval);
          RaiseX(vm, errmsg, &ip, &stacktop, &frame, &frameslots);
          continue;
        }
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValCopy(target->result);
        MutexUnlockNative(vm->globallock);
        ValFree(&taskval);
      } else {
        if (target->awaitercount >= target->awaitercap) {
          target->awaitercap =
              target->awaitercap == 0 ? 4 : target->awaitercap * 2;
          target->awaiters =
              realloc(target->awaiters, target->awaitercap * sizeof(VMTask *));
        }
        target->awaiters[target->awaitercount++] = task;
        MutexUnlockNative(vm->globallock);

        regip--;
        task->state = 2;
        task->blockedon = target;
        task->blockedontype = 4;
        ValFree(&taskval);
        return 2;
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopshl: {
#else
  case regopshl: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        if (ValKind(frameslots[curr_inst->dst]) >= VALSTR) {
          ValFreeFunc(&frameslots[curr_inst->dst]);
        } else {
          vm->static_rc_eliminated++;
        }
        frameslots[curr_inst->dst] = ValInt(ValToInt(a) << ValToInt(b));
      } else {
        ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        frame->ip = ip;
        task->stacktop = (int)(stacktop - task->stack);
        RaiseX(vm, "TypeError: bitwise shift operands must be integers", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopshr: {
#else
  case regopshr: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValInt(ValToInt(a) >> ValToInt(b));
      } else {
        ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        frame->ip = ip;
        task->stacktop = (int)(stacktop - task->stack);
        RaiseX(vm, "TypeError: bitwise shift operands must be integers", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopbitand: {
#else
  case regopbitand: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValInt(ValToInt(a) & ValToInt(b));
      } else {
        ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        frame->ip = ip;
        task->stacktop = (int)(stacktop - task->stack);
        RaiseX(vm, "TypeError: bitwise AND operands must be integers", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopbitor: {
#else
  case regopbitor: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValInt(ValToInt(a) | ValToInt(b));
      } else {
        ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        frame->ip = ip;
        task->stacktop = (int)(stacktop - task->stack);
        RaiseX(vm, "TypeError: bitwise OR operands must be integers", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopbitxor: {
#else
  case regopbitxor: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValInt(ValToInt(a) ^ ValToInt(b));
      } else {
        ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        frame->ip = ip;
        task->stacktop = (int)(stacktop - task->stack);
        RaiseX(vm, "TypeError: bitwise XOR operands must be integers", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopbitnot: {
#else
  case regopbitnot: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      if (ValKind(a) == VALINT) {
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValInt(~ValToInt(a));
      } else {
        ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
        frame->ip = ip;
        task->stacktop = (int)(stacktop - task->stack);
        RaiseX(vm, "TypeError: bitwise NOT operand must be an integer", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopapply: {
#else
  case regopapply: {
#endif
      int dst = curr_inst->dst;
      int calleereg = curr_inst->src1;
      int argsreg = curr_inst->src2;

      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);

      int res;
      FnProto *callee = NULL;
      int argc = 0;

      if (task->blockedonstdlib) {
        task->blockedonstdlib = 0;
        res = ExecuteStdlib(vm, task->stdlibname, task->stdlibargc, &stacktop);
        argc = task->stdlibargc;
      } else {
        AKSValue calleeval = ValCopy(frameslots[calleereg]);
        AKSValue argsval = ValCopy(frameslots[argsreg]);
        if (ValKind(argsval) != VALARR) {
          ValFree(&calleeval);
          ValFree(&argsval);
          RaiseX(vm, "TypeError: apply argument must be an array", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        }
        AKSArray *arr = ValToArr(argsval);
        argc = arr->count;
        for (int i = 0; i < argc; i++) {
          *stacktop++ = ValCopy(arr->items[i]);
        }
        ValFree(&argsval);

        if (ValKind(calleeval) == VALBOUNDMETHOD) {
          AKSValue receiver = ValCopy(*ValToBoundMethod(calleeval)->receiver);
          char *methodname = strdup(ValToBoundMethod(calleeval)->name);
          ValFree(&calleeval);
          AKSStruct *s = ValToObj(receiver);
          int found = -1;
          for (int i = 0; i < s->fieldcount; i++) {
            if (strcmp(s->fieldnames[i], methodname) == 0) {
              found = i;
              break;
            }
          }
          uint16_t global_fnid = 65535;
          if (found < 0 && s->typename) {
            global_fnid = FindTypeMethod(vm, s->typename, methodname);
          }
          if ((found >= 0 && ValKind(s->fieldvals[found]) == VALFN) || global_fnid != 65535) {
            if (stacktop >= stacklimit) {
              ValFree(&receiver);
              free(methodname);
              RaiseX(vm, "StackOverflowError: stack overflow on dynamic call",
                     &ip, &stacktop, &frame, &frameslots);
              continue;
            }
            for (int i = 1; i <= argc; i++) {
              stacktop[-i + 1] = stacktop[-i];
            }
            stacktop[-argc] = ValCopy(receiver);
            stacktop++;
            argc++;
            if (found >= 0) {
              calleeval = ValCopy(s->fieldvals[found]);
            } else {
              calleeval = ValFn(global_fnid);
            }
            ValFree(&receiver);
            free(methodname);
          } else {
            AKSValue arrmethod = ValArr(argc);
            ValToArr(arrmethod)->count = argc;
            for (int i = 0; i < argc; i++) {
              ValToArr(arrmethod)->items[i] = stacktop[-argc + i];
            }
            stacktop -= argc;
            int mmfound = -1;
            for (int i = 0; i < s->fieldcount; i++) {
              if (strcmp(s->fieldnames[i], "methodmissing") == 0) {
                mmfound = i;
                break;
              }
            }
            if (mmfound < 0 || ValKind(s->fieldvals[mmfound]) != VALFN) {
              ValFree(&receiver);
              free(methodname);
              ValFree(&arrmethod);
              RaiseX(vm, "methodmissing is not a function", &ip, &stacktop,
                     &frame, &frameslots);
              continue;
            }
            *stacktop++ = receiver;
            *stacktop++ = ValStr(methodname);
            free(methodname);
            *stacktop++ = arrmethod;
            calleeval = ValCopy(s->fieldvals[mmfound]);
            argc = 3;
          }
        }

        if (ValKind(calleeval) != VALFN) {
          ValFree(&calleeval);
          RaiseX(vm, "expression is not a function", &ip, &stacktop, &frame,
                 &frameslots);
          continue;
        }
        uint16_t fnid = ValToFn(calleeval);
        ValFree(&calleeval);

        if (fnid >= vm->fncount) {
          RaiseX(vm, "invalid function ID", &ip, &stacktop, &frame,
                 &frameslots);
          continue;
        }
        callee = vm->fns[fnid];
        if (callee->isasync) {
          AKSValue args[32];
          for (int i = argc - 1; i >= 0; i--) {
            args[i] = POP();
          }
          VMTask *spawned = TaskNew(vm, callee, 0);
          for (int i = 0; i < argc && i < callee->paramcount; i++) {
            ValFree(&spawned->stack[i]);
            spawned->stack[i] = args[i];
          }
          int targetworker = rand() % vm->workercount;
          WorkerPushTail(&vm->workers[targetworker], spawned);
          ValFree(&frameslots[dst]);
          frameslots[dst] = ValTask(spawned);
          REG_DISPATCH_OR_BREAK();
        }
        res = (callee && callee->chunk.count == 0) ? DispatchStdlib(vm, callee, callee->name, argc, &stacktop) : 0;
      }

      if (res == -2) {
        task->blockedonstdlib = 1;
        if (callee)
          strncpy(task->stdlibname, callee->name, 63);
        task->stdlibargc = argc;
        regip--;
        task->stacktop = (int)(stacktop - task->stack);
        return 2;
      } else if (res < 0) {
        RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      } else if (res == 1) {
        AKSValue ret = POP();
        ValFree(&frameslots[dst]);
        frameslots[dst] = ret;
      } else {
        if (task->framecount >= MAXFRAMES) {
          RaiseX(vm, "stack overflow (too many call frames)", &ip, &stacktop,
                 &frame, &frameslots);
          continue;
        }
        AKSValue *canarypos = stacktop - argc;
        for (AKSValue *p = stacktop; p > canarypos; p--) {
          *p = *(p - 1);
        }
        *canarypos = ValInt(vm->stackcanary);
        stacktop++;

        FRAME_GROW();
        CallFrame *newframe = &task->frames[task->framecount++];
        newframe->fn = callee;
        newframe->ip = callee->chunk.code;
        newframe->slotsbase = (int)(canarypos + 1 - task->stack);
        newframe->argc = argc;

        STACK_GROW(newframe->slotsbase + callee->maxregisters + 1);
        for (int i = argc; i < callee->maxregisters; i++) {
          task->stack[newframe->slotsbase + i] = ValNull();
        }
        stacktop = task->stack + newframe->slotsbase + callee->maxregisters;

        frame = newframe;
        frameslots = task->stack + frame->slotsbase;
        if (frame->fn->regcode) {
          regip = (RegInst *)frame->fn->regcode;
        } else {
          ip = frame->ip;
          goto enter_stack_interpreter;
        }
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regoprestargs: {
#else
  case regoprestargs: {
#endif
      uint8_t paramidx = (uint8_t)curr_inst->dst;
      int currentargc = frame->argc;
      if (currentargc < paramidx + 1) {
        for (int i = currentargc; i < paramidx + 1; i++) {
          frameslots[i] = ValNull();
        }
        frame->argc = paramidx + 1;
      }
      int count = currentargc - paramidx;
      if (count < 0)
        count = 0;
      AKSValue arrval = ValArr(count);
      ValToArr(arrval)->count = count;
      for (int i = 0; i < count; i++) {
        ValToArr(arrval)->items[i] = frameslots[paramidx + i];
      }
      frameslots[paramidx] = arrval;
      for (int i = paramidx + 1; i < currentargc; i++) {
        frameslots[i] = ValNull();
      }
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopaddreg: {
#else
  case regopaddreg: {
#endif
      /* Fast path: int+int without overflow (no struct overloads possible here) */
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = frameslots[curr_inst->src2];
      if (ValIsInt(a) & ValIsInt(b)) {
        int64_t r;
        if (!ChkAddOvf(ValToInt(a), ValToInt(b), &r)) {
          if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
            ValFreeFunc(&frameslots[curr_inst->dst]);
          } else {
            vm->static_rc_eliminated++;
          }
          frameslots[curr_inst->dst] = ValInt(r);
          REG_DISPATCH_FAST();
        }
      }
      /* Slow path */
      ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      if (RegCheckOverload(vm, task, "opadd", curr_inst->dst, curr_inst->src1,
                           curr_inst->src2, &regip, &stacktop, &frame,
                           &frameslots))
        continue;
      AKSValue res;
      if (ValIsInt(a) && ValIsInt(b)) {
        if (strict_ovf) {
          RaiseX(vm, "IntegerOverflowException: addition overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        }
        res = ValFloat((double)ValToInt(a) + (double)ValToInt(b));
      } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
        res = ValFloat(ValToFloat(a) + ValToFloat(b));
      } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALINT) {
        res = ValFloat(ValToFloat(a) + (double)ValToInt(b));
      } else if (ValKind(a) == VALINT && ValKind(b) == VALFLOAT) {
        res = ValFloat((double)ValToInt(a) + ValToFloat(b));
      } else {
        res = ValAdd(a, b);
      }
      if (!ValIsPrimitive(frameslots[curr_inst->dst])) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      } else {
        vm->static_rc_eliminated++;
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_OR_BREAK();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopaddint: {
#else
  case regopaddint: {
#endif
      AKSValue a = frameslots[curr_inst->src1];
      AKSValue b = curr_inst->constval;
      AKSValue res;
      if (ValKind(a) == VALINT) {
        int64_t r;
        if (ChkAddOvf(ValToInt(a), ValToInt(b), &r)) {
          if (strict_ovf) {
            ip = frame->fn->chunk.code + curr_inst->bytecodeoffset;
            frame->ip = ip;
            task->stacktop = (int)(stacktop - task->stack);
            RaiseX(vm, "IntegerOverflowException: addition overflow", &ip,
                   &stacktop, &frame, &frameslots);
            continue;
          } else {
            res = ValFloat((double)ValToInt(a) + (double)ValToInt(b));
          }
        } else {
          res = ValInt(r);
        }
      } else {
        res = ValAdd(a, b);
      }
      if (ValKind(frameslots[curr_inst->dst]) >= VALSTR) {
        ValFreeFunc(&frameslots[curr_inst->dst]);
      } else {
        vm->static_rc_eliminated++;
      }
      frameslots[curr_inst->dst] = res;
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopcopy: {
#else
  case regopcopy: {
#endif
      if (curr_inst->dst == curr_inst->src1) {
        REG_DISPATCH_FAST();
      }
      AKSValue srcval = frameslots[curr_inst->src1];
      if (ValIsPrimitive(srcval) && ValIsPrimitive(frameslots[curr_inst->dst])) {
        frameslots[curr_inst->dst] = srcval;
        vm->static_rc_eliminated++;
        REG_DISPATCH_FAST();
      }
      ValFree(&frameslots[curr_inst->dst]);
      frameslots[curr_inst->dst] = ValCopy(srcval);
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopwriteln: {
#else
  case regopwriteln: {
#endif
      ValPrintln(frameslots[curr_inst->src1]);
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopinputstr: {
#else
  case regopinputstr: {
#endif
      if (curr_inst->src1 >= 0) {
        ValPrint(frameslots[curr_inst->src1]);
        fflush(stdout);
      } else {
        fflush(stdout);
      }
      char buf[1024];
      if (fgets(buf, sizeof(buf), stdin)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
          buf[--len] = '\0';
        }
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValStr(buf);
      } else {
        ValFree(&frameslots[curr_inst->dst]);
        frameslots[curr_inst->dst] = ValStr("");
      }
      REG_DISPATCH_FAST();
    }

#if defined(__GNUC__) || defined(__clang__)
    do_regopprintrepl: {
#else
  case regopprintrepl: {
#endif
      AKSValue val = frameslots[curr_inst->src1];
      if (ValKind(val) != VALNULL) {
        if (ValKind(val) == VALSTR) {
          printf("\033[32m\"%s\"\033[0m\n", ValToStrPtr(val)->data);
        } else if (ValKind(val) == VALINT) {
          printf("\033[36m%lld\033[0m\n", (long long)ValToInt(val));
        } else if (ValKind(val) == VALFLOAT) {
          printf("\033[36m%g\033[0m\n", ValToFloat(val));
        } else if (ValKind(val) == VALBOOL) {
          printf("\033[33m%s\033[0m\n", ValToBool(val) ? "true" : "false");
        } else if (ValKind(val) == VALARR) {
          printf("\033[35m");
          ValPrint(val);
          printf("\033[0m\n");
        } else if (ValKind(val) == VALSTRUCT) {
          printf("\033[34m");
          ValPrint(val);
          printf("\033[0m\n");
        } else {
          ValPrintln(val);
        }
      }
      REG_DISPATCH_FAST();
    }

#if !defined(__GNUC__) && !defined(__clang__)
    }
#endif
  }

enter_stack_interpreter: for (;;) {
  while (task->framecount > lastframecount) {
    if (lastframecount >= 0 && lastframecount < MAXFRAMES) {
      task->arenatops[lastframecount] = task->stackarenatop;
    }
    lastframecount++;
  }
  while (task->framecount < lastframecount) {
    lastframecount--;
    if (lastframecount >= 0 && lastframecount < MAXFRAMES) {
      task->stackarenatop = task->arenatops[lastframecount];
    }
  }
  if (frame->fn->regcode && vm->jitmode == 0) {
    goto enter_register_interpreter;
  }
  if (task->state == 3) {
    frame->ip = ip;
    task->stacktop = (int)(stacktop - task->stack);
    return -1;
  }
  if (task->cancelled) {
    task->cancelled = 0;
    RaiseX(vm, "CancelledError: task was cancelled", &ip, &stacktop, &frame,
           &frameslots);
    continue;
  }
  if (instructionsrun >= maxinstructions) {
    frame->ip = ip;
    task->stacktop = (int)(stacktop - task->stack);
    return 0; // gave up the time slice
  }
  instructionsrun++;
  vm->instructioncount++;
  gVMProfiling.total_instructions++;

  if (vm->sandboxenabled) {
    task->totalinstructionsrun++;
    if (task->totalinstructionsrun > 5000000) {
      RaiseX(vm, "SandboxError: execution instruction limit exceeded", &ip,
             &stacktop, &frame, &frameslots);
      frame->ip = ip;
      task->stacktop = (int)(stacktop - task->stack);
      return -1;
    }
  }
  if (gCoverageEnabled && frame->fn->chunk.executed) {
    frame->fn->chunk.executed[(int)(ip - frame->fn->chunk.code)] = 1;
  }
  if (GDapActive) {
    frame->ip = ip;
    task->stacktop = (int)(stacktop - task->stack);
    DAPCheckDebugger(vm, task, frame, (int)(ip - frame->fn->chunk.code));
    ip = frame->ip;
    stacktop = task->stack + task->stacktop;
    frameslots = task->stack + frame->slotsbase;
  }

  int currentoffset = (int)(ip - frame->fn->chunk.code);
  // record this step if we're mid-trace
  if (task->tracingmode == 1) {
    if (currentoffset == task->tracestartoffset && task->tracestepcount > 0) {
      task->tracingmode = 0;
      TriggerTracingJIT(vm, task, frame->fn, task->tracestartoffset,
                        task->tracestartoffset,
                        (TraceStep *)task->recordedtrace, task->tracestepcount);
    } else {
      RecordTraceStep(task, *ip, ip, stacktop, frame->fn);
    }
  }

  if (HasJITCompiled(frame->fn, currentoffset, vm)) {
    task->tracingmode = 0;
    task->tracestepcount = 0;
    task->stacktop = (int)(stacktop - task->stack);
    ExecuteJITCompiled(vm, task, frame->fn, currentoffset, &stacktop, &frame,
                       &frameslots, &ip);
    continue;
  }

  uint8_t op = READBYTE();
  if (vm->profilingmode)
    vm->instructioncounters[op]++;
  // op is always a byte - __assume lets MSVC drop the range guard on the switch
  // table
  __assume(op < 256);

  switch (op) {
  case OPHALT:
    frame->ip = ip;
    task->stacktop = (int)(stacktop - task->stack);
    if (task->framecount > 1) {
      if (frame->slotsbase < 0 ||
          frame->slotsbase > (int)(stacktop - task->stack)) {
        fprintf(stderr, "SecurityError: Frame slotsbase corruption detected\n");
        exit(1);
      }
      int canaryidx = frame->slotsbase - 1;
      if (canaryidx < 0 || ValKind(task->stack[canaryidx]) != VALINT ||
          ValToInt(task->stack[canaryidx]) != (int64_t)vm->stackcanary) {
        fprintf(stderr, "SecurityError: Stack canary corruption detected\n");
        exit(1);
      }

      int base = frame->slotsbase;
      int topidx = (int)(stacktop - task->stack);
      for (int i = base; i < topidx; i++) {
        ValFree(&task->stack[i]);
      }
      task->framecount--;
      frame = &task->frames[task->framecount - 1];
      ip = frame->ip;
      frameslots = task->stack + frame->slotsbase;
      stacktop = task->stack + base - 1; // remove canary!

      uint8_t *codestart = frame->fn->chunk.code;
      uint8_t *codeend = codestart + frame->fn->chunk.count;
      if (ip < codestart || ip > codeend) {
        fprintf(stderr, "SecurityError: Return IP corruption detected\n");
        exit(1);
      }

      PUSH(ValNull());
    } else {
      task->result = ValNull();
      return 1;
    }
    break;

  case OPPUSHINT: {
    int64_t val = 0;
    for (int i = 0; i < 8; i++) {
      val = (val << 8) | READBYTE();
    }
    PUSH(ValInt(val));
    break;
  }

  case OPPUSHFLOAT: {
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) {
      bits = (bits << 8) | READBYTE();
    }
    double val;
    memcpy(&val, &bits, 8);
    PUSH(ValFloat(val));
    break;
  }

  case OPPUSHSTR: {
    uint16_t sid = READU16();
    char *unesc = strdup(vm->strpool[sid]);
    UnEscapeStrInplace(unesc);
    AKSValue sv = ValStr(unesc);
    free(unesc);
    PUSH(sv);
    break;
  }

  case OPPUSHBOOL: {
    uint8_t val = READBYTE();
    PUSH(ValBool(val));
    break;
  }

  case OPPUSHNULL:
    PUSH(ValNull());
    break;

  case OPLOAD: {
    uint16_t slot = READU16();
    AKSValue *target = frameslots + slot;
    if (target < stacktop) {
      PUSH(ValCopy(*target));
    } else {
      PUSH(ValNull());
    }
    break;
  }

  case OPPUSHREF: {
    uint16_t slot = READU16();
    AKSValue *target = frameslots + slot;
    PUSH(ValCopy(*target));
    break;
  }

  case OPDEREF: {
    AKSValue ref = POP();
    PUSH(ValCopy(ref));
    ValFree(&ref);
    break;
  }

  case OPINVALIDATE: {
    uint16_t slot = READU16();
    AKSValue *target = frameslots + slot;
    ValFree(target);
    *target = ValNull();
    break;
  }

  case OPLOADGLOBAL: {
    uint16_t gsid = READU16();
    const char *name = vm->strpool[gsid];
    int found = -1;

    int ipoffset = (int)(ip - 3 - frame->fn->chunk.code);
    if (frame->fn->chunk.globalcache) {
      int cachedidx = frame->fn->chunk.globalcache[ipoffset];
      if (cachedidx >= 0 && cachedidx < vm->globalcount &&
          strcmp(vm->globals[cachedidx].name, name) == 0) {
        found = cachedidx;
      }
    }

    if (found < 0) {
      for (int i = 0; i < vm->globalcount; i++) {
        if (strcmp(vm->globals[i].name, name) == 0) {
          found = i;
          break;
        }
      }
      if (found >= 0 && frame->fn->chunk.globalcache) {
        frame->fn->chunk.globalcache[ipoffset] = found;
      }
    }

    if (found >= 0) {
      PUSH(ValCopy(vm->globals[found].val));
    } else {
      int fn_found = -1;
      for (uint16_t k = 0; k < vm->fncount; k++) {
        if (vm->fns[k] && vm->fns[k]->name && strcmp(vm->fns[k]->name, name) == 0) {
          PUSH(ValFn(k));
          fn_found = 1;
          break;
        }
      }
      if (fn_found < 0) {
        PUSH(ValStr(name));
      }
    }
    break;
  }

  case OPSTORE: {
    uint16_t slot = READU16();
    AKSValue val = POP();
    AKSValue *target = frameslots + slot;
    if (target >= stacktop) {
      while (stacktop < target) {
        *stacktop++ = ValNull();
      }
      stacktop = target + 1;
    } else {
      ValFree(target);
    }
    *target = val;
    break;
  }

  case OPSTOREGLOBAL: {
    uint16_t gsid = READU16();
    const char *name = vm->strpool[gsid];
    AKSValue val = POP();
    int found = -1;

    int ipoffset = (int)(ip - 3 - frame->fn->chunk.code);
    if (frame->fn->chunk.globalcache) {
      int cachedidx = frame->fn->chunk.globalcache[ipoffset];
      if (cachedidx >= 0 && cachedidx < vm->globalcount &&
          strcmp(vm->globals[cachedidx].name, name) == 0) {
        found = cachedidx;
      }
    }

    if (found < 0) {
      for (int i = 0; i < vm->globalcount; i++) {
        if (strcmp(vm->globals[i].name, name) == 0) {
          found = i;
          break;
        }
      }
      if (found >= 0 && frame->fn->chunk.globalcache) {
        frame->fn->chunk.globalcache[ipoffset] = found;
      }
    }

    if (found >= 0) {
      ValFree(&vm->globals[found].val);
      vm->globals[found].val = val;
    } else {
      if (vm->globalcount >= MAXGLOBALS) {
        fprintf(stderr, "Runtime error: too many global variables\n");
        ValFree(&val);
        return 0;
      }
      vm->globals[vm->globalcount].name = strdup(name);
      vm->globals[vm->globalcount].val = val;
      if (frame->fn->chunk.globalcache) {
        frame->fn->chunk.globalcache[ipoffset] = vm->globalcount;
      }
      vm->globalcount++;
    }
    break;
  }

  case OPDUP:
    PUSH(ValCopy(*(stacktop - 1)));
    break;

  case OPPOP: {
    AKSValue val = POP();
    ValFree(&val);
    break;
  }

  case OPPRINTLN: {
    AKSValue val = POP();
    ValPrintln(val);
    ValFree(&val);
    break;
  }

  case OPINPUTSTR: {
    uint16_t flags = READU16();
    int has_prompt = (flags & 1);
    if (has_prompt) {
      if (stacktop > task->stack) {
        AKSValue promptval = POP();
        ValPrint(promptval);
        fflush(stdout);
        ValFree(&promptval);
      }
    } else {
      fflush(stdout);
    }
    char buf[1024];
    if (!fgets(buf, sizeof(buf), stdin)) {
      buf[0] = '\0';
    }
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
      buf[--len] = '\0';
    }
    PUSH(ValStr(buf));
    break;
  }

  case OPSHL: {
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValInt(ValToIntUnchecked(a) << ValToIntUnchecked(b)));
    } else {
      ValFree(&a);
      ValFree(&b);
      RaiseX(vm, "TypeError: shift left operands must be integers", &ip,
             &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }
  case OPSHR: {
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValInt(ValToIntUnchecked(a) >> ValToIntUnchecked(b)));
    } else {
      ValFree(&a);
      ValFree(&b);
      RaiseX(vm, "TypeError: shift right operands must be integers", &ip,
             &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }
  case OPBITAND: {
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValInt(ValToIntUnchecked(a) & ValToIntUnchecked(b)));
    } else {
      ValFree(&a);
      ValFree(&b);
      RaiseX(vm, "TypeError: bitwise AND operands must be integers", &ip,
             &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }
  case OPBITOR: {
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValInt(ValToIntUnchecked(a) | ValToIntUnchecked(b)));
    } else {
      ValFree(&a);
      ValFree(&b);
      RaiseX(vm, "TypeError: bitwise OR operands must be integers", &ip,
             &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }
  case OPBITXOR: {
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValInt(ValToIntUnchecked(a) ^ ValToIntUnchecked(b)));
    } else {
      ValFree(&a);
      ValFree(&b);
      RaiseX(vm, "TypeError: bitwise XOR operands must be integers", &ip,
             &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }
  case OPBITNOT: {
    AKSValue a = POP();
    if (ValKind(a) == VALINT) {
      PUSH(ValInt(~ValToIntUnchecked(a)));
    } else {
      ValFree(&a);
      RaiseX(vm, "TypeError: bitwise NOT operand must be an integer", &ip,
             &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&a);
    break;
  }

  case OPADD: {
    if (stacktop - task->stack >= 2) {
      if (ValIsInt(stacktop[-2]) && ValIsInt(stacktop[-1])) {
        int64_t res;
        if (!ChkAddOvf(ValToIntUnchecked(stacktop[-2]), ValToIntUnchecked(stacktop[-1]), &res)) {
          stacktop[-2] = ValInt(res);
          stacktop--;
          break;
        }
      } else if (ValIsFloat(stacktop[-2]) && ValIsFloat(stacktop[-1])) {
        stacktop[-2] = ValFloat(ValToFloat(stacktop[-2]) + ValToFloat(stacktop[-1]));
        stacktop--;
        break;
      }
    }
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "opadd"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      int64_t res;
      if (ChkAddOvf(ValToIntUnchecked(a), ValToIntUnchecked(b), &res)) {
        if (gStrictOvf) {
          ValFree(&a);
          ValFree(&b);
          RaiseX(vm, "IntegerOverflowException: addition overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        } else {
          PUSH(ValFloat((double)ValToIntUnchecked(a) + (double)ValToIntUnchecked(b)));
        }
      } else {
        PUSH(ValInt(res));
      }
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
      PUSH(ValFloat(ValToFloat(a) + ValToFloat(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALINT) {
      PUSH(ValFloat(ValToFloat(a) + (double)ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALINT && ValKind(b) == VALFLOAT) {
      PUSH(ValFloat((double)ValToIntUnchecked(a) + ValToFloat(b)));
    } else {
      PUSH(ValAdd(a, b));
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPSUB: {
    if (stacktop - task->stack >= 2) {
      if (ValIsInt(stacktop[-2]) && ValIsInt(stacktop[-1])) {
        int64_t res;
        if (!ChkSubOvf(ValToIntUnchecked(stacktop[-2]), ValToIntUnchecked(stacktop[-1]), &res)) {
          stacktop[-2] = ValInt(res);
          stacktop--;
          break;
        }
      } else if (ValIsFloat(stacktop[-2]) && ValIsFloat(stacktop[-1])) {
        stacktop[-2] = ValFloat(ValToFloat(stacktop[-2]) - ValToFloat(stacktop[-1]));
        stacktop--;
        break;
      }
    }
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "opsub"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      int64_t res;
      if (ChkSubOvf(ValToIntUnchecked(a), ValToIntUnchecked(b), &res)) {
        if (gStrictOvf) {
          ValFree(&a);
          ValFree(&b);
          RaiseX(vm, "IntegerOverflowException: subtraction overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        } else {
          PUSH(ValFloat((double)ValToIntUnchecked(a) - (double)ValToIntUnchecked(b)));
        }
      } else {
        PUSH(ValInt(res));
      }
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
      PUSH(ValFloat(ValToFloat(a) - ValToFloat(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALINT) {
      PUSH(ValFloat(ValToFloat(a) - (double)ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALINT && ValKind(b) == VALFLOAT) {
      PUSH(ValFloat((double)ValToIntUnchecked(a) - ValToFloat(b)));
    } else {
      PUSH(ValSub(a, b));
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPMUL: {
    if (stacktop - task->stack >= 2) {
      if (ValIsInt(stacktop[-2]) && ValIsInt(stacktop[-1])) {
        int64_t res;
        if (!ChkMulOvf(ValToIntUnchecked(stacktop[-2]), ValToIntUnchecked(stacktop[-1]), &res)) {
          stacktop[-2] = ValInt(res);
          stacktop--;
          break;
        }
      } else if (ValIsFloat(stacktop[-2]) && ValIsFloat(stacktop[-1])) {
        stacktop[-2] = ValFloat(ValToFloat(stacktop[-2]) * ValToFloat(stacktop[-1]));
        stacktop--;
        break;
      }
    }
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "opmul"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      int64_t res;
      if (ChkMulOvf(ValToIntUnchecked(a), ValToIntUnchecked(b), &res)) {
        if (gStrictOvf) {
          ValFree(&a);
          ValFree(&b);
          RaiseX(vm, "IntegerOverflowException: multiplication overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        } else {
          PUSH(ValFloat((double)ValToIntUnchecked(a) * (double)ValToIntUnchecked(b)));
        }
      } else {
        PUSH(ValInt(res));
      }
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
      PUSH(ValFloat(ValToFloat(a) * ValToFloat(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALINT) {
      PUSH(ValFloat(ValToFloat(a) * (double)ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALINT && ValKind(b) == VALFLOAT) {
      PUSH(ValFloat((double)ValToIntUnchecked(a) * ValToFloat(b)));
    } else {
      PUSH(ValMul(a, b));
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPDIV: {
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "opdiv"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    if ((ValKind(b) == VALINT && ValToIntUnchecked(b) == 0) ||
        (ValKind(b) == VALFLOAT && ValToFloat(b) == 0.0)) {
      ValFree(&a);
      ValFree(&b);
      RaiseX(vm, "division by zero", &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      int64_t va = ValToIntUnchecked(a);
      int64_t vb = ValToIntUnchecked(b);
      if (va == -140737488355328LL && vb == -1) {
        if (gStrictOvf) {
          ValFree(&a);
          ValFree(&b);
          RaiseX(vm, "IntegerOverflowException: division overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        } else {
          PUSH(ValFloat((double)va / -1.0));
        }
      } else {
        PUSH(ValInt(va / vb));
      }
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
      PUSH(ValFloat(ValToFloat(a) / ValToFloat(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALINT) {
      PUSH(ValFloat(ValToFloat(a) / (double)ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALINT && ValKind(b) == VALFLOAT) {
      PUSH(ValFloat((double)ValToIntUnchecked(a) / ValToFloat(b)));
    } else {
      PUSH(ValDiv(a, b));
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPMOD: {
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "opmod"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    if ((ValKind(b) == VALINT && ValToIntUnchecked(b) == 0) ||
        (ValKind(b) == VALFLOAT && ValToFloat(b) == 0.0)) {
      ValFree(&a);
      ValFree(&b);
      RaiseX(vm, "modulo by zero", &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      int64_t va = ValToIntUnchecked(a);
      int64_t vb = ValToIntUnchecked(b);
      if (va == -140737488355328LL && vb == -1) {
        if (gStrictOvf) {
          ValFree(&a);
          ValFree(&b);
          RaiseX(vm, "IntegerOverflowException: modulo overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        } else {
          PUSH(ValInt(0));
        }
      } else {
        PUSH(ValInt(va % vb));
      }
    } else {
      PUSH(ValMod(a, b));
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPEQ: {
    if (stacktop - task->stack >= 2 && ValIsPrimitive(stacktop[-2]) && ValIsPrimitive(stacktop[-1]) && (ValIsInt(stacktop[-2]) == ValIsInt(stacktop[-1]))) {
      stacktop[-2] = ValBool(stacktop[-2] == stacktop[-1]);
      stacktop--;
      break;
    }
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "opeq"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    PUSH(ValEq(a, b));
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPNEQ: {
    if (stacktop - task->stack >= 2 && ValIsPrimitive(stacktop[-2]) && ValIsPrimitive(stacktop[-1]) && (ValIsInt(stacktop[-2]) == ValIsInt(stacktop[-1]))) {
      stacktop[-2] = ValBool(stacktop[-2] != stacktop[-1]);
      stacktop--;
      break;
    }
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "opneq"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    PUSH(ValNeq(a, b));
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPLT: {
    if (stacktop - task->stack >= 2) {
      if (ValIsInt(stacktop[-2]) && ValIsInt(stacktop[-1])) {
        stacktop[-2] = ValBool(ValToIntUnchecked(stacktop[-2]) < ValToIntUnchecked(stacktop[-1]));
        stacktop--;
        break;
      } else if (ValIsFloat(stacktop[-2]) && ValIsFloat(stacktop[-1])) {
        stacktop[-2] = ValBool(ValToFloat(stacktop[-2]) < ValToFloat(stacktop[-1]));
        stacktop--;
        break;
      }
    }
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "oplt"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValBool(ValToIntUnchecked(a) < ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
      PUSH(ValBool(ValToFloat(a) < ValToFloat(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALINT) {
      PUSH(ValBool(ValToFloat(a) < (double)ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALINT && ValKind(b) == VALFLOAT) {
      PUSH(ValBool((double)ValToIntUnchecked(a) < ValToFloat(b)));
    } else {
      PUSH(ValLt(a, b));
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPGT: {
    if (stacktop - task->stack >= 2) {
      if (ValIsInt(stacktop[-2]) && ValIsInt(stacktop[-1])) {
        stacktop[-2] = ValBool(ValToIntUnchecked(stacktop[-2]) > ValToIntUnchecked(stacktop[-1]));
        stacktop--;
        break;
      } else if (ValIsFloat(stacktop[-2]) && ValIsFloat(stacktop[-1])) {
        stacktop[-2] = ValBool(ValToFloat(stacktop[-2]) > ValToFloat(stacktop[-1]));
        stacktop--;
        break;
      }
    }
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "opgt"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValBool(ValToIntUnchecked(a) > ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
      PUSH(ValBool(ValToFloat(a) > ValToFloat(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALINT) {
      PUSH(ValBool(ValToFloat(a) > (double)ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALINT && ValKind(b) == VALFLOAT) {
      PUSH(ValBool((double)ValToIntUnchecked(a) > ValToFloat(b)));
    } else {
      PUSH(ValGt(a, b));
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPLTE: {
    if (stacktop - task->stack >= 2) {
      if (ValIsInt(stacktop[-2]) && ValIsInt(stacktop[-1])) {
        stacktop[-2] = ValBool(ValToIntUnchecked(stacktop[-2]) <= ValToIntUnchecked(stacktop[-1]));
        stacktop--;
        break;
      } else if (ValIsFloat(stacktop[-2]) && ValIsFloat(stacktop[-1])) {
        stacktop[-2] = ValBool(ValToFloat(stacktop[-2]) <= ValToFloat(stacktop[-1]));
        stacktop--;
        break;
      }
    }
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "oplte"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValBool(ValToIntUnchecked(a) <= ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
      PUSH(ValBool(ValToFloat(a) <= ValToFloat(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALINT) {
      PUSH(ValBool(ValToFloat(a) <= (double)ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALINT && ValKind(b) == VALFLOAT) {
      PUSH(ValBool((double)ValToIntUnchecked(a) <= ValToFloat(b)));
    } else {
      PUSH(ValLte(a, b));
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPGTE: {
    if (stacktop - task->stack >= 2) {
      if (ValIsInt(stacktop[-2]) && ValIsInt(stacktop[-1])) {
        stacktop[-2] = ValBool(ValToIntUnchecked(stacktop[-2]) >= ValToIntUnchecked(stacktop[-1]));
        stacktop--;
        break;
      } else if (ValIsFloat(stacktop[-2]) && ValIsFloat(stacktop[-1])) {
        stacktop[-2] = ValBool(ValToFloat(stacktop[-2]) >= ValToFloat(stacktop[-1]));
        stacktop--;
        break;
      }
    }
    if (stacktop - task->stack >= 2 && ValKind(stacktop[-2]) == VALSTRUCT &&
        ChkopOverload(vm, task, &ip, &stacktop, &frame, &frameslots, "opgte"))
      continue;
    AKSValue b = POP();
    AKSValue a = POP();
    if (ValKind(a) == VALINT && ValKind(b) == VALINT) {
      PUSH(ValBool(ValToIntUnchecked(a) >= ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALFLOAT) {
      PUSH(ValBool(ValToFloat(a) >= ValToFloat(b)));
    } else if (ValKind(a) == VALFLOAT && ValKind(b) == VALINT) {
      PUSH(ValBool(ValToFloat(a) >= (double)ValToIntUnchecked(b)));
    } else if (ValKind(a) == VALINT && ValKind(b) == VALFLOAT) {
      PUSH(ValBool((double)ValToIntUnchecked(a) >= ValToFloat(b)));
    } else {
      PUSH(ValGte(a, b));
    }
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPAND: {
    AKSValue b = POP();
    AKSValue a = POP();
    PUSH(ValAnd(a, b));
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPOR: {
    AKSValue b = POP();
    AKSValue a = POP();
    PUSH(ValOr(a, b));
    ValFree(&a);
    ValFree(&b);
    break;
  }

  case OPNOT: {
    AKSValue a = POP();
    PUSH(ValNot(a));
    ValFree(&a);
    break;
  }

  case OPNEG: {
    AKSValue a = POP();
    if (ValKind(a) == VALINT) {
      int64_t val = ValToInt(a);
      if (val == -140737488355328LL) {
        if (gStrictOvf) {
          ValFree(&a);
          RaiseX(vm, "IntegerOverflowException: negation overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        } else {
          PUSH(ValFloat(-(double)-140737488355328LL));
        }
      } else {
        PUSH(ValInt(-val));
      }
    } else if (ValKind(a) == VALFLOAT) {
      PUSH(ValFloat(-ValToFloat(a)));
    } else {
      PUSH(ValNeg(a));
    }
    ValFree(&a);
    break;
  }

  case OPJUMP: {
    int32_t offset = READI32();
    uint8_t *targetip = ip + offset;
    uint8_t *codestart = frame->fn->chunk.code;
    uint8_t *codeend = codestart + frame->fn->chunk.count;
    if (targetip < codestart || targetip >= codeend) {
      RaiseX(vm, "CFIError: Jump target out of function bounds", &ip, &stacktop,
             &frame, &frameslots);
      continue;
    }
    if (offset < 0 && frame->fn->chunk.loopcounters) {
      int targetoffset = (int)(ip + offset - frame->fn->chunk.code);
      frame->fn->chunk.loopcounters[targetoffset]++;

      int hot_thresh = vm->hot_loop_threshold > 0 ? vm->hot_loop_threshold : 50;
      if (frame->fn->chunk.loopcounters[targetoffset] >= hot_thresh &&
          !HasJITCompiled(frame->fn, targetoffset, vm)) {
        if (task->tracingmode == 0) {
          task->tracingmode = 1;
          task->tracestartoffset = targetoffset;
          task->tracestepcount = 0;
        }
      }

      if (frame->fn->chunk.loopcounters[targetoffset] >= hot_thresh &&
          !HasJITCompiled(frame->fn, targetoffset, vm)) {
        TriggerJIT(vm, frame->fn, targetoffset,
                   (int)(ip - frame->fn->chunk.code));
      }
      if (HasJITCompiled(frame->fn, targetoffset, vm)) {
        task->tracingmode = 0;
        task->tracestepcount = 0;
        task->stacktop = (int)(stacktop - task->stack);
        ExecuteJITCompiled(vm, task, frame->fn, targetoffset, &stacktop, &frame,
                           &frameslots, &ip);
        continue;
      }
    }
    ip += offset;
    break;
  }

  case OPJUMPIFFALSE: {
    int32_t offset = READI32();
    AKSValue cond = POP();
    int truthy = ValIsTruthy(cond);
    ValFree(&cond);
    if (!truthy) {
      uint8_t *targetip = ip + offset;
      uint8_t *codestart = frame->fn->chunk.code;
      uint8_t *codeend = codestart + frame->fn->chunk.count;
      if (targetip < codestart || targetip >= codeend) {
        RaiseX(vm, "CFIError: Jump target out of function bounds", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      ip += offset;
    }
    break;
  }

  case OPCALL: {
    if (task->blockedonstdlib) {
      task->blockedonstdlib = 0;
      int res =
          ExecuteStdlib(vm, task->stdlibname, task->stdlibargc, &stacktop);
      if (res == -2) {
        task->blockedonstdlib = 1;
        frame->ip = ip - 1; // Keep ip at opcall
        task->stacktop = (int)(stacktop - task->stack);
        return 2; // blocked
      } else if (res < 0) {
        RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      } else {
        ip += 3; // advance IP past operands (fnid (2) + argc (1))
        break;
      }
    }
    uint16_t fnid = READU16();
    uint8_t argc = READBYTE();
    int hasuaf = 0;
    for (int i = 0; i < argc; i++) {
      if (ValIsInvalidated(stacktop[-argc + i])) {
        hasuaf = 1;
        break;
      }
    }
    if (hasuaf) {
      RaiseX(vm,
             "UseAfterFreeError: attempt to use an explicitly "
             "freed/invalidated object",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (fnid >= vm->fncount) {
      char buf[64];
      snprintf(buf, sizeof(buf), "function ID %d invalid", fnid);
      RaiseX(vm, buf, &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    FnProto *callee = vm->fns[fnid];

    if (callee->isasync) {
      AKSValue args[32];
      for (int i = argc - 1; i >= 0; i--) {
        args[i] = POP();
      }
      VMTask *spawned = TaskNew(vm, callee, 0);
      for (int i = 0; i < argc && i < callee->paramcount; i++) {
        ValFree(&spawned->stack[i]);
        spawned->stack[i] = args[i];
      }
      int targetworker = rand() % vm->workercount;
      WorkerPushTail(&vm->workers[targetworker], spawned);
      PUSH(ValTask(spawned));
      break;
    }

    task->stacktop = (int)(stacktop - task->stack);
    int res = (callee && callee->chunk.count == 0) ? DispatchStdlib(vm, callee, callee->name, argc, &stacktop) : 0;
    if (res == -2) {
      task->blockedonstdlib = 1;
      strncpy(task->stdlibname, callee->name, 63);
      int resumeargc = argc;
      if (strcmp(callee->name, "ParallelMapRaw") == 0)
        resumeargc = 4;
      else if (strcmp(callee->name, "ParallelReduceRaw") == 0)
        resumeargc = 6;
      task->stdlibargc = resumeargc;
      frame->ip = ip - 4; // Keep ip at opcall
      task->stacktop = (int)(stacktop - task->stack);
      return 2; // blocked
    } else if (res < 0) {
      RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    } else if (res == 1) {
      // stdlib call completed
    } else {
      if (fnid < 4096) {
        vm->fncallcounts[fnid]++;
        if (DetectHotFunction(vm, callee) && !HasJITCompiled(callee, -1, vm)) {
          TriggerMethodJIT(vm, task, callee);
        }
      }
      if (HasJITCompiled(callee, -1, vm)) {
        AKSValue *canarypos = stacktop - argc;
        for (AKSValue *p = stacktop; p > canarypos; p--) {
          *p = *(p - 1);
        }
        *canarypos = ValInt(vm->stackcanary);
        stacktop++;

        FRAME_GROW();
        CallFrame *newframe = &task->frames[task->framecount++];
        newframe->fn = callee;
        newframe->slotsbase = (int)(canarypos + 1 - task->stack);
        newframe->argc = argc;

        int maxreg = callee->maxregisters > argc ? callee->maxregisters : (callee->localcount > argc ? callee->localcount : argc);
        STACK_GROW(newframe->slotsbase + maxreg + 1);
        for (int i = argc; i < maxreg; i++) {
          task->stack[newframe->slotsbase + i] = ValNull();
        }
        stacktop = task->stack + newframe->slotsbase + argc;

        ExecuteJITCompiled(vm, task, callee, -1, &stacktop, &newframe,
                           &frameslots, &ip);
        task->framecount--;
        frame = &task->frames[task->framecount - 1];
        frameslots = task->stack + frame->slotsbase;
        ip = frame->ip;
        break;
      }
      if (task->framecount >= MAXFRAMES) {
        RaiseX(vm, "stack overflow (too many call frames)", &ip, &stacktop,
               &frame, &frameslots);
        continue;
      }
      if (stacktop >= stacklimit) {
        RaiseX(vm, "StackOverflowError: stack overflow on call", &ip, &stacktop,
               &frame, &frameslots);
        continue;
      }
      AKSValue *canarypos = stacktop - argc;
      for (AKSValue *p = stacktop; p > canarypos; p--) {
        *p = *(p - 1);
      }
      *canarypos = ValInt(vm->stackcanary);
      stacktop++;

      frame->ip = ip;

      FRAME_GROW();
      CallFrame *newframe = &task->frames[task->framecount++];
      VM_ENTER_FRAME();
      newframe->fn = callee;
      newframe->ip = callee->chunk.code;
      newframe->slotsbase = (int)(stacktop - task->stack) - argc;
      newframe->argc = argc;

      int maxreg = callee->maxregisters > argc ? callee->maxregisters : argc;
      STACK_GROW(newframe->slotsbase + maxreg + 1);
      for (int i = argc; i < maxreg; i++) {
        task->stack[newframe->slotsbase + i] = ValNull();
      }
      stacktop = task->stack + newframe->slotsbase + maxreg;

      frame = newframe;
      ip = frame->ip;
      frameslots = task->stack + frame->slotsbase;
    }
    break;
  }

  case OPPUSHFN: {
    uint16_t fnid = READU16();
    PUSH(ValFn(fnid));
    break;
  }

  case OPAPPLY: {
    if (task->blockedonstdlib) {
      task->blockedonstdlib = 0;
      int res =
          ExecuteStdlib(vm, task->stdlibname, task->stdlibargc, &stacktop);
      if (res == -2) {
        task->blockedonstdlib = 1;
        frame->ip = ip - 1;
        task->stacktop = (int)(stacktop - task->stack);
        return 2;
      } else if (res < 0) {
        RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      } else {
        break;
      }
    }
    AKSValue calleeval = POP();
    AKSValue argsval = POP();
    if (ValKind(argsval) != VALARR) {
      ValFree(&calleeval);
      ValFree(&argsval);
      RaiseX(vm, "TypeError: apply argument must be an array", &ip, &stacktop,
             &frame, &frameslots);
      continue;
    }
    AKSArray *arr = ValToArr(argsval);
    int argc = arr->count;
    if (stacktop + argc >= stacklimit) {
      ValFree(&calleeval);
      ValFree(&argsval);
      RaiseX(vm, "StackOverflowError: stack overflow on apply", &ip, &stacktop,
             &frame, &frameslots);
      continue;
    }
    for (int i = 0; i < argc; i++) {
      PUSH(ValCopy(arr->items[i]));
    }
    ValFree(&argsval);

    int hasuaf = 0;
    for (int i = 0; i < argc; i++) {
      if (ValIsInvalidated(stacktop[-argc + i])) {
        hasuaf = 1;
        break;
      }
    }
    if (hasuaf) {
      ValFree(&calleeval);
      RaiseX(vm,
             "UseAfterFreeError: attempt to use an explicitly "
             "freed/invalidated object",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (ValKind(calleeval) == VALBOUNDMETHOD) {
      AKSValue receiver = ValCopy(*ValToBoundMethod(calleeval)->receiver);
      char *methodname = strdup(ValToBoundMethod(calleeval)->name);
      ValFree(&calleeval);
      AKSStruct *s = ValToObj(receiver);
      int found = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], methodname) == 0) {
          found = i;
          break;
        }
      }
      uint16_t global_fnid = 65535;
      if (found < 0 && s->typename) {
        global_fnid = FindTypeMethod(vm, s->typename, methodname);
      }
      if ((found >= 0 && ValKind(s->fieldvals[found]) == VALFN) || global_fnid != 65535) {
        if (stacktop >= stacklimit) {
          ValFree(&receiver);
          free(methodname);
          RaiseX(vm, "StackOverflowError: stack overflow on dynamic call", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        }
        for (int i = 1; i <= argc; i++) {
          stacktop[-i + 1] = stacktop[-i];
        }
        stacktop[-argc] = ValCopy(receiver);
        stacktop++;
        argc++;
        if (found >= 0) {
          calleeval = ValCopy(s->fieldvals[found]);
        } else {
          calleeval = ValFn(global_fnid);
        }
        ValFree(&receiver);
        free(methodname);
      } else {
        AKSValue arrmethod = ValArr(argc);
        ValToArr(arrmethod)->count = argc;
        for (int i = 0; i < argc; i++) {
          ValToArr(arrmethod)->items[i] = stacktop[-argc + i];
        }
        stacktop -= argc;
        int mmfound = -1;
        for (int i = 0; i < s->fieldcount; i++) {
          if (strcmp(s->fieldnames[i], "methodmissing") == 0) {
            mmfound = i;
            break;
          }
        }
        if (mmfound < 0 || ValKind(s->fieldvals[mmfound]) != VALFN) {
          ValFree(&receiver);
          free(methodname);
          ValFree(&arrmethod);
          RaiseX(vm, "methodmissing is not a function", &ip, &stacktop, &frame,
                 &frameslots);
          continue;
        }
        *stacktop++ = receiver;
        *stacktop++ = ValStr(methodname);
        free(methodname);
        *stacktop++ = arrmethod;
        calleeval = ValCopy(s->fieldvals[mmfound]);
        argc = 3;
      }
    }
    if (ValKind(calleeval) != VALFN) {
      ValFree(&calleeval);
      RaiseX(vm, "expression is not a function", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    }
    uint16_t fnid = ValToFn(calleeval);
    ValFree(&calleeval);

    if (fnid >= vm->fncount) {
      char buf[64];
      snprintf(buf, sizeof(buf), "function ID %d invalid", fnid);
      RaiseX(vm, buf, &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    FnProto *callee = vm->fns[fnid];

    if (callee->isasync) {
      AKSValue args[32];
      for (int i = argc - 1; i >= 0; i--) {
        args[i] = POP();
      }
      VMTask *spawned = TaskNew(vm, callee, 0);
      for (int i = 0; i < argc && i < callee->paramcount; i++) {
        ValFree(&spawned->stack[i]);
        spawned->stack[i] = args[i];
      }
      int targetworker = rand() % vm->workercount;
      WorkerPushTail(&vm->workers[targetworker], spawned);
      PUSH(ValTask(spawned));
      break;
    }

    task->stacktop = (int)(stacktop - task->stack);
    int res = (callee && callee->chunk.count == 0) ? DispatchStdlib(vm, callee, callee->name, argc, &stacktop) : 0;
    if (res == -2) {
      task->blockedonstdlib = 1;
      strncpy(task->stdlibname, callee->name, 63);
      int resumeargc = argc;
      if (strcmp(callee->name, "ParallelMapRaw") == 0)
        resumeargc = 4;
      else if (strcmp(callee->name, "ParallelReduceRaw") == 0)
        resumeargc = 6;
      task->stdlibargc = resumeargc;
      frame->ip = ip - 1;
      task->stacktop = (int)(stacktop - task->stack);
      return 2;
    } else if (res < 0) {
      RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    } else if (res == 1) {
      // stdlib call completed
    } else {
      if (fnid < 4096) {
        vm->fncallcounts[fnid]++;
        if (DetectHotFunction(vm, callee) && !HasJITCompiled(callee, -1, vm)) {
          TriggerMethodJIT(vm, task, callee);
        }
      }
      if (HasJITCompiled(callee, -1, vm)) {
        AKSValue *canarypos = stacktop - argc;
        for (AKSValue *p = stacktop; p > canarypos; p--) {
          *p = *(p - 1);
        }
        *canarypos = ValInt(vm->stackcanary);
        stacktop++;

        FRAME_GROW();
        CallFrame *newframe = &task->frames[task->framecount++];
        newframe->fn = callee;
        newframe->slotsbase = (int)(canarypos + 1 - task->stack);
        newframe->argc = argc;

        int maxreg = callee->maxregisters > argc ? callee->maxregisters : (callee->localcount > argc ? callee->localcount : argc);
        STACK_GROW(newframe->slotsbase + maxreg + 1);
        for (int i = argc; i < maxreg; i++) {
          task->stack[newframe->slotsbase + i] = ValNull();
        }
        stacktop = task->stack + newframe->slotsbase + argc;

        ExecuteJITCompiled(vm, task, callee, -1, &stacktop, &newframe,
                           &frameslots, &ip);
        task->framecount--;
        frame = &task->frames[task->framecount - 1];
        frameslots = task->stack + frame->slotsbase;
        ip = frame->ip;
        break;
      }
      if (task->framecount >= MAXFRAMES) {
        RaiseX(vm, "stack overflow (too many call frames)", &ip, &stacktop,
               &frame, &frameslots);
        continue;
      }
      if (stacktop >= stacklimit) {
        RaiseX(vm, "StackOverflowError: stack overflow on apply", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      AKSValue *canarypos = stacktop - argc;
      for (AKSValue *p = stacktop; p > canarypos; p--) {
        *p = *(p - 1);
      }
      *canarypos = ValInt(vm->stackcanary);
      stacktop++;

      frame->ip = ip;
      FRAME_GROW();
      CallFrame *newframe = &task->frames[task->framecount++];
      newframe->fn = callee;
      newframe->ip = callee->chunk.code;
      newframe->slotsbase = (int)(stacktop - task->stack) - argc;
      newframe->argc = argc;
      frame = newframe;
      ip = frame->ip;
      frameslots = task->stack + frame->slotsbase;
    }
    break;
  }

  case OPRESTARGS: {
    uint8_t paramidx = READBYTE();
    int currentargc = frame->argc;
    if (currentargc < paramidx + 1) {
      for (int i = currentargc; i < paramidx + 1; i++) {
        frameslots[i] = ValNull();
      }
      stacktop = frameslots + paramidx + 1;
      frame->argc = paramidx + 1;
    }
    int count = currentargc - paramidx;
    if (count < 0)
      count = 0;
    AKSValue arrval = ValArr(count);
    ValToArr(arrval)->count = count;
    for (int i = 0; i < count; i++) {
      ValToArr(arrval)->items[i] = frameslots[paramidx + i];
    }
    frameslots[paramidx] = arrval;
    for (int i = paramidx + 1; i < currentargc; i++) {
      frameslots[i] = ValNull();
    }
    break;
  }

  case OPTYPEOF: {
    AKSValue a = POP();
    AKSValue res = ValTypeof(a);
    PUSH(res);
    ValFree(&a);
    break;
  }

  case OPCASTINT: {
    AKSValue a = POP();
    int cast_ok = 1;
    AKSValue res = ValCastInt(a, &cast_ok);
    if (!cast_ok) {
      char cast_errbuf[256];
      AKSValue vs = ValToStr(a);
      snprintf(cast_errbuf, sizeof(cast_errbuf),
               "CastError: cannot cast value '%s' to int  -  expected a valid "
               "integer string",
               ValToStrPtr(vs)->data);
      ValFree(&vs);
      ValFree(&a);
      RaiseX(vm, cast_errbuf, &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&a);
    PUSH(res);
    break;
  }
  case OPCASTFLOAT: {
    AKSValue a = POP();
    int cast_ok = 1;
    AKSValue res = ValCastFloat(a, &cast_ok);
    if (!cast_ok) {
      char cast_errbuf[256];
      AKSValue vs = ValToStr(a);
      snprintf(cast_errbuf, sizeof(cast_errbuf),
               "CastError: cannot cast value '%s' to float  -  expected a valid "
               "number string",
               ValToStrPtr(vs)->data);
      ValFree(&vs);
      ValFree(&a);
      RaiseX(vm, cast_errbuf, &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&a);
    PUSH(res);
    break;
  }
  case OPCASTBOOL: {
    AKSValue a = POP();
    int cast_ok = 1;
    AKSValue res = ValCastBool(a, &cast_ok);
    if (!cast_ok) {
      char cast_errbuf[256];
      AKSValue vs = ValToStr(a);
      snprintf(cast_errbuf, sizeof(cast_errbuf),
               "CastError: cannot cast value '%s' to bool  -  expected '1', '0', "
               "'true', or 'false'",
               ValToStrPtr(vs)->data);
      ValFree(&vs);
      ValFree(&a);
      RaiseX(vm, cast_errbuf, &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&a);
    PUSH(res);
    break;
  }
  case OPCASTSTR: {
    AKSValue a = POP();
    PUSH(ValCastStr(a));
    ValFree(&a);
    break;
  }

  case OPCALLDYNAMIC: {
    if (task->blockedonstdlib) {
      task->blockedonstdlib = 0;
      int res =
          ExecuteStdlib(vm, task->stdlibname, task->stdlibargc, &stacktop);
      if (res == -2) {
        task->blockedonstdlib = 1;
        frame->ip = ip - 1; // Keep ip at opcalldynamic
        task->stacktop = (int)(stacktop - task->stack);
        return 2; // blocked
      } else if (res < 0) {
        RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      } else {
        ip += 1; // advance IP past operand (argc (1))
        break;
      }
    }
    uint8_t argc = READBYTE();
    int hasuaf = 0;
    for (int i = 0; i < argc; i++) {
      if (ValIsInvalidated(stacktop[-argc + i])) {
        hasuaf = 1;
        break;
      }
    }
    if (hasuaf) {
      RaiseX(vm,
             "UseAfterFreeError: attempt to use an explicitly "
             "freed/invalidated object",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    AKSValue calleeval = POP();
    if (ValKind(calleeval) == VALBOUNDMETHOD) {
      AKSValue receiver = ValCopy(*ValToBoundMethod(calleeval)->receiver);
      char *methodname = strdup(ValToBoundMethod(calleeval)->name);
      ValFree(&calleeval);
      AKSStruct *s = ValToObj(receiver);
      int found = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], methodname) == 0) {
          found = i;
          break;
        }
      }
      uint16_t global_fnid = 65535;
      if (found < 0 && s->typename) {
        global_fnid = FindTypeMethod(vm, s->typename, methodname);
      }
      if ((found >= 0 && ValKind(s->fieldvals[found]) == VALFN) || global_fnid != 65535) {
        if (stacktop >= stacklimit) {
          ValFree(&receiver);
          free(methodname);
          RaiseX(vm, "StackOverflowError: stack overflow on dynamic call", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        }
        for (int i = 1; i <= argc; i++) {
          stacktop[-i + 1] = stacktop[-i];
        }
        stacktop[-argc] = ValCopy(receiver);
        stacktop++;
        argc++;
        if (found >= 0) {
          calleeval = ValCopy(s->fieldvals[found]);
        } else {
          calleeval = ValFn(global_fnid);
        }
        ValFree(&receiver);
        free(methodname);
      } else {
        AKSValue arr = ValArr(argc);
        ValToArr(arr)->count = argc;
        for (int i = 0; i < argc; i++) {
          ValToArr(arr)->items[i] = stacktop[-argc + i];
        }
        stacktop -= argc;
        int mmfound = -1;
        for (int i = 0; i < s->fieldcount; i++) {
          if (strcmp(s->fieldnames[i], "methodmissing") == 0) {
            mmfound = i;
            break;
          }
        }
        if (mmfound < 0 || ValKind(s->fieldvals[mmfound]) != VALFN) {
          ValFree(&receiver);
          free(methodname);
          ValFree(&arr);
          RaiseX(vm, "methodmissing is not a function", &ip, &stacktop, &frame,
                 &frameslots);
          continue;
        }
        *stacktop++ = receiver;
        *stacktop++ = ValStr(methodname);
        free(methodname);
        *stacktop++ = arr;
        calleeval = ValCopy(s->fieldvals[mmfound]);
        argc = 3;
      }
    }
    if (ValKind(calleeval) != VALFN) {
      if (ValKind(calleeval) == VALSTR) {
        char *stdname = strdup(ValToStrPtr(calleeval)->data);
        ValFree(&calleeval);
        int sres = ExecuteStdlib(vm, stdname, argc, &stacktop);
        free(stdname);
        if (sres >= 0) {
          break;
        }
      } else {
        ValFree(&calleeval);
      }
      RaiseX(vm, "expression is not a function", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    }
    uint16_t fnid = ValToFn(calleeval);
    ValFree(&calleeval);

    if (fnid >= vm->fncount) {
      char buf[64];
      snprintf(buf, sizeof(buf), "function ID %d invalid", fnid);
      RaiseX(vm, buf, &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    FnProto *callee = vm->fns[fnid];

    if (callee->isasync) {
      AKSValue args[32];
      for (int i = argc - 1; i >= 0; i--) {
        args[i] = POP();
      }
      VMTask *spawned = TaskNew(vm, callee, 0);
      for (int i = 0; i < argc && i < callee->paramcount; i++) {
        ValFree(&spawned->stack[i]);
        spawned->stack[i] = args[i];
      }
      int targetworker = rand() % vm->workercount;
      WorkerPushTail(&vm->workers[targetworker], spawned);
      PUSH(ValTask(spawned));
      break;
    }

    task->stacktop = (int)(stacktop - task->stack);
    int res = (callee && callee->chunk.count == 0) ? DispatchStdlib(vm, callee, callee->name, argc, &stacktop) : 0;
    if (res == -2) {
      task->blockedonstdlib = 1;
      strncpy(task->stdlibname, callee->name, 63);
      int resumeargc = argc;
      if (strcmp(callee->name, "ParallelMapRaw") == 0)
        resumeargc = 4;
      else if (strcmp(callee->name, "ParallelReduceRaw") == 0)
        resumeargc = 6;
      task->stdlibargc = resumeargc;
      frame->ip = ip - 2; // Keep ip at opcalldynamic
      task->stacktop = (int)(stacktop - task->stack);
      return 2; // blocked
    } else if (res < 0) {
      RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    } else if (res == 1) {
      // stdlib call completed
    } else {
      if (task->framecount >= MAXFRAMES) {
        RaiseX(vm, "stack overflow (too many call frames)", &ip, &stacktop,
               &frame, &frameslots);
        continue;
      }
      if (stacktop >= stacklimit) {
        RaiseX(vm, "StackOverflowError: stack overflow on dynamic call", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      AKSValue *canarypos = stacktop - argc;
      for (AKSValue *p = stacktop; p > canarypos; p--) {
        *p = *(p - 1);
      }
      *canarypos = ValInt(vm->stackcanary);
      stacktop++;

      frame->ip = ip;

      FRAME_GROW();
      CallFrame *newframe = &task->frames[task->framecount++];
      newframe->fn = callee;
      newframe->ip = callee->chunk.code;
      newframe->slotsbase = (int)(stacktop - task->stack) - argc;
      newframe->argc = argc;

      int maxreg = callee->maxregisters > argc ? callee->maxregisters : argc;
      STACK_GROW(newframe->slotsbase + maxreg + 1);
      for (int i = argc; i < maxreg; i++) {
        task->stack[newframe->slotsbase + i] = ValNull();
      }
      stacktop = task->stack + newframe->slotsbase + maxreg;

      frame = newframe;
      ip = frame->ip;
      frameslots = task->stack + frame->slotsbase;
    }
    break;
  }

  case OPTAILCALL: {
    if (task->blockedonstdlib) {
      task->blockedonstdlib = 0;
      int res =
          ExecuteStdlib(vm, task->stdlibname, task->stdlibargc, &stacktop);
      if (res == -2) {
        task->blockedonstdlib = 1;
        frame->ip = ip - 1; // Keep ip at optailcall
        task->stacktop = (int)(stacktop - task->stack);
        return 2; // blocked
      } else if (res < 0) {
        RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      } else {
        ip += 3; // advance IP past operands (fnid (2) + argc (1))
        break;
      }
    }
    uint16_t fnid = READU16();
    uint8_t argc = READBYTE();
    FnProto *callee = NULL;
    if (fnid == 65535) {
      AKSValue calleeval = POP();
      if (ValKind(calleeval) != VALFN) {
        ValFree(&calleeval);
        RaiseX(vm, "expression is not a function", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      }
      uint16_t dynamicfnid = ValToFn(calleeval);
      ValFree(&calleeval);
      if (dynamicfnid >= vm->fncount) {
        RaiseX(vm, "invalid dynamic function ID", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      }
      callee = vm->fns[dynamicfnid];
    } else {
      if (fnid >= vm->fncount) {
        RaiseX(vm, "invalid function ID", &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      callee = vm->fns[fnid];
    }

    task->stacktop = (int)(stacktop - task->stack);
    int res = (callee && callee->chunk.count == 0) ? DispatchStdlib(vm, callee, callee->name, argc, &stacktop) : 0;
    if (res == -2) {
      task->blockedonstdlib = 1;
      strncpy(task->stdlibname, callee->name, 63);
      int resumeargc = argc;
      if (strcmp(callee->name, "ParallelMapRaw") == 0)
        resumeargc = 4;
      else if (strcmp(callee->name, "ParallelReduceRaw") == 0)
        resumeargc = 6;
      task->stdlibargc = resumeargc;
      frame->ip = ip - 4; // Keep ip at optailcall
      task->stacktop = (int)(stacktop - task->stack);
      return 2; // blocked
    } else if (res < 0) {
      RaiseX(vm, "Standard library call error", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    } else if (res == 1) {
      AKSValue retval = POP();
      AKSValue *base = task->stack + frame->slotsbase;
      while (stacktop > base) {
        AKSValue val = POP();
        ValFree(&val);
      }
      PUSH(retval);
      task->framecount--;
      if (task->framecount == 0) {
        task->stacktop = (int)(stacktop - task->stack);
        return 1;
      }
      frame = &task->frames[task->framecount - 1];
      ip = frame->ip;
      frameslots = task->stack + frame->slotsbase;
    } else {
      while (task->tryhandlercount > 0 &&
             task->tryhandlers[task->tryhandlercount - 1].frameindex ==
                 task->framecount - 1) {
        task->tryhandlercount--;
      }

      AKSValue *dest = task->stack + frame->slotsbase;
      AKSValue *src = stacktop - argc;

      AKSValue temp_args[32];
      AKSValue *args_buf = temp_args;
      if (argc > 32) {
        args_buf = (AKSValue *)malloc(sizeof(AKSValue) * argc);
      }
      for (int i = 0; i < argc; i++) {
        args_buf[i] = src[i];
      }

      for (int i = 0; i < frame->fn->localcount; i++) {
        ValFree(&dest[i]);
      }

      for (int i = 0; i < argc; i++) {
        dest[i] = args_buf[i];
      }
      if (argc > 32) {
        free(args_buf);
      }

      stacktop = dest + argc;
      frame->fn = callee;
      frame->ip = callee->chunk.code;

      ip = frame->ip;
      frameslots = task->stack + frame->slotsbase;
    }
    break;
  }

  case OPRETURN: {
    if (frame->slotsbase < 0 ||
        frame->slotsbase > (int)(stacktop - task->stack)) {
      fprintf(stderr, "SecurityError: Frame slotsbase corruption detected\n");
      exit(1);
    }
    if (task->framecount > 1) {
      int canaryidx = frame->slotsbase - 1;
      if (canaryidx < 0 || ValKind(task->stack[canaryidx]) != VALINT ||
          ValToInt(task->stack[canaryidx]) != (int64_t)vm->stackcanary) {
        fprintf(stderr, "SecurityError: Stack canary corruption detected\n");
        exit(1);
      }
    }

    int base = frame->slotsbase;
    int topidx = (int)(stacktop - task->stack);
    for (int i = base; i < topidx; i++) {
      ValFree(&task->stack[i]);
    }
    if (task->framecount > 1) {
      task->framecount--;
      VM_EXIT_FRAME();
      frame = &task->frames[task->framecount - 1];
      ip = frame->ip;
      frameslots = task->stack + frame->slotsbase;
      stacktop = task->stack + base - 1; // remove canary!

      uint8_t *codestart = frame->fn->chunk.code;
      uint8_t *codeend = codestart + frame->fn->chunk.count;
      if (ip < codestart || ip > codeend) {
        fprintf(stderr, "SecurityError: Return IP corruption detected\n");
        exit(1);
      }

      if (frame->fn->regcode && vm->jitmode == 0) {
        uint8_t *call_ip = NULL;
        uint8_t *ip_val = frame->ip;
        if (ip_val >= frame->fn->chunk.code &&
            ip_val < frame->fn->chunk.code + frame->fn->chunk.count) {
          if (*ip_val == OPCALL || *ip_val == OPCALLDYNAMIC) {
            call_ip = ip_val;
          }
        }
        if (!call_ip) {
          if (ip_val - 4 >= frame->fn->chunk.code &&
              *(ip_val - 4) == OPCALL) {
            call_ip = ip_val - 4;
          } else if (ip_val - 2 >= frame->fn->chunk.code &&
                     *(ip_val - 2) == OPCALLDYNAMIC) {
            call_ip = ip_val - 2;
          }
        }
        if (call_ip) {
          int call_bytecode_offset = (int)(call_ip - frame->fn->chunk.code);
          int regoffset = frame->fn->offsettomap[call_bytecode_offset];
          regip = (RegInst *)frame->fn->regcode + regoffset + 1;
          RegInst *callinst = (RegInst *)frame->fn->regcode + regoffset;
          ValFree(&frameslots[callinst->dst]);
          frameslots[callinst->dst] = ValNull();
          frame->ip = call_ip + (*call_ip == OPCALL ? 4 : 2);
          ip = frame->ip;
        } else {
          int bytecodeoffset = (int)(frame->ip - frame->fn->chunk.code);
          int regoffset = frame->fn->offsettomap[bytecodeoffset];
          regip = (RegInst *)frame->fn->regcode + regoffset + 1;
          RegInst *callinst = (RegInst *)frame->fn->regcode + regoffset;
          ValFree(&frameslots[callinst->dst]);
          frameslots[callinst->dst] = ValNull();
        }
        if (vm->jitmode == 0) {
          int maxreg = frame->fn->maxregisters;
          if (maxreg == 0) maxreg = frame->fn->localcount;
          stacktop = task->stack + frame->slotsbase + maxreg;
          goto enter_register_interpreter_loop;
        }
      } else {
        PUSH(ValNull());
      }
    } else {
      task->stacktop = base;
      task->result = ValNull();
      task->state = 3;
      return 1;
    }
    break;
  }

  case OPRETURNVAL: {
    AKSValue ret = POP();
    if (frame->slotsbase < 0 ||
        frame->slotsbase > (int)(stacktop - task->stack)) {
      fprintf(stderr, "SecurityError: Frame slotsbase corruption detected\n");
      exit(1);
    }
    if (task->framecount > 1) {
      int canaryidx = frame->slotsbase - 1;
      if (canaryidx < 0 || ValKind(task->stack[canaryidx]) != VALINT ||
          ValToInt(task->stack[canaryidx]) != (int64_t)vm->stackcanary) {
        fprintf(stderr, "SecurityError: Stack canary corruption detected\n");
        exit(1);
      }
    }

    int base = frame->slotsbase;
    int topidx = (int)(stacktop - task->stack);
    for (int i = base; i < topidx; i++) {
      ValFree(&task->stack[i]);
    }
    if (task->framecount > 1) {
      task->framecount--;
      VM_EXIT_FRAME();
      frame = &task->frames[task->framecount - 1];
      ip = frame->ip;
      frameslots = task->stack + frame->slotsbase;
      stacktop = task->stack + base - 1; // remove canary!

      uint8_t *codestart = frame->fn->chunk.code;
      uint8_t *codeend = codestart + frame->fn->chunk.count;
      if (ip < codestart || ip > codeend) {
        fprintf(stderr, "SecurityError: Return IP corruption detected\n");
        exit(1);
      }

      if (frame->fn->regcode && vm->jitmode == 0) {
        uint8_t *call_ip = NULL;
        uint8_t *ip_val = frame->ip;
        if (ip_val >= frame->fn->chunk.code &&
            ip_val < frame->fn->chunk.code + frame->fn->chunk.count) {
          if (*ip_val == OPCALL || *ip_val == OPCALLDYNAMIC) {
            call_ip = ip_val;
          }
        }
        if (!call_ip) {
          if (ip_val - 4 >= frame->fn->chunk.code &&
              *(ip_val - 4) == OPCALL) {
            call_ip = ip_val - 4;
          } else if (ip_val - 2 >= frame->fn->chunk.code &&
                     *(ip_val - 2) == OPCALLDYNAMIC) {
            call_ip = ip_val - 2;
          }
        }
        if (call_ip) {
          int call_bytecode_offset = (int)(call_ip - frame->fn->chunk.code);
          int regoffset = frame->fn->offsettomap[call_bytecode_offset];
          regip = (RegInst *)frame->fn->regcode + regoffset + 1;
          RegInst *callinst = (RegInst *)frame->fn->regcode + regoffset;
          ValFree(&frameslots[callinst->dst]);
          frameslots[callinst->dst] = ret;
          frame->ip = call_ip + (*call_ip == OPCALL ? 4 : 2);
          ip = frame->ip;
        } else {
          int bytecodeoffset = (int)(frame->ip - frame->fn->chunk.code);
          int regoffset = frame->fn->offsettomap[bytecodeoffset];
          regip = (RegInst *)frame->fn->regcode + regoffset + 1;
          RegInst *callinst = (RegInst *)frame->fn->regcode + regoffset;
          ValFree(&frameslots[callinst->dst]);
          frameslots[callinst->dst] = ret;
        }
        if (vm->jitmode == 0) {
          int maxreg = frame->fn->maxregisters;
          if (maxreg == 0) maxreg = frame->fn->localcount;
          stacktop = task->stack + frame->slotsbase + maxreg;
          goto enter_register_interpreter_loop;
        }
      } else {
        PUSH(ret);
      }
    } else {
      task->stacktop = base;
      task->result = ret;
      task->state = 3;
      return 1;
    }
    break;
  }

  case OPNEWARR: {
    uint16_t count = READU16();
    int isstack = (count & 0x8000) != 0;
    count &= 0x7FFF;
    AKSValue arrval = isstack ? ValArrStack(task, count) : ValArr(count);
    ValToArr(arrval)->count = count;
    for (int i = count - 1; i >= 0; i--) {
      ValToArr(arrval)->items[i] = POP();
    }
    PUSH(arrval);
    break;
  }

  case OPNEWTUPLE: {
    uint16_t count = READU16();
    int isstack = (count & 0x8000) != 0;
    count &= 0x7FFF;
    AKSValue arrval = isstack ? ValTupleStack(task, count) : ValTuple(count);
    ValToTuple(arrval)->count = count;
    for (int i = count - 1; i >= 0; i--) {
      ValToTuple(arrval)->items[i] = POP();
    }
    PUSH(arrval);
    break;
  }

  case OPCLONESTRUCT: {
    AKSValue srcval = POP();
    if (ValKind(srcval) != VALSTRUCT) {
      PUSH(srcval); /* not a struct - push back unchanged */
      break;
    }
    AKSStruct *src = ValToObj(srcval);
    AKSValue cloneval = ValStruct(src->typename);
    AKSStruct *dst = ValToObj(cloneval);
    dst->fieldcount = src->fieldcount;
    if (src->fieldcount > 0) {
      dst->fieldnames = malloc(src->fieldcount * sizeof(char *));
      dst->fieldvals = malloc(src->fieldcount * sizeof(AKSValue));
      for (int i = 0; i < src->fieldcount; i++) {
        dst->fieldnames[i] = strdup(src->fieldnames[i]);
        dst->fieldvals[i] = ValCopy(src->fieldvals[i]);
      }
    } else {
      dst->fieldnames = NULL;
      dst->fieldvals = NULL;
    }
    ValFree(&srcval);
    PUSH(cloneval);
    break;
  }

  case OPARRGET: {
    if (stacktop - task->stack >= 2) {
      AKSValue arrval = stacktop[-2];
      AKSValue idxval = stacktop[-1];
      if ((ValKind(arrval) == VALARR || ValKind(arrval) == VALTUPLE) && ValIsInt(idxval) && !ValIsInvalidated(arrval)) {
        int64_t idx = ValToIntUnchecked(idxval);
        AKSArray *a = ValToArr(arrval);
        if (idx >= 0 && idx < a->count) {
          AKSValue item = ValCopy(a->items[idx]);
          ValFree(&arrval);
          stacktop[-2] = item;
          stacktop--;
          break;
        }
      }
    }
    AKSValue idxval = POP();
    AKSValue arrval = POP();
    if (ValKind(arrval) == VALNULL) {
      ValFree(&idxval);
      ValFree(&arrval);
      RaiseX(vm,
             "NullPointerException: attempt to access element on null value",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (ValIsInvalidated(arrval)) {
      ValFree(&idxval);
      ValFree(&arrval);
      RaiseX(vm,
             "UseAfterFreeError: attempt to use an explicitly "
             "freed/invalidated object",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if ((ValKind(arrval) == VALARR || ValKind(arrval) == VALTUPLE) &&
        ValKind(idxval) == VALINT) {
      int64_t idx = ValToInt(idxval);
      int64_t orig_idx = idx;
      if (idx < 0) idx += ValToArr(arrval)->count;
      if (!vm->no_bounds_check) {
        if (idx < 0 || idx >= ValToArr(arrval)->count) {
          char buf[128];
          snprintf(buf, sizeof(buf),
                   "Array index out of bounds: index %lld, size %d",
                   (long long)orig_idx, ValToArr(arrval)->count);
          ValFree(&idxval);
          ValFree(&arrval);
          RaiseX(vm, buf, &ip, &stacktop, &frame, &frameslots);
          continue;
        }
      }
      if (idx < 0)
        idx = 0;
      if (idx >= ValToArr(arrval)->count)
        idx = ValToArr(arrval)->count - 1;
      if (ValToArr(arrval)->count > 0) {
        PUSH(ValCopy(ValToArr(arrval)->items[idx]));
      } else {
        PUSH(ValNull());
      }
    } else if ((ValKind(arrval) == VALARR || ValKind(arrval) == VALTUPLE) &&
               ValKind(idxval) == VALSTRUCT &&
               strcmp(ValToObj(idxval)->typename, "Range") == 0) {
      AKSStruct *r = ValToObj(idxval);
      int64_t start = 0, end = 0;
      for (int i = 0; i < r->fieldcount; i++) {
        if (strcmp(r->fieldnames[i], "start") == 0)
          start = ValToInt(r->fieldvals[i]);
        else if (strcmp(r->fieldnames[i], "end") == 0)
          end = ValToInt(r->fieldvals[i]);
      }
      int64_t count = ValToArr(arrval)->count;
      if (start < 0)
        start = 0;
      if (end < 0)
        end = 0;
      if (start > count)
        start = count;
      if (end > count)
        end = count;
      if (start > end)
        start = end;
      int length = (int)(end - start);
      AKSArray *slicearr = malloc(sizeof(AKSArray));
      slicearr->refcount = 1;
      slicearr->invalidated = 0;
      slicearr->isstack = 0;
      slicearr->isslice = 1;
      slicearr->has_slices = 0;
      slicearr->detached = NULL;
      slicearr->parent = ValCopy(arrval);
      slicearr->items = ValToArr(arrval)->items + start;
      slicearr->count = length;
      slicearr->capacity = length;
      slicearr->arena = NULL;
      ValToArr(arrval)->has_slices = 1;
      AtomicIncrement(&gactivearrs);
      AKSValue sliceval = ((uint64_t)0xFFF5 << 48) |
                          ((uintptr_t)slicearr & 0x0000FFFFFFFFFFFFULL);
      PUSH(sliceval);
    } else if (ValKind(arrval) == VALSTR && ValKind(idxval) == VALSTRUCT &&
               strcmp(ValToObj(idxval)->typename, "Range") == 0) {
      AKSStruct *r = ValToObj(idxval);
      int64_t start = 0, end = 0;
      for (int i = 0; i < r->fieldcount; i++) {
        if (strcmp(r->fieldnames[i], "start") == 0)
          start = ValToInt(r->fieldvals[i]);
        else if (strcmp(r->fieldnames[i], "end") == 0)
          end = ValToInt(r->fieldvals[i]);
      }
      int64_t count = ValToStrPtr(arrval)->len;
      if (start < 0)
        start = 0;
      if (end < 0)
        end = 0;
      if (start > count)
        start = count;
      if (end > count)
        end = count;
      if (start > end)
        start = end;
      int length = (int)(end - start);
      char *sub = malloc(length + 1);
      memcpy(sub, ValToStrPtr(arrval)->data + start, length);
      sub[length] = '\0';
      AKSValue sliceval = ValStrLen(sub, length);
      free(sub);
      PUSH(sliceval);
    } else if (ValKind(arrval) == VALSTR && ValKind(idxval) == VALINT) {
      int64_t idx = ValToInt(idxval);
      int64_t count = ValToStrPtr(arrval)->len;
      int64_t orig_idx = idx;
      if (idx < 0) idx += count;
      if (idx < 0 || idx >= count) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "String index out of bounds: index %lld, size %lld",
                 (long long)orig_idx, (long long)count);
        ValFree(&idxval);
        ValFree(&arrval);
        RaiseX(vm, buf, &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      char charstr[2] = {ValToStrPtr(arrval)->data[idx], '\0'};
      PUSH(ValStrLen(charstr, 1));
    } else if (ValKind(arrval) == VALSTRUCT &&
               strcmp(ValToObj(arrval)->typename, "Range") == 0 &&
               ValKind(idxval) == VALINT) {
      AKSStruct *r = ValToObj(arrval);
      int64_t start = 0, end = 0;
      for (int i = 0; i < r->fieldcount; i++) {
        if (strcmp(r->fieldnames[i], "start") == 0)
          start = ValToInt(r->fieldvals[i]);
        else if (strcmp(r->fieldnames[i], "end") == 0)
          end = ValToInt(r->fieldvals[i]);
      }
      int64_t i = ValToInt(idxval);
      int64_t count = end - start;
      if (!vm->no_bounds_check) {
        if (i < 0 || i >= count) {
          char buf[128];
          snprintf(buf, sizeof(buf),
                   "Range index out of bounds: index %lld, size %lld",
                   (long long)i, (long long)count);
          ValFree(&idxval);
          ValFree(&arrval);
          RaiseX(vm, buf, &ip, &stacktop, &frame, &frameslots);
          continue;
        }
      }
      PUSH(ValInt(start + i));
    } else if (ValKind(arrval) == VALSTRUCT && ValKind(idxval) == VALSTR) {
      AKSStruct *s = ValToObj(arrval);
      const char *fieldname = ValToStrPtr(idxval)->data;
      int found = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (s->fieldnames[i] == fieldname || strcmp(s->fieldnames[i], fieldname) == 0) {
          found = i;
          break;
        }
      }
      if (found >= 0) {
        PUSH(ValCopy(s->fieldvals[found]));
      } else {
        PUSH(ValNull());
      }
    } else {
      ValFree(&idxval);
      ValFree(&arrval);
      RaiseX(vm,
             "Index target not array/struct or index not integer/string/range",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&idxval);
    ValFree(&arrval);
    break;
  }

  case OPARRSET: {
    if (stacktop - task->stack >= 3) {
      AKSValue val = stacktop[-3];
      AKSValue arrval = stacktop[-2];
      AKSValue idxval = stacktop[-1];
      if (ValKind(arrval) == VALARR && ValIsInt(idxval) && !ValIsInvalidated(arrval)) {
        int64_t idx = ValToIntUnchecked(idxval);
        AKSArray *a = ValToArr(arrval);
        if (idx >= 0 && idx < a->count) {
          ValFree(&a->items[idx]);
          a->items[idx] = val;
          stacktop -= 2;
          stacktop[-1] = ValCopy(val);
          break;
        }
      }
    }
    AKSValue idxval = POP();
    AKSValue arrval = POP();
    AKSValue val = POP();
    if (ValKind(arrval) == VALNULL) {
      ValFree(&idxval);
      ValFree(&arrval);
      ValFree(&val);
      RaiseX(vm,
             "NullPointerException: attempt to access element on null value",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (ValIsInvalidated(arrval)) {
      ValFree(&idxval);
      ValFree(&arrval);
      ValFree(&val);
      RaiseX(vm,
             "UseAfterFreeError: attempt to use an explicitly "
             "freed/invalidated object",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (ValKind(arrval) == VALTUPLE) {
      ValFree(&idxval);
      ValFree(&arrval);
      ValFree(&val);
      RaiseX(vm, "TypeError: cannot mutate tuple elements", &ip, &stacktop,
             &frame, &frameslots);
      continue;
    }
    if (ValKind(arrval) == VALARR && ValKind(idxval) == VALINT) {
      int64_t idx = ValToInt(idxval);
      if (!vm->no_bounds_check) {
        if (idx < 0 || idx >= ValToArr(arrval)->count) {
          char buf[128];
          snprintf(buf, sizeof(buf),
                   "Array index out of bounds: index %lld, size %d",
                   (long long)idx, ValToArr(arrval)->count);
          ValFree(&idxval);
          ValFree(&arrval);
          ValFree(&val);
          RaiseX(vm, buf, &ip, &stacktop, &frame, &frameslots);
          continue;
        }
      }
      if (idx >= 0 && idx < ValToArr(arrval)->count) {
        ValFree(&ValToArr(arrval)->items[idx]);
        ValToArr(arrval)->items[idx] = val;
        PUSH(ValCopy(val));
      } else {
        PUSH(ValCopy(val));
        ValFree(&val);
      }
    } else if (ValKind(arrval) == VALSTRUCT && ValKind(idxval) == VALSTR) {
      AKSStruct *s = ValToObj(arrval);
      const char *fieldname = ValToStrPtr(idxval)->data;
      int found = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (s->fieldnames[i] == fieldname || strcmp(s->fieldnames[i], fieldname) == 0) {
          found = i;
          break;
        }
      }
      if (found >= 0) {
        ValFree(&s->fieldvals[found]);
        s->fieldvals[found] = val;
      } else {
        s->fieldcount++;
        s->fieldnames = realloc(s->fieldnames, s->fieldcount * sizeof(char *));
        s->fieldvals = realloc(s->fieldvals, s->fieldcount * sizeof(AKSValue));
        s->fieldnames[s->fieldcount - 1] = strdup(fieldname);
        s->fieldvals[s->fieldcount - 1] = val;
      }
      PUSH(ValCopy(val));
    } else {
      ValFree(&idxval);
      ValFree(&arrval);
      ValFree(&val);
      RaiseX(vm, "Index target not array/struct or index not integer/string",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    ValFree(&idxval);
    ValFree(&arrval);
    break;
  }

  case OPNEWSTRUCT: {
    uint16_t tid = READU16();
    int isstack = (tid & 0x8000) != 0;
    tid &= 0x7FFF;
    const char *typename = vm->strpool[tid];
    PUSH(isstack ? ValStructStack(task, typename) : ValStruct(typename));
    break;
  }

  case OPGETFIELD: {
    int ipoffset =
        (int)(ip - frame->fn->chunk.code) - 2; // points to start of operand
    uint16_t fid = READU16();
    const char *fieldname = vm->strpool[fid];
    AKSValue objval = POP();
    if (ValKind(objval) == VALNULL) {
      ValFree(&objval);
      RaiseX(vm, "NullPointerException: attempt to access member on null value",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (ValIsInvalidated(objval)) {
      ValFree(&objval);
      RaiseX(vm,
             "UseAfterFreeError: attempt to use an explicitly "
             "freed/invalidated object",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (ValKind(objval) != VALSTRUCT) {
      ValFree(&objval);
      RaiseX(vm, "Field target is not a struct", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    }
    AKSStruct *s = ValToObj(objval);
    int found = -1;
    // inline field cache: try cached index first
    if (frame->fn->chunk.fieldcache && ipoffset >= 0) {
      int cached = frame->fn->chunk.fieldcache[ipoffset];
      if (cached >= 0 && cached < s->fieldcount &&
          (s->fieldnames[cached] == fieldname || strcmp(s->fieldnames[cached], fieldname) == 0)) {
        found = cached;
      }
    }
    if (found < 0) {
      for (int i = 0; i < s->fieldcount; i++) {
        if (s->fieldnames[i] == fieldname || strcmp(s->fieldnames[i], fieldname) == 0) {
          found = i;
          break;
        }
      }
      // update cache on hit
      if (found >= 0 && frame->fn->chunk.fieldcache && ipoffset >= 0) {
        frame->fn->chunk.fieldcache[ipoffset] = found;
      }
    }
    if (found >= 0) {
      AKSValue fieldval = s->fieldvals[found];
      if (ValKind(fieldval) == VALFN) {
        uint16_t fnid = ValToFn(fieldval);
        if (fnid < vm->fncount) {
          FnProto *fn = vm->fns[fnid];
          if (fn->paramcount > 0 && strcmp(fn->locals[0].name, "self") == 0) {
            PUSH(ValBoundmethod(objval, fieldname));
          } else {
            PUSH(ValCopy(fieldval));
          }
        } else {
          PUSH(ValCopy(fieldval));
        }
      } else {
        PUSH(ValCopy(fieldval));
      }
    } else {
      int is_swizzle = 0;
      int flen = (int)strlen(fieldname);
      if (flen >= 2 && flen <= 4) {
        AKSValue extracted[4];
        int all_ok = 1;
        for (int i = 0; i < flen; i++) {
          char ch = fieldname[i];
          const char *c1 = NULL;
          const char *c2 = NULL;
          const char *c3 = NULL;
          if (ch == 'x' || ch == 'r' || ch == 's') { c1 = "x"; c2 = "r"; c3 = "s"; }
          else if (ch == 'y' || ch == 'g' || ch == 't') { c1 = "y"; c2 = "g"; c3 = "t"; }
          else if (ch == 'z' || ch == 'b' || ch == 'p') { c1 = "z"; c2 = "b"; c3 = "p"; }
          else if (ch == 'w' || ch == 'a' || ch == 'q') { c1 = "w"; c2 = "a"; c3 = "q"; }
          else {
            for (int k = 0; k < i; k++) ValFree(&extracted[k]);
            all_ok = 0;
            break;
          }
          int found_coord = 0;
          for (int f = 0; f < s->fieldcount; f++) {
            if ((c1 && strcmp(s->fieldnames[f], c1) == 0) ||
                (c2 && strcmp(s->fieldnames[f], c2) == 0) ||
                (c3 && strcmp(s->fieldnames[f], c3) == 0)) {
              extracted[i] = ValCopy(s->fieldvals[f]);
              found_coord = 1;
              break;
            }
          }
          if (!found_coord) {
            for (int k = 0; k < i; k++) ValFree(&extracted[k]);
            all_ok = 0;
            break;
          }
        }
        if (all_ok) {
          const char *tname = (flen == 2) ? "Vec2" : ((flen == 3) ? "Vec3" : "Vec4");
          AKSValue swiz_val = ValStruct(tname);
          AKSStruct *swiz_s = ValToObj(swiz_val);
          const char *out_f[4] = {"x", "y", "z", "w"};
          swiz_s->fieldcount = flen;
          swiz_s->fieldnames = malloc(flen * sizeof(char *));
          swiz_s->fieldvals = malloc(flen * sizeof(AKSValue));
          for (int i = 0; i < flen; i++) {
            swiz_s->fieldnames[i] = strdup(out_f[i]);
            swiz_s->fieldvals[i] = extracted[i];
          }
          PUSH(swiz_val);
          is_swizzle = 1;
        }
      }
      if (!is_swizzle) {
        int has_type_method = 0;
        if (s->typename) {
          has_type_method = (FindTypeMethod(vm, s->typename, fieldname) != 65535);
        }
        if (has_type_method) {
          PUSH(ValBoundmethod(objval, fieldname));
        } else {
          int mmfound = -1;
          for (int i = 0; i < s->fieldcount; i++) {
            if (strcmp(s->fieldnames[i], "methodmissing") == 0) {
              mmfound = i;
              break;
            }
          }
          if (mmfound >= 0) {
            PUSH(ValBoundmethod(objval, fieldname));
          } else {
            PUSH(ValNull());
          }
        }
      }
    }
    ValFree(&objval);
    break;
  }

  case OPSETFIELD: {
    int sfipoffset = (int)(ip - frame->fn->chunk.code) - 2;
    uint16_t fid = READU16();
    const char *fieldname = vm->strpool[fid];
    AKSValue objval = POP();
    AKSValue val = POP();
    if (ValKind(objval) == VALNULL) {
      ValFree(&objval);
      ValFree(&val);
      RaiseX(vm, "NullPointerException: attempt to access member on null value",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (ValIsInvalidated(objval)) {
      ValFree(&objval);
      ValFree(&val);
      RaiseX(vm,
             "UseAfterFreeError: attempt to use an explicitly "
             "freed/invalidated object",
             &ip, &stacktop, &frame, &frameslots);
      continue;
    }
    if (ValKind(objval) != VALSTRUCT) {
      ValFree(&objval);
      ValFree(&val);
      RaiseX(vm, "Field target is not a struct", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    }
    AKSStruct *s = ValToObj(objval);
    int found = -1;
    // inline field cache: try cached index first
    if (frame->fn->chunk.fieldcache && sfipoffset >= 0) {
      int cached = frame->fn->chunk.fieldcache[sfipoffset];
      if (cached >= 0 && cached < s->fieldcount &&
          (s->fieldnames[cached] == fieldname || strcmp(s->fieldnames[cached], fieldname) == 0)) {
        found = cached;
      }
    }
    if (found < 0) {
      for (int i = 0; i < s->fieldcount; i++) {
        if (s->fieldnames[i] == fieldname || strcmp(s->fieldnames[i], fieldname) == 0) {
          found = i;
          break;
        }
      }
      if (found >= 0 && frame->fn->chunk.fieldcache && sfipoffset >= 0) {
        frame->fn->chunk.fieldcache[sfipoffset] = found;
      }
    }
    if (found >= 0) {
      ValFree(&s->fieldvals[found]);
      s->fieldvals[found] = val;
    } else {
      s->fieldcount++;
      s->fieldnames = realloc(s->fieldnames, s->fieldcount * sizeof(char *));
      s->fieldvals = realloc(s->fieldvals, s->fieldcount * sizeof(AKSValue));
      s->fieldnames[s->fieldcount - 1] = strdup(fieldname);
      s->fieldvals[s->fieldcount - 1] = val;
    }
    PUSH(ValCopy(val));
    ValFree(&objval);
    break;
  }

  case OPINC: {
    if (stacktop > frameslots && ValKind(*(stacktop - 1)) == VALINT) {
      int64_t v = ValToIntUnchecked(*(stacktop - 1));
      if (v < INT64_MAX) {
        *(stacktop - 1) = ValInt(v + 1);
        break;
      }
    }
    AKSValue val = POP();
    if (ValKind(val) == VALINT) {
      int64_t res;
      if (ChkAddOvf(ValToInt(val), 1, &res)) {
        if (gStrictOvf) {
          ValFree(&val);
          RaiseX(vm, "IntegerOverflowException: increment overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        } else {
          PUSH(ValFloat((double)ValToInt(val) + 1.0));
        }
      } else {
        PUSH(ValInt(res));
      }
    } else if (ValKind(val) == VALFLOAT) {
      PUSH(ValFloat(ValToFloat(val) + 1.0));
    } else {
      ValFree(&val);
      RaiseX(vm, "cannot increment non-numeric value", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    }
    ValFree(&val);
    break;
  }

  case OPDEC: {
    if (stacktop > frameslots && ValKind(*(stacktop - 1)) == VALINT) {
      int64_t v = ValToIntUnchecked(*(stacktop - 1));
      if (v > INT64_MIN) {
        *(stacktop - 1) = ValInt(v - 1);
        break;
      }
    }
    AKSValue val = POP();
    if (ValKind(val) == VALINT) {
      int64_t res;
      if (ChkSubOvf(ValToInt(val), 1, &res)) {
        if (gStrictOvf) {
          ValFree(&val);
          RaiseX(vm, "IntegerOverflowException: decrement overflow", &ip,
                 &stacktop, &frame, &frameslots);
          continue;
        } else {
          PUSH(ValFloat((double)ValToInt(val) - 1.0));
        }
      } else {
        PUSH(ValInt(res));
      }
    } else if (ValKind(val) == VALFLOAT) {
      PUSH(ValFloat(ValToFloat(val) - 1.0));
    } else {
      ValFree(&val);
      RaiseX(vm, "cannot decrement non-numeric value", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    }
    ValFree(&val);
    break;
  }

  case OPTRYPUSH: {
    int32_t offset = READI32();
    if (task->tryhandlercount >= 256) {
      fprintf(stderr, "Runtime error: too many nested try-catch blocks\n");
      return 0;
    }
    task->tryhandlers[task->tryhandlercount].frameindex = task->framecount;
    task->tryhandlers[task->tryhandlercount].stacktopbase =
        (int)(stacktop - task->stack);
    task->tryhandlers[task->tryhandlercount].catchip = ip + offset;
    task->tryhandlercount++;
    break;
  }

  case OPTRYPOP: {
    if (task->tryhandlercount > 0) {
      task->tryhandlercount--;
    }
    break;
  }

  case OPTHROW: {
    AKSValue err = POP();
    RaiseExceptionValue(vm, task, err, &ip, &stacktop, &frame, &frameslots);
    ValFree(&err);
    continue;
  }

  case OPCOALESCE: {
    int32_t offset = READI32();
    AKSValue val = PEEK();
    if (ValKind(val) != VALNULL) {
      uint8_t *targetip = ip + offset;
      uint8_t *codestart = frame->fn->chunk.code;
      uint8_t *codeend = codestart + frame->fn->chunk.count;
      if (targetip < codestart || targetip >= codeend) {
        RaiseX(vm, "CFIError: Jump target out of function bounds", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      ip += offset;
    } else {
      POP();
    }
    break;
  }

  case OPJUMPIFNULL: {
    int32_t offset = READI32();
    AKSValue val = PEEK();
    if (ValKind(val) == VALNULL) {
      uint8_t *targetip = ip + offset;
      uint8_t *codestart = frame->fn->chunk.code;
      uint8_t *codeend = codestart + frame->fn->chunk.count;
      if (targetip < codestart || targetip >= codeend) {
        RaiseX(vm, "CFIError: Jump target out of function bounds", &ip,
               &stacktop, &frame, &frameslots);
        continue;
      }
      ip += offset;
    }
    break;
  }

  case OPRANGE: {
    AKSValue endval = POP();
    AKSValue startval = POP();
    if (ValKind(startval) != VALINT || ValKind(endval) != VALINT) {
      ValFree(&startval);
      ValFree(&endval);
      RaiseX(vm, "Range bounds must be integers", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    }
    int64_t start = ValToInt(startval);
    int64_t end = ValToInt(endval);

    AKSValue rangeval = ValStruct("Range");
    AKSStruct *s = ValToObj(rangeval);
    s->fieldcount = 2;
    s->fieldnames = malloc(2 * sizeof(char *));
    s->fieldvals = malloc(2 * sizeof(AKSValue));
    s->fieldnames[0] = strdup("start");
    s->fieldvals[0] = ValInt(start);
    s->fieldnames[1] = strdup("end");
    s->fieldvals[1] = ValInt(end);

    ValFree(&startval);
    ValFree(&endval);
    PUSH(rangeval);
    break;
  }

  case OPPRINTREPL: {
    AKSValue val = PEEK();
    if (ValKind(val) != VALNULL) {
      if (ValKind(val) == VALSTR) {
        printf("\033[32m\"%s\"\033[0m\n", ValToStrPtr(val)->data);
      } else if (ValKind(val) == VALINT) {
        printf("\033[36m%lld\033[0m\n", (long long)ValToInt(val));
      } else if (ValKind(val) == VALFLOAT) {
        printf("\033[36m%g\033[0m\n", ValToFloat(val));
      } else if (ValKind(val) == VALBOOL) {
        printf("\033[33m%s\033[0m\n", ValToBool(val) ? "true" : "false");
      } else if (ValKind(val) == VALARR) {
        printf("\033[35m");
        ValPrint(val);
        printf("\033[0m\n");
      } else if (ValKind(val) == VALSTRUCT) {
        printf("\033[34m");
        ValPrint(val);
        printf("\033[0m\n");
      } else {
        ValPrintln(val);
      }
    }
    AKSValue popped = POP();
    ValFree(&popped);
    break;
  }

  case OPSPAWN: {
    uint16_t fnid = READU16();
    uint8_t argc = READBYTE();
    FnProto *callee = NULL;
    if (fnid == 65535) {
      AKSValue calleeval = POP();
      if (ValKind(calleeval) != VALFN) {
        ValFree(&calleeval);
        RaiseX(vm, "Callee is not a function", &ip, &stacktop, &frame,
               &frameslots);
        continue;
      }
      callee = vm->fns[ValToFn(calleeval)];
    } else {
      callee = vm->fns[fnid];
    }
    AKSValue args[32];
    for (int i = argc - 1; i >= 0; i--) {
      args[i] = POP();
    }
    VMTask *spawned = TaskNew(vm, callee, 0);
    for (int i = 0; i < argc && i < callee->paramcount; i++) {
      ValFree(&spawned->stack[i]);
      spawned->stack[i] = args[i];
    }
    int targetworker = rand() % vm->workercount;
    WorkerPushTail(&vm->workers[targetworker], spawned);
    PUSH(ValTask(spawned));
    break;
  }
  case OPSELECT: {
    uint16_t count = READU16();
    uint8_t has_default = READBYTE();
    if (count > 32) {
      fprintf(stderr, "Runtime error: select statement exceeds maximum operations (32)\n");
      return 0;
    }
    uint8_t *jumps_base = ip;
    if (task->select_woken_index >= 0) {
      int chosen = task->select_woken_index;
      task->select_woken_index = -1;
      task->select_woken_index = -1;
      task->is_in_select = 0;

      // Clean up registration on all channels
      LockSelectChannels(task);
      for (int i = 0; i < count; i++) {
        VMChannel *c = task->select_ops[i].chan;
        if (task->select_ops[i].is_send) {
          for (int k = 0; k < c->writercount; k++) {
            if (c->blockedwriters[k] == task) {
              for (int m = k; m < c->writercount - 1; m++) {
                c->blockedwriters[m] = c->blockedwriters[m + 1];
              }
              c->writercount--;
              break;
            }
          }
        } else {
          for (int k = 0; k < c->readercount; k++) {
            if (c->blockedreaders[k] == task) {
              for (int m = k; m < c->readercount - 1; m++) {
                c->blockedreaders[m] = c->blockedreaders[m + 1];
              }
              c->readercount--;
              break;
            }
          }
        }
      }
      UnlockSelectChannels(task);

      int base_stack_idx = (int)(stacktop - task->stack) - 3 * count;
      AKSValue chosen_chanval = task->stack[base_stack_idx + chosen * 3];
      AKSValue chosen_sendval = task->stack[base_stack_idx + chosen * 3 + 1];
      AKSValue chosen_is_sendval = task->stack[base_stack_idx + chosen * 3 + 2];

      VMChannel *c = ValToChan(chosen_chanval);
      int is_send = (int)ValToInt(chosen_is_sendval);

      AKSValue result_val = ValNull();
      if (is_send) {
        MutexLockNative(c->lock);
        if (c->isclosed) {
          MutexUnlockNative(c->lock);
          RaiseX(vm, "send on closed channel", &ip, &stacktop, &frame,
                 &frameslots);
          continue;
        }
        c->buffer[(c->head + c->count) % c->capacity] = ValCopy(chosen_sendval);
        c->count++;
        if (c->readercount > 0) {
          VMTask *reader = c->blockedreaders[0];
          for (int k = 0; k < c->readercount - 1; k++) {
            c->blockedreaders[k] = c->blockedreaders[k + 1];
          }
          c->readercount--;
          WakeBlockedTaskOnChannel(vm, reader, c);
        }
        MutexUnlockNative(c->lock);
        result_val = ValNull();
      } else {
        MutexLockNative(c->lock);
        if (c->count > 0) {
          result_val = c->buffer[c->head];
          c->head = (c->head + 1) % c->capacity;
          c->count--;
          if (c->writercount > 0) {
            VMTask *writer = c->blockedwriters[0];
            for (int k = 0; k < c->writercount - 1; k++) {
              c->blockedwriters[k] = c->blockedwriters[k + 1];
            }
            c->writercount--;
            WakeBlockedTaskOnChannel(vm, writer, c);
          }
        } else if (c->isclosed) {
          result_val = ValNull();
        }
        MutexUnlockNative(c->lock);
      }

      for (int k = 0; k < count * 3; k++) {
        AKSValue val = POP();
        ValFree(&val);
      }

      PUSH(result_val);

      uint8_t *jump_base = jumps_base + chosen * 2;
      int16_t offset = (int16_t)((jump_base[0] << 8) | jump_base[1]);
      ip = jump_base + 2 + offset;
      break;
    }

    task->select_op_count = count;
    int base_stack_idx = (int)(stacktop - task->stack) - 3 * count;
    for (int i = 0; i < count; i++) {
      task->select_ops[i].chan = ValToChan(task->stack[base_stack_idx + i * 3]);
      task->select_ops[i].send_val = task->stack[base_stack_idx + i * 3 + 1];
      task->select_ops[i].is_send =
          (int)ValToInt(task->stack[base_stack_idx + i * 3 + 2]);
    }

    LockSelectChannels(task);

    int chosen_ready = -1;
    for (int i = 0; i < count; i++) {
      VMChannel *c = task->select_ops[i].chan;
      if (task->select_ops[i].is_send) {
        if (c->isclosed || c->count < c->capacity) {
          chosen_ready = i;
          break;
        }
      } else {
        if (c->count > 0 || c->isclosed) {
          chosen_ready = i;
          break;
        }
      }
    }

    if (chosen_ready >= 0) {
      VMChannel *c = task->select_ops[chosen_ready].chan;
      int is_send = task->select_ops[chosen_ready].is_send;
      AKSValue sendval = task->select_ops[chosen_ready].send_val;

      AKSValue result_val = ValNull();
      if (is_send) {
        if (c->isclosed) {
          UnlockSelectChannels(task);
          RaiseX(vm, "send on closed channel", &ip, &stacktop, &frame,
                 &frameslots);
          continue;
        }
        c->buffer[(c->head + c->count) % c->capacity] = ValCopy(sendval);
        c->count++;
        if (c->readercount > 0) {
          VMTask *reader = c->blockedreaders[0];
          for (int k = 0; k < c->readercount - 1; k++) {
            c->blockedreaders[k] = c->blockedreaders[k + 1];
          }
          c->readercount--;
          WakeBlockedTaskOnChannel(vm, reader, c);
        }
        result_val = ValNull();
      } else {
        if (c->count > 0) {
          result_val = c->buffer[c->head];
          c->head = (c->head + 1) % c->capacity;
          c->count--;
          if (c->writercount > 0) {
            VMTask *writer = c->blockedwriters[0];
            for (int k = 0; k < c->writercount - 1; k++) {
              c->blockedwriters[k] = c->blockedwriters[k + 1];
            }
            c->writercount--;
            WakeBlockedTaskOnChannel(vm, writer, c);
          }
        } else if (c->isclosed) {
          result_val = ValNull();
        }
      }

      UnlockSelectChannels(task);

      for (int k = 0; k < count * 3; k++) {
        AKSValue val = POP();
        ValFree(&val);
      }

      PUSH(result_val);

      uint8_t *jump_base = jumps_base + chosen_ready * 2;
      int16_t offset = (int16_t)((jump_base[0] << 8) | jump_base[1]);
      ip = jump_base + 2 + offset;
      break;
    }

    if (has_default) {
      UnlockSelectChannels(task);

      for (int k = 0; k < count * 3; k++) {
        AKSValue val = POP();
        ValFree(&val);
      }

      PUSH(ValNull());

      uint8_t *jump_base = jumps_base + count * 2;
      int16_t offset = (int16_t)((jump_base[0] << 8) | jump_base[1]);
      ip = jump_base + 2 + offset;
      break;
    }

    task->is_in_select = 1;
    task->select_woken_index = -1;
    for (int i = 0; i < count; i++) {
      VMChannel *c = task->select_ops[i].chan;
      if (task->select_ops[i].is_send) {
        if (c->writercount < 256) {
          c->blockedwriters[c->writercount++] = task;
        }
      } else {
        if (c->readercount < 256) {
          c->blockedreaders[c->readercount++] = task;
        }
      }
    }

    UnlockSelectChannels(task);

    frame->ip = ip - 4;
    task->stacktop = (int)(stacktop - task->stack);
    task->state = 2; // Blocked
    task->blockedon = task->select_ops[0].chan;
    task->blockedontype = 5;
    return 2;
  }
  case OPAWAIT: {
    AKSValue timeoutval = POP();
    AKSValue taskval = POP();
    if (ValKind(taskval) != VALTASK) {
      ValFree(&timeoutval);
      ValFree(&taskval);
      RaiseX(vm, "await operand is not a task", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    }
    VMTask *target = ValToTask(taskval);

    if (task->timedout) {
      task->timedout = 0;
      MutexLockNative(vm->globallock);
      for (int i = 0; i < target->awaitercount; i++) {
        if (target->awaiters[i] == task) {
          for (int j = i; j < target->awaitercount - 1; j++) {
            target->awaiters[j] = target->awaiters[j + 1];
          }
          target->awaitercount--;
          break;
        }
      }
      MutexUnlockNative(vm->globallock);
      VMRemoveTimer(vm, task);
      ValFree(&taskval);
      ValFree(&timeoutval);
      RaiseX(vm, "TimeoutError: await timed out", &ip, &stacktop, &frame,
             &frameslots);
      continue;
    }

    MutexLockNative(vm->globallock);
    if (target->state == 3) {
      if (target->haserror) {
        const char *errmsg = "Unknown task error";
        if (ValKind(target->result) == VALSTR) {
          errmsg = ValToStrPtr(target->result)->data;
        }
        MutexUnlockNative(vm->globallock);
        VMRemoveTimer(vm, task);
        ValFree(&taskval);
        ValFree(&timeoutval);
        RaiseX(vm, errmsg, &ip, &stacktop, &frame, &frameslots);
        continue;
      }
      PUSH(ValCopy(target->result));
      MutexUnlockNative(vm->globallock);
      VMRemoveTimer(vm, task);
      ValFree(&taskval);
      ValFree(&timeoutval);
      break;
    } else {
      if (target->awaitercount >= target->awaitercap) {
        target->awaitercap =
            target->awaitercap == 0 ? 4 : target->awaitercap * 2;
        target->awaiters =
            realloc(target->awaiters, target->awaitercap * sizeof(VMTask *));
      }
      int found = 0;
      for (int i = 0; i < target->awaitercount; i++) {
        if (target->awaiters[i] == task) {
          found = 1;
          break;
        }
      }
      if (!found) {
        target->awaiters[target->awaitercount++] = task;
      }
      MutexUnlockNative(vm->globallock);

      uint64_t t_ms = 0;
      int has_timeout = 0;
      if (ValKind(timeoutval) == VALINT) {
        t_ms = (uint64_t)ValToInt(timeoutval);
        has_timeout = 1;
      } else if (ValKind(timeoutval) == VALFLOAT) {
        t_ms = (uint64_t)ValToFloat(timeoutval);
        has_timeout = 1;
      }
      if (has_timeout) {
        VMAddTimer(vm, task, t_ms);
      }

      PUSH(taskval);
      PUSH(timeoutval);
      frame->ip = ip - 1; // Keep ip at opawait
      task->stacktop = (int)(stacktop - task->stack);
      task->state = 2; // Blocked
      task->blockedon = target;
      task->blockedontype = 4;
      return 2; // blocked
    }
  }
  case OPASM: {
    uint16_t strid = READU16();
    const char *asm_code = (strid < (uint16_t)vm->strcount && vm->strpool[strid]) ? vm->strpool[strid] : "";
    (void)asm_code;
    PUSH(ValInt(0));
    break;
  }
  case OP_STATIC_RELEASE: {
    if (stacktop > frameslots) {
      stacktop--;
      if (ValKind(*stacktop) >= VALSTR) {
        ValFree(stacktop);
      }
    }
    vm->static_rc_eliminated++;
    break;
  }
  default:
    fprintf(stderr,
            "Runtime error: unknown opcode: %d at PC: %d inside function: %s\n",
            op, (int)(ip - 1 - frame->fn->chunk.code), frame->fn->name);
    task->state = 3; // terminated/Completed (with error)
    task->result = ValNull();
    task->stacktop = (int)(stacktop - task->stack);
    frame->ip = ip;
    return 1;
  }
}
return 0;
}
#pragma warning(pop)
#if defined(_MSC_VER)
#pragma optimize("", on)
#elif defined(__clang__)
#pragma clang optimize on
#elif defined(__GNUC__)
#pragma GCC optimize("O2")
#endif
#undef RaiseX
#undef ExecuteStdlib
#undef DispatchStdlib
#undef FRAME_GROW
#undef STACK_GROW
static void StructSetFieldNative(AKSStruct *s, const char *fieldname,
                                 AKSValue val) {
  int found = -1;
  for (int i = 0; i < s->fieldcount; i++) {
    if (strcmp(s->fieldnames[i], fieldname) == 0) {
      found = i;
      break;
    }
  }
  if (found >= 0) {
    ValFree(&s->fieldvals[found]);
    s->fieldvals[found] = ValCopy(val);
  } else {
    s->fieldcount++;
    s->fieldnames = realloc(s->fieldnames, s->fieldcount * sizeof(char *));
    s->fieldvals = realloc(s->fieldvals, s->fieldcount * sizeof(AKSValue));
    s->fieldnames[s->fieldcount - 1] = strdup(fieldname);
    s->fieldvals[s->fieldcount - 1] = ValCopy(val);
  }
}

static void TriggerPanic(VM *vm, VMTask *task, const char *msg,
                         uint8_t **ipptr) {
  if (vm->panic_hook != 0 && vm->panic_hook != ValNull()) {
    AKSValue backtrace_arr = ValArr(task->framecount);
    ValToArr(backtrace_arr)->count = task->framecount;
    for (int i = 0; i < task->framecount; i++) {
      CallFrame *f = &task->frames[i];
      AKSValue frame_info = ValStruct("CallFrame");
      StructSetFieldNative(ValToObj(frame_info), "function",
                           ValStr(f->fn->name));
      StructSetFieldNative(ValToObj(frame_info), "file",
                           ValStr(f->fn->file_name));
      int offset = (ipptr && *ipptr && i == task->framecount - 1)
                       ? (int)(*ipptr - f->fn->chunk.code)
                       : (int)(f->ip - f->fn->chunk.code);
      int line =
          (f->fn->chunk.lines && offset >= 0 && offset < f->fn->chunk.count)
              ? f->fn->chunk.lines[offset]
              : 0;
      StructSetFieldNative(ValToObj(frame_info), "line", ValInt(line));
      ValToArr(backtrace_arr)->items[i] = frame_info;
    }

    AKSValue panic_info = ValStruct("Panic");
    StructSetFieldNative(ValToObj(panic_info), "message", ValStr(msg));
    StructSetFieldNative(ValToObj(panic_info), "backtrace", backtrace_arr);

    if (ValKind(vm->panic_hook) == VALFN) {
      int fnid = ValToFn(vm->panic_hook);
      if (fnid < vm->fncount) {
        FnProto *hook_fn = vm->fns[fnid];
        VMTask *hook_task = TaskNew(vm, hook_fn, 0);
        hook_task->stack[0] = ValCopy(panic_info);
        hook_task->frames[0].argc = 1;
        hook_task->stacktop =
            hook_fn->maxregisters > 1 ? hook_fn->maxregisters : 1;

        while (hook_task->state != 3 && !vm->shouldterminate) {
          int res = RunTaskSlice(vm, hook_task, 100);
          if (res == 2) {
            SleepMs(1);
          }
        }
        TaskReLease(vm, hook_task);
      }
    }
    ValFree(&panic_info);
  }

  fprintf(stderr, "Panic: %s\n", msg);
  fprintf(stderr, "--- PANIC BACKTRACE ---\n");
  for (int i = task->framecount - 1; i >= 0; i--) {
    CallFrame *f = &task->frames[i];
    int offset = (ipptr && *ipptr && i == task->framecount - 1)
                     ? (int)(*ipptr - f->fn->chunk.code)
                     : (int)(f->ip - f->fn->chunk.code);
    int line =
        (f->fn->chunk.lines && offset >= 0 && offset < f->fn->chunk.count)
            ? f->fn->chunk.lines[offset]
            : 0;
    fprintf(stderr, "  at %s (%s:%d)\n", f->fn->name,
            f->fn->file_name[0] ? f->fn->file_name : "<unknown>", line);
  }
  fflush(stderr);
  exit(101);
}

static void RaiseExceptionValue(VM *vm, VMTask *task, AKSValue exception_val,
                                uint8_t **ipptr, AKSValue **stacktopptr,
                                CallFrame **frameptr,
                                AKSValue **frameslotsptr) {
  if (task->tryhandlercount > 0) {
    task->tryhandlercount--;
    int targetframeidx = task->tryhandlers[task->tryhandlercount].frameindex;
    int targetstackbase = task->tryhandlers[task->tryhandlercount].stacktopbase;
    uint8_t *catchip = task->tryhandlers[task->tryhandlercount].catchip;
    while (task->framecount > targetframeidx) {
      CallFrame *f = &task->frames[task->framecount - 1];
      AKSValue *base = task->stack + f->slotsbase;
      while (*stacktopptr > base) {
        AKSValue val = *--(*stacktopptr);
        ValFree(&val);
      }
      task->framecount--;
    }
    AKSValue *targettop = task->stack + targetstackbase;
    while (*stacktopptr > targettop) {
      AKSValue val = *--(*stacktopptr);
      ValFree(&val);
    }
    *frameptr = &task->frames[task->framecount - 1];
    *ipptr = catchip;
    *frameslotsptr = task->stack + (*frameptr)->slotsbase;
    *(*stacktopptr)++ = ValCopy(exception_val);
  } else {
    const char *msg = "Uncaught exception";
    char temp_msg[512];
    AKSValue str_val = ValToStr(exception_val);
    if (ValKind(str_val) == VALSTR) {
      strncpy(temp_msg, ValToStrPtr(str_val)->data, sizeof(temp_msg) - 1);
      temp_msg[sizeof(temp_msg) - 1] = '\0';
      msg = temp_msg;
    }
    ValFree(&str_val);

    if (gTestMode) {
      task->haserror = 1;
      task->result = ValCopy(exception_val);
      task->state = 3;
      return;
    }
    task->haserror = 1;
    // Generate core dump
    FILE *dump = fopen("akscoredump.json", "w");
    if (dump) {
      fprintf(dump,
              "{\n  \"error\": \"%s\",\n  \"task\": \"%p\",\n  \"frames\": [\n",
              msg, (void *)task);
      for (int i = 0; i < task->framecount; i++) {
        CallFrame *f = &task->frames[i];
        int offset = (ipptr && *ipptr && i == task->framecount - 1)
                         ? (int)(*ipptr - f->fn->chunk.code)
                         : (int)(f->ip - f->fn->chunk.code);
        int line =
            (f->fn->chunk.lines && offset >= 0 && offset < f->fn->chunk.count)
                ? f->fn->chunk.lines[offset]
                : 0;
        fprintf(dump, "    { \"function\": \"%s\", \"line\": %d }%s\n",
                f->fn->name, line, (i == task->framecount - 1) ? "" : ",");
      }
      fprintf(dump, "  ]\n}\n");
      fclose(dump);
    }
    fprintf(stderr, "Unhandled exception in %s(): %s\n",
            task->frames[task->framecount - 1].fn->name, msg);
    for (int i = task->framecount - 1; i >= 0; i--) {
      CallFrame *f = &task->frames[i];
      int offset = (ipptr && *ipptr && i == task->framecount - 1)
                       ? (int)(*ipptr - f->fn->chunk.code)
                       : (int)(f->ip - f->fn->chunk.code);
      int line = 0;
      if (f->fn->chunk.lines && offset >= 0 && offset < f->fn->chunk.count) {
        line = f->fn->chunk.lines[offset];
      }
      fprintf(stderr, "  at %s() (line %d)\n", f->fn->name, line);
    }
    if (task == vm->maintask &&
        strncmp(task->frames[0].fn->name, "axseval", 7) != 0) {
      vm->shouldterminate = 1;
    } else {
      task->result = ValCopy(exception_val);
    }
    task->stacktop = (int)(*stacktopptr - task->stack);
    task->state = 3;
  }
}

static void RaiseEx(VM *vm, VMTask *task, const char *msg, uint8_t **ipptr,
                    AKSValue **stacktopptr, CallFrame **frameptr,
                    AKSValue **frameslotsptr) {
  AKSValue exception_val = ValStr(msg);
  RaiseExceptionValue(vm, task, exception_val, ipptr, stacktopptr, frameptr,
                      frameslotsptr);
  ValFree(&exception_val);
}

int VMRepl(VM *vm, int startipoffset) {
  if (vm->fncount == 0) {
    fprintf(stderr, "Runtime error: no functions loaded in VM\n");
    return 0;
  }
  VMTask *repltask = TaskNew(vm, vm->fns[0], startipoffset);
  WorkerPushTail(&vm->workers[0], repltask);
  while (repltask->state != 3 && !vm->shouldterminate) {
    SleepMs(1);
  }
  int success = (repltask->state == 3 && !vm->shouldterminate);
  MutexLockNative(vm->globallock);
  for (int i = 0; i < vm->alltaskscount; i++) {
    if (vm->alltasks[i] == repltask) {
      for (int j = i; j < vm->alltaskscount - 1; j++) {
        vm->alltasks[j] = vm->alltasks[j + 1];
      }
      vm->alltaskscount--;
      break;
    }
  }
  MutexUnlockNative(vm->globallock);
  TaskFree(repltask);
  return success;
}

static void VMLoadPGOProfile(VM *vm) {
  if (!vm->pgouse)
    return;
  FILE *pf = fopen(vm->pgouse, "r");
  if (!pf)
    return;
  char line[128];
  while (fgets(line, sizeof(line), pf)) {
    char type[16];
    int fnid = 0;
    int offset = 0;
    int count = 0;
    if (sscanf(line, "%s %d %d %d", type, &fnid, &offset, &count) == 4) {
      if (strcmp(type, "loop") == 0) {
        if (fnid >= 0 && fnid < vm->fncount && vm->fns[fnid]) {
          FnProto *fn = vm->fns[fnid];
          if (fn->chunk.loopcounters && offset >= 0 &&
              offset < fn->chunk.count) {
            fn->chunk.loopcounters[offset] = count;
          }
        }
      } else if (strcmp(type, "call") == 0) {
        if (fnid >= 0 && fnid < 4096) {
          vm->fncallcounts[fnid] = count;
          if (count >= 50 && fnid < vm->fncount && vm->fns[fnid] &&
              vm->jitmode > 0) {
            TriggerMethodJIT(vm, NULL, vm->fns[fnid]);
          }
        }
      }
    }
  }
  fclose(pf);
}

VM *GActiveVm = NULL;
int gTestMode = 0;
int gCoverageEnabled = 0;
char gStdlibErrorMessage[2048] = {0};

int VMRun(VM *vm) {
  if (vm->fncount == 0) {
    fprintf(stderr, "Runtime error: no functions loaded in VM\n");
    return 0;
  }
  GActiveVm = vm;
  VMLoadPGOProfile(vm);
  vm->maintask = TaskNew(vm, vm->fns[0], 0);
  WorkerPushTail(&vm->workers[0], vm->maintask);
  while (vm->maintask->state != 3 && !vm->shouldterminate) {
    SleepMs(1);
  }
  return (vm->maintask->state == 3 && !vm->shouldterminate);
}
void VMSync(VM *vm, Compiler *c, int syncmain) {
  int old_fncount = vm->fncount;
  // Sync string pool
  if (vm->strpool) {
    for (int i = 0; i < vm->strcount; i++) {
      free(vm->strpool[i]);
    }
    free(vm->strpool);
  }
  vm->strcount = c->strcount;
  vm->strpool = calloc(vm->strcount, sizeof(char *));
  for (int i = 0; i < vm->strcount; i++) {
    vm->strpool[i] = strdup(c->strpool[i]);
  }

  // Sync functions list
  for (int i = 0; i < vm->fncount && i < c->fncount; i++) {
    if (i == 0 && !syncmain)
      continue;
    FnProto *dest = vm->fns[i];
    FnProto *src = c->fns[i];

    dest->localcount = src->localcount;
    dest->scopedepth = src->scopedepth;

    if (dest->chunk.code)
      free(dest->chunk.code);
    if (dest->chunk.lines)
      free(dest->chunk.lines);
    if (dest->chunk.globalcache)
      free(dest->chunk.globalcache);
    if (dest->chunk.fieldcache)
      free(dest->chunk.fieldcache);
    if (dest->chunk.loopcounters)
      free(dest->chunk.loopcounters);

    dest->chunk.count = src->chunk.count;
    dest->chunk.cap = src->chunk.cap;
    dest->chunk.code = malloc(src->chunk.count);
    memcpy(dest->chunk.code, src->chunk.code, src->chunk.count);

    dest->chunk.lines = malloc(src->chunk.count * sizeof(int));
    memcpy(dest->chunk.lines, src->chunk.lines, src->chunk.count * sizeof(int));

    dest->chunk.globalcache = malloc(src->chunk.count * sizeof(int));
    for (int k = 0; k < src->chunk.count; k++)
      dest->chunk.globalcache[k] = -1;
    dest->chunk.fieldcache = malloc(src->chunk.count * sizeof(int));
    for (int k = 0; k < src->chunk.count; k++)
      dest->chunk.fieldcache[k] = -1;

    dest->chunk.loopcounters = calloc(src->chunk.count, sizeof(int));
  }

  for (int i = vm->fncount; i < c->fncount; i++) {
    FnProto *srcfn = c->fns[i];
    FnProto *destfn = calloc(1, sizeof(FnProto));
    strncpy(destfn->name, srcfn->name, 127);
    strncpy(destfn->file_name, srcfn->file_name, sizeof(destfn->file_name) - 1);
    destfn->file_name[sizeof(destfn->file_name) - 1] = '\0';
    destfn->id = srcfn->id;
    destfn->paramcount = srcfn->paramcount;
    destfn->localcount = srcfn->localcount;
    destfn->scopedepth = srcfn->scopedepth;

    destfn->chunk.count = srcfn->chunk.count;
    destfn->chunk.cap = srcfn->chunk.cap;
    destfn->chunk.code = malloc(srcfn->chunk.count);
    memcpy(destfn->chunk.code, srcfn->chunk.code, srcfn->chunk.count);

    destfn->chunk.lines = malloc(srcfn->chunk.count * sizeof(int));
    memcpy(destfn->chunk.lines, srcfn->chunk.lines,
           srcfn->chunk.count * sizeof(int));

    destfn->chunk.globalcache = malloc(srcfn->chunk.count * sizeof(int));
    for (int k = 0; k < srcfn->chunk.count; k++)
      destfn->chunk.globalcache[k] = -1;
    destfn->chunk.fieldcache = malloc(srcfn->chunk.count * sizeof(int));
    for (int k = 0; k < srcfn->chunk.count; k++)
      destfn->chunk.fieldcache[k] = -1;

    destfn->chunk.loopcounters = calloc(srcfn->chunk.count, sizeof(int));

    vm->fns[i] = destfn;
  }
  vm->fncount = c->fncount;
  if (syncmain) {
    for (int i = 0; i < vm->fncount; i++) {
      TranslateToRegister(vm, vm->fns[i]);
    }
  } else {
    for (int i = (old_fncount == 0 ? 1 : old_fncount); i < vm->fncount; i++) {
      TranslateToRegister(vm, vm->fns[i]);
    }
  }
  VMInjectNatives(vm);
}

const char *StackOpName(int op) {
  switch ((OpCode)op) {
  case OPHALT:
    return "OPHALT";
  case OPPUSHINT:
    return "OPPUSHINT";
  case OPPUSHFLOAT:
    return "OPPUSHFLOAT";
  case OPPUSHSTR:
    return "OPPUSHSTR";
  case OPPUSHBOOL:
    return "OPPUSHBOOL";
  case OPPUSHNULL:
    return "OPPUSHNULL";
  case OPLOAD:
    return "OPLOAD";
  case OPLOADGLOBAL:
    return "OPLOADGLOBAL";
  case OPSTORE:
    return "OPSTORE";
  case OPSTOREGLOBAL:
    return "OPSTOREGLOBAL";
  case OPDUP:
    return "OPDUP";
  case OPPOP:
    return "OPPOP";
  case OPPRINTLN:
    return "OPPRINTLN";
  case OPINPUTSTR:
    return "OPINPUTSTR";
  case OPADD:
    return "OPADD";
  case OPSUB:
    return "OPSUB";
  case OPMUL:
    return "OPMUL";
  case OPDIV:
    return "OPDIV";
  case OPMOD:
    return "OPMOD";
  case OPEQ:
    return "OPEQ";
  case OPNEQ:
    return "OPNEQ";
  case OPLT:
    return "OPLT";
  case OPGT:
    return "OPGT";
  case OPLTE:
    return "OPLTE";
  case OPGTE:
    return "OPGTE";
  case OPAND:
    return "OPAND";
  case OPOR:
    return "OPOR";
  case OPNOT:
    return "OPNOT";
  case OPNEG:
    return "OPNEG";
  case OPJUMP:
    return "OPJUMP";
  case OPJUMPIFFALSE:
    return "OPJUMPIFFALSE";
  case OPCALL:
    return "OPCALL";
  case OPRETURN:
    return "OPRETURN";
  case OPRETURNVAL:
    return "OPRETURNVAL";
  case OPNEWARR:
    return "OPNEWARR";
  case OPARRGET:
    return "OPARRGET";
  case OPARRSET:
    return "OPARRSET";
  case OPNEWSTRUCT:
    return "OPNEWSTRUCT";
  case OPGETFIELD:
    return "OPGETFIELD";
  case OPSETFIELD:
    return "OPSETFIELD";
  case OPINC:
    return "OPINC";
  case OPDEC:
    return "OPDEC";
  case OPPUSHFN:
    return "OPPUSHFN";
  case OPCALLDYNAMIC:
    return "OPCALLDYNAMIC";
  case OPTRYPUSH:
    return "OPTRYPUSH";
  case OPTRYPOP:
    return "OPTRYPOP";
  case OPTHROW:
    return "OPTHROW";
  case OPCOALESCE:
    return "OPCOALESCE";
  case OPRANGE:
    return "OPRANGE";
  case OPJUMPIFNULL:
    return "OPJUMPIFNULL";
  case OPTAILCALL:
    return "OPTAILCALL";
  case OPPRINTREPL:
    return "OPPRINTREPL";
  case OPSPAWN:
    return "OPSPAWN";
  case OPAWAIT:
    return "OPAWAIT";
  case OPSHL:
    return "OPSHL";
  case OPSHR:
    return "OPSHR";
  case OPBITAND:
    return "OPBITAND";
  case OPBITOR:
    return "OPBITOR";
  case OPBITXOR:
    return "OPBITXOR";
  case OPBITNOT:
    return "OPBITNOT";
  case OPAPPLY:
    return "OPAPPLY";
  case OPRESTARGS:
    return "OPRESTARGS";
  case OPTYPEOF:
    return "OPTYPEOF";
  case OPCLONESTRUCT:
    return "OPCLONESTRUCT";
  case OPCASTINT:
    return "OPCASTINT";
  case OPCASTFLOAT:
    return "OPCASTFLOAT";
  case OPCASTBOOL:
    return "OPCASTBOOL";
  case OPCASTSTR:
    return "OPCASTSTR";
  case OP_STATIC_RELEASE:
    return "OP_STATIC_RELEASE";
  default:
    return "UNKNOWN";
  }
}

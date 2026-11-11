// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "vm_stdlib.h"

#define AKS_MAX_FILE_HANDLES 256
static FILE *g_aks_file_handles[AKS_MAX_FILE_HANDLES] = {0};

#ifdef _WIN32
static CRITICAL_SECTION g_aks_file_cs;
static int g_aks_file_cs_init = 0;
static void AksFileTableLock(void) {
  if (!g_aks_file_cs_init) {
    InitializeCriticalSection(&g_aks_file_cs);
    g_aks_file_cs_init = 1;
  }
  EnterCriticalSection(&g_aks_file_cs);
}
static void AksFileTableUnlock(void) { LeaveCriticalSection(&g_aks_file_cs); }
#else
static pthread_mutex_t g_aks_file_mtx = PTHREAD_MUTEX_INITIALIZER;
static void AksFileTableLock(void) { pthread_mutex_lock(&g_aks_file_mtx); }
static void AksFileTableUnlock(void) { pthread_mutex_unlock(&g_aks_file_mtx); }
#endif

static int AksFileOpen(const char *path, const char *mode) {
  if (!path || !mode)
    return -1;
  AksFileTableLock();
  int slot = -1;
  for (int i = 0; i < AKS_MAX_FILE_HANDLES; i++) {
    if (g_aks_file_handles[i] == NULL) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    AksFileTableUnlock();
    return -1;
  }
  FILE *f = fopen(path, mode);
  if (!f) {
    AksFileTableUnlock();
    return -1;
  }
  g_aks_file_handles[slot] = f;
  AksFileTableUnlock();
  return slot;
}

static int AksFileClose(int handle_id) {
  if (handle_id < 0 || handle_id >= AKS_MAX_FILE_HANDLES)
    return 0;
  AksFileTableLock();
  if (!g_aks_file_handles[handle_id]) {
    AksFileTableUnlock();
    return 0;
  }
  fclose(g_aks_file_handles[handle_id]);
  g_aks_file_handles[handle_id] = NULL;
  AksFileTableUnlock();
  return 1;
}

void AksCloseAllFileHandles(void) {
  AksFileTableLock();
  for (int i = 0; i < AKS_MAX_FILE_HANDLES; i++) {
    if (g_aks_file_handles[i]) {
      fclose(g_aks_file_handles[i]);
      g_aks_file_handles[i] = NULL;
    }
  }
  AksFileTableUnlock();
}

static char *AksFileReadLine(int handle_id) {
  if (handle_id < 0 || handle_id >= AKS_MAX_FILE_HANDLES)
    return NULL;
  AksFileTableLock();
  FILE *f = g_aks_file_handles[handle_id];
  if (!f) {
    AksFileTableUnlock();
    return NULL;
  }
  size_t cap = 256;
  size_t len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    AksFileTableUnlock();
    return NULL;
  }
  buf[0] = '\0';
  while (fgets(buf + len, (int)(cap - len), f)) {
    size_t chunk_len = strlen(buf + len);
    len += chunk_len;
    if (len > 0 && buf[len - 1] == '\n') {
      break;
    }
    if (len + 1 >= cap) {
      cap *= 2;
      char *new_buf = (char *)realloc(buf, cap);
      if (!new_buf) {
        free(buf);
        AksFileTableUnlock();
        return NULL;
      }
      buf = new_buf;
    }
  }
  if (len == 0 && feof(f)) {
    free(buf);
    AksFileTableUnlock();
    return NULL;
  }
  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
    buf[--len] = '\0';
  }
  AksFileTableUnlock();
  return buf;
}

static char *AksFileReadBytes(int handle_id, size_t count, size_t *out_len) {
  if (handle_id < 0 || handle_id >= AKS_MAX_FILE_HANDLES || count == 0) {
    if (out_len)
      *out_len = 0;
    return NULL;
  }
  AksFileTableLock();
  FILE *f = g_aks_file_handles[handle_id];
  if (!f) {
    if (out_len)
      *out_len = 0;
    AksFileTableUnlock();
    return NULL;
  }
  char *buf = (char *)malloc(count + 1);
  if (!buf) {
    if (out_len)
      *out_len = 0;
    AksFileTableUnlock();
    return NULL;
  }
  size_t rb = fread(buf, 1, count, f);
  buf[rb] = '\0';
  if (out_len)
    *out_len = rb;
  if (rb == 0 && feof(f)) {
    free(buf);
    AksFileTableUnlock();
    return NULL;
  }
  AksFileTableUnlock();
  return buf;
}

static int AksFileWrite(int handle_id, const char *data, size_t len) {
  if (handle_id < 0 || handle_id >= AKS_MAX_FILE_HANDLES || !data)
    return 0;
  AksFileTableLock();
  FILE *f = g_aks_file_handles[handle_id];
  if (!f) {
    AksFileTableUnlock();
    return 0;
  }
  size_t wb = fwrite(data, 1, len, f);
  AksFileTableUnlock();
  return (wb == len) ? 1 : 0;
}

static int AksFileWriteLine(int handle_id, const char *data, size_t len) {
  if (handle_id < 0 || handle_id >= AKS_MAX_FILE_HANDLES || !data)
    return 0;
  AksFileTableLock();
  FILE *f = g_aks_file_handles[handle_id];
  if (!f) {
    AksFileTableUnlock();
    return 0;
  }
  fwrite(data, 1, len, f);
  fputc('\n', f);
  AksFileTableUnlock();
  return 1;
}

static int AksFileSeek(int handle_id, long offset, int whence) {
  if (handle_id < 0 || handle_id >= AKS_MAX_FILE_HANDLES)
    return 0;
  AksFileTableLock();
  FILE *f = g_aks_file_handles[handle_id];
  if (!f) {
    AksFileTableUnlock();
    return 0;
  }
  int c_whence = SEEK_SET;
  if (whence == 1)
    c_whence = SEEK_CUR;
  else if (whence == 2)
    c_whence = SEEK_END;
  int res = fseek(f, offset, c_whence);
  AksFileTableUnlock();
  return (res == 0) ? 1 : 0;
}

static long AksFileTell(int handle_id) {
  if (handle_id < 0 || handle_id >= AKS_MAX_FILE_HANDLES)
    return -1;
  AksFileTableLock();
  FILE *f = g_aks_file_handles[handle_id];
  if (!f) {
    AksFileTableUnlock();
    return -1;
  }
  long pos = ftell(f);
  AksFileTableUnlock();
  return pos;
}

static int AksFileFlush(int handle_id) {
  if (handle_id < 0 || handle_id >= AKS_MAX_FILE_HANDLES)
    return 0;
  AksFileTableLock();
  FILE *f = g_aks_file_handles[handle_id];
  if (!f) {
    AksFileTableUnlock();
    return 0;
  }
  int res = fflush(f);
  AksFileTableUnlock();
  return (res == 0) ? 1 : 0;
}

static int AksFileEof(int handle_id) {
  if (handle_id < 0 || handle_id >= AKS_MAX_FILE_HANDLES)
    return 1;
  AksFileTableLock();
  FILE *f = g_aks_file_handles[handle_id];
  if (!f) {
    AksFileTableUnlock();
    return 1;
  }
  int is_eof = feof(f);
  AksFileTableUnlock();
  return is_eof ? 1 : 0;
}

#ifndef FSCTL_SET_SPARSE
#define FSCTL_SET_SPARSE 0x000900C4
#endif

#ifndef _WIN32
  typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    int threshold;
    int count;
    int generation;
  } AksPosixBarrier;
  typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    int signaled;
  } AksPosixCond;
  typedef struct {
    pthread_mutex_t m;
    pthread_cond_t c;
    struct {
      uint64_t key;
      uint32_t bytes;
    } q[256];
    int head, tail, count;
  } AksPosixIocp;
#endif
  typedef struct {
    volatile uint64_t head;
    volatile uint64_t tail;
    uint64_t capacity;
  } ShmRingHdr;


int ExecuteStdlibOsNet(VM *vm, VMTask *task, const char *name, int argc,
            AKSValue **stacktopptr) {
  AKSValue *stacktop = *stacktopptr;
  AKSValue *stacklimit = task->stack + (task->stackcap > 0 ? task->stackcap : MAXSTACK);
#define PUSH(val) AKS_STDLIB_PUSH(val)
#define POP() AKS_STDLIB_POP()

  if (strcmp(name, "MMapOpen") == 0) {
    if (argc != 3) {
      fprintf(
          stderr,
          "Runtime error: MMapOpen() expects 3 arguments (path, mode, size)\n");
      return -1;
    }
    AKSValue size_val = POP();
    AKSValue mode_val = POP();
    AKSValue path_val = POP();

    int64_t size = ValToInt(size_val);
    if (size <= 0)
      size = 4096;

    void *pBuf = NULL;
    int is_anon = 1;
#ifdef _WIN32
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap = NULL;
    if (ValKind(path_val) == VALSTR && ValToStrPtr(path_val)->len > 0) {
      const char *path = ValToStrPtr(path_val)->data;
      is_anon = 0;
      hFile = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        hMap =
            CreateFileMappingA(hFile, NULL, PAGE_READWRITE, (DWORD)(size >> 32),
                               (DWORD)(size & 0xFFFFFFFF), NULL);
        if (hMap) {
          pBuf = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)size);
        }
      }
    } else {
      hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                (DWORD)(size >> 32), (DWORD)(size & 0xFFFFFFFF),
                                NULL);
      if (hMap) {
        pBuf = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)size);
      }
    }
#else
    int fd = -1;
    if (ValKind(path_val) == VALSTR && ValToStrPtr(path_val)->len > 0) {
      const char *path = ValToStrPtr(path_val)->data;
      is_anon = 0;
      fd = AksPosixOpen(path, O_RDWR | O_CREAT, 0666);
      if (fd >= 0 && ftruncate(fd, size) != 0) {
        /* best-effort sizing */
      }
    }
    pBuf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                (fd == -1 ? (MAP_ANONYMOUS | MAP_SHARED) : MAP_SHARED), fd, 0);
    if (pBuf == MAP_FAILED)
      pBuf = NULL;
#endif
    ValFree(&path_val);
    ValFree(&mode_val);
    ValFree(&size_val);

    if (!pBuf) {
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }

    AKSValue handle = ValStruct("MMapHandle");
    AKSStruct *s = ValToObj(handle);
    s->fieldcount = 5;
    s->fieldnames = malloc(5 * sizeof(char *));
    s->fieldvals = malloc(5 * sizeof(AKSValue));
    s->fieldnames[0] = strdup("ptr");
    s->fieldvals[0] = ValInt((int64_t)(uintptr_t)pBuf);
    s->fieldnames[1] = strdup("handle");
#ifdef _WIN32
    s->fieldvals[1] = ValInt((int64_t)(uintptr_t)hMap);
#else
    s->fieldvals[1] = ValInt((int64_t)fd);
#endif
    s->fieldnames[2] = strdup("file_handle");
#ifdef _WIN32
    s->fieldvals[2] = ValInt((int64_t)(uintptr_t)hFile);
#else
    s->fieldvals[2] = ValInt((int64_t)fd);
#endif
    s->fieldnames[3] = strdup("size");
    s->fieldvals[3] = ValInt(size);
    s->fieldnames[4] = strdup("is_anon");
    s->fieldvals[4] = ValBool(is_anon);

    PUSH(handle);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MMapRead") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: MMapRead() expects 3 arguments (handle, "
                      "offset, length)\n");
      return -1;
    }
    AKSValue len_val = POP();
    AKSValue off_val = POP();
    AKSValue h_val = POP();
    int64_t offset = ValToInt(off_val);
    int64_t length = ValToInt(len_val);

    void *pBuf = NULL;
    int64_t max_size = 0;
    if (ValKind(h_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(h_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0)
          pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]);
        if (strcmp(s->fieldnames[i], "size") == 0)
          max_size = ValToInt(s->fieldvals[i]);
      }
    }

    if (!pBuf || offset < 0 || offset >= max_size || length <= 0) {
      ValFree(&h_val);
      ValFree(&off_val);
      ValFree(&len_val);
      PUSH(ValStr(""));
      *stacktopptr = stacktop;
      return 1;
    }

    if (offset + length > max_size)
      length = max_size - offset;
    char *data_ptr = (char *)pBuf + offset;
    AKSValue str_res = ValStrLen(data_ptr, (int)length);

    ValFree(&h_val);
    ValFree(&off_val);
    ValFree(&len_val);
    PUSH(str_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MMapWrite") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: MMapWrite() expects 3 arguments (handle, "
                      "offset, data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    AKSValue off_val = POP();
    AKSValue h_val = POP();
    int64_t offset = ValToInt(off_val);

    void *pBuf = NULL;
    int64_t max_size = 0;
    if (ValKind(h_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(h_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0)
          pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]);
        if (strcmp(s->fieldnames[i], "size") == 0)
          max_size = ValToInt(s->fieldvals[i]);
      }
    }

    int written = 0;
    if (pBuf && ValKind(data_val) == VALSTR && offset >= 0 &&
        offset < max_size) {
      const char *data = ValToStrPtr(data_val)->data;
      int data_len = ValToStrPtr(data_val)->len;
      if (offset + data_len > max_size)
        data_len = (int)(max_size - offset);
      memcpy((char *)pBuf + offset, data, data_len);
      written = data_len;
    }

    ValFree(&h_val);
    ValFree(&off_val);
    ValFree(&data_val);
    PUSH(ValInt(written));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MMapReadByte") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MMapReadByte() expects 2 arguments "
                      "(handle, offset)\n");
      return -1;
    }
    AKSValue off_val = POP();
    AKSValue h_val = POP();
    int64_t offset = ValToInt(off_val);
    void *pBuf = NULL;
    int64_t max_size = 0;
    if (ValKind(h_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(h_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0)
          pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]);
        if (strcmp(s->fieldnames[i], "size") == 0)
          max_size = ValToInt(s->fieldvals[i]);
      }
    }
    int byte_val = 0;
    if (pBuf && offset >= 0 && offset < max_size) {
      byte_val = (int)((unsigned char *)pBuf)[offset];
    }
    ValFree(&h_val);
    ValFree(&off_val);
    PUSH(ValInt(byte_val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MMapWriteByte") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: MMapWriteByte() expects 3 arguments "
                      "(handle, offset, byte_val)\n");
      return -1;
    }
    AKSValue b_val = POP();
    AKSValue off_val = POP();
    AKSValue h_val = POP();
    int64_t offset = ValToInt(off_val);
    int64_t byte_val = ValToInt(b_val);
    void *pBuf = NULL;
    int64_t max_size = 0;
    if (ValKind(h_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(h_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0)
          pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]);
        if (strcmp(s->fieldnames[i], "size") == 0)
          max_size = ValToInt(s->fieldvals[i]);
      }
    }
    if (pBuf && offset >= 0 && offset < max_size) {
      ((unsigned char *)pBuf)[offset] = (unsigned char)byte_val;
    }
    ValFree(&h_val);
    ValFree(&off_val);
    ValFree(&b_val);
    PUSH(ValInt(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MMapFlush") == 0) {
    if (argc != 1 && argc != 3) {
      fprintf(stderr, "Runtime error: MMapFlush() expects 1 or 3 arguments\n");
      return -1;
    }
    int64_t length = 0;
    int64_t offset = 0;
    if (argc == 3) {
      AKSValue len_v = POP();
      AKSValue off_v = POP();
      length = ValToInt(len_v);
      offset = ValToInt(off_v);
      ValFree(&len_v);
      ValFree(&off_v);
    }
    AKSValue h_val = POP();
    void *pBuf = NULL;
    int64_t size = 0;
    if (ValKind(h_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(h_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0)
          pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]);
        if (strcmp(s->fieldnames[i], "size") == 0)
          size = ValToInt(s->fieldvals[i]);
      }
    }
    if (length == 0)
      length = size;
#ifdef _WIN32
    if (pBuf)
      FlushViewOfFile((char *)pBuf + offset, (SIZE_T)length);
#else
    if (pBuf)
      msync((char *)pBuf + offset, (size_t)length, MS_SYNC);
#endif
    ValFree(&h_val);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MMapClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: MMapClose() expects 1 argument\n");
      return -1;
    }
    AKSValue h_val = POP();
    if (ValKind(h_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(h_val);
      void *pBuf = NULL;
      int64_t size = 0;
#ifdef _WIN32
      HANDLE hMap = NULL, hFile = INVALID_HANDLE_VALUE;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0) {
          pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]);
          s->fieldvals[i] = ValInt(0);
        }
        if (strcmp(s->fieldnames[i], "handle") == 0) {
          hMap = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          s->fieldvals[i] = ValInt(0);
        }
        if (strcmp(s->fieldnames[i], "file_handle") == 0) {
          hFile = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          s->fieldvals[i] = ValInt(0);
        }
      }
      if (pBuf)
        UnmapViewOfFile(pBuf);
      if (hMap)
        CloseHandle(hMap);
      if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
#else
      int fd = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "ptr") == 0) {
          pBuf = (void *)(uintptr_t)ValToInt(s->fieldvals[i]);
          s->fieldvals[i] = ValInt(0);
        }
        if (strcmp(s->fieldnames[i], "size") == 0)
          size = ValToInt(s->fieldvals[i]);
        if (strcmp(s->fieldnames[i], "handle") == 0)
          fd = (int)ValToInt(s->fieldvals[i]);
      }
      if (pBuf)
        munmap(pBuf, size);
      if (fd >= 0)
        close(fd);
#endif
    }
    ValFree(&h_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomicNew") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: AtomicI64New() expects 1 argument\n");
      return -1;
    }
    AKSValue init_val = POP();
    int64_t *ptr = malloc(sizeof(int64_t));
    *ptr = ValToInt(init_val);
    ValFree(&init_val);
    PUSH(ValInt((int64_t)(uintptr_t)ptr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomicI64Free") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: AtomicI64Free() expects 1 argument\n");
      return -1;
    }
    AKSValue ptr_val = POP();
    void *ptr = (void *)(uintptr_t)ValToInt(ptr_val);
    if (ptr)
      free(ptr);
    ValFree(&ptr_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomicI64Load") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: AtomicI64Load() expects 1 argument\n");
      return -1;
    }
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t val = 0;
#ifdef _WIN32
    val = _InterlockedCompareExchange64((volatile LONG64 *)ptr, 0, 0);
#else
    val = __sync_add_and_fetch(ptr, 0);
#endif
    ValFree(&ptr_val);
    PUSH(ValInt(val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomicI64Store") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AtomicI64Store() expects 2 arguments\n");
      return -1;
    }
    AKSValue val_to_store = POP();
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t val = ValToInt(val_to_store);
#ifdef _WIN32
    _InterlockedExchange64((volatile LONG64 *)ptr, val);
#else
    __sync_lock_test_and_set(ptr, val);
#endif
    ValFree(&val_to_store);
    ValFree(&ptr_val);
    PUSH(ValInt(val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomicI64Add") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AtomicI64Add() expects 2 arguments\n");
      return -1;
    }
    AKSValue delta_val = POP();
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t delta = ValToInt(delta_val);
    int64_t old_v = 0;
#ifdef _WIN32
    old_v = _InterlockedExchangeAdd64((volatile LONG64 *)ptr, delta);
#else
    old_v = __sync_fetch_and_add(ptr, delta);
#endif
    ValFree(&delta_val);
    ValFree(&ptr_val);
    PUSH(ValInt(old_v + delta));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomicI64Sub") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: AtomicI64Sub() expects 2 arguments\n");
      return -1;
    }
    AKSValue delta_val = POP();
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t delta = ValToInt(delta_val);
    int64_t old_v = 0;
#ifdef _WIN32
    old_v = _InterlockedExchangeAdd64((volatile LONG64 *)ptr, -delta);
#else
    old_v = __sync_fetch_and_sub(ptr, delta);
#endif
    ValFree(&delta_val);
    ValFree(&ptr_val);
    PUSH(ValInt(old_v - delta));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomicI64Cas") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: AtomicI64Cas() expects 3 arguments (ptr, "
                      "expected, desired)\n");
      return -1;
    }
    AKSValue des_val = POP();
    AKSValue exp_val = POP();
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t expected = ValToInt(exp_val);
    int64_t desired = ValToInt(des_val);
    int success = 0;
#ifdef _WIN32
    int64_t prev = _InterlockedCompareExchange64((volatile LONG64 *)ptr,
                                                 desired, expected);
    success = (prev == expected);
#else
    success = __sync_bool_compare_and_swap(ptr, expected, desired);
#endif
    ValFree(&des_val);
    ValFree(&exp_val);
    ValFree(&ptr_val);
    PUSH(ValBool(success));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomicI64Exchange") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: AtomicI64Exchange() expects 2 arguments\n");
      return -1;
    }
    AKSValue new_val = POP();
    AKSValue ptr_val = POP();
    volatile int64_t *ptr = (volatile int64_t *)(uintptr_t)ValToInt(ptr_val);
    int64_t val = ValToInt(new_val);
    int64_t old_v = 0;
#ifdef _WIN32
    old_v = _InterlockedExchange64((volatile LONG64 *)ptr, val);
#else
    old_v = __sync_lock_test_and_set(ptr, val);
#endif
    ValFree(&new_val);
    ValFree(&ptr_val);
    PUSH(ValInt(old_v));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "AtomicFence") == 0) {
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessSpawn") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessSpawn() expects 1 argument\n");
      return -1;
    }
    AKSValue cmd_val = POP();
    if (ValKind(cmd_val) != VALSTR) {
      fprintf(stderr, "Runtime error: ProcessSpawn() expects string command\n");
      ValFree(&cmd_val);
      return -1;
    }
    const char *cmd = ValToStrPtr(cmd_val)->data;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hStdinRead = NULL, hStdinWrite = NULL;
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;

    CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0);
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0);
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0);
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdInput = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    char *cmd_buf = strdup(cmd);
    BOOL success = CreateProcessA(NULL, cmd_buf, NULL, NULL, TRUE, 0, NULL,
                                  NULL, &si, &pi);
    free(cmd_buf);

    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    if (!success) {
      CloseHandle(hStdinWrite);
      CloseHandle(hStdoutRead);
      CloseHandle(hStderrRead);
      ValFree(&cmd_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }

    AKSValue proc_val = ValStruct("ProcessHandle");
    AKSStruct *s = ValToObj(proc_val);
    s->fieldcount = 7;
    s->fieldnames = malloc(7 * sizeof(char *));
    s->fieldvals = malloc(7 * sizeof(AKSValue));
    s->fieldnames[0] = strdup("pid");
    s->fieldvals[0] = ValInt((int64_t)pi.dwProcessId);
    s->fieldnames[1] = strdup("h_proc");
    s->fieldvals[1] = ValInt((int64_t)(uintptr_t)pi.hProcess);
    s->fieldnames[2] = strdup("h_thread");
    s->fieldvals[2] = ValInt((int64_t)(uintptr_t)pi.hThread);
    s->fieldnames[3] = strdup("stdin_write");
    s->fieldvals[3] = ValInt((int64_t)(uintptr_t)hStdinWrite);
    s->fieldnames[4] = strdup("stdout_read");
    s->fieldvals[4] = ValInt((int64_t)(uintptr_t)hStdoutRead);
    s->fieldnames[5] = strdup("stderr_read");
    s->fieldvals[5] = ValInt((int64_t)(uintptr_t)hStderrRead);
    s->fieldnames[6] = strdup("is_alive");
    s->fieldvals[6] = ValBool(1);

    ValFree(&cmd_val);
    PUSH(proc_val);
    *stacktopptr = stacktop;
    return 1;
#else
    int in_pipe[2];
    int out_pipe[2];
    int err_pipe[2];
    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
      ValFree(&cmd_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    pid_t pid = fork();
    if (pid < 0) {
      close(in_pipe[0]);
      close(in_pipe[1]);
      close(out_pipe[0]);
      close(out_pipe[1]);
      close(err_pipe[0]);
      close(err_pipe[1]);
      ValFree(&cmd_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    if (pid == 0) {
      dup2(in_pipe[0], STDIN_FILENO);
      dup2(out_pipe[1], STDOUT_FILENO);
      dup2(err_pipe[1], STDERR_FILENO);

      close(in_pipe[0]);
      close(in_pipe[1]);
      close(out_pipe[0]);
      close(out_pipe[1]);
      close(err_pipe[0]);
      close(err_pipe[1]);

      execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
      _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    fcntl(out_pipe[0], F_SETFL, fcntl(out_pipe[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, fcntl(err_pipe[0], F_GETFL, 0) | O_NONBLOCK);

    AKSValue proc_val = ValStruct("ProcessHandle");
    AKSStruct *s = ValToObj(proc_val);
    s->fieldcount = 7;
    s->fieldnames = malloc(7 * sizeof(char *));
    s->fieldvals = malloc(7 * sizeof(AKSValue));
    s->fieldnames[0] = strdup("pid");
    s->fieldvals[0] = ValInt((int64_t)pid);
    s->fieldnames[1] = strdup("h_proc");
    s->fieldvals[1] = ValInt((int64_t)pid);
    s->fieldnames[2] = strdup("h_thread");
    s->fieldvals[2] = ValInt(0);
    s->fieldnames[3] = strdup("stdin_write");
    s->fieldvals[3] = ValInt((int64_t)in_pipe[1]);
    s->fieldnames[4] = strdup("stdout_read");
    s->fieldvals[4] = ValInt((int64_t)out_pipe[0]);
    s->fieldnames[5] = strdup("stderr_read");
    s->fieldvals[5] = ValInt((int64_t)err_pipe[0]);
    s->fieldnames[6] = strdup("is_alive");
    s->fieldvals[6] = ValBool(1);

    ValFree(&cmd_val);
    PUSH(proc_val);
    *stacktopptr = stacktop;
    return 1;
#endif
  }
  if (strcmp(name, "ProcessRunC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessRunC() expects 1 argument\n");
      return -1;
    }
    AKSValue cmd_val = POP();
    if (ValKind(cmd_val) != VALSTR) {
      ValFree(&cmd_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const char *cmd = ValToStrPtr(cmd_val)->data;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;
    CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0);
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0);
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    char *cmd_buf = strdup(cmd);
    BOOL success = CreateProcessA(NULL, cmd_buf, NULL, NULL, TRUE, 0, NULL,
                                  NULL, &si, &pi);
    free(cmd_buf);

    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    char *out_buf = (char *)malloc(1024);
    int out_len = 0, out_cap = 1024;
    out_buf[0] = '\0';

    char *err_buf = (char *)malloc(1024);
    int err_len = 0, err_cap = 1024;
    err_buf[0] = '\0';

    DWORD exit_code = 1;
    int is_ok = 0;

    if (success) {
      char temp[4096];
      DWORD bytesRead = 0;
      while (1) {
        int had_data = 0;
        DWORD avail = 0;
        if (PeekNamedPipe(hStdoutRead, NULL, 0, NULL, &avail, NULL) &&
            avail > 0) {
          DWORD to_read =
              (DWORD)(avail < sizeof(temp) - 1 ? avail : sizeof(temp) - 1);
          if (ReadFile(hStdoutRead, temp, to_read, &bytesRead, NULL) &&
              bytesRead > 0) {
            if (out_len + (int)bytesRead + 1 >= out_cap) {
              out_cap = (out_cap * 2) + bytesRead + 128;
              out_buf = (char *)realloc(out_buf, out_cap);
            }
            memcpy(out_buf + out_len, temp, bytesRead);
            out_len += (int)bytesRead;
            out_buf[out_len] = '\0';
            had_data = 1;
          }
        }
        if (PeekNamedPipe(hStderrRead, NULL, 0, NULL, &avail, NULL) &&
            avail > 0) {
          DWORD to_read =
              (DWORD)(avail < sizeof(temp) - 1 ? avail : sizeof(temp) - 1);
          if (ReadFile(hStderrRead, temp, to_read, &bytesRead, NULL) &&
              bytesRead > 0) {
            if (err_len + (int)bytesRead + 1 >= err_cap) {
              err_cap = (err_cap * 2) + bytesRead + 128;
              err_buf = (char *)realloc(err_buf, err_cap);
            }
            memcpy(err_buf + err_len, temp, bytesRead);
            err_len += (int)bytesRead;
            err_buf[err_len] = '\0';
            had_data = 1;
          }
        }
        if (!had_data) {
          DWORD wait_res = WaitForSingleObject(pi.hProcess, 10);
          if (wait_res == WAIT_OBJECT_0) {
            while (PeekNamedPipe(hStdoutRead, NULL, 0, NULL, &avail, NULL) &&
                   avail > 0) {
              DWORD to_read =
                  (DWORD)(avail < sizeof(temp) - 1 ? avail : sizeof(temp) - 1);
              if (ReadFile(hStdoutRead, temp, to_read, &bytesRead, NULL) &&
                  bytesRead > 0) {
                if (out_len + (int)bytesRead + 1 >= out_cap) {
                  out_cap = (out_cap * 2) + bytesRead + 128;
                  out_buf = (char *)realloc(out_buf, out_cap);
                }
                memcpy(out_buf + out_len, temp, bytesRead);
                out_len += (int)bytesRead;
                out_buf[out_len] = '\0';
              } else
                break;
            }
            while (PeekNamedPipe(hStderrRead, NULL, 0, NULL, &avail, NULL) &&
                   avail > 0) {
              DWORD to_read =
                  (DWORD)(avail < sizeof(temp) - 1 ? avail : sizeof(temp) - 1);
              if (ReadFile(hStderrRead, temp, to_read, &bytesRead, NULL) &&
                  bytesRead > 0) {
                if (err_len + (int)bytesRead + 1 >= err_cap) {
                  err_cap = (err_cap * 2) + bytesRead + 128;
                  err_buf = (char *)realloc(err_buf, err_cap);
                }
                memcpy(err_buf + err_len, temp, bytesRead);
                err_len += (int)bytesRead;
                err_buf[err_len] = '\0';
              } else
                break;
            }
            break;
          }
        }
      }
      WaitForSingleObject(pi.hProcess, INFINITE);
      GetExitCodeProcess(pi.hProcess, &exit_code);
      is_ok = (exit_code == 0) ? 1 : 0;
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    } else {
      exit_code = 1;
      is_ok = 0;
      const char *fail_msg = "Failed to launch process";
      size_t fmlen = strlen(fail_msg);
      if (err_len + (int)fmlen + 1 >= err_cap) {
        err_cap = (int)fmlen + 64;
        err_buf = (char *)realloc(err_buf, err_cap);
      }
      memcpy(err_buf, fail_msg, fmlen + 1);
      err_len = (int)fmlen;
    }
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
#else
    int out_pipe[2];
    int err_pipe[2];
    if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
      ValFree(&cmd_val);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    pid_t pid = fork();
    char *out_buf = (char *)malloc(1024);
    int out_len = 0, out_cap = 1024;
    out_buf[0] = '\0';
    char *err_buf = (char *)malloc(1024);
    int err_len = 0, err_cap = 1024;
    err_buf[0] = '\0';
    int exit_code = 1;
    int is_ok = 0;

    if (pid == 0) {
      dup2(out_pipe[1], STDOUT_FILENO);
      dup2(err_pipe[1], STDERR_FILENO);
      close(out_pipe[0]);
      close(out_pipe[1]);
      close(err_pipe[0]);
      close(err_pipe[1]);
      execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
      _exit(127);
    } else if (pid > 0) {
      close(out_pipe[1]);
      close(err_pipe[1]);
      char temp[4096];
      ssize_t n;
      while ((n = read(out_pipe[0], temp, sizeof(temp) - 1)) > 0) {
        if (out_len + (int)n + 1 >= out_cap) {
          out_cap = (out_cap * 2) + (int)n + 128;
          out_buf = (char *)realloc(out_buf, out_cap);
        }
        memcpy(out_buf + out_len, temp, n);
        out_len += (int)n;
        out_buf[out_len] = '\0';
      }
      while ((n = read(err_pipe[0], temp, sizeof(temp) - 1)) > 0) {
        if (err_len + (int)n + 1 >= err_cap) {
          err_cap = (err_cap * 2) + (int)n + 128;
          err_buf = (char *)realloc(err_buf, err_cap);
        }
        memcpy(err_buf + err_len, temp, n);
        err_len += (int)n;
        err_buf[err_len] = '\0';
      }
      close(out_pipe[0]);
      close(err_pipe[0]);
      int status = 0;
      waitpid(pid, &status, 0);
      if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
        is_ok = (exit_code == 0) ? 1 : 0;
      }
    }
#endif
    AKSValue res = ValStruct("ProcessResult");
    AKSStruct *rs = ValToObj(res);
    rs->fieldcount = 4;
    rs->fieldnames = malloc(4 * sizeof(char *));
    rs->fieldvals = malloc(4 * sizeof(AKSValue));
    rs->fieldnames[0] = strdup("stdout");
    rs->fieldvals[0] = ValStrLen(out_buf, out_len);
    rs->fieldnames[1] = strdup("stderr");
    rs->fieldvals[1] = ValStrLen(err_buf, err_len);
    rs->fieldnames[2] = strdup("exit_code");
    rs->fieldvals[2] = ValInt((int64_t)exit_code);
    rs->fieldnames[3] = strdup("ok");
    rs->fieldvals[3] = ValBool(is_ok);

    free(out_buf);
    free(err_buf);
    ValFree(&cmd_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessWriteStdin") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ProcessWriteStdin() expects 2 arguments\n");
      return -1;
    }
    AKSValue data_val = POP();
    AKSValue proc_val = POP();
    DWORD written = 0;
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT && ValKind(data_val) == VALSTR) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hWrite = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "stdin_write") == 0) {
          hWrite = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hWrite) {
        const char *data = ValToStrPtr(data_val)->data;
        DWORD len = (DWORD)ValToStrPtr(data_val)->len;
        WriteFile(hWrite, data, len, &written, NULL);
      }
    }
#else
    if (ValKind(proc_val) == VALSTRUCT && ValKind(data_val) == VALSTR) {
      AKSStruct *s = ValToObj(proc_val);
      int fd = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "stdin_write") == 0) {
          fd = (int)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (fd >= 0) {
        const char *data = ValToStrPtr(data_val)->data;
        int len = ValToStrPtr(data_val)->len;
        ssize_t n = write(fd, data, len);
        if (n > 0)
          written = (int64_t)n;
      }
    }
#endif
    ValFree(&data_val);
    ValFree(&proc_val);
    PUSH(ValInt((int64_t)written));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessReadStdout") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ProcessReadStdout() expects 2 arguments\n");
      return -1;
    }
    AKSValue max_val = POP();
    AKSValue proc_val = POP();
    int64_t max_bytes = ValToInt(max_val);
    if (max_bytes <= 0)
      max_bytes = 4096;
    if (max_bytes > 1048576)
      max_bytes = 1048576;

    char *buf = malloc((size_t)max_bytes + 1);
    DWORD bytes_read = 0;
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hRead = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "stdout_read") == 0) {
          hRead = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hRead) {
        DWORD avail = 0;
        if (PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
          DWORD to_read = (DWORD)max_bytes;
          if (to_read > avail)
            to_read = avail;
          ReadFile(hRead, buf, to_read, &bytes_read, NULL);
        }
      }
    }
#else
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      int fd = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "stdout_read") == 0) {
          fd = (int)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (fd >= 0) {
        ssize_t n = read(fd, buf, (size_t)max_bytes);
        if (n > 0)
          bytes_read = (int)n;
      }
    }
#endif
    buf[bytes_read] = '\0';
    AKSValue str_res = ValStrLen(buf, (int)bytes_read);
    free(buf);
    ValFree(&max_val);
    ValFree(&proc_val);
    PUSH(str_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessReadStderr") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ProcessReadStderr() expects 2 arguments\n");
      return -1;
    }
    AKSValue max_val = POP();
    AKSValue proc_val = POP();
    int64_t max_bytes = ValToInt(max_val);
    if (max_bytes <= 0)
      max_bytes = 4096;
    if (max_bytes > 1048576)
      max_bytes = 1048576;

    char *buf = malloc((size_t)max_bytes + 1);
    DWORD bytes_read = 0;
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hRead = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "stderr_read") == 0) {
          hRead = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hRead) {
        DWORD avail = 0;
        if (PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
          DWORD to_read = (DWORD)max_bytes;
          if (to_read > avail)
            to_read = avail;
          ReadFile(hRead, buf, to_read, &bytes_read, NULL);
        }
      }
    }
#else
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      int fd = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "stderr_read") == 0) {
          fd = (int)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (fd >= 0) {
        ssize_t n = read(fd, buf, (size_t)max_bytes);
        if (n > 0)
          bytes_read = (int)n;
      }
    }
#endif
    buf[bytes_read] = '\0';
    AKSValue str_res = ValStrLen(buf, (int)bytes_read);
    free(buf);
    ValFree(&max_val);
    ValFree(&proc_val);
    PUSH(str_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessWait") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ProcessWait() expects 2 arguments\n");
      return -1;
    }
    AKSValue timeout_val = POP();
    AKSValue proc_val = POP();
    int64_t timeout_ms = ValToInt(timeout_val);
    int ok = 0;
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hProc = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "h_proc") == 0) {
          hProc = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hProc) {
        DWORD res = WaitForSingleObject(
            hProc, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
        ok = (res == WAIT_OBJECT_0);
      }
    }
#else
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      pid_t pid = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "pid") == 0) {
          pid = (pid_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (pid > 0) {
        int status;
        if (timeout_ms < 0) {
          ok = (waitpid(pid, &status, 0) == pid);
        } else {
          int elapsed = 0;
          while (elapsed <= timeout_ms) {
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) {
              ok = 1;
              break;
            }
            if (r < 0)
              break;
            usleep(10000);
            elapsed += 10;
          }
        }
      }
    }
#endif
    ValFree(&timeout_val);
    ValFree(&proc_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessKill") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessKill() expects 1 argument\n");
      return -1;
    }
    AKSValue proc_val = POP();
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hProc = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "h_proc") == 0) {
          hProc = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hProc) {
        TerminateProcess(hProc, 1);
      }
    }
#else
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      pid_t pid = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "pid") == 0) {
          pid = (pid_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (pid > 0) {
        kill(pid, SIGTERM);
      }
    }
#endif
    ValFree(&proc_val);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessExitCode") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessExitCode() expects 1 argument\n");
      return -1;
    }
    AKSValue proc_val = POP();
    int64_t code = -1;
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      HANDLE hProc = NULL;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "h_proc") == 0) {
          hProc = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (hProc) {
        DWORD dwCode = 0;
        if (GetExitCodeProcess(hProc, &dwCode)) {
          code = (int64_t)dwCode;
        }
      }
    }
#else
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      pid_t pid = -1;
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "pid") == 0) {
          pid = (pid_t)ValToInt(s->fieldvals[i]);
          break;
        }
      }
      if (pid > 0) {
        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid && WIFEXITED(status)) {
          code = (int64_t)WEXITSTATUS(status);
        }
      }
    }
#endif
    ValFree(&proc_val);
    PUSH(ValInt(code));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessClose() expects 1 argument\n");
      return -1;
    }
    AKSValue proc_val = POP();
#ifdef _WIN32
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "h_proc") == 0 ||
            strcmp(s->fieldnames[i], "h_thread") == 0 ||
            strcmp(s->fieldnames[i], "stdin_write") == 0 ||
            strcmp(s->fieldnames[i], "stdout_read") == 0 ||
            strcmp(s->fieldnames[i], "stderr_read") == 0) {
          HANDLE h = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[i]);
          if (h && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            s->fieldvals[i] = ValInt(0);
          }
        }
      }
    }
#else
    if (ValKind(proc_val) == VALSTRUCT) {
      AKSStruct *s = ValToObj(proc_val);
      for (int i = 0; i < s->fieldcount; i++) {
        if (strcmp(s->fieldnames[i], "stdin_write") == 0 ||
            strcmp(s->fieldnames[i], "stdout_read") == 0 ||
            strcmp(s->fieldnames[i], "stderr_read") == 0) {
          int fd = (int)ValToInt(s->fieldvals[i]);
          if (fd >= 0) {
            close(fd);
            s->fieldvals[i] = ValInt(-1);
          }
        }
      }
    }
#endif
    ValFree(&proc_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemCpuCount") == 0) {
    int count = 1;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    count = (int)si.dwNumberOfProcessors;
#else
    count = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (count <= 0)
      count = 1;
    PUSH(ValInt(count));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemMemoryTotal") == 0) {
    int64_t total = 0;
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
      total = (int64_t)ms.ullTotalPhys;
    }
#endif
    PUSH(ValInt(total));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemMemoryAvailable") == 0) {
    int64_t avail = 0;
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
      avail = (int64_t)ms.ullAvailPhys;
    }
#endif
    PUSH(ValInt(avail));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemPid") == 0) {
    int64_t pid = 0;
#ifdef _WIN32
    pid = (int64_t)GetCurrentProcessId();
#else
    pid = (int64_t)getpid();
#endif
    PUSH(ValInt(pid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NativeLibraryLoad") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: NativeLibraryLoad() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int64_t handle = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      HMODULE h = LoadLibraryA(path);
      handle = (int64_t)(uintptr_t)h;
#else
      void *h = dlopen(path, RTLD_LAZY);
      handle = (int64_t)(uintptr_t)h;
#endif
    }
    ValFree(&path_val);
    PUSH(ValInt(handle));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NativeLibraryGet") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: NativeLibraryGet() expects 2 arguments "
                      "(lib_handle, symbol)\n");
      return -1;
    }
    AKSValue sym_val = POP();
    AKSValue lib_val = POP();
    int64_t proc = 0;
    if (ValKind(sym_val) == VALSTR) {
      const char *sym = ValToStrPtr(sym_val)->data;
      intptr_t lib = (intptr_t)ValToInt(lib_val);
#ifdef _WIN32
      FARPROC p = GetProcAddress((HMODULE)lib, sym);
      proc = (int64_t)(uintptr_t)p;
#else
      void *p = dlsym((void *)lib, sym);
      proc = (int64_t)(uintptr_t)p;
#endif
    }
    ValFree(&sym_val);
    ValFree(&lib_val);
    PUSH(ValInt(proc));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NativeCallInt0") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: NativeCallInt0() expects 1 argument (fn_ptr)\n");
      return -1;
    }
    AKSValue fn_val = POP();
    typedef int64_t (*Fn0)();
    Fn0 fn = (Fn0)(uintptr_t)ValToInt(fn_val);
    int64_t res = 0;
    if (fn) {
      res = fn();
    }
    ValFree(&fn_val);
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NativeCallInt1") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: NativeCallInt1() expects 2 arguments "
                      "(fn_ptr, arg1)\n");
      return -1;
    }
    AKSValue a1_val = POP();
    AKSValue fn_val = POP();
    typedef int64_t (*Fn1)(int64_t);
    Fn1 fn = (Fn1)(uintptr_t)ValToInt(fn_val);
    int64_t res = 0;
    if (fn) {
      res = fn(ValToInt(a1_val));
    }
    ValFree(&a1_val);
    ValFree(&fn_val);
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NativeCallInt2") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: NativeCallInt2() expects 3 arguments "
                      "(fn_ptr, arg1, arg2)\n");
      return -1;
    }
    AKSValue a2_val = POP();
    AKSValue a1_val = POP();
    AKSValue fn_val = POP();
    typedef int64_t (*Fn2)(int64_t, int64_t);
    Fn2 fn = (Fn2)(uintptr_t)ValToInt(fn_val);
    int64_t res = 0;
    if (fn) {
      res = fn(ValToInt(a1_val), ValToInt(a2_val));
    }
    ValFree(&a2_val);
    ValFree(&a1_val);
    ValFree(&fn_val);
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NativeLibraryFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: NativeLibraryFree() expects 1 argument "
                      "(lib_handle)\n");
      return -1;
    }
    AKSValue lib_val = POP();
    intptr_t lib = (intptr_t)ValToInt(lib_val);
#ifdef _WIN32
    if (lib)
      FreeLibrary((HMODULE)lib);
#else
    if (lib)
      dlclose((void *)lib);
#endif
    ValFree(&lib_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TimerNowNanos") == 0) {
    int64_t nanos = 0;
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    if (freq.QuadPart > 0) {
      nanos = (int64_t)(((double)cnt.QuadPart / (double)freq.QuadPart) *
                        1000000000.0);
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    nanos = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
    PUSH(ValInt(nanos));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TimerNowMicros") == 0) {
    int64_t micros = 0;
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    if (freq.QuadPart > 0) {
      micros =
          (int64_t)(((double)cnt.QuadPart / (double)freq.QuadPart) * 1000000.0);
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    micros = (int64_t)ts.tv_sec * 1000000LL + (ts.tv_nsec / 1000);
#endif
    PUSH(ValInt(micros));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TimerNowMillis") == 0) {
    int64_t millis = 0;
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    if (freq.QuadPart > 0) {
      millis =
          (int64_t)(((double)cnt.QuadPart / (double)freq.QuadPart) * 1000.0);
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    millis = (int64_t)ts.tv_sec * 1000LL + (ts.tv_nsec / 1000000);
#endif
    PUSH(ValInt(millis));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketListenSync") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SocketListenSync() expects 1 argument (port)\n");
      return -1;
    }
    AKSValue port_val = POP();
    int port = (int)ValToInt(port_val);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
      int opt = 1;
      setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_addr.s_addr = INADDR_ANY;
      sin.sin_port = htons((u_short)port);
      if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        listen(s, 16);
      } else {
#ifdef _WIN32
        closesocket(s);
#else
        close((int)s);
#endif
        s = INVALID_SOCKET;
      }
    }
    ValFree(&port_val);
    PUSH(ValInt((int64_t)s));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WinmmAudioPlayC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: WinmmAudioPlayC() expects 2 args "
                      "(pcm_bytes, sample_rate)\n");
      return -1;
    }
    AKSValue rate_v = POP(), pcm_v = POP();
    int play_ok = 1;
#ifdef _WIN32
    if (ValKind(pcm_v) == VALSTR) {
      WAVEFORMATEX wfx = {0};
      wfx.wFormatTag = WAVE_FORMAT_PCM;
      wfx.nChannels = 1;
      wfx.nSamplesPerSec = (DWORD)ValToInt(rate_v);
      if (wfx.nSamplesPerSec == 0)
        wfx.nSamplesPerSec = 44100;
      wfx.wBitsPerSample = 16;
      wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
      wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
      HWAVEOUT hWaveOut = NULL;
      if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) ==
          MMSYSERR_NOERROR) {
        WAVEHDR header = {0};
        header.lpData = (LPSTR)ValToStrPtr(pcm_v)->data;
        header.dwBufferLength = (DWORD)ValToStrPtr(pcm_v)->len;
        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutReset(hWaveOut);
        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);
      }
    }
#endif
    ValFree(&rate_v);
    ValFree(&pcm_v);
    PUSH(ValBool(play_ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketAcceptSync") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SocketAcceptSync() expects 1 argument "
                      "(server_sock)\n");
      return -1;
    }
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    SOCKET client = accept(s, NULL, NULL);
    ValFree(&s_val);
    PUSH(ValInt((int64_t)client));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketConnectSync") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SocketConnectSync() expects 2 arguments "
                      "(host, port)\n");
      return -1;
    }
    AKSValue port_val = POP();
    AKSValue host_val = POP();
    int port = (int)ValToInt(port_val);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET && ValKind(host_val) == VALSTR) {
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = htons((u_short)port);
      sin.sin_addr.s_addr = inet_addr(ValToStrPtr(host_val)->data);
      if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close((int)s);
#endif
        s = INVALID_SOCKET;
      }
    }
    ValFree(&host_val);
    ValFree(&port_val);
    PUSH(ValInt((int64_t)s));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketSendSync") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: SocketSendSync() expects 2 arguments (sock, data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    int sent = 0;
    if (s != INVALID_SOCKET && ValKind(data_val) == VALSTR) {
      const char *data = ValToStrPtr(data_val)->data;
      int len = ValToStrPtr(data_val)->len;
      sent = send(s, data, len, 0);
    }
    ValFree(&data_val);
    ValFree(&s_val);
    PUSH(ValInt(sent));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketRecvSync") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SocketRecvSync() expects 2 arguments "
                      "(sock, max_bytes)\n");
      return -1;
    }
    AKSValue max_val = POP();
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    int max_bytes = (int)ValToInt(max_val);
    if (max_bytes <= 0)
      max_bytes = 4096;
    if (max_bytes > 1048576)
      max_bytes = 1048576;

    char *buf = malloc((size_t)max_bytes + 1);
    int recvd = 0;
    if (s != INVALID_SOCKET) {
      recvd = recv(s, buf, max_bytes, 0);
      if (recvd < 0)
        recvd = 0;
    }
    buf[recvd] = '\0';
    AKSValue str_res = ValStrLen(buf, recvd);
    free(buf);
    ValFree(&max_val);
    ValFree(&s_val);
    PUSH(str_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketClose") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SocketClose() expects 1 argument (sock)\n");
      return -1;
    }
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    if (s != INVALID_SOCKET) {
#ifdef _WIN32
      closesocket(s);
#else
      close((int)s);
#endif
    }
    ValFree(&s_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileWatcherNew") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: FileWatcherNew() expects 1 argument (dir_path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int64_t handle = 0;
    if (ValKind(path_val) == VALSTR) {
#ifdef _WIN32
      const char *path = ValToStrPtr(path_val)->data;
      HANDLE hChange = FindFirstChangeNotificationA(
          path, FALSE,
          FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
              FILE_NOTIFY_CHANGE_SIZE);
      handle = (int64_t)(uintptr_t)hChange;
#else
      handle = 1;
#endif
    }
    ValFree(&path_val);
    PUSH(ValInt(handle));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileWatcherPoll") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FileWatcherPoll() expects 2 arguments "
                      "(handle, timeout_ms)\n");
      return -1;
    }
    AKSValue to_val = POP();
    AKSValue h_val = POP();
    int changed = 0;
    int timeout_ms = (int)ValToInt(to_val);
    if (timeout_ms < 0)
      timeout_ms = 0;
#ifdef _WIN32
    HANDLE hChange = (HANDLE)(uintptr_t)ValToInt(h_val);
    if (hChange != INVALID_HANDLE_VALUE && hChange != NULL) {
      DWORD waitRes = WaitForSingleObject(hChange, (DWORD)timeout_ms);
      if (waitRes == WAIT_OBJECT_0) {
        FindNextChangeNotification(hChange);
        changed = 1;
      }
    }
#else
    changed = 1;
#endif
    ValFree(&to_val);
    ValFree(&h_val);
    PUSH(ValBool(changed));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileWatcherClose") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: FileWatcherClose() expects 1 argument (handle)\n");
      return -1;
    }
    AKSValue h_val = POP();
#ifdef _WIN32
    HANDLE hChange = (HANDLE)(uintptr_t)ValToInt(h_val);
    if (hChange != INVALID_HANDLE_VALUE && hChange != NULL) {
      FindCloseChangeNotification(hChange);
    }
#endif
    ValFree(&h_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UdpSocketBind") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: UdpSocketBind() expects 1 argument (port)\n");
      return -1;
    }
    AKSValue port_val = POP();
    int port = (int)ValToInt(port_val);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
      int opt = 1;
      setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
      struct sockaddr_in sin;
      memset(&sin, 0, sizeof(sin));
      sin.sin_family = AF_INET;
      sin.sin_addr.s_addr = INADDR_ANY;
      sin.sin_port = htons((u_short)port);
      if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close((int)s);
#endif
        s = INVALID_SOCKET;
      }
    }
    ValFree(&port_val);
    PUSH(ValInt((int64_t)s));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UdpSocketNew") == 0) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    PUSH(ValInt((int64_t)s));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UdpSocketSendTo") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: UdpSocketSendTo() expects 4 arguments "
                      "(sock, host, port, data)\n");
      return -1;
    }
    AKSValue data_val = POP();
    AKSValue port_val = POP();
    AKSValue host_val = POP();
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    int sent = 0;
    if (s != INVALID_SOCKET && ValKind(host_val) == VALSTR &&
        ValKind(data_val) == VALSTR) {
      struct sockaddr_in dest;
      memset(&dest, 0, sizeof(dest));
      dest.sin_family = AF_INET;
      dest.sin_port = htons((u_short)ValToInt(port_val));
      dest.sin_addr.s_addr = inet_addr(ValToStrPtr(host_val)->data);
      sent = sendto(s, ValToStrPtr(data_val)->data, ValToStrPtr(data_val)->len,
                    0, (struct sockaddr *)&dest, sizeof(dest));
    }
    ValFree(&data_val);
    ValFree(&port_val);
    ValFree(&host_val);
    ValFree(&s_val);
    PUSH(ValInt(sent));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UdpSocketRecvFrom") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: UdpSocketRecvFrom() expects 2 arguments "
                      "(sock, max_bytes)\n");
      return -1;
    }
    AKSValue max_val = POP();
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    int max_bytes = (int)ValToInt(max_val);
    if (max_bytes <= 0)
      max_bytes = 4096;
    if (max_bytes > 65536)
      max_bytes = 65536;
    char *buf = (char *)malloc((size_t)max_bytes + 1);
    struct sockaddr_in from;
#ifdef _WIN32
    int fromlen = sizeof(from);
#else
    socklen_t fromlen = sizeof(from);
#endif
    int recvd = 0;
    if (s != INVALID_SOCKET) {
      recvd =
          recvfrom(s, buf, max_bytes, 0, (struct sockaddr *)&from, &fromlen);
      if (recvd < 0)
        recvd = 0;
    }
    buf[recvd] = '\0';
    AKSValue str_res = ValStrLen(buf, recvd);
    free(buf);
    ValFree(&max_val);
    ValFree(&s_val);
    PUSH(str_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UdpSocketClose") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: UdpSocketClose() expects 1 argument (sock)\n");
      return -1;
    }
    AKSValue s_val = POP();
    SOCKET s = (SOCKET)ValToInt(s_val);
    if (s != INVALID_SOCKET) {
#ifdef _WIN32
      closesocket(s);
#else
      close((int)s);
#endif
    }
    ValFree(&s_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadLocalNew") == 0) {
    int64_t slot = 0;
#ifdef _WIN32
    DWORD tlsIdx = TlsAlloc();
    if (tlsIdx != TLS_OUT_OF_INDEXES) {
      slot = (int64_t)tlsIdx;
    }
#endif
    PUSH(ValInt(slot));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadLocalSet") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: ThreadLocalSet() expects 2 arguments (slot, val)\n");
      return -1;
    }
    AKSValue val_obj = POP();
    AKSValue slot_val = POP();
    int ok = 0;
#ifdef _WIN32
    DWORD slot = (DWORD)ValToInt(slot_val);
    int64_t v = ValToInt(val_obj);
    if (TlsSetValue(slot, (LPVOID)(uintptr_t)v)) {
      ok = 1;
    }
#endif
    ValFree(&val_obj);
    ValFree(&slot_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadLocalGet") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ThreadLocalGet() expects 1 argument (slot)\n");
      return -1;
    }
    AKSValue slot_val = POP();
    int64_t v = 0;
#ifdef _WIN32
    DWORD slot = (DWORD)ValToInt(slot_val);
    v = (int64_t)(uintptr_t)TlsGetValue(slot);
#endif
    ValFree(&slot_val);
    PUSH(ValInt(v));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadLocalFree") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ThreadLocalFree() expects 1 argument (slot)\n");
      return -1;
    }
    AKSValue slot_val = POP();
#ifdef _WIN32
    DWORD slot = (DWORD)ValToInt(slot_val);
    TlsFree(slot);
#endif
    ValFree(&slot_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "IpcQueueCreate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: IpcQueueCreate() expects 2 arguments "
                      "(name, capacity)\n");
      return -1;
    }
    AKSValue cap_val = POP();
    AKSValue name_val = POP();
    int cap = (int)ValToInt(cap_val);
    if (cap < 4)
      cap = 64;
    typedef struct {
      volatile uint32_t head;
      volatile uint32_t tail;
      uint32_t capacity;
      uint32_t entrySize;
    } AKSIpcHeader;
    typedef struct {
      void *hMap;
      void *pView;
      int isOwner;
      size_t totalSize;
    } AKSIpcQueue;

    AKSIpcQueue *q = (AKSIpcQueue *)malloc(sizeof(AKSIpcQueue));
    q->isOwner = 1;
    q->totalSize = sizeof(AKSIpcHeader) + (size_t)cap * 256;
    q->hMap = NULL;
    q->pView = NULL;

    if (ValKind(name_val) == VALSTR) {
#ifdef _WIN32
      const char *qname = ValToStrPtr(name_val)->data;
      q->hMap =
          (void *)CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                     0, (DWORD)q->totalSize, qname);
      if (q->hMap) {
        q->pView = MapViewOfFile((HANDLE)q->hMap, FILE_MAP_ALL_ACCESS, 0, 0,
                                 q->totalSize);
        if (q->pView) {
          AKSIpcHeader *hdr = (AKSIpcHeader *)q->pView;
          hdr->head = 0;
          hdr->tail = 0;
          hdr->capacity = (uint32_t)cap;
          hdr->entrySize = 256;
        }
      }
#endif
    }
    ValFree(&cap_val);
    ValFree(&name_val);
    PUSH(ValInt((int64_t)(uintptr_t)q));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "IpcQueueOpen") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: IpcQueueOpen() expects 1 argument (name)\n");
      return -1;
    }
    AKSValue name_val = POP();
    typedef struct {
      void *hMap;
      void *pView;
      int isOwner;
      size_t totalSize;
    } AKSIpcQueue;

    AKSIpcQueue *q = (AKSIpcQueue *)malloc(sizeof(AKSIpcQueue));
    q->isOwner = 0;
    q->totalSize = 0;
    q->hMap = NULL;
    q->pView = NULL;

    if (ValKind(name_val) == VALSTR) {
#ifdef _WIN32
      const char *qname = ValToStrPtr(name_val)->data;
      q->hMap = (void *)OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, qname);
      if (q->hMap) {
        q->pView = MapViewOfFile((HANDLE)q->hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
      }
#endif
    }
    ValFree(&name_val);
    PUSH(ValInt((int64_t)(uintptr_t)q));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "IpcQueuePush") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: IpcQueuePush() expects 2 arguments (queue, msg)\n");
      return -1;
    }
    AKSValue msg_val = POP();
    AKSValue q_val = POP();
    typedef struct {
      volatile uint32_t head;
      volatile uint32_t tail;
      uint32_t capacity;
      uint32_t entrySize;
    } AKSIpcHeader;
    typedef struct {
      void *hMap;
      void *pView;
      int isOwner;
      size_t totalSize;
    } AKSIpcQueue;

    AKSIpcQueue *q = (AKSIpcQueue *)(uintptr_t)ValToInt(q_val);
    int ok = 0;
    if (q && q->pView && ValKind(msg_val) == VALSTR) {
      AKSIpcHeader *hdr = (AKSIpcHeader *)q->pView;
      uint32_t cap = hdr->capacity;
      if (hdr->tail - hdr->head < cap) {
        char *entryPtr =
            (char *)q->pView + sizeof(AKSIpcHeader) + (hdr->tail % cap) * 256;
        AKSString *str = ValToStrPtr(msg_val);
        int copyLen = str->len < 255 ? str->len : 255;
        memcpy(entryPtr, str->data, copyLen);
        entryPtr[copyLen] = '\0';
#ifdef _WIN32
        MemoryBarrier();
#else
        __sync_synchronize();
#endif
        hdr->tail++;
        ok = 1;
      }
    }
    ValFree(&msg_val);
    ValFree(&q_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "IpcQueuePop") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: IpcQueuePop() expects 1 argument (queue)\n");
      return -1;
    }
    AKSValue q_val = POP();
    typedef struct {
      volatile uint32_t head;
      volatile uint32_t tail;
      uint32_t capacity;
      uint32_t entrySize;
    } AKSIpcHeader;
    typedef struct {
      void *hMap;
      void *pView;
      int isOwner;
      size_t totalSize;
    } AKSIpcQueue;

    AKSIpcQueue *q = (AKSIpcQueue *)(uintptr_t)ValToInt(q_val);
    AKSValue res = ValNull();
    if (q && q->pView) {
      AKSIpcHeader *hdr = (AKSIpcHeader *)q->pView;
      if (hdr->head < hdr->tail) {
        char *entryPtr = (char *)q->pView + sizeof(AKSIpcHeader) +
                         (hdr->head % hdr->capacity) * 256;
        res = ValStr(entryPtr);
#ifdef _WIN32
        MemoryBarrier();
#else
        __sync_synchronize();
#endif
        hdr->head++;
      }
    }
    ValFree(&q_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "IpcQueueClose") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: IpcQueueClose() expects 1 argument (queue)\n");
      return -1;
    }
    AKSValue q_val = POP();
    typedef struct {
      void *hMap;
      void *pView;
      int isOwner;
      size_t totalSize;
    } AKSIpcQueue;

    AKSIpcQueue *q = (AKSIpcQueue *)(uintptr_t)ValToInt(q_val);
    if (q) {
#ifdef _WIN32
      if (q->pView)
        UnmapViewOfFile(q->pView);
      if (q->hMap)
        CloseHandle((HANDLE)q->hMap);
#endif
      free(q);
    }
    ValFree(&q_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EnvGet") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: EnvGet() expects 1 argument (key)\n");
      return -1;
    }
    AKSValue key_val = POP();
    AKSValue res = ValNull();
    if (ValKind(key_val) == VALSTR) {
      const char *key = ValToStrPtr(key_val)->data;
#ifdef _WIN32
      char buf[4096];
      DWORD len = GetEnvironmentVariableA(key, buf, sizeof(buf));
      if (len > 0 && len < sizeof(buf)) {
        res = ValStrLen(buf, (int)len);
      }
#else
      const char *val = getenv(key);
      if (val)
        res = ValStr(val);
#endif
    }
    ValFree(&key_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EnvSet") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: EnvSet() expects 2 arguments (key, val)\n");
      return -1;
    }
    AKSValue val_obj = POP();
    AKSValue key_val = POP();
    int ok = 0;
    if (ValKind(key_val) == VALSTR) {
      const char *key = ValToStrPtr(key_val)->data;
      const char *val =
          (ValKind(val_obj) == VALSTR) ? ValToStrPtr(val_obj)->data : NULL;
#ifdef _WIN32
      ok = SetEnvironmentVariableA(key, val) ? 1 : 0;
#else
      if (val)
        setenv(key, val, 1);
      else
        unsetenv(key);
      ok = 1;
#endif
    }
    ValFree(&val_obj);
    ValFree(&key_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EnvCwd") == 0) {
    char cwd[MAX_PATH];
    cwd[0] = '\0';
#ifdef _WIN32
    GetCurrentDirectoryA(sizeof(cwd), cwd);
#else
    if (!getcwd(cwd, sizeof(cwd)))
      cwd[0] = '\0';
#endif
    PUSH(ValStr(cwd));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EnvSetCwd") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: EnvSetCwd() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int ok = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      ok = SetCurrentDirectoryA(path) ? 1 : 0;
#else
      ok = (chdir(path) == 0) ? 1 : 0;
#endif
    }
    ValFree(&path_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemSize") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FileSystemSize() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int64_t sz = -1;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      WIN32_FILE_ATTRIBUTE_DATA fad;
      if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER li;
        li.LowPart = fad.nFileSizeLow;
        li.HighPart = fad.nFileSizeHigh;
        sz = (int64_t)li.QuadPart;
      }
#else
      struct stat st;
      if (stat(path, &st) == 0)
        sz = (int64_t)st.st_size;
#endif
    }
    ValFree(&path_val);
    PUSH(ValInt(sz));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemIsFile") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FileSystemIsFile() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int isFile = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      DWORD attr = GetFileAttributesA(path);
      isFile = (attr != INVALID_FILE_ATTRIBUTES &&
                !(attr & FILE_ATTRIBUTE_DIRECTORY));
#else
      struct stat st;
      if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        isFile = 1;
#endif
    }
    ValFree(&path_val);
    PUSH(ValBool(isFile));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemIsDir") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FileSystemIsDir() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int isDir = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      DWORD attr = GetFileAttributesA(path);
      isDir = (attr != INVALID_FILE_ATTRIBUTES &&
               (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
      struct stat st;
      if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        isDir = 1;
#endif
    }
    ValFree(&path_val);
    PUSH(ValBool(isDir));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemModifiedTime") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileSystemModifiedTime() expects 1 "
                      "argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int64_t mtime = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *path = ValToStrPtr(path_val)->data;
#ifdef _WIN32
      WIN32_FILE_ATTRIBUTE_DATA fad;
      if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER uli;
        uli.LowPart = fad.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = fad.ftLastWriteTime.dwHighDateTime;
        mtime = (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
      }
#else
      struct stat st;
      if (stat(path, &st) == 0)
        mtime = (int64_t)st.st_mtime * 1000LL;
#endif
    }
    ValFree(&path_val);
    PUSH(ValInt(mtime));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemCopy") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: FileSystemCopy() expects 2 arguments (src, dst)\n");
      return -1;
    }
    AKSValue dst_val = POP();
    AKSValue src_val = POP();
    int ok = 0;
    if (ValKind(src_val) == VALSTR && ValKind(dst_val) == VALSTR) {
#ifdef _WIN32
      const char *src = ValToStrPtr(src_val)->data;
      const char *dst = ValToStrPtr(dst_val)->data;
      ok = CopyFileA(src, dst, FALSE) ? 1 : 0;
#endif
    }
    ValFree(&dst_val);
    ValFree(&src_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemWriteText") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FileSystemWriteText() expects 2 "
                      "arguments (path, content)\n");
      return -1;
    }
    AKSValue content_val = POP();
    AKSValue path_val = POP();
    int ok = 0;
    if (ValKind(path_val) == VALSTR && ValKind(content_val) == VALSTR) {
      FILE *fp = fopen(ValToStrPtr(path_val)->data, "wb");
      if (fp) {
        fwrite(ValToStrPtr(content_val)->data, 1, ValToStrPtr(content_val)->len,
               fp);
        fclose(fp);
        ok = 1;
      }
    }
    ValFree(&content_val);
    ValFree(&path_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemReadText") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: FileSystemReadText() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    AKSValue res = ValNull();
    if (ValKind(path_val) == VALSTR) {
      FILE *fp = fopen(ValToStrPtr(path_val)->data, "rb");
      if (fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz >= 0) {
          char *buf = (char *)malloc((size_t)sz + 1);
          size_t rd = fread(buf, 1, (size_t)sz, fp);
          buf[rd] = '\0';
          res = ValStrLen(buf, (int)rd);
          free(buf);
        }
        fclose(fp);
      }
    }
    ValFree(&path_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemDelete") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FileSystemDelete() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue path_val = POP();
    int ok = 0;
    if (ValKind(path_val) == VALSTR) {
      const char *p = ValToStrPtr(path_val)->data;
      if (remove(p) == 0) {
        ok = 1;
      } else {
#ifdef _WIN32
        ok = (RemoveDirectoryA(p) != 0) ? 1 : 0;
#else
        ok = (rmdir(p) == 0) ? 1 : 0;
#endif
      }
    }
    ValFree(&path_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryProtectSet") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: MemoryProtectSet() expects 3 arguments "
                      "(ptr, size, mode)\n");
      return -1;
    }
    AKSValue mode_val = POP();
    AKSValue size_val = POP();
    AKSValue ptr_val = POP();
    int ok = 0;
    uintptr_t ptr = (uintptr_t)ValToInt(ptr_val);
    size_t size = (size_t)ValToInt(size_val);
    if (ptr != 0 && size > 0 && ValKind(mode_val) == VALSTR) {
#ifdef _WIN32
      const char *mode = ValToStrPtr(mode_val)->data;
      DWORD prot = PAGE_READWRITE;
      if (strcmp(mode, "ro") == 0)
        prot = PAGE_READONLY;
      else if (strcmp(mode, "none") == 0)
        prot = PAGE_NOACCESS;
      else if (strcmp(mode, "rwx") == 0)
        prot = PAGE_EXECUTE_READWRITE;
      DWORD oldProt = 0;
      ok = VirtualProtect((LPVOID)ptr, size, prot, &oldProt) ? 1 : 0;
#endif
    }
    ValFree(&mode_val);
    ValFree(&size_val);
    ValFree(&ptr_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessWorkingSet") == 0) {
    int64_t bytes = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
      bytes = (int64_t)pmc.WorkingSetSize;
    }
#endif
    PUSH(ValInt(bytes));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessPeakWorkingSet") == 0) {
    int64_t bytes = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
      bytes = (int64_t)pmc.PeakWorkingSetSize;
    }
#endif
    PUSH(ValInt(bytes));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessCpuTime") == 0) {
    int64_t totalMs = 0;
#ifdef _WIN32
    FILETIME createTime, exitTime, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime,
                        &kernelTime, &userTime)) {
      ULARGE_INTEGER k, u;
      k.LowPart = kernelTime.dwLowDateTime;
      k.HighPart = kernelTime.dwHighDateTime;
      u.LowPart = userTime.dwLowDateTime;
      u.HighPart = userTime.dwHighDateTime;
      totalMs = (int64_t)((k.QuadPart + u.QuadPart) / 10000ULL);
    }
#endif
    PUSH(ValInt(totalMs));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSignalIgnoreBreak") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSignalIgnoreBreak() expects 1 argument "
                      "(ignore_bool)\n");
      return -1;
    }
    AKSValue ign_val = POP();
    int ok = 0;
#ifdef _WIN32
    BOOL ignore = (BOOL)ValToBool(ign_val);
    ok = SetConsoleCtrlHandler(NULL, ignore) ? 1 : 0;
#endif
    ValFree(&ign_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSignalGenerateInterrupt") == 0) {
    int ok = 0;
#ifdef _WIN32
    ok = GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0) ? 1 : 0;
#endif
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuPhysicalCores") == 0) {
    int count = 1;
#ifdef _WIN32
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    if (len > 0) {
      PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer =
          (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(len);
      if (buffer && GetLogicalProcessorInformationEx(RelationProcessorCore,
                                                     buffer, &len)) {
        count = 0;
        DWORD offset = 0;
        while (offset < len) {
          PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info =
              (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((char *)buffer +
                                                         offset);
          if (info->Relationship == RelationProcessorCore)
            count++;
          offset += info->Size;
        }
      }
      if (buffer)
        free(buffer);
    }
    if (count <= 0)
      count = 1;
#endif
    PUSH(ValInt((int64_t)count));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuLogicalCores") == 0) {
    int count = 1;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    count = (int)si.dwNumberOfProcessors;
#endif
    PUSH(ValInt((int64_t)count));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuPageSize") == 0) {
    int64_t pageSize = 4096;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    pageSize = (int64_t)si.dwPageSize;
#endif
    PUSH(ValInt(pageSize));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NamedPipeCreateServer") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: NamedPipeCreateServer() expects 1 argument (name)\n");
      return -1;
    }
    AKSValue name_val = POP();
    int64_t handleId = 0;
    if (ValKind(name_val) == VALSTR) {
#ifdef _WIN32
      const char *pName = ValToStrPtr(name_val)->data;
      HANDLE hPipe = CreateNamedPipeA(pName, PIPE_ACCESS_DUPLEX,
                                      PIPE_TYPE_MESSAGE |
                                          PIPE_READMODE_MESSAGE | PIPE_NOWAIT,
                                      1, 4096, 4096, 0, NULL);
      if (hPipe != INVALID_HANDLE_VALUE) {
        handleId = (int64_t)(uintptr_t)hPipe;
      }
#endif
    }
    ValFree(&name_val);
    PUSH(ValInt(handleId));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NamedPipeCall") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: NamedPipeCall() expects 2 arguments (name, msg)\n");
      return -1;
    }
    AKSValue msg_val = POP();
    AKSValue name_val = POP();
    AKSValue res = ValNull();
    if (ValKind(name_val) == VALSTR && ValKind(msg_val) == VALSTR) {
#ifdef _WIN32
      const char *pName = ValToStrPtr(name_val)->data;
      AKSString *msgStr = ValToStrPtr(msg_val);
      char readBuf[4096];
      DWORD bytesRead = 0;
      BOOL ok = CallNamedPipeA(pName, (LPVOID)msgStr->data, (DWORD)msgStr->len,
                               readBuf, sizeof(readBuf) - 1, &bytesRead, 500);
      if (ok && bytesRead > 0) {
        readBuf[bytesRead] = '\0';
        res = ValStrLen(readBuf, (int)bytesRead);
      }
#endif
    }
    ValFree(&msg_val);
    ValFree(&name_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NamedPipeClose") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: NamedPipeClose() expects 1 argument (handle)\n");
      return -1;
    }
    AKSValue h_val = POP();
    int ok = 0;
#ifdef _WIN32
    HANDLE hPipe = (HANDLE)(uintptr_t)ValToInt(h_val);
    if (hPipe != NULL && hPipe != INVALID_HANDLE_VALUE) {
      ok = CloseHandle(hPipe) ? 1 : 0;
    }
#endif
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegistryGetString") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: RegistryGetString() expects 3 arguments "
                      "(root, subkey, name)\n");
      return -1;
    }
    AKSValue n_val = POP();
    AKSValue k_val = POP();
    AKSValue r_val = POP();
    AKSValue res = ValNull();
    if (ValKind(r_val) == VALSTR && ValKind(k_val) == VALSTR &&
        ValKind(n_val) == VALSTR) {
#ifdef _WIN32
      const char *root = ValToStrPtr(r_val)->data;
      const char *subkey = ValToStrPtr(k_val)->data;
      const char *valName = ValToStrPtr(n_val)->data;
      HKEY hRoot = HKEY_CURRENT_USER;
      if (strcmp(root, "HKLM") == 0)
        hRoot = HKEY_LOCAL_MACHINE;
      else if (strcmp(root, "HKCR") == 0)
        hRoot = HKEY_CLASSES_ROOT;
      HKEY hKey = NULL;
      if (RegOpenKeyExA(hRoot, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[1024];
        DWORD sz = sizeof(buf);
        DWORD type = 0;
        if (RegQueryValueExA(hKey, valName, NULL, &type, (LPBYTE)buf, &sz) ==
            ERROR_SUCCESS) {
          if (type == REG_SZ || type == REG_EXPAND_SZ) {
            res = ValStr(buf);
          }
        }
        RegCloseKey(hKey);
      }
#endif
    }
    ValFree(&n_val);
    ValFree(&k_val);
    ValFree(&r_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegistrySetString") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: RegistrySetString() expects 4 arguments "
                      "(root, subkey, name, val)\n");
      return -1;
    }
    AKSValue v_val = POP();
    AKSValue n_val = POP();
    AKSValue k_val = POP();
    AKSValue r_val = POP();
    int ok = 0;
    if (ValKind(r_val) == VALSTR && ValKind(k_val) == VALSTR &&
        ValKind(n_val) == VALSTR && ValKind(v_val) == VALSTR) {
#ifdef _WIN32
      const char *root = ValToStrPtr(r_val)->data;
      const char *subkey = ValToStrPtr(k_val)->data;
      const char *valName = ValToStrPtr(n_val)->data;
      const char *valStr = ValToStrPtr(v_val)->data;
      HKEY hRoot = HKEY_CURRENT_USER;
      if (strcmp(root, "HKLM") == 0)
        hRoot = HKEY_LOCAL_MACHINE;
      else if (strcmp(root, "HKCR") == 0)
        hRoot = HKEY_CLASSES_ROOT;
      HKEY hKey = NULL;
      if (RegCreateKeyExA(hRoot, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey,
                          NULL) == ERROR_SUCCESS) {
        if (RegSetValueExA(hKey, valName, 0, REG_SZ, (const BYTE *)valStr,
                           (DWORD)strlen(valStr) + 1) == ERROR_SUCCESS) {
          ok = 1;
        }
        RegCloseKey(hKey);
      }
#endif
    }
    ValFree(&v_val);
    ValFree(&n_val);
    ValFree(&k_val);
    ValFree(&r_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegistryGetDword") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: RegistryGetDword() expects 3 arguments "
                      "(root, subkey, name)\n");
      return -1;
    }
    AKSValue n_val = POP();
    AKSValue k_val = POP();
    AKSValue r_val = POP();
    AKSValue res = ValNull();
    if (ValKind(r_val) == VALSTR && ValKind(k_val) == VALSTR &&
        ValKind(n_val) == VALSTR) {
#ifdef _WIN32
      const char *root = ValToStrPtr(r_val)->data;
      const char *subkey = ValToStrPtr(k_val)->data;
      const char *valName = ValToStrPtr(n_val)->data;
      HKEY hRoot = HKEY_CURRENT_USER;
      if (strcmp(root, "HKLM") == 0)
        hRoot = HKEY_LOCAL_MACHINE;
      else if (strcmp(root, "HKCR") == 0)
        hRoot = HKEY_CLASSES_ROOT;
      HKEY hKey = NULL;
      if (RegOpenKeyExA(hRoot, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwVal = 0;
        DWORD sz = sizeof(dwVal);
        DWORD type = 0;
        if (RegQueryValueExA(hKey, valName, NULL, &type, (LPBYTE)&dwVal, &sz) ==
            ERROR_SUCCESS) {
          if (type == REG_DWORD) {
            res = ValInt((int64_t)dwVal);
          }
        }
        RegCloseKey(hKey);
      }
#endif
    }
    ValFree(&n_val);
    ValFree(&k_val);
    ValFree(&r_val);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegistrySetDword") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: RegistrySetDword() expects 4 arguments "
                      "(root, subkey, name, val)\n");
      return -1;
    }
    AKSValue v_val = POP();
    AKSValue n_val = POP();
    AKSValue k_val = POP();
    AKSValue r_val = POP();
    int ok = 0;
    if (ValKind(r_val) == VALSTR && ValKind(k_val) == VALSTR &&
        ValKind(n_val) == VALSTR) {
#ifdef _WIN32
      const char *root = ValToStrPtr(r_val)->data;
      const char *subkey = ValToStrPtr(k_val)->data;
      const char *valName = ValToStrPtr(n_val)->data;
      DWORD dwVal = (DWORD)ValToInt(v_val);
      HKEY hRoot = HKEY_CURRENT_USER;
      if (strcmp(root, "HKLM") == 0)
        hRoot = HKEY_LOCAL_MACHINE;
      else if (strcmp(root, "HKCR") == 0)
        hRoot = HKEY_CLASSES_ROOT;
      HKEY hKey = NULL;
      if (RegCreateKeyExA(hRoot, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey,
                          NULL) == ERROR_SUCCESS) {
        if (RegSetValueExA(hKey, valName, 0, REG_DWORD, (const BYTE *)&dwVal,
                           sizeof(dwVal)) == ERROR_SUCCESS) {
          ok = 1;
        }
        RegCloseKey(hKey);
      }
#endif
    }
    ValFree(&v_val);
    ValFree(&n_val);
    ValFree(&k_val);
    ValFree(&r_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RegistryDeleteValue") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: RegistryDeleteValue() expects 3 "
                      "arguments (root, subkey, name)\n");
      return -1;
    }
    AKSValue n_val = POP();
    AKSValue k_val = POP();
    AKSValue r_val = POP();
    int ok = 0;
    if (ValKind(r_val) == VALSTR && ValKind(k_val) == VALSTR &&
        ValKind(n_val) == VALSTR) {
#ifdef _WIN32
      const char *root = ValToStrPtr(r_val)->data;
      const char *subkey = ValToStrPtr(k_val)->data;
      const char *valName = ValToStrPtr(n_val)->data;
      HKEY hRoot = HKEY_CURRENT_USER;
      if (strcmp(root, "HKLM") == 0)
        hRoot = HKEY_LOCAL_MACHINE;
      else if (strcmp(root, "HKCR") == 0)
        hRoot = HKEY_CLASSES_ROOT;
      HKEY hKey = NULL;
      if (RegOpenKeyExA(hRoot, subkey, 0, KEY_SET_VALUE, &hKey) ==
          ERROR_SUCCESS) {
        ok = (RegDeleteValueA(hKey, valName) == ERROR_SUCCESS) ? 1 : 0;
        RegCloseKey(hKey);
      }
#endif
    }
    ValFree(&n_val);
    ValFree(&k_val);
    ValFree(&r_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConsoleSetColor") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: ConsoleSetColor() expects 2 arguments (fg, bg)\n");
      return -1;
    }
    AKSValue bg_val = POP();
    AKSValue fg_val = POP();
    int ok = 0;
#ifdef _WIN32
    WORD fg = (WORD)ValToInt(fg_val);
    WORD bg = (WORD)ValToInt(bg_val);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD attr = (WORD)(fg | (bg << 4));
    if (hConsole != INVALID_HANDLE_VALUE &&
        SetConsoleTextAttribute(hConsole, attr)) {
      ok = 1;
    } else {
      HANDLE hCon = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, 0, NULL);
      if (hCon != INVALID_HANDLE_VALUE) {
        ok = SetConsoleTextAttribute(hCon, attr) ? 1 : 0;
        CloseHandle(hCon);
      } else {
        ok = 1;
      }
    }
#else
    ok = 1;
#endif
    ValFree(&bg_val);
    ValFree(&fg_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConsoleResetColor") == 0) {
    int ok = 0;
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    if (hConsole != INVALID_HANDLE_VALUE &&
        SetConsoleTextAttribute(hConsole, attr)) {
      ok = 1;
    } else {
      HANDLE hCon = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, 0, NULL);
      if (hCon != INVALID_HANDLE_VALUE) {
        ok = SetConsoleTextAttribute(hCon, attr) ? 1 : 0;
        CloseHandle(hCon);
      } else {
        ok = 1;
      }
    }
#else
    ok = 1;
#endif
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConsoleSetCursorPos") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: ConsoleSetCursorPos() expects 2 arguments (x, y)\n");
      return -1;
    }
    AKSValue y_val = POP();
    AKSValue x_val = POP();
    int ok = 0;
#ifdef _WIN32
    SHORT x = (SHORT)ValToInt(x_val);
    SHORT y = (SHORT)ValToInt(y_val);
    COORD pos = {x, y};
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE &&
        SetConsoleCursorPosition(hConsole, pos)) {
      ok = 1;
    } else {
      HANDLE hCon = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, 0, NULL);
      if (hCon != INVALID_HANDLE_VALUE) {
        ok = SetConsoleCursorPosition(hCon, pos) ? 1 : 0;
        CloseHandle(hCon);
      } else {
        ok = 1;
      }
    }
#else
    ok = 1;
#endif
    ValFree(&y_val);
    ValFree(&x_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConsoleGetSize") == 0) {
    AKSValue res = ValArr(2);
    AKSArray *arr = ValToArr(res);
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (hConsole != INVALID_HANDLE_VALUE &&
        GetConsoleScreenBufferInfo(hConsole, &csbi)) {
      int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
      int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
      arr->items[arr->count++] = ValInt(width);
      arr->items[arr->count++] = ValInt(height);
    } else {
      arr->items[arr->count++] = ValInt(80);
      arr->items[arr->count++] = ValInt(25);
    }
#else
    arr->items[arr->count++] = ValInt(80);
    arr->items[arr->count++] = ValInt(25);
#endif
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DiskVolumeFreeSpace") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: DiskVolumeFreeSpace() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue p_val = POP();
    int64_t freeBytesVal = 0;
    if (ValKind(p_val) == VALSTR) {
#ifdef _WIN32
      const char *path = ValToStrPtr(p_val)->data;
      ULARGE_INTEGER freeBytes, totalBytes, totalFree;
      if (GetDiskFreeSpaceExA(path, &freeBytes, &totalBytes, &totalFree)) {
        freeBytesVal = (int64_t)freeBytes.QuadPart;
      }
#endif
    }
    ValFree(&p_val);
    PUSH(ValInt(freeBytesVal));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DiskVolumeTotalSpace") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: DiskVolumeTotalSpace() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue p_val = POP();
    int64_t totalBytesVal = 0;
    if (ValKind(p_val) == VALSTR) {
#ifdef _WIN32
      const char *path = ValToStrPtr(p_val)->data;
      ULARGE_INTEGER freeBytes, totalBytes, totalFree;
      if (GetDiskFreeSpaceExA(path, &freeBytes, &totalBytes, &totalFree)) {
        totalBytesVal = (int64_t)totalBytes.QuadPart;
      }
#endif
    }
    ValFree(&p_val);
    PUSH(ValInt(totalBytesVal));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DiskVolumeVolumeName") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: DiskVolumeVolumeName() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue p_val = POP();
    char volName[MAX_PATH + 1] = {0};
    if (ValKind(p_val) == VALSTR) {
#ifdef _WIN32
      const char *path = ValToStrPtr(p_val)->data;
      char fsName[MAX_PATH + 1] = {0};
      GetVolumeInformationA(path, volName, sizeof(volName), NULL, NULL, NULL,
                            fsName, sizeof(fsName));
#endif
    }
    ValFree(&p_val);
    PUSH(ValStr(volName));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DiskVolumeFileSystemName") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: DiskVolumeFileSystemName() expects 1 "
                      "argument (path)\n");
      return -1;
    }
    AKSValue p_val = POP();
    char fsName[MAX_PATH + 1] = {0};
    if (ValKind(p_val) == VALSTR) {
#ifdef _WIN32
      const char *path = ValToStrPtr(p_val)->data;
      char volName[MAX_PATH + 1] = {0};
      GetVolumeInformationA(path, volName, sizeof(volName), NULL, NULL, NULL,
                            fsName, sizeof(fsName));
#endif
    }
    ValFree(&p_val);
    PUSH(ValStr(fsName));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadSyncCreateMutex") == 0) {
    int64_t handleId = 0;
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)malloc(sizeof(CRITICAL_SECTION));
    if (cs) {
      InitializeCriticalSection(cs);
      handleId = (int64_t)(uintptr_t)cs;
    }
#endif
    PUSH(ValInt(handleId));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadSyncLock") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: ThreadSyncLock() expects 1 argument (mutex_id)\n");
      return -1;
    }
    AKSValue h_val = POP();
    int ok = 0;
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)(uintptr_t)ValToInt(h_val);
    if (cs) {
      EnterCriticalSection(cs);
      ok = 1;
    }
#endif
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadSyncUnlock") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: ThreadSyncUnlock() expects 1 argument (mutex_id)\n");
      return -1;
    }
    AKSValue h_val = POP();
    int ok = 0;
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)(uintptr_t)ValToInt(h_val);
    if (cs) {
      LeaveCriticalSection(cs);
      ok = 1;
    }
#endif
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadSyncDestroy") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: ThreadSyncDestroy() expects 1 argument (mutex_id)\n");
      return -1;
    }
    AKSValue h_val = POP();
    int ok = 0;
#ifdef _WIN32
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)(uintptr_t)ValToInt(h_val);
    if (cs) {
      DeleteCriticalSection(cs);
      free(cs);
      ok = 1;
    }
#endif
    ValFree(&h_val);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemPowerAcStatus") == 0) {
    int64_t status = 255;
#ifdef _WIN32
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
      status = (int64_t)sps.ACLineStatus;
    }
#endif
    PUSH(ValInt(status));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemPowerBatteryPercent") == 0) {
    int64_t percent = 255;
#ifdef _WIN32
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
      percent = (int64_t)sps.BatteryLifePercent;
    }
#endif
    PUSH(ValInt(percent));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemPowerBatteryLifeTime") == 0) {
    int64_t lifeTime = -1;
#ifdef _WIN32
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
      lifeTime = (int64_t)(int32_t)sps.BatteryLifeTime;
    }
#endif
    PUSH(ValInt(lifeTime));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemPowerIsCharging") == 0) {
    int isCharging = 0;
#ifdef _WIN32
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
      isCharging = (sps.BatteryFlag & 8) ? 1 : 0;
    }
#endif
    PUSH(ValBool(isCharging));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NetworkInterfacePrimaryIp") == 0) {
    char ipStr[64] = "127.0.0.1";
#ifdef _WIN32
    ULONG bufLen = sizeof(IP_ADAPTER_INFO);
    PIP_ADAPTER_INFO pAdapterInfo = (IP_ADAPTER_INFO *)malloc(bufLen);
    if (pAdapterInfo &&
        GetAdaptersInfo(pAdapterInfo, &bufLen) == ERROR_BUFFER_OVERFLOW) {
      free(pAdapterInfo);
      pAdapterInfo = (IP_ADAPTER_INFO *)malloc(bufLen);
    }
    if (pAdapterInfo && GetAdaptersInfo(pAdapterInfo, &bufLen) == NO_ERROR) {
      PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
      while (pAdapter) {
        if (pAdapter->IpAddressList.IpAddress.String[0] != '\0' &&
            strcmp(pAdapter->IpAddressList.IpAddress.String, "0.0.0.0") != 0) {
          strncpy(ipStr, pAdapter->IpAddressList.IpAddress.String,
                  sizeof(ipStr) - 1);
          ipStr[sizeof(ipStr) - 1] = '\0';
          break;
        }
        pAdapter = pAdapter->Next;
      }
    }
    if (pAdapterInfo)
      free(pAdapterInfo);
#endif
    PUSH(ValStr(ipStr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NetworkInterfacePrimaryMac") == 0) {
    char macStr[64] = "00:00:00:00:00:00";
#ifdef _WIN32
    ULONG bufLen = sizeof(IP_ADAPTER_INFO);
    PIP_ADAPTER_INFO pAdapterInfo = (IP_ADAPTER_INFO *)malloc(bufLen);
    if (pAdapterInfo &&
        GetAdaptersInfo(pAdapterInfo, &bufLen) == ERROR_BUFFER_OVERFLOW) {
      free(pAdapterInfo);
      pAdapterInfo = (IP_ADAPTER_INFO *)malloc(bufLen);
    }
    if (pAdapterInfo && GetAdaptersInfo(pAdapterInfo, &bufLen) == NO_ERROR) {
      PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
      while (pAdapter) {
        if (pAdapter->AddressLength >= 6) {
          sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", pAdapter->Address[0],
                  pAdapter->Address[1], pAdapter->Address[2],
                  pAdapter->Address[3], pAdapter->Address[4],
                  pAdapter->Address[5]);
          break;
        }
        pAdapter = pAdapter->Next;
      }
    }
    if (pAdapterInfo)
      free(pAdapterInfo);
#endif
    PUSH(ValStr(macStr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSemaphoreCreate") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: OsSemaphoreCreate() expects 3 arguments "
                      "(init_count, max_count, name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    AKSValue MaxVal = POP();
    AKSValue InitVal = POP();
    AKSValue ResVal = ValInt(0);
#ifdef _WIN32
    const char *NameStr =
        (ValKind(NameVal) == VALSTR) ? ValToStrPtr(NameVal)->data : NULL;
    LONG InitCount = (LONG)ValToInt(InitVal);
    LONG MaxCount = (LONG)ValToInt(MaxVal);
    HANDLE SemHandle = CreateSemaphoreA(NULL, InitCount, MaxCount, NameStr);
    ResVal = ValInt((int64_t)(uintptr_t)SemHandle);
#else
    ResVal = ValInt(1);
#endif
    ValFree(&InitVal);
    ValFree(&MaxVal);
    ValFree(&NameVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSemaphoreAcquire") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: OsSemaphoreAcquire() expects 2 arguments "
                      "(sem, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue SemVal = POP();
    AKSValue ResVal = ValBool(0);
#ifdef _WIN32
    HANDLE SemHandle = (HANDLE)(uintptr_t)ValToInt(SemVal);
    DWORD TimeoutMs = (DWORD)ValToInt(TimeoutVal);
    if (SemHandle && SemHandle != INVALID_HANDLE_VALUE) {
      DWORD WaitResult = WaitForSingleObject(SemHandle, TimeoutMs);
      ResVal = ValBool(WaitResult == WAIT_OBJECT_0 ? 1 : 0);
    }
#else
    ResVal = ValBool(1);
#endif
    ValFree(&SemVal);
    ValFree(&TimeoutVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSemaphoreRelease") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: OsSemaphoreRelease() expects 2 arguments "
                      "(sem, count)\n");
      return -1;
    }
    AKSValue CountVal = POP();
    AKSValue SemVal = POP();
    AKSValue ResVal = ValInt(-1);
#ifdef _WIN32
    HANDLE SemHandle = (HANDLE)(uintptr_t)ValToInt(SemVal);
    LONG RelCount = (LONG)ValToInt(CountVal);
    LONG PrevCount = 0;
    if (SemHandle && SemHandle != INVALID_HANDLE_VALUE) {
      if (ReleaseSemaphore(SemHandle, RelCount, &PrevCount)) {
        ResVal = ValInt((int64_t)PrevCount);
      }
    }
#else
    ResVal = ValInt(0);
#endif
    ValFree(&SemVal);
    ValFree(&CountVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSemaphoreDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: OsSemaphoreDestroy() expects 1 argument (sem)\n");
      return -1;
    }
    AKSValue SemVal = POP();
#ifdef _WIN32
    HANDLE SemHandle = (HANDLE)(uintptr_t)ValToInt(SemVal);
    if (SemHandle && SemHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(SemHandle);
    }
#endif
    ValFree(&SemVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessWorkingSetCurrent") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessWorkingSetCurrent() expects 1 "
                      "argument (pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    int64_t WorkingSetBytes = 0;
#ifdef _WIN32
    DWORD TargetPid = (DWORD)ValToInt(PidVal);
    HANDLE ProcHandle =
        (TargetPid == 0)
            ? GetCurrentProcess()
            : OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                          TargetPid);
    if (ProcHandle) {
      PROCESS_MEMORY_COUNTERS Pmc;
      memset(&Pmc, 0, sizeof(Pmc));
      if (GetProcessMemoryInfo(ProcHandle, &Pmc, sizeof(Pmc))) {
        WorkingSetBytes = (int64_t)Pmc.WorkingSetSize;
      }
      if (TargetPid != 0) {
        CloseHandle(ProcHandle);
      }
    }
#endif
    ValFree(&PidVal);
    PUSH(ValInt(WorkingSetBytes));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessWorkingSetPeak") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: ProcessWorkingSetPeak() expects 1 argument (pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    int64_t PeakWorkingSetBytes = 0;
#ifdef _WIN32
    DWORD TargetPid = (DWORD)ValToInt(PidVal);
    HANDLE ProcHandle =
        (TargetPid == 0)
            ? GetCurrentProcess()
            : OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                          TargetPid);
    if (ProcHandle) {
      PROCESS_MEMORY_COUNTERS Pmc;
      memset(&Pmc, 0, sizeof(Pmc));
      if (GetProcessMemoryInfo(ProcHandle, &Pmc, sizeof(Pmc))) {
        PeakWorkingSetBytes = (int64_t)Pmc.PeakWorkingSetSize;
      }
      if (TargetPid != 0) {
        CloseHandle(ProcHandle);
      }
    }
#endif
    ValFree(&PidVal);
    PUSH(ValInt(PeakWorkingSetBytes));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessWorkingSetPageFaults") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessWorkingSetPageFaults() expects 1 "
                      "argument (pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    int64_t PageFaultCount = 0;
#ifdef _WIN32
    DWORD TargetPid = (DWORD)ValToInt(PidVal);
    HANDLE ProcHandle =
        (TargetPid == 0)
            ? GetCurrentProcess()
            : OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                          TargetPid);
    if (ProcHandle) {
      PROCESS_MEMORY_COUNTERS Pmc;
      memset(&Pmc, 0, sizeof(Pmc));
      if (GetProcessMemoryInfo(ProcHandle, &Pmc, sizeof(Pmc))) {
        PageFaultCount = (int64_t)Pmc.PageFaultCount;
      }
      if (TargetPid != 0) {
        CloseHandle(ProcHandle);
      }
    }
#endif
    ValFree(&PidVal);
    PUSH(ValInt(PageFaultCount));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessWorkingSetTrim") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: ProcessWorkingSetTrim() expects 1 argument (pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    BOOL TrimSuccess = FALSE;
#ifdef _WIN32
    DWORD TargetPid = (DWORD)ValToInt(PidVal);
    HANDLE ProcHandle =
        (TargetPid == 0)
            ? GetCurrentProcess()
            : OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE,
                          TargetPid);
    if (ProcHandle) {
      TrimSuccess = EmptyWorkingSet(ProcHandle);
      if (TargetPid != 0) {
        CloseHandle(ProcHandle);
      }
    }
#endif
    ValFree(&PidVal);
    PUSH(ValBool(TrimSuccess ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HardwareTimestampRdtsc") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: HardwareTimestampRdtsc() expects 0 arguments\n");
      return -1;
    }
    uint64_t CycleCount = 0;
#ifdef _WIN32
    CycleCount = __rdtsc();
#endif
    PUSH(ValInt((int64_t)(CycleCount & 0x7FFFFFFFFFFFFFFFLL)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HardwareTimestampRdtscp") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: HardwareTimestampRdtscp() expects 0 arguments\n");
      return -1;
    }
    uint64_t CycleCount = 0;
    unsigned int CoreId = 0;
#ifdef _WIN32
    CycleCount = __rdtscp(&CoreId);
#endif
    AKSValue ResStruct = ValStruct("HardwareTimestampRdtscpResult");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("cycles");
    StructPtr->fieldvals[0] =
        ValInt((int64_t)(CycleCount & 0x7FFFFFFFFFFFFFFFLL));
    StructPtr->fieldnames[1] = strdup("core_id");
    StructPtr->fieldvals[1] = ValInt((int64_t)CoreId);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HardwareTimestampTicksPerSecond") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: HardwareTimestampTicksPerSecond() "
                      "expects 0 arguments\n");
      return -1;
    }
    int64_t TicksPerSec = 10000000;
#ifdef _WIN32
    LARGE_INTEGER Freq;
    if (QueryPerformanceFrequency(&Freq)) {
      TicksPerSec = (int64_t)Freq.QuadPart;
    }
#endif
    PUSH(ValInt(TicksPerSec));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HardwareTimestampNanoseconds") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: HardwareTimestampNanoseconds() expects 0 "
                      "arguments\n");
      return -1;
    }
    int64_t TotalNanoseconds = 0;
#ifdef _WIN32
    LARGE_INTEGER Freq, Counter;
    if (QueryPerformanceFrequency(&Freq) && QueryPerformanceCounter(&Counter) &&
        Freq.QuadPart > 0) {
      TotalNanoseconds =
          (int64_t)((Counter.QuadPart / Freq.QuadPart) * 1000000000LL +
                    ((Counter.QuadPart % Freq.QuadPart) * 1000000000LL) /
                        Freq.QuadPart);
    }
#endif
    PUSH(ValInt(TotalNanoseconds));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileLockAcquireShared") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: FileLockAcquireShared() expects 4 "
                      "arguments (path, offset, length, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue LenVal = POP();
    AKSValue OffVal = POP();
    AKSValue PathVal = POP();
    AKSValue ResVal = ValInt(0);
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *FilePath = ValToStrPtr(PathVal)->data;
      HANDLE FileHandle = CreateFileA(FilePath, GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (FileHandle != INVALID_HANDLE_VALUE) {
        OVERLAPPED OverlappedStruct;
        memset(&OverlappedStruct, 0, sizeof(OverlappedStruct));
        OverlappedStruct.Offset = (DWORD)ValToInt(OffVal);
        DWORD LockLength = (DWORD)ValToInt(LenVal);
        if (LockFileEx(FileHandle, 0, 0, LockLength, 0, &OverlappedStruct)) {
          ResVal = ValInt((int64_t)(uintptr_t)FileHandle);
        } else {
          CloseHandle(FileHandle);
        }
      }
    }
#else
    if (ValKind(PathVal) == VALSTR) {
      const char *FilePath = ValToStrPtr(PathVal)->data;
      int fd = AksPosixOpen(FilePath, O_RDWR | O_CREAT, 0666);
      if (fd >= 0) {
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_RDLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = (off_t)ValToInt(OffVal);
        fl.l_len = (off_t)ValToInt(LenVal);
#ifdef F_OFD_SETLK
        if (fcntl(fd, F_OFD_SETLK, &fl) == 0) {
#else
        if (fcntl(fd, F_SETLK, &fl) == 0) {
#endif
          ResVal = ValInt((int64_t)(fd + 1));
        } else {
          close(fd);
        }
      }
    }
#endif
    ValFree(&PathVal);
    ValFree(&OffVal);
    ValFree(&LenVal);
    ValFree(&TimeoutVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileLockAcquireExclusive") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: FileLockAcquireExclusive() expects 4 "
                      "arguments (path, offset, length, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue LenVal = POP();
    AKSValue OffVal = POP();
    AKSValue PathVal = POP();
    AKSValue ResVal = ValInt(0);
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *FilePath = ValToStrPtr(PathVal)->data;
      HANDLE FileHandle = CreateFileA(FilePath, GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (FileHandle != INVALID_HANDLE_VALUE) {
        OVERLAPPED OverlappedStruct;
        memset(&OverlappedStruct, 0, sizeof(OverlappedStruct));
        OverlappedStruct.Offset = (DWORD)ValToInt(OffVal);
        DWORD LockLength = (DWORD)ValToInt(LenVal);
        if (LockFileEx(FileHandle, LOCKFILE_EXCLUSIVE_LOCK, 0, LockLength, 0,
                       &OverlappedStruct)) {
          ResVal = ValInt((int64_t)(uintptr_t)FileHandle);
        } else {
          CloseHandle(FileHandle);
        }
      }
    }
#else
    if (ValKind(PathVal) == VALSTR) {
      const char *FilePath = ValToStrPtr(PathVal)->data;
      int fd = AksPosixOpen(FilePath, O_RDWR | O_CREAT, 0666);
      if (fd >= 0) {
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = (off_t)ValToInt(OffVal);
        fl.l_len = (off_t)ValToInt(LenVal);
#ifdef F_OFD_SETLK
        if (fcntl(fd, F_OFD_SETLK, &fl) == 0) {
#else
        if (fcntl(fd, F_SETLK, &fl) == 0) {
#endif
          ResVal = ValInt((int64_t)(fd + 1));
        } else {
          close(fd);
        }
      }
    }
#endif
    ValFree(&PathVal);
    ValFree(&OffVal);
    ValFree(&LenVal);
    ValFree(&TimeoutVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileLockRelease") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: FileLockRelease() expects 3 arguments "
                      "(handle, offset, length)\n");
      return -1;
    }
    AKSValue LenVal = POP();
    AKSValue OffVal = POP();
    AKSValue HandleVal = POP();
    BOOL UnlockOk = FALSE;
#ifdef _WIN32
    HANDLE FileHandle = (HANDLE)(uintptr_t)ValToInt(HandleVal);
    if (FileHandle && FileHandle != INVALID_HANDLE_VALUE) {
      OVERLAPPED OverlappedStruct;
      memset(&OverlappedStruct, 0, sizeof(OverlappedStruct));
      OverlappedStruct.Offset = (DWORD)ValToInt(OffVal);
      DWORD LockLength = (DWORD)ValToInt(LenVal);
      UnlockOk = UnlockFileEx(FileHandle, 0, LockLength, 0, &OverlappedStruct);
      CloseHandle(FileHandle);
    }
#else
    int fd = (int)(ValToInt(HandleVal) - 1);
    if (fd >= 0) {
      struct flock fl;
      memset(&fl, 0, sizeof(fl));
      fl.l_type = F_UNLCK;
      fl.l_whence = SEEK_SET;
      fl.l_start = (off_t)ValToInt(OffVal);
      fl.l_len = (off_t)ValToInt(LenVal);
#ifdef F_OFD_SETLK
      UnlockOk = (fcntl(fd, F_OFD_SETLK, &fl) == 0) ? TRUE : FALSE;
#else
      UnlockOk = (fcntl(fd, F_SETLK, &fl) == 0) ? TRUE : FALSE;
#endif
      close(fd);
    }
#endif
    ValFree(&HandleVal);
    ValFree(&OffVal);
    ValFree(&LenVal);
    PUSH(ValBool(UnlockOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileLockIsLocked") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FileLockIsLocked() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue PathVal = POP();
    BOOL IsFileLocked = FALSE;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *FilePath = ValToStrPtr(PathVal)->data;
      HANDLE FileHandle =
          CreateFileA(FilePath, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (FileHandle == INVALID_HANDLE_VALUE) {
        DWORD ErrorCode = GetLastError();
        if (ErrorCode == ERROR_SHARING_VIOLATION ||
            ErrorCode == ERROR_LOCK_VIOLATION) {
          IsFileLocked = TRUE;
        }
      } else {
        CloseHandle(FileHandle);
      }
    }
#else
    if (ValKind(PathVal) == VALSTR) {
      const char *FilePath = ValToStrPtr(PathVal)->data;
      int fd = AksPosixOpen(FilePath, O_RDWR, 0666);
      if (fd < 0)
        fd = AksPosixOpen(FilePath, O_RDONLY, 0666);
      if (fd >= 0) {
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
#ifdef F_OFD_GETLK
        if (fcntl(fd, F_OFD_GETLK, &fl) == 0 && fl.l_type != F_UNLCK) {
#else
        if (fcntl(fd, F_GETLK, &fl) == 0 && fl.l_type != F_UNLCK) {
#endif
          IsFileLocked = TRUE;
        }
        close(fd);
      }
    }
#endif
    ValFree(&PathVal);
    PUSH(ValBool(IsFileLocked ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsEventCreate") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: OsEventCreate() expects 3 arguments "
                      "(manual_reset, init_state, name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    AKSValue InitVal = POP();
    AKSValue ManualVal = POP();
    const char *NameStr =
        (ValKind(NameVal) == VALSTR) ? ValToStrPtr(NameVal)->data : NULL;
    BOOL ManualReset = ValToBool(ManualVal) ? TRUE : FALSE;
    BOOL InitState = ValToBool(InitVal) ? TRUE : FALSE;
#ifndef _WIN32
    (void)NameStr;
#endif
    HANDLE EventHandle = CreateEventA(NULL, ManualReset, InitState, NameStr);
    AKSValue ResVal = ValInt((int64_t)(uintptr_t)EventHandle);
    ValFree(&ManualVal);
    ValFree(&InitVal);
    ValFree(&NameVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsEventSet") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: OsEventSet() expects 1 argument (event)\n");
      return -1;
    }
    AKSValue EventVal = POP();
    BOOL SetOk = FALSE;
    HANDLE EventHandle = (HANDLE)(uintptr_t)ValToInt(EventVal);
    if (EventHandle && EventHandle != INVALID_HANDLE_VALUE) {
      SetOk = SetEvent(EventHandle);
    }
    ValFree(&EventVal);
    PUSH(ValBool(SetOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsEventReset") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: OsEventReset() expects 1 argument (event)\n");
      return -1;
    }
    AKSValue EventVal = POP();
    BOOL ResetOk = FALSE;
    HANDLE EventHandle = (HANDLE)(uintptr_t)ValToInt(EventVal);
    if (EventHandle && EventHandle != INVALID_HANDLE_VALUE) {
      ResetOk = ResetEvent(EventHandle);
    }
    ValFree(&EventVal);
    PUSH(ValBool(ResetOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsEventWait") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: OsEventWait() expects 2 arguments "
                      "(event, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue EventVal = POP();
    AKSValue ResVal = ValBool(0);
    HANDLE EventHandle = (HANDLE)(uintptr_t)ValToInt(EventVal);
    DWORD TimeoutMs = (DWORD)ValToInt(TimeoutVal);
    if (EventHandle && EventHandle != INVALID_HANDLE_VALUE) {
      DWORD WaitResult = WaitForSingleObject(EventHandle, TimeoutMs);
      ResVal = ValBool(WaitResult == WAIT_OBJECT_0 ? 1 : 0);
    }
    ValFree(&EventVal);
    ValFree(&TimeoutVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsEventDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: OsEventDestroy() expects 1 argument (event)\n");
      return -1;
    }
    AKSValue EventVal = POP();
    HANDLE EventHandle = (HANDLE)(uintptr_t)ValToInt(EventVal);
    if (EventHandle && EventHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(EventHandle);
    }
    ValFree(&EventVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadSchedulerSetAffinity") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadSchedulerSetAffinity() expects 1 "
                      "argument (mask)\n");
      return -1;
    }
    AKSValue MaskVal = POP();
    int64_t PrevAffinity = 0;
#ifdef _WIN32
    DWORD_PTR AffinityMask = (DWORD_PTR)ValToInt(MaskVal);
    DWORD_PTR PrevMask =
        SetThreadAffinityMask(GetCurrentThread(), AffinityMask);
    PrevAffinity = (int64_t)PrevMask;
#endif
    ValFree(&MaskVal);
    PUSH(ValInt(PrevAffinity));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadSchedulerGetProcessAffinity") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ThreadSchedulerGetProcessAffinity() "
                      "expects 0 arguments\n");
      return -1;
    }
    int64_t ProcessAffinity = 1;
#ifdef _WIN32
    DWORD_PTR ProcMask = 0, SysMask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &ProcMask, &SysMask)) {
      ProcessAffinity = (int64_t)ProcMask;
    }
#endif
    PUSH(ValInt(ProcessAffinity));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadSchedulerSetPriority") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadSchedulerSetPriority() expects 1 "
                      "argument (priority)\n");
      return -1;
    }
    AKSValue PrioVal = POP();
    BOOL SetOk = FALSE;
#ifdef _WIN32
    int PriorityVal = (int)ValToInt(PrioVal);
    SetOk = SetThreadPriority(GetCurrentThread(), PriorityVal);
#endif
    ValFree(&PrioVal);
    PUSH(ValBool(SetOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadSchedulerGetPriority") == 0) {
    if (argc != 0) {
      fprintf(
          stderr,
          "Runtime error: ThreadSchedulerGetPriority() expects 0 arguments\n");
      return -1;
    }
    int CurrentPriority = 0;
#ifdef _WIN32
    CurrentPriority = GetThreadPriority(GetCurrentThread());
#endif
    PUSH(ValInt(CurrentPriority));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemLocaleCurrentLocaleName") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemLocaleCurrentLocaleName() expects "
                      "0 arguments\n");
      return -1;
    }
    char LocaleNameStr[128] = {0};
#ifdef _WIN32
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SNAME, LocaleNameStr,
                   sizeof(LocaleNameStr));
    if (LocaleNameStr[0] == '\0')
      strcpy(LocaleNameStr, "en-US");
#else
    strcpy(LocaleNameStr, "en-US");
#endif
    PUSH(ValStr(LocaleNameStr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemLocaleTimezoneName") == 0) {
    if (argc != 0) {
      fprintf(
          stderr,
          "Runtime error: SystemLocaleTimezoneName() expects 0 arguments\n");
      return -1;
    }
    char TimezoneNameStr[128] = {0};
#ifdef _WIN32
    TIME_ZONE_INFORMATION TzInfo;
    memset(&TzInfo, 0, sizeof(TzInfo));
    GetTimeZoneInformation(&TzInfo);
    WideCharToMultiByte(CP_UTF8, 0, TzInfo.StandardName, -1, TimezoneNameStr,
                        sizeof(TimezoneNameStr), NULL, NULL);
    if (TimezoneNameStr[0] == '\0')
      strcpy(TimezoneNameStr, "UTC");
#else
    strcpy(TimezoneNameStr, "UTC");
#endif
    PUSH(ValStr(TimezoneNameStr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemLocaleTimezoneBiasMinutes") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemLocaleTimezoneBiasMinutes() "
                      "expects 0 arguments\n");
      return -1;
    }
    int64_t BiasMinutes = 0;
#ifdef _WIN32
    TIME_ZONE_INFORMATION TzInfo;
    memset(&TzInfo, 0, sizeof(TzInfo));
    GetTimeZoneInformation(&TzInfo);
    BiasMinutes = (int64_t)TzInfo.Bias;
#endif
    PUSH(ValInt(BiasMinutes));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemLocaleIsDaylightSaving") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemLocaleIsDaylightSaving() expects 0 "
                      "arguments\n");
      return -1;
    }
    BOOL IsDaylight = FALSE;
#ifdef _WIN32
    TIME_ZONE_INFORMATION TzInfo;
    memset(&TzInfo, 0, sizeof(TzInfo));
    DWORD TzResult = GetTimeZoneInformation(&TzInfo);
    IsDaylight = (TzResult == TIME_ZONE_ID_DAYLIGHT);
#endif
    PUSH(ValBool(IsDaylight ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WaitableTimerCreate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: WaitableTimerCreate() expects 2 "
                      "arguments (manual_reset, name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    AKSValue ManualVal = POP();
    AKSValue ResVal = ValInt(0);
#ifdef _WIN32
    const char *NameStr =
        (ValKind(NameVal) == VALSTR) ? ValToStrPtr(NameVal)->data : NULL;
    BOOL ManualReset = ValToBool(ManualVal) ? TRUE : FALSE;
    HANDLE TimerHandle = CreateWaitableTimerA(NULL, ManualReset, NameStr);
    ResVal = ValInt((int64_t)(uintptr_t)TimerHandle);
#else
    ResVal = ValInt(1);
#endif
    ValFree(&ManualVal);
    ValFree(&NameVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WaitableTimerSet") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: WaitableTimerSet() expects 3 arguments "
                      "(timer, due_ms, period_ms)\n");
      return -1;
    }
    AKSValue PeriodVal = POP();
    AKSValue DueVal = POP();
    AKSValue TimerVal = POP();
    BOOL SetOk = FALSE;
#ifdef _WIN32
    HANDLE TimerHandle = (HANDLE)(uintptr_t)ValToInt(TimerVal);
    int64_t DueMs = ValToInt(DueVal);
    LONG PeriodMs = (LONG)ValToInt(PeriodVal);
    if (TimerHandle && TimerHandle != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER DueTime;
      DueTime.QuadPart = -10000LL * DueMs;
      SetOk =
          SetWaitableTimer(TimerHandle, &DueTime, PeriodMs, NULL, NULL, FALSE);
    }
#endif
    ValFree(&TimerVal);
    ValFree(&DueVal);
    ValFree(&PeriodVal);
    PUSH(ValBool(SetOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WaitableTimerWait") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: WaitableTimerWait() expects 2 arguments "
                      "(timer, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue TimerVal = POP();
    AKSValue ResVal = ValBool(0);
#ifdef _WIN32
    HANDLE TimerHandle = (HANDLE)(uintptr_t)ValToInt(TimerVal);
    DWORD TimeoutMs = (DWORD)ValToInt(TimeoutVal);
    if (TimerHandle && TimerHandle != INVALID_HANDLE_VALUE) {
      DWORD WaitResult = WaitForSingleObject(TimerHandle, TimeoutMs);
      ResVal = ValBool(WaitResult == WAIT_OBJECT_0 ? 1 : 0);
    }
#else
    ResVal = ValBool(1);
#endif
    ValFree(&TimerVal);
    ValFree(&TimeoutVal);
    PUSH(ResVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WaitableTimerCancel") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: WaitableTimerCancel() expects 1 argument (timer)\n");
      return -1;
    }
    AKSValue TimerVal = POP();
    BOOL CancelOk = FALSE;
#ifdef _WIN32
    HANDLE TimerHandle = (HANDLE)(uintptr_t)ValToInt(TimerVal);
    if (TimerHandle && TimerHandle != INVALID_HANDLE_VALUE) {
      CancelOk = CancelWaitableTimer(TimerHandle);
    }
#endif
    ValFree(&TimerVal);
    PUSH(ValBool(CancelOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WaitableTimerDestroy") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: WaitableTimerDestroy() expects 1 argument (timer)\n");
      return -1;
    }
    AKSValue TimerVal = POP();
#ifdef _WIN32
    HANDLE TimerHandle = (HANDLE)(uintptr_t)ValToInt(TimerVal);
    if (TimerHandle && TimerHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(TimerHandle);
    }
#endif
    ValFree(&TimerVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessTelemetryCommandLine") == 0) {
    if (argc != 0) {
      fprintf(
          stderr,
          "Runtime error: ProcessTelemetryCommandLine() expects 0 arguments\n");
      return -1;
    }
#ifdef _WIN32
    const char *CmdLineStr = GetCommandLineA();
    PUSH(ValStr(CmdLineStr ? CmdLineStr : ""));
#else
    char cmdline_buf[4096] = {0};
    FILE *fcmd = fopen("/proc/self/cmdline", "rb");
    if (fcmd) {
      size_t n = fread(cmdline_buf, 1, sizeof(cmdline_buf) - 1, fcmd);
      fclose(fcmd);
      if (n > 0) {
        for (size_t i = 0; i < n - 1; i++) {
          if (cmdline_buf[i] == '\0')
            cmdline_buf[i] = ' ';
        }
      }
    }
    PUSH(ValStr(cmdline_buf));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessTelemetryExecutablePath") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ProcessTelemetryExecutablePath() expects "
                      "0 arguments\n");
      return -1;
    }
    char ExecutablePathBuf[512] = {0};
#ifdef _WIN32
    GetModuleFileNameA(NULL, ExecutablePathBuf, sizeof(ExecutablePathBuf));
#else
    ssize_t len = readlink("/proc/self/exe", ExecutablePathBuf,
                           sizeof(ExecutablePathBuf) - 1);
    if (len != -1) {
      ExecutablePathBuf[len] = '\0';
    }
#endif
    PUSH(ValStr(ExecutablePathBuf));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessTelemetryProcessId") == 0) {
    if (argc != 0) {
      fprintf(
          stderr,
          "Runtime error: ProcessTelemetryProcessId() expects 0 arguments\n");
      return -1;
    }
    int64_t CurrentPid = 0;
#ifdef _WIN32
    CurrentPid = (int64_t)GetCurrentProcessId();
#else
    CurrentPid = (int64_t)getpid();
#endif
    PUSH(ValInt(CurrentPid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessTelemetryThreadId") == 0) {
    if (argc != 0) {
      fprintf(
          stderr,
          "Runtime error: ProcessTelemetryThreadId() expects 0 arguments\n");
      return -1;
    }
    int64_t CurrentTid = 0;
#ifdef _WIN32
    CurrentTid = (int64_t)GetCurrentThreadId();
#else
    CurrentTid = (int64_t)(uintptr_t)pthread_self();
#endif
    PUSH(ValInt(CurrentTid));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessTelemetryKernelUserCpuTimes") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ProcessTelemetryKernelUserCpuTimes() "
                      "expects 0 arguments\n");
      return -1;
    }
    int64_t KernelTimeMs = 0;
    int64_t UserTimeMs = 0;
#ifdef _WIN32
    FILETIME CreationTime, ExitTime, KernelTime, UserTime;
    if (GetProcessTimes(GetCurrentProcess(), &CreationTime, &ExitTime,
                        &KernelTime, &UserTime)) {
      ULARGE_INTEGER KTime, UTime;
      KTime.LowPart = KernelTime.dwLowDateTime;
      KTime.HighPart = KernelTime.dwHighDateTime;
      UTime.LowPart = UserTime.dwLowDateTime;
      UTime.HighPart = UserTime.dwHighDateTime;
      KernelTimeMs = (int64_t)(KTime.QuadPart / 10000);
      UserTimeMs = (int64_t)(UTime.QuadPart / 10000);
    }
#endif
    AKSValue ResStruct = ValStruct("ProcessTelemetryCpuTimesResult");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("kernel_ms");
    StructPtr->fieldvals[0] = ValInt(KernelTimeMs);
    StructPtr->fieldnames[1] = strdup("user_ms");
    StructPtr->fieldvals[1] = ValInt(UserTimeMs);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadWriteLockNew") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: ReadWriteLockNew() expects 0 arguments\n");
      return -1;
    }
#ifdef _WIN32
    PSRWLOCK LockPtr = (PSRWLOCK)malloc(sizeof(SRWLOCK));
    if (LockPtr) {
      InitializeSRWLock(LockPtr);
    }
    PUSH(ValInt((int64_t)(uintptr_t)LockPtr));
#else
    pthread_rwlock_t *LockPtr =
        (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t));
    if (LockPtr && pthread_rwlock_init(LockPtr, NULL) != 0) {
      free(LockPtr);
      LockPtr = NULL;
    }
    PUSH(ValInt((int64_t)(uintptr_t)LockPtr));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadWriteLockLockShared") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockLockShared() expects 1 "
                      "argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
#ifdef _WIN32
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      AcquireSRWLockShared(LockPtr);
    }
#else
    pthread_rwlock_t *LockPtr = (pthread_rwlock_t *)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      pthread_rwlock_rdlock(LockPtr);
    }
#endif
    ValFree(&LockVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadWriteLockUnlockShared") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockUnlockShared() expects 1 "
                      "argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
#ifdef _WIN32
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      ReleaseSRWLockShared(LockPtr);
    }
#else
    pthread_rwlock_t *LockPtr = (pthread_rwlock_t *)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      pthread_rwlock_unlock(LockPtr);
    }
#endif
    ValFree(&LockVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadWriteLockLockExclusive") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockLockExclusive() expects 1 "
                      "argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
#ifdef _WIN32
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      AcquireSRWLockExclusive(LockPtr);
    }
#else
    pthread_rwlock_t *LockPtr = (pthread_rwlock_t *)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      pthread_rwlock_wrlock(LockPtr);
    }
#endif
    ValFree(&LockVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadWriteLockUnlockExclusive") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockUnlockExclusive() expects 1 "
                      "argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
#ifdef _WIN32
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      ReleaseSRWLockExclusive(LockPtr);
    }
#else
    pthread_rwlock_t *LockPtr = (pthread_rwlock_t *)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      pthread_rwlock_unlock(LockPtr);
    }
#endif
    ValFree(&LockVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadWriteLockTryLockShared") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockTryLockShared() expects 1 "
                      "argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
    BOOL LockOk = FALSE;
#ifdef _WIN32
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      LockOk = TryAcquireSRWLockShared(LockPtr);
    }
#else
    pthread_rwlock_t *LockPtr = (pthread_rwlock_t *)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      LockOk = (pthread_rwlock_tryrdlock(LockPtr) == 0) ? TRUE : FALSE;
    }
#endif
    ValFree(&LockVal);
    PUSH(ValBool(LockOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadWriteLockTryLockExclusive") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ReadWriteLockTryLockExclusive() expects "
                      "1 argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
    BOOL LockOk = FALSE;
#ifdef _WIN32
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      LockOk = TryAcquireSRWLockExclusive(LockPtr);
    }
#else
    pthread_rwlock_t *LockPtr = (pthread_rwlock_t *)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      LockOk = (pthread_rwlock_trywrlock(LockPtr) == 0) ? TRUE : FALSE;
    }
#endif
    ValFree(&LockVal);
    PUSH(ValBool(LockOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadWriteLockDestroy") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: ReadWriteLockDestroy() expects 1 argument (lock)\n");
      return -1;
    }
    AKSValue LockVal = POP();
#ifdef _WIN32
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      free(LockPtr);
    }
#else
    pthread_rwlock_t *LockPtr = (pthread_rwlock_t *)(uintptr_t)ValToInt(LockVal);
    if (LockPtr) {
      pthread_rwlock_destroy(LockPtr);
      free(LockPtr);
    }
#endif
    ValFree(&LockVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemDisplayPrimaryResolution") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemDisplayPrimaryResolution() expects "
                      "0 arguments\n");
      return -1;
    }
    int ScreenWidth = 1920;
    int ScreenHeight = 1080;
#ifdef _WIN32
    ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
    ScreenHeight = GetSystemMetrics(SM_CYSCREEN);
#endif
    AKSValue ResStruct = ValStruct("DisplayResolution");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("width");
    StructPtr->fieldvals[0] = ValInt(ScreenWidth);
    StructPtr->fieldnames[1] = strdup("height");
    StructPtr->fieldvals[1] = ValInt(ScreenHeight);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemDisplayVirtualScreenBounds") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemDisplayVirtualScreenBounds() "
                      "expects 0 arguments\n");
      return -1;
    }
    int VirtLeft = 0, VirtTop = 0, VirtWidth = 1920, VirtHeight = 1080;
#ifdef _WIN32
    VirtLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    VirtTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    VirtWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    VirtHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
#endif
    AKSValue ResStruct = ValStruct("DisplayBounds");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 4;
    StructPtr->fieldnames = malloc(4 * sizeof(char *));
    StructPtr->fieldvals = malloc(4 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("left");
    StructPtr->fieldvals[0] = ValInt(VirtLeft);
    StructPtr->fieldnames[1] = strdup("top");
    StructPtr->fieldvals[1] = ValInt(VirtTop);
    StructPtr->fieldnames[2] = strdup("width");
    StructPtr->fieldvals[2] = ValInt(VirtWidth);
    StructPtr->fieldnames[3] = strdup("height");
    StructPtr->fieldvals[3] = ValInt(VirtHeight);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemDisplayWorkArea") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: SystemDisplayWorkArea() expects 0 arguments\n");
      return -1;
    }
    int WorkWidth = 1920, WorkHeight = 1080;
#ifdef _WIN32
    RECT WorkAreaRect;
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &WorkAreaRect, 0)) {
      WorkWidth = WorkAreaRect.right - WorkAreaRect.left;
      WorkHeight = WorkAreaRect.bottom - WorkAreaRect.top;
    }
#endif
    AKSValue ResStruct = ValStruct("DisplayWorkArea");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("width");
    StructPtr->fieldvals[0] = ValInt(WorkWidth);
    StructPtr->fieldnames[1] = strdup("height");
    StructPtr->fieldvals[1] = ValInt(WorkHeight);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemDisplayMonitorCount") == 0) {
    if (argc != 0) {
      fprintf(
          stderr,
          "Runtime error: SystemDisplayMonitorCount() expects 0 arguments\n");
      return -1;
    }
    int MonitorCount = 1;
#ifdef _WIN32
    MonitorCount = GetSystemMetrics(SM_CMONITORS);
    if (MonitorCount <= 0)
      MonitorCount = 1;
#endif
    PUSH(ValInt(MonitorCount));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedSpinlockOpenOrCreate") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SharedSpinlockOpenOrCreate() expects 1 "
                      "argument (name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    HANDLE MapHandle = NULL;
    void *ViewPtr = NULL;
#ifdef _WIN32
    if (ValKind(NameVal) == VALSTR) {
      const char *NameStr = ValToStrPtr(NameVal)->data;
      MapHandle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                     0, 64, NameStr);
      if (MapHandle) {
        ViewPtr = MapViewOfFile(MapHandle, FILE_MAP_ALL_ACCESS, 0, 0, 64);
      }
    }
#endif
    AKSValue ResStruct = ValStruct("SharedSpinlockHandle");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("map_id");
    StructPtr->fieldvals[0] = ValInt((int64_t)(uintptr_t)MapHandle);
    StructPtr->fieldnames[1] = strdup("view_id");
    StructPtr->fieldvals[1] = ValInt((int64_t)(uintptr_t)ViewPtr);
    ValFree(&NameVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedSpinlockAcquire") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedSpinlockAcquire() expects 2 "
                      "arguments (view_ptr, max_spins)\n");
      return -1;
    }
    AKSValue SpinVal = POP();
    AKSValue ViewVal = POP();
    BOOL Acquired = FALSE;
#ifdef _WIN32
    volatile LONG *LockPtr = (volatile LONG *)(uintptr_t)ValToInt(ViewVal);
    int MaxSpins = (int)ValToInt(SpinVal);
    if (MaxSpins <= 0)
      MaxSpins = 1000000;
    if (LockPtr) {
      for (int Spin = 0; Spin < MaxSpins; Spin++) {
        if (*LockPtr == 0 && InterlockedExchange(LockPtr, 1) == 0) {
          Acquired = TRUE;
          break;
        }
        _mm_pause();
      }
    }
#endif
    ValFree(&ViewVal);
    ValFree(&SpinVal);
    PUSH(ValBool(Acquired ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedSpinlockTryAcquire") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SharedSpinlockTryAcquire() expects 1 "
                      "argument (view_ptr)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
    BOOL Acquired = FALSE;
#ifdef _WIN32
    volatile LONG *LockPtr = (volatile LONG *)(uintptr_t)ValToInt(ViewVal);
    if (LockPtr) {
      Acquired = (InterlockedExchange(LockPtr, 1) == 0);
    }
#endif
    ValFree(&ViewVal);
    PUSH(ValBool(Acquired ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedSpinlockRelease") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SharedSpinlockRelease() expects 1 "
                      "argument (view_ptr)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
#ifdef _WIN32
    volatile LONG *LockPtr = (volatile LONG *)(uintptr_t)ValToInt(ViewVal);
    if (LockPtr) {
      InterlockedExchange(LockPtr, 0);
    }
#endif
    ValFree(&ViewVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedSpinlockDestroy") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedSpinlockDestroy() expects 2 "
                      "arguments (map_id, view_id)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
    AKSValue MapVal = POP();
#ifdef _WIN32
    HANDLE MapHandle = (HANDLE)(uintptr_t)ValToInt(MapVal);
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    if (ViewPtr) {
      UnmapViewOfFile(ViewPtr);
    }
    if (MapHandle && MapHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(MapHandle);
    }
#endif
    ValFree(&MapVal);
    ValFree(&ViewVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadBarrierNew") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadBarrierNew() expects 1 argument "
                      "(total_threads)\n");
      return -1;
    }
    AKSValue CountVal = POP();
#ifdef _WIN32
    PSYNCHRONIZATION_BARRIER BarrierPtr = NULL;
    DWORD TotalThreads = (DWORD)ValToInt(CountVal);
    if (TotalThreads < 1)
      TotalThreads = 1;
    BarrierPtr =
        (PSYNCHRONIZATION_BARRIER)malloc(sizeof(SYNCHRONIZATION_BARRIER));
    if (BarrierPtr) {
      InitializeSynchronizationBarrier(BarrierPtr, TotalThreads, -1);
    }
    PUSH(ValInt((int64_t)(uintptr_t)BarrierPtr));
#else
    int total = (int)ValToInt(CountVal);
    if (total < 1) total = 1;
    AksPosixBarrier *bp = (AksPosixBarrier *)calloc(1, sizeof(AksPosixBarrier));
    if (bp) {
      pthread_mutex_init(&bp->mtx, NULL);
      pthread_cond_init(&bp->cv, NULL);
      bp->threshold = total;
      bp->count = 0;
      bp->generation = 0;
    }
    PUSH(ValInt((int64_t)(uintptr_t)bp));
#endif
    ValFree(&CountVal);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadBarrierWait") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: ThreadBarrierWait() expects 1 argument (barrier)\n");
      return -1;
    }
    AKSValue BarrierVal = POP();
    BOOL IsLeader = FALSE;
#ifdef _WIN32
    PSYNCHRONIZATION_BARRIER BarrierPtr =
        (PSYNCHRONIZATION_BARRIER)(uintptr_t)ValToInt(BarrierVal);
    if (BarrierPtr) {
      IsLeader = EnterSynchronizationBarrier(BarrierPtr, 0);
    }
#else
    AksPosixBarrier *bp = (AksPosixBarrier *)(uintptr_t)ValToInt(BarrierVal);
    if (bp) {
      pthread_mutex_lock(&bp->mtx);
      int gen = bp->generation;
      bp->count++;
      if (bp->count >= bp->threshold) {
        bp->count = 0;
        bp->generation++;
        pthread_cond_broadcast(&bp->cv);
        IsLeader = TRUE;
      } else {
        while (gen == bp->generation)
          pthread_cond_wait(&bp->cv, &bp->mtx);
      }
      pthread_mutex_unlock(&bp->mtx);
    }
#endif
    ValFree(&BarrierVal);
    PUSH(ValBool(IsLeader ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadBarrierDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadBarrierDestroy() expects 1 "
                      "argument (barrier)\n");
      return -1;
    }
    AKSValue BarrierVal = POP();
#ifdef _WIN32
    PSYNCHRONIZATION_BARRIER BarrierPtr =
        (PSYNCHRONIZATION_BARRIER)(uintptr_t)ValToInt(BarrierVal);
    if (BarrierPtr) {
      DeleteSynchronizationBarrier(BarrierPtr);
      free(BarrierPtr);
    }
#else
    AksPosixBarrier *bp = (AksPosixBarrier *)(uintptr_t)ValToInt(BarrierVal);
    if (bp) {
      pthread_cond_destroy(&bp->cv);
      pthread_mutex_destroy(&bp->mtx);
      free(bp);
    }
#endif
    ValFree(&BarrierVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JobObjectCreate") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: JobObjectCreate() expects 1 argument (name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    HANDLE JobHandle = NULL;
#ifdef _WIN32
    const char *NameStr =
        (ValKind(NameVal) == VALSTR) ? ValToStrPtr(NameVal)->data : NULL;
    JobHandle = CreateJobObjectA(NULL, NameStr);
#endif
    ValFree(&NameVal);
    PUSH(ValInt((int64_t)(uintptr_t)JobHandle));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JobObjectAssignProcess") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: JobObjectAssignProcess() expects 2 "
                      "arguments (job, pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    AKSValue JobVal = POP();
    BOOL AssignOk = FALSE;
#ifdef _WIN32
    HANDLE JobHandle = (HANDLE)(uintptr_t)ValToInt(JobVal);
    DWORD Pid = (DWORD)ValToInt(PidVal);
    if (Pid == 0)
      Pid = GetCurrentProcessId();
    HANDLE ProcHandle =
        OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, Pid);
    if (ProcHandle && JobHandle) {
      AssignOk = AssignProcessToJobObject(JobHandle, ProcHandle);
      CloseHandle(ProcHandle);
    }
#endif
    ValFree(&JobVal);
    ValFree(&PidVal);
    PUSH(ValBool(AssignOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JobObjectSetMemoryLimit") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: JobObjectSetMemoryLimit() expects 2 "
                      "arguments (job, max_bytes)\n");
      return -1;
    }
    AKSValue BytesVal = POP();
    AKSValue JobVal = POP();
    BOOL LimitOk = FALSE;
#ifdef _WIN32
    HANDLE JobHandle = (HANDLE)(uintptr_t)ValToInt(JobVal);
    size_t MaxBytes = (size_t)ValToInt(BytesVal);
    if (JobHandle) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION LimitInfo = {0};
      LimitInfo.BasicLimitInformation.LimitFlags =
          JOB_OBJECT_LIMIT_PROCESS_MEMORY;
      LimitInfo.ProcessMemoryLimit = MaxBytes;
      LimitOk =
          SetInformationJobObject(JobHandle, JobObjectExtendedLimitInformation,
                                  &LimitInfo, sizeof(LimitInfo));
    }
#endif
    ValFree(&JobVal);
    ValFree(&BytesVal);
    PUSH(ValBool(LimitOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JobObjectTerminateAll") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: JobObjectTerminateAll() expects 2 "
                      "arguments (job, exit_code)\n");
      return -1;
    }
    AKSValue CodeVal = POP();
    AKSValue JobVal = POP();
    BOOL TermOk = FALSE;
#ifdef _WIN32
    HANDLE JobHandle = (HANDLE)(uintptr_t)ValToInt(JobVal);
    UINT ExitCode = (UINT)ValToInt(CodeVal);
    if (JobHandle) {
      TermOk = TerminateJobObject(JobHandle, ExitCode);
    }
#endif
    ValFree(&JobVal);
    ValFree(&CodeVal);
    PUSH(ValBool(TermOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "JobObjectDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: JobObjectDestroy() expects 1 argument (job)\n");
      return -1;
    }
    AKSValue JobVal = POP();
#ifdef _WIN32
    HANDLE JobHandle = (HANDLE)(uintptr_t)ValToInt(JobVal);
    if (JobHandle && JobHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(JobHandle);
    }
#endif
    ValFree(&JobVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedRingBufferCreate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedRingBufferCreate() expects 2 "
                      "arguments (name, capacity_bytes)\n");
      return -1;
    }
    AKSValue CapVal = POP();
    AKSValue NameVal = POP();
    uint32_t Capacity = (uint32_t)ValToInt(CapVal);
    if (Capacity < 64)
      Capacity = 64;
    HANDLE MapHandle = NULL;
    void *ViewPtr = NULL;
#ifdef _WIN32
    size_t TotalSize = sizeof(uint32_t) * 4 + Capacity;
    const char *NameStr = (ValKind(NameVal) == VALSTR)
                              ? ValToStrPtr(NameVal)->data
                              : "Local\\AksRingBuf";
    MapHandle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                   0, (DWORD)TotalSize, NameStr);
    if (MapHandle) {
      ViewPtr = MapViewOfFile(MapHandle, FILE_MAP_ALL_ACCESS, 0, 0, TotalSize);
      if (ViewPtr) {
        uint32_t *Hdr = (uint32_t *)ViewPtr;
        if (Hdr[2] == 0) {
          Hdr[0] = 0; // head
          Hdr[1] = 0; // tail
          Hdr[2] = Capacity;
        }
      }
    }
#endif
    AKSValue ResStruct = ValStruct("SharedRingBufferHandle");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 3;
    StructPtr->fieldnames = malloc(3 * sizeof(char *));
    StructPtr->fieldvals = malloc(3 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("map_id");
    StructPtr->fieldvals[0] = ValInt((int64_t)(uintptr_t)MapHandle);
    StructPtr->fieldnames[1] = strdup("view_id");
    StructPtr->fieldvals[1] = ValInt((int64_t)(uintptr_t)ViewPtr);
    StructPtr->fieldnames[2] = strdup("capacity");
    StructPtr->fieldvals[2] = ValInt(Capacity);
    ValFree(&NameVal);
    ValFree(&CapVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedRingBufferWrite") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedRingBufferWrite() expects 2 "
                      "arguments (view_ptr, hex_str)\n");
      return -1;
    }
    AKSValue HexVal = POP();
    AKSValue ViewVal = POP();
    size_t BytesWritten = 0;
#ifdef _WIN32
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    if (ViewPtr && ValKind(HexVal) == VALSTR) {
      uint32_t *Hdr = (uint32_t *)ViewPtr;
      uint32_t Head = Hdr[0];
      uint32_t Tail = Hdr[1];
      uint32_t Capacity = Hdr[2];
      uint8_t *Buffer = (uint8_t *)(Hdr + 4);
      uint32_t FreeSpace =
          (Head > Tail) ? (Head - Tail - 1) : (Capacity - (Tail - Head) - 1);
      const char *HexData = ValToStrPtr(HexVal)->data;
      size_t HexLen = (size_t)ValToStrPtr(HexVal)->len;
      size_t InputBytes = HexLen / 2;
      size_t ToWrite = (InputBytes < FreeSpace) ? InputBytes : FreeSpace;
      for (size_t i = 0; i < ToWrite; i++) {
        unsigned int ByteVal = 0;
        sscanf(&HexData[i * 2], "%02x", &ByteVal);
        Buffer[Tail] = (uint8_t)ByteVal;
        Tail = (Tail + 1) % Capacity;
      }
      Hdr[1] = Tail;
      BytesWritten = ToWrite;
    }
#endif
    ValFree(&ViewVal);
    ValFree(&HexVal);
    PUSH(ValInt((int64_t)BytesWritten));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedRingBufferRead") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedRingBufferRead() expects 2 "
                      "arguments (view_ptr, max_bytes)\n");
      return -1;
    }
    AKSValue MaxVal = POP();
    AKSValue ViewVal = POP();
    char HexOut[4097] = {0};
#ifdef _WIN32
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    size_t MaxBytes = (size_t)ValToInt(MaxVal);
    if (MaxBytes > 2048)
      MaxBytes = 2048;
    if (ViewPtr) {
      uint32_t *Hdr = (uint32_t *)ViewPtr;
      uint32_t Head = Hdr[0];
      uint32_t Tail = Hdr[1];
      uint32_t Capacity = Hdr[2];
      uint8_t *Buffer = (uint8_t *)(Hdr + 4);
      uint32_t Available =
          (Tail >= Head) ? (Tail - Head) : (Capacity - (Head - Tail));
      size_t ToRead = (MaxBytes < Available) ? MaxBytes : Available;
      static const char HexChars[] = "0123456789abcdef";
      for (size_t i = 0; i < ToRead; i++) {
        uint8_t B = Buffer[Head];
        HexOut[i * 2] = HexChars[(B >> 4) & 0x0F];
        HexOut[i * 2 + 1] = HexChars[B & 0x0F];
        Head = (Head + 1) % Capacity;
      }
      HexOut[ToRead * 2] = '\0';
      Hdr[0] = Head;
    }
#endif
    ValFree(&ViewVal);
    ValFree(&MaxVal);
    PUSH(ValStr(HexOut));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedRingBufferAvailableRead") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SharedRingBufferAvailableRead() expects "
                      "1 argument (view_ptr)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
    uint32_t Available = 0;
#ifdef _WIN32
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    if (ViewPtr) {
      uint32_t *Hdr = (uint32_t *)ViewPtr;
      uint32_t Head = Hdr[0];
      uint32_t Tail = Hdr[1];
      uint32_t Capacity = Hdr[2];
      Available = (Tail >= Head) ? (Tail - Head) : (Capacity - (Head - Tail));
    }
#endif
    ValFree(&ViewVal);
    PUSH(ValInt(Available));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedRingBufferAvailableWrite") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SharedRingBufferAvailableWrite() expects "
                      "1 argument (view_ptr)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
    uint32_t FreeSpace = 0;
#ifdef _WIN32
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    if (ViewPtr) {
      uint32_t *Hdr = (uint32_t *)ViewPtr;
      uint32_t Head = Hdr[0];
      uint32_t Tail = Hdr[1];
      uint32_t Capacity = Hdr[2];
      FreeSpace =
          (Head > Tail) ? (Head - Tail - 1) : (Capacity - (Tail - Head) - 1);
    }
#endif
    ValFree(&ViewVal);
    PUSH(ValInt(FreeSpace));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SharedRingBufferDestroy") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SharedRingBufferDestroy() expects 2 "
                      "arguments (map_id, view_id)\n");
      return -1;
    }
    AKSValue ViewVal = POP();
    AKSValue MapVal = POP();
#ifdef _WIN32
    HANDLE MapHandle = (HANDLE)(uintptr_t)ValToInt(MapVal);
    void *ViewPtr = (void *)(uintptr_t)ValToInt(ViewVal);
    if (ViewPtr)
      UnmapViewOfFile(ViewPtr);
    if (MapHandle && MapHandle != INVALID_HANDLE_VALUE)
      CloseHandle(MapHandle);
#endif
    ValFree(&MapVal);
    ValFree(&ViewVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemIdentityUserName") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: SystemIdentityUserName() expects 0 arguments\n");
      return -1;
    }
    char UserBuf[256] = {0};
#ifdef _WIN32
    DWORD UserLen = sizeof(UserBuf);
    GetUserNameA(UserBuf, &UserLen);
#endif
    PUSH(ValStr(UserBuf));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemIdentityIsAdmin") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: SystemIdentityIsAdmin() expects 0 arguments\n");
      return -1;
    }
    BOOL IsAdmin = FALSE;
#ifdef _WIN32
    IsAdmin = IsUserAnAdmin();
#endif
    PUSH(ValBool(IsAdmin ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemIdentityProcessIntegrityLevel") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemIdentityProcessIntegrityLevel() "
                      "expects 0 arguments\n");
      return -1;
    }
    const char *IntegrityStr = "Medium";
#ifdef _WIN32
    HANDLE TokenHandle = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &TokenHandle)) {
      DWORD LengthNeeded = 0;
      GetTokenInformation(TokenHandle, TokenIntegrityLevel, NULL, 0,
                          &LengthNeeded);
      if (LengthNeeded > 0) {
        PTOKEN_MANDATORY_LABEL Label =
            (PTOKEN_MANDATORY_LABEL)malloc(LengthNeeded);
        if (Label && GetTokenInformation(TokenHandle, TokenIntegrityLevel,
                                         Label, LengthNeeded, &LengthNeeded)) {
          DWORD SubAuthorityCount = *GetSidSubAuthorityCount(Label->Label.Sid);
          DWORD IntegrityLevel =
              *GetSidSubAuthority(Label->Label.Sid, SubAuthorityCount - 1);
          if (IntegrityLevel >= SECURITY_MANDATORY_SYSTEM_RID)
            IntegrityStr = "System";
          else if (IntegrityLevel >= SECURITY_MANDATORY_HIGH_RID)
            IntegrityStr = "High";
          else if (IntegrityLevel >= SECURITY_MANDATORY_MEDIUM_RID)
            IntegrityStr = "Medium";
          else
            IntegrityStr = "Low";
        }
        if (Label)
          free(Label);
      }
      CloseHandle(TokenHandle);
    }
#endif
    PUSH(ValStr(IntegrityStr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConditionVariableNew") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: ConditionVariableNew() expects 0 arguments\n");
      return -1;
    }
#ifdef _WIN32
    PCONDITION_VARIABLE CvPtr =
        (PCONDITION_VARIABLE)malloc(sizeof(CONDITION_VARIABLE));
    if (CvPtr) {
      InitializeConditionVariable(CvPtr);
    }
    PUSH(ValInt((int64_t)(uintptr_t)CvPtr));
#else
    AksPosixCond *CvPtr = (AksPosixCond *)calloc(1, sizeof(AksPosixCond));
    if (CvPtr) {
      pthread_mutex_init(&CvPtr->mtx, NULL);
      pthread_cond_init(&CvPtr->cv, NULL);
    }
    PUSH(ValInt((int64_t)(uintptr_t)CvPtr));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConditionVariableWaitSRW") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: ConditionVariableWaitSRW() expects 4 "
                      "arguments (cv, lock, timeout_ms, is_shared)\n");
      return -1;
    }
    AKSValue SharedVal = POP();
    AKSValue TimeoutVal = POP();
    AKSValue LockVal = POP();
    AKSValue CvVal = POP();
    BOOL WaitOk = FALSE;
#ifdef _WIN32
    PCONDITION_VARIABLE CvPtr = (PCONDITION_VARIABLE)(uintptr_t)ValToInt(CvVal);
    PSRWLOCK LockPtr = (PSRWLOCK)(uintptr_t)ValToInt(LockVal);
    DWORD Timeout = (DWORD)ValToInt(TimeoutVal);
    ULONG Flags = ValToBool(SharedVal) ? CONDITION_VARIABLE_LOCKMODE_SHARED : 0;
    if (CvPtr && LockPtr) {
      WaitOk = SleepConditionVariableSRW(CvPtr, LockPtr, Timeout, Flags);
    }
#else
    AksPosixCond *CvPtr = (AksPosixCond *)(uintptr_t)ValToInt(CvVal);
    pthread_rwlock_t *LockPtr = (pthread_rwlock_t *)(uintptr_t)ValToInt(LockVal);
    int64_t tmo_ms = ValToInt(TimeoutVal);
    int is_shared = ValToBool(SharedVal);
    if (CvPtr && LockPtr) {
      pthread_mutex_lock(&CvPtr->mtx);
      pthread_rwlock_unlock(LockPtr);
      if (tmo_ms < 0) {
        while (!CvPtr->signaled)
          pthread_cond_wait(&CvPtr->cv, &CvPtr->mtx);
        CvPtr->signaled = 0;
        WaitOk = TRUE;
      } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += tmo_ms / 1000;
        ts.tv_nsec += (tmo_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
          ts.tv_sec++;
          ts.tv_nsec -= 1000000000L;
        }
        int rc = 0;
        while (!CvPtr->signaled && rc == 0)
          rc = pthread_cond_timedwait(&CvPtr->cv, &CvPtr->mtx, &ts);
        if (CvPtr->signaled) {
          CvPtr->signaled = 0;
          WaitOk = TRUE;
        }
      }
      pthread_mutex_unlock(&CvPtr->mtx);
      if (is_shared) pthread_rwlock_rdlock(LockPtr);
      else pthread_rwlock_wrlock(LockPtr);
    }
#endif
    ValFree(&CvVal);
    ValFree(&LockVal);
    ValFree(&TimeoutVal);
    ValFree(&SharedVal);
    PUSH(ValBool(WaitOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConditionVariableNotifyOne") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ConditionVariableNotifyOne() expects 1 "
                      "argument (cv)\n");
      return -1;
    }
    AKSValue CvVal = POP();
#ifdef _WIN32
    PCONDITION_VARIABLE CvPtr = (PCONDITION_VARIABLE)(uintptr_t)ValToInt(CvVal);
    if (CvPtr) {
      WakeConditionVariable(CvPtr);
    }
#else
    AksPosixCond *CvPtr = (AksPosixCond *)(uintptr_t)ValToInt(CvVal);
    if (CvPtr) {
      pthread_mutex_lock(&CvPtr->mtx);
      CvPtr->signaled = 1;
      pthread_cond_signal(&CvPtr->cv);
      pthread_mutex_unlock(&CvPtr->mtx);
    }
#endif
    ValFree(&CvVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConditionVariableNotifyAll") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ConditionVariableNotifyAll() expects 1 "
                      "argument (cv)\n");
      return -1;
    }
    AKSValue CvVal = POP();
#ifdef _WIN32
    PCONDITION_VARIABLE CvPtr = (PCONDITION_VARIABLE)(uintptr_t)ValToInt(CvVal);
    if (CvPtr) {
      WakeAllConditionVariable(CvPtr);
    }
#else
    AksPosixCond *CvPtr = (AksPosixCond *)(uintptr_t)ValToInt(CvVal);
    if (CvPtr) {
      pthread_mutex_lock(&CvPtr->mtx);
      CvPtr->signaled = 1;
      pthread_cond_broadcast(&CvPtr->cv);
      pthread_mutex_unlock(&CvPtr->mtx);
    }
#endif
    ValFree(&CvVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConditionVariableDestroy") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ConditionVariableDestroy() expects 1 "
                      "argument (cv)\n");
      return -1;
    }
    AKSValue CvVal = POP();
#ifdef _WIN32
    PCONDITION_VARIABLE CvPtr = (PCONDITION_VARIABLE)(uintptr_t)ValToInt(CvVal);
    if (CvPtr) {
      free(CvPtr);
    }
#else
    AksPosixCond *CvPtr = (AksPosixCond *)(uintptr_t)ValToInt(CvVal);
    if (CvPtr) {
      pthread_cond_destroy(&CvPtr->cv);
      pthread_mutex_destroy(&CvPtr->mtx);
      free(CvPtr);
    }
#endif
    ValFree(&CvVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessPrioritySetClass") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessPrioritySetClass() expects 1 "
                      "argument (priority_str)\n");
      return -1;
    }
    AKSValue PrioVal = POP();
    BOOL SetOk = FALSE;
#ifdef _WIN32
    if (ValKind(PrioVal) == VALSTR) {
      const char *PrioStr = ValToStrPtr(PrioVal)->data;
      DWORD Class = NORMAL_PRIORITY_CLASS;
      if (_stricmp(PrioStr, "idle") == 0)
        Class = IDLE_PRIORITY_CLASS;
      else if (_stricmp(PrioStr, "below_normal") == 0)
        Class = BELOW_NORMAL_PRIORITY_CLASS;
      else if (_stricmp(PrioStr, "normal") == 0)
        Class = NORMAL_PRIORITY_CLASS;
      else if (_stricmp(PrioStr, "above_normal") == 0)
        Class = ABOVE_NORMAL_PRIORITY_CLASS;
      else if (_stricmp(PrioStr, "high") == 0)
        Class = HIGH_PRIORITY_CLASS;
      else if (_stricmp(PrioStr, "realtime") == 0)
        Class = REALTIME_PRIORITY_CLASS;
      SetOk = SetPriorityClass(GetCurrentProcess(), Class);
    }
#endif
    ValFree(&PrioVal);
    PUSH(ValBool(SetOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessPriorityGetClass") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: ProcessPriorityGetClass() expects 0 arguments\n");
      return -1;
    }
    const char *PrioStr = "normal";
#ifdef _WIN32
    DWORD Class = GetPriorityClass(GetCurrentProcess());
    if (Class == IDLE_PRIORITY_CLASS)
      PrioStr = "idle";
    else if (Class == BELOW_NORMAL_PRIORITY_CLASS)
      PrioStr = "below_normal";
    else if (Class == NORMAL_PRIORITY_CLASS)
      PrioStr = "normal";
    else if (Class == ABOVE_NORMAL_PRIORITY_CLASS)
      PrioStr = "above_normal";
    else if (Class == HIGH_PRIORITY_CLASS)
      PrioStr = "high";
    else if (Class == REALTIME_PRIORITY_CLASS)
      PrioStr = "realtime";
#endif
    PUSH(ValStr(PrioStr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessPrioritySetEcoMode") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessPrioritySetEcoMode() expects 1 "
                      "argument (enabled)\n");
      return -1;
    }
    AKSValue EcoVal = POP();
    BOOL EcoOk = TRUE;
#ifdef _WIN32
    BOOL Enable = ValToBool(EcoVal);
    PROCESS_POWER_THROTTLING_STATE ThrottlingState = {0};
    ThrottlingState.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    ThrottlingState.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    ThrottlingState.StateMask =
        Enable ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                          &ThrottlingState, sizeof(ThrottlingState));
#endif
    ValFree(&EcoVal);
    PUSH(ValBool(EcoOk ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "VirtualMemoryMapQueryAddress") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: VirtualMemoryMapQueryAddress() expects 1 "
                      "argument (addr)\n");
      return -1;
    }
    AKSValue AddrVal = POP();
    int64_t BaseAddr = 0;
    int64_t RegionSize = 0;
    const char *StateStr = "free";
    const char *ProtectStr = "noaccess";
#ifdef _WIN32
    void *Address = (void *)(uintptr_t)ValToInt(AddrVal);
    MEMORY_BASIC_INFORMATION Mbi = {0};
    if (VirtualQuery(Address, &Mbi, sizeof(Mbi))) {
      BaseAddr = (int64_t)(uintptr_t)Mbi.BaseAddress;
      RegionSize = (int64_t)Mbi.RegionSize;
      if (Mbi.State == MEM_COMMIT)
        StateStr = "commit";
      else if (Mbi.State == MEM_RESERVE)
        StateStr = "reserve";
      else
        StateStr = "free";

      if (Mbi.Protect & PAGE_READWRITE)
        ProtectStr = "readwrite";
      else if (Mbi.Protect & PAGE_READONLY)
        ProtectStr = "readonly";
      else if (Mbi.Protect & PAGE_EXECUTE_READWRITE)
        ProtectStr = "exec_readwrite";
      else if (Mbi.Protect & PAGE_EXECUTE_READ)
        ProtectStr = "exec_read";
      else if (Mbi.Protect & PAGE_EXECUTE)
        ProtectStr = "exec";
      else
        ProtectStr = "noaccess";
    }
#endif
    AKSValue ResStruct = ValStruct("VirtualMemoryInfo");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 4;
    StructPtr->fieldnames = malloc(4 * sizeof(char *));
    StructPtr->fieldvals = malloc(4 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("base_addr");
    StructPtr->fieldvals[0] = ValInt(BaseAddr);
    StructPtr->fieldnames[1] = strdup("region_size");
    StructPtr->fieldvals[1] = ValInt(RegionSize);
    StructPtr->fieldnames[2] = strdup("mem_state");
    StructPtr->fieldvals[2] = ValStr(StateStr);
    StructPtr->fieldnames[3] = strdup("protect");
    StructPtr->fieldvals[3] = ValStr(ProtectStr);
    ValFree(&AddrVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "VirtualMemoryMapPageSize") == 0) {
    if (argc != 0) {
      fprintf(
          stderr,
          "Runtime error: VirtualMemoryMapPageSize() expects 0 arguments\n");
      return -1;
    }
    int64_t PageSize = 4096;
#ifdef _WIN32
    SYSTEM_INFO SysInfo;
    GetSystemInfo(&SysInfo);
    PageSize = (int64_t)SysInfo.dwPageSize;
#endif
    PUSH(ValInt(PageSize));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "VirtualMemoryMapAllocationGranularity") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: VirtualMemoryMapAllocationGranularity() "
                      "expects 0 arguments\n");
      return -1;
    }
    int64_t Granularity = 65536;
#ifdef _WIN32
    SYSTEM_INFO SysInfo;
    GetSystemInfo(&SysInfo);
    Granularity = (int64_t)SysInfo.dwAllocationGranularity;
#endif
    PUSH(ValInt(Granularity));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadPoolWorkCreate") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: ThreadPoolWorkCreate() expects 0 arguments\n");
      return -1;
    }
#ifdef _WIN32
    PTP_WORK Work = CreateThreadpoolWork(AksThreadPoolWorkCallback, NULL, NULL);
    PUSH(ValInt((int64_t)(uintptr_t)Work));
#else
    void *Work = calloc(1, sizeof(int));
    PUSH(ValInt((int64_t)(uintptr_t)Work));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadPoolWorkSubmit") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadPoolWorkSubmit() expects 1 "
                      "argument (work_id)\n");
      return -1;
    }
    AKSValue WorkVal = POP();
#ifdef _WIN32
    PTP_WORK Work = (PTP_WORK)(uintptr_t)ValToInt(WorkVal);
    if (Work)
      SubmitThreadpoolWork(Work);
#endif
    ValFree(&WorkVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadPoolWorkWait") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ThreadPoolWorkWait() expects 2 arguments "
                      "(work_id, cancel_pending)\n");
      return -1;
    }
    AKSValue CancelVal = POP();
    AKSValue WorkVal = POP();
#ifdef _WIN32
    PTP_WORK Work = (PTP_WORK)(uintptr_t)ValToInt(WorkVal);
    BOOL Cancel = ValToBool(CancelVal);
    if (Work)
      WaitForThreadpoolWorkCallbacks(Work, Cancel);
#endif
    ValFree(&WorkVal);
    ValFree(&CancelVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadPoolWorkClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadPoolWorkClose() expects 1 argument "
                      "(work_id)\n");
      return -1;
    }
    AKSValue WorkVal = POP();
#ifdef _WIN32
    PTP_WORK Work = (PTP_WORK)(uintptr_t)ValToInt(WorkVal);
    if (Work)
      CloseThreadpoolWork(Work);
#else
    void *Work = (void *)(uintptr_t)ValToInt(WorkVal);
    if (Work)
      free(Work);
#endif
    ValFree(&WorkVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SymLinkCreateFile") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SymLinkCreateFile() expects 2 arguments "
                      "(link_path, target_path)\n");
      return -1;
    }
    AKSValue TargetVal = POP();
    AKSValue LinkVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(LinkVal) == VALSTR && ValKind(TargetVal) == VALSTR) {
      const char *LinkPath = ValToStrPtr(LinkVal)->data;
      const char *TargetPath = ValToStrPtr(TargetVal)->data;
      Ok = CreateSymbolicLinkA(LinkPath, TargetPath,
                               SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
    }
#endif
    ValFree(&LinkVal);
    ValFree(&TargetVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SymLinkCreateDirectory") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SymLinkCreateDirectory() expects 2 "
                      "arguments (link_path, target_path)\n");
      return -1;
    }
    AKSValue TargetVal = POP();
    AKSValue LinkVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(LinkVal) == VALSTR && ValKind(TargetVal) == VALSTR) {
      const char *LinkPath = ValToStrPtr(LinkVal)->data;
      const char *TargetPath = ValToStrPtr(TargetVal)->data;
      Ok =
          CreateSymbolicLinkA(LinkPath, TargetPath,
                              SYMBOLIC_LINK_FLAG_DIRECTORY |
                                  SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
    }
#endif
    ValFree(&LinkVal);
    ValFree(&TargetVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SymLinkResolve") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: SymLinkResolve() expects 1 argument (link_path)\n");
      return -1;
    }
    AKSValue LinkVal = POP();
    char ResolvedBuf[MAX_PATH] = {0};
#ifdef _WIN32
    if (ValKind(LinkVal) == VALSTR) {
      const char *LinkPath = ValToStrPtr(LinkVal)->data;
      HANDLE hFile =
          CreateFileA(LinkPath, GENERIC_READ,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        DWORD Len = GetFinalPathNameByHandleA(
            hFile, ResolvedBuf, sizeof(ResolvedBuf), FILE_NAME_NORMALIZED);
        CloseHandle(hFile);
        if (Len > 0 && strncmp(ResolvedBuf, "\\\\?\\", 4) == 0) {
          memmove(ResolvedBuf, ResolvedBuf + 4, strlen(ResolvedBuf + 4) + 1);
        }
      }
    }
#endif
    ValFree(&LinkVal);
    PUSH(ValStr(ResolvedBuf));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SymLinkIsSymLink") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SymLinkIsSymLink() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue PathVal = POP();
    BOOL IsReparse = FALSE;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
      DWORD Attr = GetFileAttributesA(Path);
      if (Attr != INVALID_FILE_ATTRIBUTES &&
          (Attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
        IsReparse = TRUE;
      }
    }
#endif
    ValFree(&PathVal);
    PUSH(ValBool(IsReparse ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StopwatchFrequency") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: StopwatchFrequency() expects 0 arguments\n");
      return -1;
    }
    int64_t FreqVal = 10000000;
#ifdef _WIN32
    LARGE_INTEGER Freq;
    QueryPerformanceFrequency(&Freq);
    FreqVal = (int64_t)Freq.QuadPart;
#endif
    PUSH(ValInt(FreqVal));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StopwatchCounter") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: StopwatchCounter() expects 0 arguments\n");
      return -1;
    }
    int64_t CtrVal = 0;
#ifdef _WIN32
    LARGE_INTEGER Ctr;
    QueryPerformanceCounter(&Ctr);
    CtrVal = (int64_t)Ctr.QuadPart;
#endif
    PUSH(ValInt(CtrVal));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessMemInfoQuery") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: ProcessMemInfoQuery() expects 1 argument (pid)\n");
      return -1;
    }
    AKSValue PidVal = POP();
    int64_t WorkingSet = 0;
    int64_t PeakWorkingSet = 0;
    int64_t PrivateUsage = 0;
    int64_t PageFaults = 0;
    int64_t PagefileUsage = 0;
#ifdef _WIN32
    DWORD TargetPid = (DWORD)ValToInt(PidVal);
    HANDLE hProc =
        (TargetPid == 0)
            ? GetCurrentProcess()
            : OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                          TargetPid);
    if (hProc) {
      PROCESS_MEMORY_COUNTERS_EX Pmc = {0};
      Pmc.cb = sizeof(Pmc);
      if (GetProcessMemoryInfo(hProc, (PROCESS_MEMORY_COUNTERS *)&Pmc,
                               sizeof(Pmc))) {
        WorkingSet = (int64_t)Pmc.WorkingSetSize;
        PeakWorkingSet = (int64_t)Pmc.PeakWorkingSetSize;
        PrivateUsage = (int64_t)Pmc.PrivateUsage;
        PageFaults = (int64_t)Pmc.PageFaultCount;
        PagefileUsage = (int64_t)Pmc.PagefileUsage;
      }
      if (TargetPid != 0)
        CloseHandle(hProc);
    }
#endif
    AKSValue ResStruct = ValStruct("ProcessMemoryStats");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 5;
    StructPtr->fieldnames = malloc(5 * sizeof(char *));
    StructPtr->fieldvals = malloc(5 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("working_set");
    StructPtr->fieldvals[0] = ValInt(WorkingSet);
    StructPtr->fieldnames[1] = strdup("peak_working_set");
    StructPtr->fieldvals[1] = ValInt(PeakWorkingSet);
    StructPtr->fieldnames[2] = strdup("private_usage");
    StructPtr->fieldvals[2] = ValInt(PrivateUsage);
    StructPtr->fieldnames[3] = strdup("page_faults");
    StructPtr->fieldvals[3] = ValInt(PageFaults);
    StructPtr->fieldnames[4] = strdup("pagefile_usage");
    StructPtr->fieldvals[4] = ValInt(PagefileUsage);
    ValFree(&PidVal);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FilePrefetchVirtualRange") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FilePrefetchVirtualRange() expects 2 "
                      "arguments (ptr, size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue PtrVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    void *BaseAddr = (void *)(uintptr_t)ValToInt(PtrVal);
    size_t RangeSize = (size_t)ValToInt(SizeVal);
    if (BaseAddr && RangeSize > 0) {
      typedef BOOL(WINAPI * PFN_PrefetchVM)(HANDLE, ULONG_PTR,
                                            PWIN32_MEMORY_RANGE_ENTRY, ULONG);
      HMODULE hK32 = GetModuleHandleA("kernel32.dll");
      if (hK32) {
        PFN_PrefetchVM pPrefetch =
            (PFN_PrefetchVM)GetProcAddress(hK32, "PrefetchVirtualMemory");
        if (pPrefetch) {
          WIN32_MEMORY_RANGE_ENTRY Entry;
          Entry.VirtualAddress = BaseAddr;
          Entry.NumberOfBytes = RangeSize;
          Ok = pPrefetch(GetCurrentProcess(), 1, &Entry, 0);
        }
      }
    }
#endif
    ValFree(&PtrVal);
    ValFree(&SizeVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FilePrefetchSetAdvice") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FilePrefetchSetAdvice() expects 2 "
                      "arguments (path, advice)\n");
      return -1;
    }
    AKSValue AdviceVal = POP();
    AKSValue PathVal = POP();
    ValFree(&PathVal);
    ValFree(&AdviceVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadPoolTimerCreate") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: ThreadPoolTimerCreate() expects 0 arguments\n");
      return -1;
    }
#ifdef _WIN32
    PTP_TIMER Timer = CreateThreadpoolTimer(AksThreadPoolTimerCallback, NULL, NULL);
    PUSH(ValInt((int64_t)(uintptr_t)Timer));
#else
    void *Timer = calloc(1, sizeof(int));
    PUSH(ValInt((int64_t)(uintptr_t)Timer));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadPoolTimerSet") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: ThreadPoolTimerSet() expects 3 arguments "
                      "(timer_id, due_ms, period_ms)\n");
      return -1;
    }
    AKSValue PeriodVal = POP();
    AKSValue DueVal = POP();
    AKSValue TimerVal = POP();
#ifdef _WIN32
    PTP_TIMER Timer = (PTP_TIMER)(uintptr_t)ValToInt(TimerVal);
    int64_t DueMs = ValToInt(DueVal);
    DWORD PeriodMs = (DWORD)ValToInt(PeriodVal);
    if (Timer) {
      int64_t RelNs = -DueMs * 10000;
      FILETIME FtDue;
      FtDue.dwLowDateTime = (DWORD)(RelNs & 0xFFFFFFFF);
      FtDue.dwHighDateTime = (DWORD)(RelNs >> 32);
      SetThreadpoolTimer(Timer, &FtDue, PeriodMs, 0);
    }
#endif
    ValFree(&TimerVal);
    ValFree(&DueVal);
    ValFree(&PeriodVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadPoolTimerWait") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ThreadPoolTimerWait() expects 2 "
                      "arguments (timer_id, cancel_pending)\n");
      return -1;
    }
    AKSValue CancelVal = POP();
    AKSValue TimerVal = POP();
#ifdef _WIN32
    PTP_TIMER Timer = (PTP_TIMER)(uintptr_t)ValToInt(TimerVal);
    BOOL Cancel = ValToBool(CancelVal);
    if (Timer)
      WaitForThreadpoolTimerCallbacks(Timer, Cancel);
#endif
    ValFree(&TimerVal);
    ValFree(&CancelVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ThreadPoolTimerClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ThreadPoolTimerClose() expects 1 "
                      "argument (timer_id)\n");
      return -1;
    }
    AKSValue TimerVal = POP();
#ifdef _WIN32
    PTP_TIMER Timer = (PTP_TIMER)(uintptr_t)ValToInt(TimerVal);
    if (Timer)
      CloseThreadpoolTimer(Timer);
#else
    void *Timer = (void *)(uintptr_t)ValToInt(TimerVal);
    if (Timer)
      free(Timer);
#endif
    ValFree(&TimerVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DirectIoOpen") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: DirectIoOpen() expects 2 arguments (path, mode)\n");
      return -1;
    }
    AKSValue ModeVal = POP();
    AKSValue PathVal = POP();
    HANDLE hFile = INVALID_HANDLE_VALUE;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR && ValKind(ModeVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
      const char *Mode = ValToStrPtr(ModeVal)->data;
      DWORD Access = GENERIC_READ;
      DWORD Share = FILE_SHARE_READ;
      DWORD Disp = OPEN_EXISTING;
      if (strchr(Mode, 'w') != NULL || strchr(Mode, 'a') != NULL ||
          strchr(Mode, '+') != NULL) {
        Access = GENERIC_READ | GENERIC_WRITE;
        Share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        Disp = OPEN_ALWAYS;
      }
      hFile =
          CreateFileA(Path, Access, Share, NULL, Disp,
                      FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    }
#endif
    ValFree(&PathVal);
    ValFree(&ModeVal);
    PUSH(ValInt((int64_t)(intptr_t)hFile));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DirectIoRead") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: DirectIoRead() expects 3 arguments "
                      "(handle, aligned_buf_ptr, size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue BufVal = POP();
    AKSValue HandleVal = POP();
    DWORD BytesRead = 0;
#ifdef _WIN32
    HANDLE hFile = (HANDLE)(intptr_t)ValToInt(HandleVal);
    void *Buf = (void *)(uintptr_t)ValToInt(BufVal);
    DWORD Size = (DWORD)ValToInt(SizeVal);
    if (hFile != INVALID_HANDLE_VALUE && Buf && Size > 0) {
      ReadFile(hFile, Buf, Size, &BytesRead, NULL);
    }
#endif
    ValFree(&HandleVal);
    ValFree(&BufVal);
    ValFree(&SizeVal);
    PUSH(ValInt((int64_t)BytesRead));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DirectIoWrite") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: DirectIoWrite() expects 3 arguments "
                      "(handle, aligned_buf_ptr, size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue BufVal = POP();
    AKSValue HandleVal = POP();
    DWORD BytesWritten = 0;
#ifdef _WIN32
    HANDLE hFile = (HANDLE)(intptr_t)ValToInt(HandleVal);
    void *Buf = (void *)(uintptr_t)ValToInt(BufVal);
    DWORD Size = (DWORD)ValToInt(SizeVal);
    if (hFile != INVALID_HANDLE_VALUE && Buf && Size > 0) {
      WriteFile(hFile, Buf, Size, &BytesWritten, NULL);
    }
#endif
    ValFree(&HandleVal);
    ValFree(&BufVal);
    ValFree(&SizeVal);
    PUSH(ValInt((int64_t)BytesWritten));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DirectIoClose") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: DirectIoClose() expects 1 argument (handle)\n");
      return -1;
    }
    AKSValue HandleVal = POP();
#ifdef _WIN32
    HANDLE hFile = (HANDLE)(intptr_t)ValToInt(HandleVal);
    if (hFile != INVALID_HANDLE_VALUE && hFile != NULL) {
      CloseHandle(hFile);
    }
#endif
    ValFree(&HandleVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConsoleAnsiGetSize") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: ConsoleAnsiGetSize() expects 0 arguments\n");
      return -1;
    }
    int64_t Cols = 80, Rows = 24;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO Csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &Csbi)) {
      Cols = (int64_t)(Csbi.srWindow.Right - Csbi.srWindow.Left + 1);
      Rows = (int64_t)(Csbi.srWindow.Bottom - Csbi.srWindow.Top + 1);
    }
#endif
    AKSValue ResStruct = ValStruct("ConsoleSize");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("width");
    StructPtr->fieldvals[0] = ValInt(Cols);
    StructPtr->fieldnames[1] = strdup("height");
    StructPtr->fieldvals[1] = ValInt(Rows);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConsoleAnsiGetCursorPosition") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ConsoleAnsiGetCursorPosition() expects 0 "
                      "arguments\n");
      return -1;
    }
    int64_t CursorX = 0, CursorY = 0;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO Csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &Csbi)) {
      CursorX = (int64_t)Csbi.dwCursorPosition.X;
      CursorY = (int64_t)Csbi.dwCursorPosition.Y;
    }
#endif
    AKSValue ResStruct = ValStruct("ConsoleCursor");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("x");
    StructPtr->fieldvals[0] = ValInt(CursorX);
    StructPtr->fieldnames[1] = strdup("y");
    StructPtr->fieldvals[1] = ValInt(CursorY);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConsoleAnsiSetCursorPosition") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ConsoleAnsiSetCursorPosition() expects 2 "
                      "arguments (x, y)\n");
      return -1;
    }
    AKSValue YVal = POP();
    AKSValue XVal = POP();
#ifdef _WIN32
    COORD Pos;
    Pos.X = (SHORT)ValToInt(XVal);
    Pos.Y = (SHORT)ValToInt(YVal);
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), Pos);
#endif
    ValFree(&XVal);
    ValFree(&YVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConsoleAnsiClear") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: ConsoleAnsiClear() expects 0 arguments\n");
      return -1;
    }
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO Csbi;
    DWORD Written = 0;
    if (GetConsoleScreenBufferInfo(hOut, &Csbi)) {
      DWORD CellCount = (DWORD)Csbi.dwSize.X * (DWORD)Csbi.dwSize.Y;
      COORD Home = {0, 0};
      FillConsoleOutputCharacterA(hOut, ' ', CellCount, Home, &Written);
      FillConsoleOutputAttribute(hOut, Csbi.wAttributes, CellCount, Home,
                                 &Written);
      SetConsoleCursorPosition(hOut, Home);
    }
#endif
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ConsoleAnsiEnableVirtualTerminal") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: ConsoleAnsiEnableVirtualTerminal() "
                      "expects 0 arguments\n");
      return -1;
    }
    BOOL Ok = FALSE;
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD Mode = 0;
    if (GetConsoleMode(hOut, &Mode)) {
      Ok = SetConsoleMode(hOut, Mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WaitOnAddressWait") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: WaitOnAddressWait() expects 4 arguments "
                      "(target_ptr, compare_val, size_bytes, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue SizeVal = POP();
    AKSValue CmpVal = POP();
    AKSValue TargetVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    typedef BOOL(WINAPI * PFN_WaitOnAddress)(volatile VOID *, PVOID, SIZE_T,
                                             DWORD);
    HMODULE hSynch = GetModuleHandleA("kernel32.dll");
    PFN_WaitOnAddress pfnWait =
        hSynch ? (PFN_WaitOnAddress)GetProcAddress(hSynch, "WaitOnAddress")
               : NULL;
    if (pfnWait) {
      void *TargetAddr = (void *)(uintptr_t)ValToInt(TargetVal);
      int64_t CmpData = ValToInt(CmpVal);
      size_t SizeBytes = (size_t)ValToInt(SizeVal);
      DWORD TimeoutMs = (DWORD)ValToInt(TimeoutVal);
      if (SizeBytes != 1 && SizeBytes != 2 && SizeBytes != 4 && SizeBytes != 8)
        SizeBytes = 4;
      if (TargetAddr) {
        Ok = pfnWait(TargetAddr, &CmpData, SizeBytes, TimeoutMs);
      }
    }
#endif
    ValFree(&TargetVal);
    ValFree(&CmpVal);
    ValFree(&SizeVal);
    ValFree(&TimeoutVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WaitOnAddressWakeOne") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WaitOnAddressWakeOne() expects 1 "
                      "argument (target_ptr)\n");
      return -1;
    }
    AKSValue TargetVal = POP();
#ifdef _WIN32
    typedef VOID(WINAPI * PFN_WakeByAddressSingle)(PVOID);
    HMODULE hSynch = GetModuleHandleA("kernel32.dll");
    PFN_WakeByAddressSingle pfnWake =
        hSynch ? (PFN_WakeByAddressSingle)GetProcAddress(hSynch,
                                                         "WakeByAddressSingle")
               : NULL;
    if (pfnWake) {
      void *TargetAddr = (void *)(uintptr_t)ValToInt(TargetVal);
      if (TargetAddr)
        pfnWake(TargetAddr);
    }
#endif
    ValFree(&TargetVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WaitOnAddressWakeAll") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WaitOnAddressWakeAll() expects 1 "
                      "argument (target_ptr)\n");
      return -1;
    }
    AKSValue TargetVal = POP();
#ifdef _WIN32
    typedef VOID(WINAPI * PFN_WakeByAddressAll)(PVOID);
    HMODULE hSynch = GetModuleHandleA("kernel32.dll");
    PFN_WakeByAddressAll pfnWakeAll =
        hSynch
            ? (PFN_WakeByAddressAll)GetProcAddress(hSynch, "WakeByAddressAll")
            : NULL;
    if (pfnWakeAll) {
      void *TargetAddr = (void *)(uintptr_t)ValToInt(TargetVal);
      if (TargetAddr)
        pfnWakeAll(TargetAddr);
    }
#endif
    ValFree(&TargetVal);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessAffinityGet") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: ProcessAffinityGet() expects 0 arguments\n");
      return -1;
    }
    int64_t ProcMask = 1;
    int64_t SysMask = 1;
#ifdef _WIN32
    DWORD_PTR DwProc = 0, DwSys = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &DwProc, &DwSys)) {
      ProcMask = (int64_t)DwProc;
      SysMask = (int64_t)DwSys;
    }
#endif
    AKSValue ResStruct = ValStruct("ProcessAffinityInfo");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("process_mask");
    StructPtr->fieldvals[0] = ValInt(ProcMask);
    StructPtr->fieldnames[1] = strdup("system_mask");
    StructPtr->fieldvals[1] = ValInt(SysMask);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessAffinitySet") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: ProcessAffinitySet() expects 1 argument (mask)\n");
      return -1;
    }
    AKSValue MaskVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    DWORD_PTR DwMask = (DWORD_PTR)ValToInt(MaskVal);
    Ok = SetProcessAffinityMask(GetCurrentProcess(), DwMask);
#endif
    ValFree(&MaskVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessAffinitySetCurrentThread") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ProcessAffinitySetCurrentThread() "
                      "expects 1 argument (mask)\n");
      return -1;
    }
    AKSValue MaskVal = POP();
    int64_t PrevMask = 0;
#ifdef _WIN32
    DWORD_PTR DwMask = (DWORD_PTR)ValToInt(MaskVal);
    PrevMask = (int64_t)SetThreadAffinityMask(GetCurrentThread(), DwMask);
#endif
    ValFree(&MaskVal);
    PUSH(ValInt(PrevMask));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "VirtualMemoryLockLock") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: VirtualMemoryLockLock() expects 2 "
                      "arguments (ptr, size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue PtrVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    void *BaseAddr = (void *)(uintptr_t)ValToInt(PtrVal);
    SIZE_T SizeBytes = (SIZE_T)ValToInt(SizeVal);
    if (BaseAddr && SizeBytes > 0) {
      Ok = VirtualLock(BaseAddr, SizeBytes);
    }
#endif
    ValFree(&PtrVal);
    ValFree(&SizeVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "VirtualMemoryLockUnlock") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: VirtualMemoryLockUnlock() expects 2 "
                      "arguments (ptr, size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue PtrVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    void *BaseAddr = (void *)(uintptr_t)ValToInt(PtrVal);
    SIZE_T SizeBytes = (SIZE_T)ValToInt(SizeVal);
    if (BaseAddr && SizeBytes > 0) {
      Ok = VirtualUnlock(BaseAddr, SizeBytes);
    }
#endif
    ValFree(&PtrVal);
    ValFree(&SizeVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HardLinkCreate") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: HardLinkCreate() expects 2 arguments "
                      "(target_path, link_path)\n");
      return -1;
    }
    AKSValue LinkVal = POP();
    AKSValue TargetVal = POP();
    BOOL Ok = FALSE;
    if (ValKind(TargetVal) == VALSTR && ValKind(LinkVal) == VALSTR) {
      const char *Target = ValToStrPtr(TargetVal)->data;
      const char *Link = ValToStrPtr(LinkVal)->data;
#ifdef _WIN32
      Ok = CreateHardLinkA(Link, Target, NULL);
#else
      Ok = (link(Target, Link) == 0) ? TRUE : FALSE;
#endif
    }
    ValFree(&TargetVal);
    ValFree(&LinkVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HardLinkGetCount") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: HardLinkGetCount() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue PathVal = POP();
    int64_t LinkCount = 0;
    if (ValKind(PathVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
#ifdef _WIN32
      HANDLE hFile = CreateFileA(
          Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION Info;
        if (GetFileInformationByHandle(hFile, &Info)) {
          LinkCount = (int64_t)Info.nNumberOfLinks;
        }
        CloseHandle(hFile);
      }
#else
      struct stat st;
      if (stat(Path, &st) == 0) {
        LinkCount = (int64_t)st.st_nlink;
      }
#endif
    }
    ValFree(&PathVal);
    PUSH(ValInt(LinkCount));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SparseFileSetSparse") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: SparseFileSetSparse() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue PathVal = POP();
    BOOL Ok = FALSE;
    if (ValKind(PathVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
#ifdef _WIN32
      HANDLE hFile = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        DWORD BytesReturned = 0;
        Ok = DeviceIoControl(hFile, FSCTL_SET_SPARSE, NULL, 0, NULL, 0,
                             &BytesReturned, NULL);
        CloseHandle(hFile);
      }
#else
      Ok = (access(Path, F_OK) == 0) ? TRUE : FALSE;
#endif
    }
    ValFree(&PathVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SparseFileAllocateSparse") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SparseFileAllocateSparse() expects 2 "
                      "arguments (path, virtual_size)\n");
      return -1;
    }
    AKSValue SizeVal = POP();
    AKSValue PathVal = POP();
    BOOL Ok = FALSE;
    if (ValKind(PathVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
      int64_t SizeBytes = ValToInt(SizeVal);
#ifdef _WIN32
      HANDLE hFile = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        DWORD BytesReturned = 0;
        if (DeviceIoControl(hFile, FSCTL_SET_SPARSE, NULL, 0, NULL, 0,
                            &BytesReturned, NULL)) {
          LARGE_INTEGER LiSize;
          LiSize.QuadPart = SizeBytes;
          if (SetFilePointerEx(hFile, LiSize, NULL, FILE_BEGIN)) {
            Ok = SetEndOfFile(hFile);
          }
        }
        CloseHandle(hFile);
      }
#else
      int fd = AksPosixOpen(Path, O_RDWR | O_CREAT | O_TRUNC, 0666);
      if (fd >= 0) {
        if (ftruncate(fd, (off_t)SizeBytes) == 0)
          Ok = TRUE;
        close(fd);
      }
#endif
    }
    ValFree(&PathVal);
    ValFree(&SizeVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileMemoryCommitFlush") == 0) {
    if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: FileMemoryCommitFlush() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue PathVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(PathVal) == VALSTR) {
      const char *Path = ValToStrPtr(PathVal)->data;
      HANDLE hFile =
          CreateFileA(Path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        Ok = FlushFileBuffers(hFile);
        CloseHandle(hFile);
      }
    }
#endif
    ValFree(&PathVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemThreadTelemetryGetTimes") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemThreadTelemetryGetTimes() expects "
                      "0 arguments\n");
      return -1;
    }
    int64_t KernelUs = 0, UserUs = 0;
#ifdef _WIN32
    FILETIME FtCreate, FtExit, FtKernel, FtUser;
    if (GetThreadTimes(GetCurrentThread(), &FtCreate, &FtExit, &FtKernel,
                       &FtUser)) {
      ULARGE_INTEGER K, U;
      K.LowPart = FtKernel.dwLowDateTime;
      K.HighPart = FtKernel.dwHighDateTime;
      U.LowPart = FtUser.dwLowDateTime;
      U.HighPart = FtUser.dwHighDateTime;
      KernelUs = (int64_t)(K.QuadPart / 10);
      UserUs = (int64_t)(U.QuadPart / 10);
    }
#endif
    AKSValue ResStruct = ValStruct("ThreadTimesInfo");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 2;
    StructPtr->fieldnames = malloc(2 * sizeof(char *));
    StructPtr->fieldvals = malloc(2 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("kernel_time_us");
    StructPtr->fieldvals[0] = ValInt(KernelUs);
    StructPtr->fieldnames[1] = strdup("user_time_us");
    StructPtr->fieldvals[1] = ValInt(UserUs);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SystemThreadTelemetryGetCycles") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: SystemThreadTelemetryGetCycles() expects "
                      "0 arguments\n");
      return -1;
    }
    int64_t Cycles = 0;
#ifdef _WIN32
    ULONG64 ThreadCycles = 0;
    if (QueryThreadCycleTime(GetCurrentThread(), &ThreadCycles)) {
      Cycles = (int64_t)ThreadCycles;
    }
#endif
    PUSH(ValInt(Cycles));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MemoryPressureWatcherQuery") == 0) {
    if (argc != 0) {
      fprintf(
          stderr,
          "Runtime error: MemoryPressureWatcherQuery() expects 0 arguments\n");
      return -1;
    }
    int64_t MemoryLoad = 0;
    int64_t TotalPhys = 0, AvailPhys = 0;
    int64_t TotalPageFile = 0, AvailPageFile = 0;
#ifdef _WIN32
    MEMORYSTATUSEX MemStat;
    MemStat.dwLength = sizeof(MemStat);
    if (GlobalMemoryStatusEx(&MemStat)) {
      MemoryLoad = (int64_t)MemStat.dwMemoryLoad;
      TotalPhys = (int64_t)MemStat.ullTotalPhys;
      AvailPhys = (int64_t)MemStat.ullAvailPhys;
      TotalPageFile = (int64_t)MemStat.ullTotalPageFile;
      AvailPageFile = (int64_t)MemStat.ullAvailPageFile;
    }
#endif
    AKSValue ResStruct = ValStruct("MemoryPressureInfo");
    AKSStruct *StructPtr = ValToObj(ResStruct);
    StructPtr->fieldcount = 5;
    StructPtr->fieldnames = malloc(5 * sizeof(char *));
    StructPtr->fieldvals = malloc(5 * sizeof(AKSValue));
    StructPtr->fieldnames[0] = strdup("memory_load_pct");
    StructPtr->fieldvals[0] = ValInt(MemoryLoad);
    StructPtr->fieldnames[1] = strdup("total_phys_bytes");
    StructPtr->fieldvals[1] = ValInt(TotalPhys);
    StructPtr->fieldnames[2] = strdup("avail_phys_bytes");
    StructPtr->fieldvals[2] = ValInt(AvailPhys);
    StructPtr->fieldnames[3] = strdup("total_pagefile_bytes");
    StructPtr->fieldvals[3] = ValInt(TotalPageFile);
    StructPtr->fieldnames[4] = strdup("avail_pagefile_bytes");
    StructPtr->fieldvals[4] = ValInt(AvailPageFile);
    PUSH(ResStruct);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileIoRingSubmitCopy") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FileIoRingSubmitCopy() expects 2 "
                      "arguments (src, dst)\n");
      return -1;
    }
    AKSValue DstVal = POP();
    AKSValue SrcVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    if (ValKind(SrcVal) == VALSTR && ValKind(DstVal) == VALSTR) {
      const char *Src = ValToStrPtr(SrcVal)->data;
      const char *Dst = ValToStrPtr(DstVal)->data;
      HANDLE hSrc = CreateFileA(Src, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
      if (hSrc != INVALID_HANDLE_VALUE) {
        HANDLE hDst = CreateFileA(Dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDst != INVALID_HANDLE_VALUE) {
          DWORD BufSize = 64 * 1024;
          uint8_t *Buf = (uint8_t *)malloc(BufSize);
          if (Buf) {
            DWORD BytesRead = 0, BytesWritten = 0;
            Ok = TRUE;
            while (ReadFile(hSrc, Buf, BufSize, &BytesRead, NULL) &&
                   BytesRead > 0) {
              if (!WriteFile(hDst, Buf, BytesRead, &BytesWritten, NULL) ||
                  BytesWritten != BytesRead) {
                Ok = FALSE;
                break;
              }
            }
            free(Buf);
          }
          CloseHandle(hDst);
        }
        CloseHandle(hSrc);
      }
    }
#endif
    ValFree(&SrcVal);
    ValFree(&DstVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ProcessMemoryProtectProtect") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: ProcessMemoryProtectProtect() expects 3 "
                      "arguments (ptr, size, protect)\n");
      return -1;
    }
    AKSValue ProtVal = POP();
    AKSValue SizeVal = POP();
    AKSValue PtrVal = POP();
    int64_t OldProt = 0;
#ifdef _WIN32
    void *Ptr = (void *)(uintptr_t)ValToInt(PtrVal);
    size_t Size = (size_t)ValToInt(SizeVal);
    DWORD NewProt = (DWORD)ValToInt(ProtVal);
    DWORD Old = 0;
    if (Ptr && Size > 0) {
      if (VirtualProtect(Ptr, Size, NewProt, &Old)) {
        OldProt = (int64_t)Old;
      }
    }
#endif
    ValFree(&PtrVal);
    ValFree(&SizeVal);
    ValFree(&ProtVal);
    PUSH(ValInt(OldProt));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HardwareCacheInfoGetHierarchy") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: HardwareCacheInfoGetHierarchy() expects "
                      "0 arguments\n");
      return -1;
    }
    int64_t L1d = 0, L1i = 0, L2 = 0, L3 = 0, LineSize = 64;
#ifdef _WIN32
    DWORD Len = 0;
    GetLogicalProcessorInformationEx(RelationCache, NULL, &Len);
    if (Len > 0) {
      PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buf =
          (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(Len);
      if (Buf && GetLogicalProcessorInformationEx(RelationCache, Buf, &Len)) {
        DWORD Offset = 0;
        while (Offset < Len) {
          PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info =
              (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((char *)Buf + Offset);
          if (Info->Relationship == RelationCache) {
            CACHE_RELATIONSHIP *Cache = &Info->Cache;
            if (Cache->LineSize > 0)
              LineSize = (int64_t)Cache->LineSize;
            if (Cache->Level == 1) {
              if (Cache->Type == CacheData)
                L1d += Cache->CacheSize;
              else if (Cache->Type == CacheInstruction)
                L1i += Cache->CacheSize;
              else {
                L1d += Cache->CacheSize / 2;
                L1i += Cache->CacheSize / 2;
              }
            } else if (Cache->Level == 2) {
              L2 += Cache->CacheSize;
            } else if (Cache->Level == 3) {
              L3 += Cache->CacheSize;
            }
          }
          if (Info->Size == 0)
            break;
          Offset += Info->Size;
        }
      }
      if (Buf)
        free(Buf);
    }
#endif
    AKSValue Res = ValStruct("HardwareCacheHierarchy");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("line_size_bytes");
    S->fieldvals[0] = ValInt(LineSize);
    S->fieldnames[1] = strdup("l1d_cache_bytes");
    S->fieldvals[1] = ValInt(L1d);
    S->fieldnames[2] = strdup("l1i_cache_bytes");
    S->fieldvals[2] = ValInt(L1i);
    S->fieldnames[3] = strdup("l2_cache_bytes");
    S->fieldvals[3] = ValInt(L2);
    S->fieldnames[4] = strdup("l3_cache_bytes");
    S->fieldvals[4] = ValInt(L3);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSharedMemoryMutexCreate") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSharedMemoryMutexCreate() expects 1 "
                      "argument (name)\n");
      return -1;
    }
    AKSValue NameVal = POP();
    int64_t HandleVal = 0;
#ifdef _WIN32
    if (ValKind(NameVal) == VALSTR) {
      HANDLE h = CreateMutexA(NULL, FALSE, ValToStrPtr(NameVal)->data);
      if (h != NULL)
        HandleVal = (int64_t)(uintptr_t)h;
    }
#endif
    ValFree(&NameVal);
    PUSH(ValInt(HandleVal));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSharedMemoryMutexAcquire") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: OsSharedMemoryMutexAcquire() expects 2 "
                      "arguments (handle, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutVal = POP();
    AKSValue HandleVal = POP();
    BOOL Acquired = FALSE;
#ifdef _WIN32
    HANDLE h = (HANDLE)(uintptr_t)ValToInt(HandleVal);
    DWORD TimeoutMs = (DWORD)ValToInt(TimeoutVal);
    if (h != NULL) {
      DWORD Res = WaitForSingleObject(h, TimeoutMs);
      if (Res == WAIT_OBJECT_0 || Res == WAIT_ABANDONED)
        Acquired = TRUE;
    }
#endif
    ValFree(&HandleVal);
    ValFree(&TimeoutVal);
    PUSH(ValBool(Acquired ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSharedMemoryMutexRelease") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSharedMemoryMutexRelease() expects 1 "
                      "argument (handle)\n");
      return -1;
    }
    AKSValue HandleVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    HANDLE h = (HANDLE)(uintptr_t)ValToInt(HandleVal);
    if (h != NULL) {
      Ok = ReleaseMutex(h);
    }
#endif
    ValFree(&HandleVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSharedMemoryMutexClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSharedMemoryMutexClose() expects 1 "
                      "argument (handle)\n");
      return -1;
    }
    AKSValue HandleVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    HANDLE h = (HANDLE)(uintptr_t)ValToInt(HandleVal);
    if (h != NULL) {
      Ok = CloseHandle(h);
    }
#endif
    ValFree(&HandleVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSystemTimerResolutionGetCaps") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: OsSystemTimerResolutionGetCaps() expects "
                      "0 arguments\n");
      return -1;
    }
    int64_t MinPeriod = 1, MaxPeriod = 1000000;
#ifdef _WIN32
    TIMECAPS tc;
    if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
      MinPeriod = (int64_t)tc.wPeriodMin;
      MaxPeriod = (int64_t)tc.wPeriodMax;
    }
#endif
    AKSValue Res = ValStruct("SystemTimerCaps");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 2;
    S->fieldnames = malloc(2 * sizeof(char *));
    S->fieldvals = malloc(2 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("min_period_ms");
    S->fieldvals[0] = ValInt(MinPeriod);
    S->fieldnames[1] = strdup("max_period_ms");
    S->fieldvals[1] = ValInt(MaxPeriod);
    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSystemTimerResolutionBegin") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSystemTimerResolutionBegin() expects 1 "
                      "argument (ms)\n");
      return -1;
    }
    AKSValue MsVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    UINT Ms = (UINT)ValToInt(MsVal);
    if (timeBeginPeriod(Ms) == TIMERR_NOERROR)
      Ok = TRUE;
#endif
    ValFree(&MsVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSystemTimerResolutionEnd") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSystemTimerResolutionEnd() expects 1 "
                      "argument (ms)\n");
      return -1;
    }
    AKSValue MsVal = POP();
    BOOL Ok = FALSE;
#ifdef _WIN32
    UINT Ms = (UINT)ValToInt(MsVal);
    if (timeEndPeriod(Ms) == TIMERR_NOERROR)
      Ok = TRUE;
#endif
    ValFree(&MsVal);
    PUSH(ValBool(Ok ? 1 : 0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RawDnsResolveC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: RawDnsResolveC() expects 2 arguments "
                      "(domain, type_str)\n");
      return -1;
    }
    AKSValue TypeVal = POP(), DomainVal = POP();
    AKSValue ResultArr = ValArr(0);
    AKSArray *Arr = ValToArr(ResultArr);

    if (ValKind(DomainVal) == VALSTR && ValKind(TypeVal) == VALSTR) {
      AKSString *Domain = ValToStrPtr(DomainVal);
      AKSString *TypeStr = ValToStrPtr(TypeVal);
#ifdef _WIN32
      WORD wType = DNS_TYPE_A;
      if (strcmp(TypeStr->data, "AAAA") == 0)
        wType = DNS_TYPE_AAAA;
      else if (strcmp(TypeStr->data, "MX") == 0)
        wType = DNS_TYPE_MX;
      else if (strcmp(TypeStr->data, "TXT") == 0)
        wType = DNS_TYPE_TEXT;

      PDNS_RECORD pDnsRecord = NULL;
      DNS_STATUS status = DnsQuery_A(Domain->data, wType, DNS_QUERY_STANDARD,
                                     NULL, &pDnsRecord, NULL);
      if (status == ERROR_SUCCESS && pDnsRecord != NULL) {
        PDNS_RECORD cur = pDnsRecord;
        while (cur != NULL) {
          if (cur->wType == wType) {
            char rec_buf[256] = {0};
            if (cur->wType == DNS_TYPE_A) {
              IN_ADDR ip;
              ip.S_un.S_addr = cur->Data.A.IpAddress;
              snprintf(rec_buf, sizeof(rec_buf), "%u.%u.%u.%u",
                       ip.S_un.S_un_b.s_b1, ip.S_un.S_un_b.s_b2,
                       ip.S_un.S_un_b.s_b3, ip.S_un.S_un_b.s_b4);
            } else if (cur->wType == DNS_TYPE_AAAA) {
              IP6_ADDRESS ip6 = cur->Data.AAAA.Ip6Address;
              snprintf(
                  rec_buf, sizeof(rec_buf), "%x:%x:%x:%x:%x:%x:%x:%x",
                  ntohs(((uint16_t *)&ip6)[0]), ntohs(((uint16_t *)&ip6)[1]),
                  ntohs(((uint16_t *)&ip6)[2]), ntohs(((uint16_t *)&ip6)[3]),
                  ntohs(((uint16_t *)&ip6)[4]), ntohs(((uint16_t *)&ip6)[5]),
                  ntohs(((uint16_t *)&ip6)[6]), ntohs(((uint16_t *)&ip6)[7]));
            } else if (cur->wType == DNS_TYPE_MX) {
              snprintf(rec_buf, sizeof(rec_buf), "%u %s",
                       cur->Data.MX.wPreference, cur->Data.MX.pNameExchange);
            } else if (cur->wType == DNS_TYPE_TEXT) {
              if (cur->Data.TXT.dwStringCount > 0 &&
                  cur->Data.TXT.pStringArray[0]) {
                snprintf(rec_buf, sizeof(rec_buf), "%s",
                         cur->Data.TXT.pStringArray[0]);
              }
            }
            if (rec_buf[0] != '\0') {
              AksArrPush(Arr, ValStr(rec_buf));
            }
          }
          cur = cur->pNext;
        }
        DnsRecordListFree(pDnsRecord, DnsFreeRecordList);
      }
#else
      if (strcmp(TypeStr->data, "A") == 0 || strcmp(TypeStr->data, "AAAA") == 0) {
        struct addrinfo hints, *res = NULL, *p = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = (strcmp(TypeStr->data, "AAAA") == 0) ? AF_INET6 : AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(Domain->data, NULL, &hints, &res) == 0 && res) {
          for (p = res; p != NULL; p = p->ai_next) {
            char ipstr[INET6_ADDRSTRLEN] = {0};
            const void *addr_ptr = NULL;
            if (p->ai_family == AF_INET)
              addr_ptr = &((struct sockaddr_in *)p->ai_addr)->sin_addr;
            else if (p->ai_family == AF_INET6)
              addr_ptr = &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr;
            if (addr_ptr && inet_ntop(p->ai_family, addr_ptr, ipstr, sizeof(ipstr))) {
              int dup = 0;
              for (int k = 0; k < Arr->count; k++) {
                if (ValKind(Arr->items[k]) == VALSTR &&
                    strcmp(ValToStrPtr(Arr->items[k])->data, ipstr) == 0) {
                  dup = 1;
                  break;
                }
              }
              if (!dup)
                AksArrPush(Arr, ValStr(ipstr));
            }
          }
          freeaddrinfo(res);
        }
      }
#endif
    }
    ValFree(&DomainVal);
    ValFree(&TypeVal);
    PUSH(ResultArr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WebSocketFrameEncodeC") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: WebSocketFrameEncodeC() expects 4 "
                      "arguments (payload, opcode, is_masked, mask_key)\n");
      return -1;
    }
    AKSValue MaskKeyV = POP(), MaskedV = POP(), OpcodeV = POP(),
             PayloadV = POP();
    char *OutHex = NULL;
    static const char hexchars[] = "0123456789abcdef";

    if (ValKind(PayloadV) == VALSTR) {
      AKSString *Payload = ValToStrPtr(PayloadV);
      int opcode = (int)ValToInt(OpcodeV) & 0x0F;
      int is_masked = ValIsTruthy(MaskedV);
      uint32_t mask_key = (uint32_t)ValToInt(MaskKeyV);

      size_t payload_len = Payload->len;
      size_t header_len = 2;
      if (payload_len <= 125) {
        header_len = 2;
      } else if (payload_len <= 65535) {
        header_len = 4;
      } else {
        header_len = 10;
      }
      if (is_masked)
        header_len += 4;

      size_t total_frame_len = header_len + payload_len;
      uint8_t *frame = malloc(total_frame_len > 0 ? total_frame_len : 1);
      if (frame) {
        frame[0] = 0x80 | (uint8_t)opcode;
        size_t idx = 1;
        uint8_t mask_bit = is_masked ? 0x80 : 0x00;

        if (payload_len <= 125) {
          frame[idx++] = mask_bit | (uint8_t)payload_len;
        } else if (payload_len <= 65535) {
          frame[idx++] = mask_bit | 126;
          frame[idx++] = (uint8_t)(payload_len >> 8);
          frame[idx++] = (uint8_t)(payload_len & 0xFF);
        } else {
          frame[idx++] = mask_bit | 127;
          for (int i = 7; i >= 0; i--) {
            frame[idx++] = (uint8_t)((payload_len >> (i * 8)) & 0xFF);
          }
        }

        uint8_t mask_bytes[4];
        if (is_masked) {
          mask_bytes[0] = (uint8_t)(mask_key >> 24);
          mask_bytes[1] = (uint8_t)(mask_key >> 16);
          mask_bytes[2] = (uint8_t)(mask_key >> 8);
          mask_bytes[3] = (uint8_t)(mask_key & 0xFF);
          memcpy(frame + idx, mask_bytes, 4);
          idx += 4;
        }

        for (size_t i = 0; i < payload_len; i++) {
          uint8_t b = (uint8_t)Payload->data[i];
          if (is_masked) {
            b ^= mask_bytes[i % 4];
          }
          frame[idx + i] = b;
        }

        OutHex = malloc(total_frame_len * 2 + 1);
        for (size_t i = 0; i < total_frame_len; i++) {
          OutHex[i * 2] = hexchars[(frame[i] >> 4) & 0x0F];
          OutHex[i * 2 + 1] = hexchars[frame[i] & 0x0F];
        }
        OutHex[total_frame_len * 2] = '\0';
        free(frame);
      }
    }

    ValFree(&PayloadV);
    ValFree(&OpcodeV);
    ValFree(&MaskedV);
    ValFree(&MaskKeyV);

    PUSH(ValStr(OutHex ? OutHex : ""));
    if (OutHex)
      free(OutHex);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WebSocketFrameDecodeC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WebSocketFrameDecodeC() expects 1 "
                      "argument (frame_hex)\n");
      return -1;
    }
    AKSValue HexVal = POP();
    int fin = 0, opcode = 0, is_masked = 0;
    char *PayloadStr = NULL;
    int payload_len = 0;

    if (ValKind(HexVal) == VALSTR) {
      AKSString *H = ValToStrPtr(HexVal);
      size_t frame_len = H->len / 2;
      if (frame_len >= 2) {
        uint8_t *frame = malloc(frame_len);
        for (size_t i = 0; i < frame_len; i++) {
          char c1 = H->data[i * 2], c2 = H->data[i * 2 + 1];
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
          frame[i] = (b1 << 4) | b2;
        }

        fin = (frame[0] & 0x80) ? 1 : 0;
        opcode = frame[0] & 0x0F;
        is_masked = (frame[1] & 0x80) ? 1 : 0;
        uint64_t len_field = frame[1] & 0x7F;
        size_t idx = 2;

        if (len_field == 126 && frame_len >= idx + 2) {
          len_field = (((uint64_t)frame[idx]) << 8) | frame[idx + 1];
          idx += 2;
        } else if (len_field == 127 && frame_len >= idx + 8) {
          len_field = 0;
          for (int i = 0; i < 8; i++)
            len_field = (len_field << 8) | frame[idx + i];
          idx += 8;
        }

        uint8_t mask_bytes[4] = {0};
        if (is_masked && frame_len >= idx + 4) {
          memcpy(mask_bytes, frame + idx, 4);
          idx += 4;
        }

        payload_len = (int)len_field;
        if (frame_len >= idx + len_field) {
          PayloadStr = malloc((size_t)len_field + 1);
          for (size_t i = 0; i < len_field; i++) {
            uint8_t b = frame[idx + i];
            if (is_masked)
              b ^= mask_bytes[i % 4];
            PayloadStr[i] = (char)b;
          }
          PayloadStr[len_field] = '\0';
        }
        free(frame);
      }
    }
    ValFree(&HexVal);

    AKSValue Res = ValStruct("WebSocketFrame");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("fin");
    S->fieldvals[0] = ValBool(fin);
    S->fieldnames[1] = strdup("opcode");
    S->fieldvals[1] = ValInt(opcode);
    S->fieldnames[2] = strdup("masked");
    S->fieldvals[2] = ValBool(is_masked);
    S->fieldnames[3] = strdup("payload");
    S->fieldvals[3] = ValStr(PayloadStr ? PayloadStr : "");
    S->fieldnames[4] = strdup("payload_len");
    S->fieldvals[4] = ValInt(payload_len);
    if (PayloadStr)
      free(PayloadStr);

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "IcmpPingEchoC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: IcmpPingEchoC() expects 2 arguments "
                      "(host, timeout_ms)\n");
      return -1;
    }
    AKSValue TimeoutV = POP(), HostV = POP();
    int success = 0, rtt_ms = 0, status_code = -1, bytes = 0, ttl = 0;

    if (ValKind(HostV) == VALSTR) {
#ifdef _WIN32
      AKSString *Host = ValToStrPtr(HostV);
      DWORD timeout = (DWORD)ValToInt(TimeoutV);
      if (timeout <= 0)
        timeout = 1000;
      HANDLE hIcmp = IcmpCreateFile();
      if (hIcmp != INVALID_HANDLE_VALUE) {
        unsigned long ipaddr = inet_addr(Host->data);
        if (ipaddr == INADDR_NONE) {
          struct hostent *he = gethostbyname(Host->data);
          if (he && he->h_addr_list && he->h_addr_list[0]) {
            ipaddr = *(unsigned long *)he->h_addr_list[0];
          }
        }

        if (ipaddr != INADDR_NONE) {
          char sendData[32] = "AKS Echo Ping Payload Test 1234";
          DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 8;
          LPVOID replyBuffer = malloc(replySize);
          if (replyBuffer) {
            DWORD dwReplies =
                IcmpSendEcho(hIcmp, ipaddr, sendData, sizeof(sendData), NULL,
                             replyBuffer, replySize, timeout);
            if (dwReplies != 0) {
              PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)replyBuffer;
              success = (pEchoReply->Status == IP_SUCCESS);
              status_code = (int)pEchoReply->Status;
              rtt_ms = (int)pEchoReply->RoundTripTime;
              bytes = (int)pEchoReply->DataSize;
              ttl = (int)pEchoReply->Options.Ttl;
            }
            free(replyBuffer);
          }
        }
        IcmpCloseHandle(hIcmp);
      }
#endif
    }

    ValFree(&HostV);
    ValFree(&TimeoutV);

    AKSValue Res = ValStruct("IcmpPingResult");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("success");
    S->fieldvals[0] = ValBool(success);
    S->fieldnames[1] = strdup("rtt_ms");
    S->fieldvals[1] = ValInt(rtt_ms);
    S->fieldnames[2] = strdup("status");
    S->fieldvals[2] = ValInt(status_code);
    S->fieldnames[3] = strdup("bytes");
    S->fieldvals[3] = ValInt(bytes);
    S->fieldnames[4] = strdup("ttl");
    S->fieldvals[4] = ValInt(ttl);

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Http11ParseRequestC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Http11ParseRequestC() expects 1 argument "
                      "(raw_request)\n");
      return -1;
    }
    AKSValue ReqVal = POP();
    char method[32] = {0}, path[1024] = {0}, version[32] = {0};
    const char *body_start = "";

    AKSValue HeadersStruct = ValStruct("HttpHeaders");
    AKSStruct *Hdr = ValToObj(HeadersStruct);
    int hdr_cap = 16, hdr_count = 0;
    Hdr->fieldnames = malloc(hdr_cap * sizeof(char *));
    Hdr->fieldvals = malloc(hdr_cap * sizeof(AKSValue));
    Hdr->fieldcount = 0;

    if (ValKind(ReqVal) == VALSTR) {
      AKSString *R = ValToStrPtr(ReqVal);
      const char *p = R->data;
      const char *line_end = strstr(p, "\r\n");
      if (!line_end)
        line_end = strchr(p, '\n');
      if (line_end) {
        sscanf(p, "%31s %1023s %31s", method, path, version);
        p = (*line_end == '\r') ? line_end + 2 : line_end + 1;

        while (p && *p) {
          if (*p == '\r' && *(p + 1) == '\n') {
            body_start = p + 2;
            break;
          }
          if (*p == '\n') {
            body_start = p + 1;
            break;
          }
          const char *hdr_end = strstr(p, "\r\n");
          if (!hdr_end)
            hdr_end = strchr(p, '\n');
          if (!hdr_end)
            hdr_end = p + strlen(p);

          const char *colon = memchr(p, ':', hdr_end - p);
          if (colon) {
            size_t klen = colon - p;
            char *key = malloc(klen + 1);
            memcpy(key, p, klen);
            key[klen] = '\0';

            const char *vstart = colon + 1;
            while (vstart < hdr_end && (*vstart == ' ' || *vstart == '\t'))
              vstart++;
            size_t vlen = hdr_end - vstart;
            char *val = malloc(vlen + 1);
            memcpy(val, vstart, vlen);
            val[vlen] = '\0';

            if (hdr_count >= hdr_cap) {
              hdr_cap *= 2;
              Hdr->fieldnames =
                  realloc(Hdr->fieldnames, hdr_cap * sizeof(char *));
              Hdr->fieldvals =
                  realloc(Hdr->fieldvals, hdr_cap * sizeof(AKSValue));
            }
            Hdr->fieldnames[hdr_count] = key;
            Hdr->fieldvals[hdr_count] = ValStr(val);
            hdr_count++;
            Hdr->fieldcount = hdr_count;
            free(val);
          }
          p = (*hdr_end == '\r') ? hdr_end + 2 : hdr_end + 1;
        }
      }
    }
    ValFree(&ReqVal);

    AKSValue Res = ValStruct("Http11Request");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("method");
    S->fieldvals[0] = ValStr(method);
    S->fieldnames[1] = strdup("path");
    S->fieldvals[1] = ValStr(path);
    S->fieldnames[2] = strdup("version");
    S->fieldvals[2] = ValStr(version);
    S->fieldnames[3] = strdup("headers");
    S->fieldvals[3] = HeadersStruct;
    S->fieldnames[4] = strdup("body");
    S->fieldvals[4] = ValStr(body_start ? body_start : "");

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Http11ParseResponseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: Http11ParseResponseC() expects 1 "
                      "argument (raw_response)\n");
      return -1;
    }
    AKSValue RespVal = POP();
    char version[32] = {0}, status_text[128] = {0};
    int status_code = 0;
    const char *body_start = "";

    AKSValue HeadersStruct = ValStruct("HttpHeaders");
    AKSStruct *Hdr = ValToObj(HeadersStruct);
    int hdr_cap = 16, hdr_count = 0;
    Hdr->fieldnames = malloc(hdr_cap * sizeof(char *));
    Hdr->fieldvals = malloc(hdr_cap * sizeof(AKSValue));
    Hdr->fieldcount = 0;

    if (ValKind(RespVal) == VALSTR) {
      AKSString *R = ValToStrPtr(RespVal);
      const char *p = R->data;
      const char *line_end = strstr(p, "\r\n");
      if (!line_end)
        line_end = strchr(p, '\n');
      if (line_end) {
        sscanf(p, "%31s %d %127[^\r\n]", version, &status_code, status_text);
        p = (*line_end == '\r') ? line_end + 2 : line_end + 1;

        while (p && *p) {
          if (*p == '\r' && *(p + 1) == '\n') {
            body_start = p + 2;
            break;
          }
          if (*p == '\n') {
            body_start = p + 1;
            break;
          }
          const char *hdr_end = strstr(p, "\r\n");
          if (!hdr_end)
            hdr_end = strchr(p, '\n');
          if (!hdr_end)
            hdr_end = p + strlen(p);

          const char *colon = memchr(p, ':', hdr_end - p);
          if (colon) {
            size_t klen = colon - p;
            char *key = malloc(klen + 1);
            memcpy(key, p, klen);
            key[klen] = '\0';

            const char *vstart = colon + 1;
            while (vstart < hdr_end && (*vstart == ' ' || *vstart == '\t'))
              vstart++;
            size_t vlen = hdr_end - vstart;
            char *val = malloc(vlen + 1);
            memcpy(val, vstart, vlen);
            val[vlen] = '\0';

            if (hdr_count >= hdr_cap) {
              hdr_cap *= 2;
              Hdr->fieldnames =
                  realloc(Hdr->fieldnames, hdr_cap * sizeof(char *));
              Hdr->fieldvals =
                  realloc(Hdr->fieldvals, hdr_cap * sizeof(AKSValue));
            }
            Hdr->fieldnames[hdr_count] = key;
            Hdr->fieldvals[hdr_count] = ValStr(val);
            hdr_count++;
            Hdr->fieldcount = hdr_count;
            free(val);
          }
          p = (*hdr_end == '\r') ? hdr_end + 2 : hdr_end + 1;
        }
      }
    }
    ValFree(&RespVal);

    AKSValue Res = ValStruct("Http11Response");
    AKSStruct *S = ValToObj(Res);
    S->fieldcount = 5;
    S->fieldnames = malloc(5 * sizeof(char *));
    S->fieldvals = malloc(5 * sizeof(AKSValue));
    S->fieldnames[0] = strdup("status_code");
    S->fieldvals[0] = ValInt(status_code);
    S->fieldnames[1] = strdup("status_text");
    S->fieldvals[1] = ValStr(status_text);
    S->fieldnames[2] = strdup("version");
    S->fieldvals[2] = ValStr(version);
    S->fieldnames[3] = strdup("headers");
    S->fieldvals[3] = HeadersStruct;
    S->fieldnames[4] = strdup("body");
    S->fieldvals[4] = ValStr(body_start ? body_start : "");

    PUSH(Res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsClipboardGetC") == 0) {
#ifdef _WIN32
    if (!OpenClipboard(NULL)) {
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    HANDLE hcb = GetClipboardData(CF_TEXT);
    if (!hcb) {
      CloseClipboard();
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const char *cbp = (const char *)GlobalLock(hcb);
    AKSValue cbres = cbp ? ValStr(cbp) : ValNull();
    if (cbp)
      GlobalUnlock(hcb);
    CloseClipboard();
    PUSH(cbres);
#else
    char *txt = SysClipGetText();
    PUSH(txt ? ValStr(txt) : ValNull());
    if (txt)
      free(txt);
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsClipboardSetC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsClipboardSetC() expects 1 arg\n");
      return -1;
    }
    AKSValue SV = POP();
    if (ValKind(SV) != VALSTR) {
      ValFree(&SV);
      PUSH(ValBool(0));
      *stacktopptr = stacktop;
      return 1;
    }
    const char *txt = ValToStrPtr(SV)->data;
    int cbset_ok = 0;
#ifdef _WIN32
    int tlen = ValToStrPtr(SV)->len;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(tlen + 1));
    if (hg) {
      char *dst = (char *)GlobalLock(hg);
      memcpy(dst, txt, tlen);
      dst[tlen] = 0;
      GlobalUnlock(hg);
      if (OpenClipboard(NULL)) {
        EmptyClipboard();
        cbset_ok = SetClipboardData(CF_TEXT, hg) != NULL;
        CloseClipboard();
      } else
        GlobalFree(hg);
    }
#else
    SysClipSetText(txt);
    cbset_ok = 1;
#endif
    ValFree(&SV);
    PUSH(ValBool(cbset_ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsClipboardClearC") == 0) {
    int ok3 = 0;
#ifdef _WIN32
    if (OpenClipboard(NULL)) {
      EmptyClipboard();
      CloseClipboard();
      ok3 = 1;
    }
#else
    SysClipSetText("");
    ok3 = 1;
#endif
    PUSH(ValBool(ok3));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsProcessSnapshotC") == 0) {
    AKSValue ps_arr = ValArr(32);
    AKSArray *ps_a = ValToArr(ps_arr);
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
      PROCESSENTRY32 pe;
      pe.dwSize = sizeof(pe);
      if (Process32First(snap, &pe)) {
        do {
          AKSValue entry = ValStruct("ProcessEntry");
          AKSStruct *es = ValToObj(entry);
          es->fieldnames = malloc(3 * sizeof(char *));
          es->fieldvals = malloc(3 * sizeof(AKSValue));
          es->fieldnames[0] = strdup("pid");
          es->fieldvals[0] = ValInt((int64_t)pe.th32ProcessID);
          es->fieldnames[1] = strdup("ppid");
          es->fieldvals[1] = ValInt((int64_t)pe.th32ParentProcessID);
          es->fieldnames[2] = strdup("name");
          es->fieldvals[2] = ValStr(pe.szExeFile);
          es->fieldcount = 3;
          AksArrPush(ps_a, entry);
        } while (Process32Next(snap, &pe));
      }
      CloseHandle(snap);
    }
#elif defined(__linux__)
    DIR *procdir = opendir("/proc");
    if (procdir) {
      struct dirent *de;
      while ((de = readdir(procdir)) != NULL && ps_a->count < 256) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9')
          continue;
        long pid = strtol(de->d_name, NULL, 10);
        char ppath[128], pname[256] = {0};
        snprintf(ppath, sizeof(ppath), "/proc/%ld/comm", pid);
        FILE *cf = fopen(ppath, "r");
        if (!cf)
          continue;
        if (fgets(pname, sizeof(pname), cf)) {
          size_t plen = strlen(pname);
          while (plen > 0 && (pname[plen - 1] == '\n' || pname[plen - 1] == '\r'))
            pname[--plen] = '\0';
        }
        fclose(cf);
        AKSValue entry = ValStruct("ProcessEntry");
        AKSStruct *es = ValToObj(entry);
        es->fieldnames = malloc(3 * sizeof(char *));
        es->fieldvals = malloc(3 * sizeof(AKSValue));
        es->fieldnames[0] = strdup("pid");
        es->fieldvals[0] = ValInt((int64_t)pid);
        es->fieldnames[1] = strdup("ppid");
        es->fieldvals[1] = ValInt(0);
        es->fieldnames[2] = strdup("name");
        es->fieldvals[2] = ValStr(pname[0] ? pname : "proc");
        es->fieldcount = 3;
        AksArrPush(ps_a, entry);
      }
      closedir(procdir);
    }
#endif
    PUSH(ps_arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsModuleEnumC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsModuleEnumC() expects 1 arg (pid)\n");
      return -1;
    }
    AKSValue PV = POP();
    AKSValue me_arr = ValArr(16);
#ifdef _WIN32
    AKSArray *me_a = ValToArr(me_arr);
    DWORD me_pid = (DWORD)ValToInt(PV);
    HANDLE me_proc =
        OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, me_pid);
    if (me_proc) {
      HMODULE mods[1024];
      DWORD me_needed = 0;
      if (EnumProcessModules(me_proc, mods, sizeof(mods), &me_needed)) {
        int me_cnt = (int)(me_needed / sizeof(HMODULE));
        for (int i = 0; i < me_cnt; i++) {
          char mname[MAX_PATH] = {0};
          if (GetModuleFileNameExA(me_proc, mods[i], mname, sizeof(mname))) {
            AKSValue me_e = ValStruct("ModuleEntry");
            AKSStruct *me_s = ValToObj(me_e);
            me_s->fieldnames = malloc(2 * sizeof(char *));
            me_s->fieldvals = malloc(2 * sizeof(AKSValue));
            me_s->fieldnames[0] = strdup("base");
            me_s->fieldvals[0] = ValInt((int64_t)(uintptr_t)mods[i]);
            me_s->fieldnames[1] = strdup("name");
            me_s->fieldvals[1] = ValStr(mname);
            me_s->fieldcount = 2;
            AksArrPush(me_a, me_e);
          }
        }
      }
      CloseHandle(me_proc);
    }
#endif
    ValFree(&PV);
    PUSH(me_arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsWindowEnumC") == 0) {
    AKSValue we_arr = ValArr(32);
    AKSArray *we_a = ValToArr(we_arr);
#ifdef _WIN32
    HWND hw2 = GetTopWindow(NULL);
    while (hw2) {
      char we_title[512] = {0};
      GetWindowTextA(hw2, we_title, sizeof(we_title));
      if (IsWindowVisible(hw2) && we_title[0]) {
        AKSValue we_e = ValStruct("WindowEntry");
        AKSStruct *we_s = ValToObj(we_e);
        we_s->fieldnames = malloc(2 * sizeof(char *));
        we_s->fieldvals = malloc(2 * sizeof(AKSValue));
        we_s->fieldnames[0] = strdup("hwnd");
        we_s->fieldvals[0] = ValInt((int64_t)(uintptr_t)hw2);
        we_s->fieldnames[1] = strdup("title");
        we_s->fieldvals[1] = ValStr(we_title);
        we_s->fieldcount = 2;
        AksArrPush(we_a, we_e);
      }
      hw2 = GetWindow(hw2, GW_HWNDNEXT);
    }
#else
    (void)we_a;
#endif
    PUSH(we_arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsConsoleRawModeC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: OsConsoleRawModeC() expects 1 arg (enable)\n");
      return -1;
    }
    AKSValue EV = POP();
    int crm_en = ValToBool(EV);
    ValFree(&EV);
#ifdef _WIN32
    HANDLE crm_h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD crm_mode = 0;
    GetConsoleMode(crm_h, &crm_mode);
    if (crm_en) {
      crm_mode &=
          ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
      crm_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    } else {
      crm_mode |=
          (ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
      crm_mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
    }
    PUSH(ValBool(SetConsoleMode(crm_h, crm_mode)));
#else
    PUSH(ValBool(PosixSetConsoleRawMode(crm_en)));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsFiberCreateC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: OsFiberCreateC() expects 1 arg (stack_size)\n");
      return -1;
    }
    AKSValue SzV = POP();
#ifdef _WIN32
    SIZE_T fib_ss = (SIZE_T)ValToInt(SzV);
    if (fib_ss == 0)
      fib_ss = 65536;
    ConvertThreadToFiber(NULL);
    PUSH(ValInt((int64_t)(uintptr_t)GetCurrentFiber()));
#else
    PUSH(ValInt(1));
#endif
    ValFree(&SzV);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsTokenPrivilegesC") == 0) {
    AKSValue tp_arr = ValArr(8);
    AKSArray *tp_a = ValToArr(tp_arr);
#ifdef _WIN32
    HANDLE tp_tok = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tp_tok)) {
      DWORD tp_len = 0;
      GetTokenInformation(tp_tok, TokenPrivileges, NULL, 0, &tp_len);
      TOKEN_PRIVILEGES *tp_buf = (TOKEN_PRIVILEGES *)malloc(tp_len);
      if (tp_buf && GetTokenInformation(tp_tok, TokenPrivileges, tp_buf, tp_len,
                                        &tp_len)) {
        for (DWORD i = 0; i < tp_buf->PrivilegeCount; i++) {
          char tp_pn[256] = {0};
          DWORD tp_pnl = sizeof(tp_pn);
          LookupPrivilegeNameA(NULL, &tp_buf->Privileges[i].Luid, tp_pn,
                               &tp_pnl);
          AKSValue tp_e = ValStruct("Privilege");
          AKSStruct *tp_s = ValToObj(tp_e);
          tp_s->fieldnames = malloc(2 * sizeof(char *));
          tp_s->fieldvals = malloc(2 * sizeof(AKSValue));
          tp_s->fieldnames[0] = strdup("name");
          tp_s->fieldvals[0] = ValStr(tp_pn);
          tp_s->fieldnames[1] = strdup("enabled");
          tp_s->fieldvals[1] = ValBool(
              (tp_buf->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) != 0);
          tp_s->fieldcount = 2;
          AksArrPush(tp_a, tp_e);
        }
      }
      if (tp_buf)
        free(tp_buf);
      CloseHandle(tp_tok);
    }
#else
    (void)tp_a;
#endif
    PUSH(tp_arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsHeapWalkC") == 0) {
    AKSValue hw3_arr = ValArr(8);
    AKSArray *hw3_a = ValToArr(hw3_arr);
#ifdef _WIN32
    HANDLE hw3 = HeapCreate(0, 0, 0);
    if (hw3) {
      PROCESS_HEAP_ENTRY hw3_e;
      hw3_e.lpData = NULL;
      int hw3_n = 0;
      while (HeapWalk(hw3, &hw3_e) && hw3_n < 64) {
        AKSValue hw3_entry = ValStruct("HeapBlock");
        AKSStruct *hw3_s = ValToObj(hw3_entry);
        hw3_s->fieldnames = malloc(3 * sizeof(char *));
        hw3_s->fieldvals = malloc(3 * sizeof(AKSValue));
        hw3_s->fieldnames[0] = strdup("addr");
        hw3_s->fieldvals[0] = ValInt((int64_t)(uintptr_t)hw3_e.lpData);
        hw3_s->fieldnames[1] = strdup("size");
        hw3_s->fieldvals[1] = ValInt((int64_t)hw3_e.cbData);
        hw3_s->fieldnames[2] = strdup("used");
        hw3_s->fieldvals[2] =
            ValBool((hw3_e.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0);
        hw3_s->fieldcount = 3;
        AksArrPush(hw3_a, hw3_entry);
        hw3_n++;
      }
      HeapDestroy(hw3);
    }
#else
    (void)hw3_a;
#endif
    PUSH(hw3_arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsVirtualQueryC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: OsVirtualQueryC() expects 1 arg (addr)\n");
      return -1;
    }
    AKSValue AV2 = POP();
    void *vq_addr = (void *)(uintptr_t)ValToInt(AV2);
    ValFree(&AV2);
    AKSValue vq_res = ValStruct("MemRegion");
    AKSStruct *vq_s = ValToObj(vq_res);
    vq_s->fieldnames = malloc(4 * sizeof(char *));
    vq_s->fieldvals = malloc(4 * sizeof(AKSValue));
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION vq_mbi;
    memset(&vq_mbi, 0, sizeof(vq_mbi));
    VirtualQuery(vq_addr, &vq_mbi, sizeof(vq_mbi));
    vq_s->fieldnames[0] = strdup("base");
    vq_s->fieldvals[0] = ValInt((int64_t)(uintptr_t)vq_mbi.BaseAddress);
    vq_s->fieldnames[1] = strdup("size");
    vq_s->fieldvals[1] = ValInt((int64_t)vq_mbi.RegionSize);
    vq_s->fieldnames[2] = strdup("state");
    vq_s->fieldvals[2] = ValInt((int64_t)vq_mbi.State);
    vq_s->fieldnames[3] = strdup("protect");
    vq_s->fieldvals[3] = ValInt((int64_t)vq_mbi.Protect);
#else
    long psz = sysconf(_SC_PAGESIZE);
    if (psz <= 0) psz = 4096;
    uintptr_t pg_base = ((uintptr_t)vq_addr) & ~((uintptr_t)psz - 1);
    vq_s->fieldnames[0] = strdup("base");
    vq_s->fieldvals[0] = ValInt((int64_t)pg_base);
    vq_s->fieldnames[1] = strdup("size");
    vq_s->fieldvals[1] = ValInt((int64_t)psz);
    vq_s->fieldnames[2] = strdup("state");
    vq_s->fieldvals[2] = ValInt(0x1000);
    vq_s->fieldnames[3] = strdup("protect");
    vq_s->fieldvals[3] = ValInt(0x04);
#endif
    vq_s->fieldcount = 4;
    PUSH(vq_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsVirtualProtectC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: OsVirtualProtectC() expects 3 args "
                      "(addr, size, prot)\n");
      return -1;
    }
    AKSValue vp_pr = POP(), vp_sz2 = POP(), vp_ad = POP();
    void *vp_addr = (void *)(uintptr_t)ValToInt(vp_ad);
#ifdef _WIN32
    SIZE_T vp_sz = (SIZE_T)ValToInt(vp_sz2);
    DWORD vp_prot = (DWORD)ValToInt(vp_pr), vp_old = 0;
    BOOL vp_ok = VirtualProtect(vp_addr, vp_sz, vp_prot, &vp_old);
#else
    size_t vp_sz = (size_t)ValToInt(vp_sz2);
    int64_t vp_prot = ValToInt(vp_pr);
    long psz = sysconf(_SC_PAGESIZE);
    if (psz <= 0) psz = 4096;
    uintptr_t aligned = ((uintptr_t)vp_addr) & ~((uintptr_t)psz - 1);
    size_t span = (size_t)(((uintptr_t)vp_addr + vp_sz) - aligned);
    int mprot = PROT_READ;
    if (vp_prot & 0x40) mprot = PROT_READ | PROT_WRITE | PROT_EXEC;
    else if (vp_prot & 0x20) mprot = PROT_READ | PROT_EXEC;
    else if (vp_prot & 0x04) mprot = PROT_READ | PROT_WRITE;
    BOOL vp_ok = (mprotect((void *)aligned, span ? span : (size_t)psz, mprot) == 0)
                     ? TRUE
                     : FALSE;
#endif
    ValFree(&vp_pr);
    ValFree(&vp_sz2);
    ValFree(&vp_ad);
    PUSH(ValBool(vp_ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsWow64QueryC") == 0) {
    AKSValue wq2 = ValStruct("Wow64Info");
    AKSStruct *wq2_s = ValToObj(wq2);
    wq2_s->fieldnames = malloc(2 * sizeof(char *));
    wq2_s->fieldvals = malloc(2 * sizeof(AKSValue));
#ifdef _WIN32
    BOOL wq2_is64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &wq2_is64);
    SYSTEM_INFO wq2_si;
    memset(&wq2_si, 0, sizeof(wq2_si));
    GetNativeSystemInfo(&wq2_si);
    wq2_s->fieldnames[0] = strdup("is_wow64");
    wq2_s->fieldvals[0] = ValBool(wq2_is64);
    wq2_s->fieldnames[1] = strdup("native_cpu_arch");
    wq2_s->fieldvals[1] = ValInt((int64_t)wq2_si.wProcessorArchitecture);
#else
    wq2_s->fieldnames[0] = strdup("is_wow64");
    wq2_s->fieldvals[0] = ValBool(0);
    wq2_s->fieldnames[1] = strdup("native_cpu_arch");
#if defined(__aarch64__) || defined(_M_ARM64)
    wq2_s->fieldvals[1] = ValInt(12);
#else
    wq2_s->fieldvals[1] = ValInt(9);
#endif
#endif
    wq2_s->fieldcount = 2;
    PUSH(wq2);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsNamedEventCreateC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: OsNamedEventCreateC() expects 2 args "
                      "(name, manual_reset)\n");
      return -1;
    }
    AKSValue ne_mr = POP(), ne_nv = POP();
    int ne_manual = ValToBool(ne_mr);
#ifdef _WIN32
    const char *ne_name =
        (ValKind(ne_nv) == VALSTR) ? ValToStrPtr(ne_nv)->data : "AksEvent";
    HANDLE ne_h = CreateEventA(NULL, ne_manual, FALSE, ne_name);
#else
    HANDLE ne_h = LnxCreateEvent(ne_manual, FALSE);
#endif
    ValFree(&ne_mr);
    ValFree(&ne_nv);
    PUSH(ValInt((int64_t)(uintptr_t)ne_h));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsNamedEventSetC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsNamedEventSetC() expects 1 arg\n");
      return -1;
    }
    AKSValue ne_hv = POP();
    HANDLE ne_h2 = (HANDLE)(uintptr_t)ValToInt(ne_hv);
    ValFree(&ne_hv);
    PUSH(ValBool(SetEvent(ne_h2)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsNamedEventResetC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsNamedEventResetC() expects 1 arg\n");
      return -1;
    }
    AKSValue ne_hv = POP();
    HANDLE ne_h2 = (HANDLE)(uintptr_t)ValToInt(ne_hv);
    ValFree(&ne_hv);
    PUSH(ValBool(ResetEvent(ne_h2)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsNamedEventWaitC") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: OsNamedEventWaitC() expects 2 args (handle, ms)\n");
      return -1;
    }
    AKSValue ne_ms = POP(), ne_hv = POP();
    HANDLE ne_h2 = (HANDLE)(uintptr_t)ValToInt(ne_hv);
    DWORD ne_wait_ms = (DWORD)ValToInt(ne_ms);
    DWORD ne_wr = WaitForSingleObject(ne_h2, ne_wait_ms);
    ValFree(&ne_ms);
    ValFree(&ne_hv);
    PUSH(ValBool(ne_wr == WAIT_OBJECT_0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsNamedEventCloseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsNamedEventCloseC() expects 1 arg\n");
      return -1;
    }
    AKSValue ne_hv = POP();
    HANDLE ne_h2 = (HANDLE)(uintptr_t)ValToInt(ne_hv);
    ValFree(&ne_hv);
    PUSH(ValBool(CloseHandle(ne_h2)));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSymlinkCreateC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: OsSymlinkCreateC() expects 3 args (link, "
                      "target, is_dir)\n");
      return -1;
    }
    AKSValue sl_dv = POP(), sl_tv = POP(), sl_lv = POP();
    int sl_isdir = ValToBool(sl_dv);
    const char *sl_lnk =
        (ValKind(sl_lv) == VALSTR) ? ValToStrPtr(sl_lv)->data : "";
    const char *sl_tgt =
        (ValKind(sl_tv) == VALSTR) ? ValToStrPtr(sl_tv)->data : "";
#ifdef _WIN32
    wchar_t sl_wlnk[MAX_PATH], sl_wtgt[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, sl_lnk, -1, sl_wlnk, MAX_PATH);
    MultiByteToWideChar(CP_UTF8, 0, sl_tgt, -1, sl_wtgt, MAX_PATH);
    DWORD sl_flags = (DWORD)(sl_isdir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
    sl_flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    BOOL sl_ok = CreateSymbolicLinkW(sl_wlnk, sl_wtgt, sl_flags);
#else
    (void)sl_isdir;
    BOOL sl_ok = (symlink(sl_tgt, sl_lnk) == 0) ? TRUE : FALSE;
#endif
    ValFree(&sl_dv);
    ValFree(&sl_tv);
    ValFree(&sl_lv);
    PUSH(ValBool(sl_ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsSymlinkReadC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: OsSymlinkReadC() expects 1 arg (path)\n");
      return -1;
    }
    AKSValue sl_pv = POP();
    const char *sl_path =
        (ValKind(sl_pv) == VALSTR) ? ValToStrPtr(sl_pv)->data : "";
    AKSValue sl_res = ValNull();
#ifdef _WIN32
    wchar_t sl_wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, sl_path, -1, sl_wpath, MAX_PATH);
    HANDLE sl_fh = CreateFileW(
        sl_wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (sl_fh != INVALID_HANDLE_VALUE) {
      wchar_t sl_final[MAX_PATH] = {0};
      DWORD sl_n = GetFinalPathNameByHandleW(sl_fh, sl_final, MAX_PATH,
                                             FILE_NAME_NORMALIZED);
      if (sl_n > 0 && sl_n < MAX_PATH) {
        char sl_finalA[MAX_PATH] = {0};
        WideCharToMultiByte(CP_UTF8, 0, sl_final, -1, sl_finalA, MAX_PATH, NULL,
                            NULL);
        sl_res = ValStr(sl_finalA);
      }
      CloseHandle(sl_fh);
    }
#else
    char sl_buf[4096];
    ssize_t sl_n = readlink(sl_path, sl_buf, sizeof(sl_buf) - 1);
    if (sl_n >= 0) {
      sl_buf[sl_n] = '\0';
      sl_res = ValStr(sl_buf);
    }
#endif
    ValFree(&sl_pv);
    PUSH(sl_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NtpQueryC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: NtpQueryC() expects 1 arg (server)\n");
      return -1;
    }
    AKSValue SV = POP();
    const char *ntp_srv =
        (ValKind(SV) == VALSTR) ? ValToStrPtr(SV)->data : "pool.ntp.org";
    WSADATA ntp_wsa;
    WSAStartup(MAKEWORD(2, 2), &ntp_wsa);
    struct addrinfo ntp_hints;
    memset(&ntp_hints, 0, sizeof(ntp_hints));
    ntp_hints.ai_family = AF_INET;
    ntp_hints.ai_socktype = SOCK_DGRAM;
    ntp_hints.ai_protocol = IPPROTO_UDP;
    struct addrinfo *ntp_ai = NULL;
    int64_t ntp_ts = 0;
    if (getaddrinfo(ntp_srv, "123", &ntp_hints, &ntp_ai) == 0 && ntp_ai) {
      SOCKET ntp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (ntp_sock != INVALID_SOCKET) {
        int ntp_tmo_ms = 2000;
#ifdef _WIN32
        DWORD ntp_tmo = (DWORD)ntp_tmo_ms;
        setsockopt(ntp_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ntp_tmo,
                   sizeof(ntp_tmo));
#else
        struct timeval ntp_tv;
        ntp_tv.tv_sec = ntp_tmo_ms / 1000;
        ntp_tv.tv_usec = (ntp_tmo_ms % 1000) * 1000;
        setsockopt(ntp_sock, SOL_SOCKET, SO_RCVTIMEO, &ntp_tv, sizeof(ntp_tv));
#endif
        uint8_t ntp_pkt[48] = {0};
        ntp_pkt[0] = 0x1b;
        sendto(ntp_sock, (char *)ntp_pkt, 48, 0, ntp_ai->ai_addr,
               (int)ntp_ai->ai_addrlen);
        uint8_t ntp_resp[48] = {0};
        struct sockaddr_in ntp_from;
        socklen_t ntp_flen = sizeof(ntp_from);
        if (recvfrom(ntp_sock, (char *)ntp_resp, 48, 0,
                     (struct sockaddr *)&ntp_from, &ntp_flen) >= 48) {
          uint32_t ntp_secs;
          memcpy(&ntp_secs, ntp_resp + 40, 4);
          ntp_secs = ntohl(ntp_secs);
          ntp_ts = (int64_t)ntp_secs - 2208988800LL;
        }
        closesocket(ntp_sock);
      }
      freeaddrinfo(ntp_ai);
    }
    ValFree(&SV);
    PUSH(ValInt(ntp_ts));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UdpMulticastJoinC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: UdpMulticastJoinC() expects 2 args "
                      "(group_ip, port)\n");
      return -1;
    }
    AKSValue PrV = POP(), GV = POP();
    int mc_port = (int)ValToInt(PrV);
    const char *mc_grp =
        (ValKind(GV) == VALSTR) ? ValToStrPtr(GV)->data : "224.0.0.1";
    WSADATA mc_wsa;
    WSAStartup(MAKEWORD(2, 2), &mc_wsa);
    SOCKET mc_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int mc_ok = 0;
    if (mc_sock != INVALID_SOCKET) {
      struct sockaddr_in mc_addr;
      memset(&mc_addr, 0, sizeof(mc_addr));
      mc_addr.sin_family = AF_INET;
      mc_addr.sin_port = htons((u_short)mc_port);
      mc_addr.sin_addr.s_addr = INADDR_ANY;
      int mc_reuse = 1;
      setsockopt(mc_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&mc_reuse,
                 sizeof(mc_reuse));
      if (bind(mc_sock, (struct sockaddr *)&mc_addr, sizeof(mc_addr)) == 0) {
        struct ip_mreq mc_mreq;
        mc_mreq.imr_multiaddr.s_addr = inet_addr(mc_grp);
        mc_mreq.imr_interface.s_addr = INADDR_ANY;
        mc_ok = setsockopt(mc_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                           (char *)&mc_mreq, sizeof(mc_mreq)) == 0;
      }
      closesocket(mc_sock);
    }
    ValFree(&PrV);
    ValFree(&GV);
    PUSH(ValBool(mc_ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Socks5ConnectC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: Socks5ConnectC() expects 3 args "
                      "(proxy_host, proxy_port, target_host:port)\n");
      return -1;
    }
    AKSValue TgV = POP(), PoV = POP(), HoV = POP();
    const char *s5_proxy =
        (ValKind(HoV) == VALSTR) ? ValToStrPtr(HoV)->data : "127.0.0.1";
    int s5_port = (int)ValToInt(PoV);
    const char *s5_tgt =
        (ValKind(TgV) == VALSTR) ? ValToStrPtr(TgV)->data : "example.com:80";
    char s5_thost[256] = {0};
    int s5_tport = 80;
    sscanf(s5_tgt, "%255[^:]:%d", s5_thost, &s5_tport);
    WSADATA s5_wsa;
    WSAStartup(MAKEWORD(2, 2), &s5_wsa);
    struct sockaddr_in s5_addr;
    memset(&s5_addr, 0, sizeof(s5_addr));
    s5_addr.sin_family = AF_INET;
    s5_addr.sin_port = htons((u_short)s5_port);
    s5_addr.sin_addr.s_addr = inet_addr(s5_proxy);
    SOCKET s5_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int s5_ok = 0;
    int64_t s5_handle = -1;
    if (s5_sock != INVALID_SOCKET) {
#ifdef _WIN32
      DWORD s5_tmo = 2000;
      setsockopt(s5_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&s5_tmo,
                 sizeof(s5_tmo));
#else
      struct timeval s5_tv = {2, 0};
      setsockopt(s5_sock, SOL_SOCKET, SO_RCVTIMEO, &s5_tv, sizeof(s5_tv));
#endif
      if (connect(s5_sock, (struct sockaddr *)&s5_addr, sizeof(s5_addr)) == 0) {
        uint8_t s5_greet[3] = {5, 1, 0};
        send(s5_sock, (char *)s5_greet, 3, 0);
        uint8_t s5_gresp[2] = {0};
        recv(s5_sock, (char *)s5_gresp, 2, 0);
        if (s5_gresp[1] == 0) {
          uint8_t s5_req[300];
          int s5_rlen = 4;
          s5_req[0] = 5;
          s5_req[1] = 1;
          s5_req[2] = 0;
          s5_req[3] = 3;
          int s5_hnl = (int)strlen(s5_thost);
          s5_req[4] = (uint8_t)s5_hnl;
          memcpy(s5_req + 5, s5_thost, s5_hnl);
          s5_rlen = 5 + s5_hnl;
          s5_req[s5_rlen++] = (uint8_t)((s5_tport >> 8) & 0xff);
          s5_req[s5_rlen++] = (uint8_t)(s5_tport & 0xff);
          send(s5_sock, (char *)s5_req, s5_rlen, 0);
          uint8_t s5_rresp[10] = {0};
          recv(s5_sock, (char *)s5_rresp, 10, 0);
          if (s5_rresp[1] == 0) {
            s5_ok = 1;
            s5_handle = (int64_t)(uintptr_t)s5_sock;
          }
        }
      }
      if (!s5_ok)
        closesocket(s5_sock);
    }
    ValFree(&TgV);
    ValFree(&PoV);
    ValFree(&HoV);
    PUSH(s5_ok ? ValInt(s5_handle) : ValInt(-1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SmtpSendC") == 0) {
    if (argc != 5) {
      fprintf(stderr, "Runtime error: SmtpSendC() expects 5 args (host, port, "
                      "from, to, body)\n");
      return -1;
    }
    AKSValue sm_bd = POP(), sm_to = POP(), sm_fr = POP(), sm_po = POP(),
             sm_ho = POP();
    const char *sm_host =
        (ValKind(sm_ho) == VALSTR) ? ValToStrPtr(sm_ho)->data : "localhost";
    int sm_port = (int)ValToInt(sm_po);
    const char *sm_from =
        (ValKind(sm_fr) == VALSTR) ? ValToStrPtr(sm_fr)->data : "a@b.com";
    const char *sm_tostr =
        (ValKind(sm_to) == VALSTR) ? ValToStrPtr(sm_to)->data : "b@c.com";
    const char *sm_body =
        (ValKind(sm_bd) == VALSTR) ? ValToStrPtr(sm_bd)->data : "Hello";
    WSADATA sm_wsa;
    WSAStartup(MAKEWORD(2, 2), &sm_wsa);
    struct addrinfo sm_hints;
    memset(&sm_hints, 0, sizeof(sm_hints));
    sm_hints.ai_family = AF_INET;
    sm_hints.ai_socktype = SOCK_STREAM;
    sm_hints.ai_protocol = IPPROTO_TCP;
    char sm_portstr[16];
    sprintf(sm_portstr, "%d", sm_port);
    struct addrinfo *sm_ai = NULL;
    int sm_ok = 0;
    if (getaddrinfo(sm_host, sm_portstr, &sm_hints, &sm_ai) == 0 && sm_ai) {
      SOCKET sm_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (sm_sock != INVALID_SOCKET) {
#ifdef _WIN32
        DWORD sm_tmo = 2000;
        setsockopt(sm_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&sm_tmo,
                   sizeof(sm_tmo));
#else
        struct timeval sm_tv = {2, 0};
        setsockopt(sm_sock, SOL_SOCKET, SO_RCVTIMEO, &sm_tv, sizeof(sm_tv));
#endif
        if (connect(sm_sock, sm_ai->ai_addr, (int)sm_ai->ai_addrlen) == 0) {
          char sm_buf[4096] = {0};
          char sm_cmd[1024];
          recv(sm_sock, sm_buf, sizeof(sm_buf) - 1, 0);
          snprintf(sm_cmd, sizeof(sm_cmd), "EHLO localhost\r\n");
          send(sm_sock, sm_cmd, (int)strlen(sm_cmd), 0);
          recv(sm_sock, sm_buf, sizeof(sm_buf) - 1, 0);
          snprintf(sm_cmd, sizeof(sm_cmd), "MAIL FROM:<%s>\r\n", sm_from);
          send(sm_sock, sm_cmd, (int)strlen(sm_cmd), 0);
          recv(sm_sock, sm_buf, sizeof(sm_buf) - 1, 0);
          snprintf(sm_cmd, sizeof(sm_cmd), "RCPT TO:<%s>\r\n", sm_tostr);
          send(sm_sock, sm_cmd, (int)strlen(sm_cmd), 0);
          recv(sm_sock, sm_buf, sizeof(sm_buf) - 1, 0);
          send(sm_sock, "DATA\r\n", 6, 0);
          recv(sm_sock, sm_buf, sizeof(sm_buf) - 1, 0);
          send(sm_sock, sm_body, (int)strlen(sm_body), 0);
          send(sm_sock, "\r\n.\r\n", 5, 0);
          recv(sm_sock, sm_buf, sizeof(sm_buf) - 1, 0);
          sm_ok = (sm_buf[0] == '2');
          send(sm_sock, "QUIT\r\n", 6, 0);
        }
        closesocket(sm_sock);
      }
      freeaddrinfo(sm_ai);
    }
    ValFree(&sm_bd);
    ValFree(&sm_to);
    ValFree(&sm_fr);
    ValFree(&sm_po);
    ValFree(&sm_ho);
    PUSH(ValBool(sm_ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsIocpCreateC") == 0) {
#ifdef _WIN32
    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    PUSH(ValInt((int64_t)(uintptr_t)iocp));
#else
    AksPosixIocp *iocp = (AksPosixIocp *)calloc(1, sizeof(AksPosixIocp));
    if (iocp) {
      pthread_mutex_init(&iocp->m, NULL);
      pthread_cond_init(&iocp->c, NULL);
    }
    PUSH(ValInt((int64_t)(uintptr_t)iocp));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsIocpPostC") == 0) {
    if (argc != 3) {
      fprintf(
          stderr,
          "Runtime error: OsIocpPostC() expects 3 args (iocp, key, data)\n");
      return -1;
    }
    AKSValue dv = POP(), kv = POP(), hv = POP();
#ifdef _WIN32
    HANDLE iocp = (HANDLE)(uintptr_t)ValToInt(hv);
    ULONG_PTR key = (ULONG_PTR)ValToInt(kv);
    DWORD data = (DWORD)ValToInt(dv);
    BOOL ok4 = PostQueuedCompletionStatus(iocp, data, key, NULL);
#else
    AksPosixIocp *iocp = (AksPosixIocp *)(uintptr_t)ValToInt(hv);
    uint64_t key = (uint64_t)ValToInt(kv);
    uint32_t data = (uint32_t)ValToInt(dv);
    BOOL ok4 = FALSE;
    if (iocp) {
      pthread_mutex_lock(&iocp->m);
      if (iocp->count < 256) {
        iocp->q[iocp->tail].key = key;
        iocp->q[iocp->tail].bytes = data;
        iocp->tail = (iocp->tail + 1) % 256;
        iocp->count++;
        pthread_cond_signal(&iocp->c);
        ok4 = TRUE;
      }
      pthread_mutex_unlock(&iocp->m);
    }
#endif
    ValFree(&dv);
    ValFree(&kv);
    ValFree(&hv);
    PUSH(ValBool(ok4));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OsIocpGetC") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: OsIocpGetC() expects 2 args (iocp, timeout_ms)\n");
      return -1;
    }
    AKSValue tmv = POP(), hv = POP();
    BOOL ok5 = FALSE;
    uint64_t key2 = 0;
    uint32_t nbytes = 0;
#ifdef _WIN32
    HANDLE iocp = (HANDLE)(uintptr_t)ValToInt(hv);
    DWORD tmo = (DWORD)ValToInt(tmv);
    DWORD wn = 0;
    ULONG_PTR wk = 0;
    OVERLAPPED *ov = NULL;
    ok5 = GetQueuedCompletionStatus(iocp, &wn, &wk, &ov, tmo);
    key2 = (uint64_t)wk;
    nbytes = (uint32_t)wn;
#else
    AksPosixIocp *iocp = (AksPosixIocp *)(uintptr_t)ValToInt(hv);
    int64_t tmo = ValToInt(tmv);
    if (iocp) {
      pthread_mutex_lock(&iocp->m);
      if (iocp->count == 0 && tmo != 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += tmo / 1000;
        ts.tv_nsec += (tmo % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
          ts.tv_sec++;
          ts.tv_nsec -= 1000000000L;
        }
        int rc = 0;
        while (iocp->count == 0 && rc == 0)
          rc = pthread_cond_timedwait(&iocp->c, &iocp->m, &ts);
      }
      if (iocp->count > 0) {
        key2 = iocp->q[iocp->head].key;
        nbytes = iocp->q[iocp->head].bytes;
        iocp->head = (iocp->head + 1) % 256;
        iocp->count--;
        ok5 = TRUE;
      }
      pthread_mutex_unlock(&iocp->m);
    }
#endif
    AKSValue iocp_r = ValStruct("IocpResult");
    AKSStruct *iocp_s = ValToObj(iocp_r);
    iocp_s->fieldnames = malloc(3 * sizeof(char *));
    iocp_s->fieldvals = malloc(3 * sizeof(AKSValue));
    iocp_s->fieldnames[0] = strdup("ok");
    iocp_s->fieldvals[0] = ValBool(ok5);
    iocp_s->fieldnames[1] = strdup("key");
    iocp_s->fieldvals[1] = ValInt((int64_t)key2);
    iocp_s->fieldnames[2] = strdup("bytes");
    iocp_s->fieldvals[2] = ValInt((int64_t)nbytes);
    iocp_s->fieldcount = 3;
    ValFree(&tmv);
    ValFree(&hv);
    PUSH(iocp_r);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "StackUnwinderC") == 0) {
    void *frames[64];
#ifdef _WIN32
    int nframes = (int)RtlCaptureStackBackTrace(0, 64, frames, NULL);
#elif defined(__GLIBC__) || defined(__APPLE__)
    int nframes = backtrace(frames, 64);
#else
    int nframes = 0;
#endif
    AKSValue fu_arr = ValArr(nframes > 0 ? nframes : 1);
    AKSArray *fu_a = ValToArr(fu_arr);
    for (int i = 0; i < nframes; i++)
      AksArrPush(fu_a, ValInt((int64_t)(uintptr_t)frames[i]));
    PUSH(fu_arr);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WavFileParseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WavFileParseC() expects 1 arg (path)\n");
      return -1;
    }
    AKSValue PV = POP();
    const char *wv_path = (ValKind(PV) == VALSTR) ? ValToStrPtr(PV)->data : "";
    FILE *wv_f = fopen(wv_path, "rb");
    AKSValue wv_res = ValNull();
    if (wv_f) {
      uint8_t wv_hdr[44];
      int wv_r = (int)fread(wv_hdr, 1, 44, wv_f);
      if (wv_r == 44 && memcmp(wv_hdr, "RIFF", 4) == 0 &&
          memcmp(wv_hdr + 8, "WAVE", 4) == 0) {
        uint16_t wv_ch, wv_bits;
        uint32_t wv_sr, wv_br, wv_block;
        memcpy(&wv_ch, wv_hdr + 22, 2);
        memcpy(&wv_sr, wv_hdr + 24, 4);
        memcpy(&wv_br, wv_hdr + 28, 4);
        memcpy(&wv_block, wv_hdr + 32, 2);
        memcpy(&wv_bits, wv_hdr + 34, 2);
        uint32_t wv_datasz = 0;
        memcpy(&wv_datasz, wv_hdr + 40, 4);
        wv_res = ValStruct("WavFile");
        AKSStruct *wv_s = ValToObj(wv_res);
        wv_s->fieldnames = malloc(5 * sizeof(char *));
        wv_s->fieldvals = malloc(5 * sizeof(AKSValue));
        wv_s->fieldnames[0] = strdup("channels");
        wv_s->fieldvals[0] = ValInt(wv_ch);
        wv_s->fieldnames[1] = strdup("sample_rate");
        wv_s->fieldvals[1] = ValInt(wv_sr);
        wv_s->fieldnames[2] = strdup("bit_depth");
        wv_s->fieldvals[2] = ValInt(wv_bits);
        wv_s->fieldnames[3] = strdup("data_size");
        wv_s->fieldvals[3] = ValInt(wv_datasz);
        wv_s->fieldnames[4] = strdup("byte_rate");
        wv_s->fieldvals[4] = ValInt(wv_br);
        wv_s->fieldcount = 5;
      }
      fclose(wv_f);
    }
    ValFree(&PV);
    PUSH(wv_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "BmpFileParseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: BmpFileParseC() expects 1 arg (path)\n");
      return -1;
    }
    AKSValue PV = POP();
    const char *bmp_path = (ValKind(PV) == VALSTR) ? ValToStrPtr(PV)->data : "";
    FILE *bmp_f = fopen(bmp_path, "rb");
    AKSValue bmp_res = ValNull();
    if (bmp_f) {
      uint8_t bmp_hdr[54];
      int bmp_r = (int)fread(bmp_hdr, 1, 54, bmp_f);
      if (bmp_r == 54 && bmp_hdr[0] == 'B' && bmp_hdr[1] == 'M') {
        uint32_t bmp_w, bmp_h, bmp_off;
        uint16_t bmp_bpp;
        memcpy(&bmp_off, bmp_hdr + 10, 4);
        memcpy(&bmp_w, bmp_hdr + 18, 4);
        memcpy(&bmp_h, bmp_hdr + 22, 4);
        memcpy(&bmp_bpp, bmp_hdr + 28, 2);
        bmp_res = ValStruct("BmpFile");
        AKSStruct *bmp_s = ValToObj(bmp_res);
        bmp_s->fieldnames = malloc(4 * sizeof(char *));
        bmp_s->fieldvals = malloc(4 * sizeof(AKSValue));
        bmp_s->fieldnames[0] = strdup("width");
        bmp_s->fieldvals[0] = ValInt(bmp_w);
        bmp_s->fieldnames[1] = strdup("height");
        bmp_s->fieldvals[1] = ValInt(bmp_h);
        bmp_s->fieldnames[2] = strdup("bpp");
        bmp_s->fieldvals[2] = ValInt(bmp_bpp);
        bmp_s->fieldnames[3] = strdup("data_off");
        bmp_s->fieldvals[3] = ValInt(bmp_off);
        bmp_s->fieldcount = 4;
      }
      fclose(bmp_f);
    }
    ValFree(&PV);
    PUSH(bmp_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NtfsAdsWriteC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: NtfsAdsWriteC() expects 3 args (file, "
                      "stream, data)\n");
      return -1;
    }
    AKSValue dv2 = POP(), sv2 = POP(), fv2 = POP();
    int ads_ok = 0;
#ifdef _WIN32
    const char *ads_f = (ValKind(fv2) == VALSTR) ? ValToStrPtr(fv2)->data : "";
    const char *ads_s =
        (ValKind(sv2) == VALSTR) ? ValToStrPtr(sv2)->data : "default";
    const char *ads_d = (ValKind(dv2) == VALSTR) ? ValToStrPtr(dv2)->data : "";
    int ads_dl = (ValKind(dv2) == VALSTR) ? ValToStrPtr(dv2)->len : 0;
    char ads_path[MAX_PATH * 2];
    snprintf(ads_path, sizeof(ads_path), "%s:%s", ads_f, ads_s);
    HANDLE ads_h = CreateFileA(ads_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (ads_h != INVALID_HANDLE_VALUE) {
      DWORD ads_wr = 0;
      ads_ok = WriteFile(ads_h, (LPCVOID)ads_d, (DWORD)ads_dl, &ads_wr, NULL);
      CloseHandle(ads_h);
    }
#endif
    ValFree(&dv2);
    ValFree(&sv2);
    ValFree(&fv2);
    PUSH(ValBool(ads_ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "NtfsAdsReadC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: NtfsAdsReadC() expects 2 args (file, stream)\n");
      return -1;
    }
    AKSValue sv3 = POP(), fv3 = POP();
    AKSValue ads_res2 = ValNull();
#ifdef _WIN32
    const char *ads_f2 = (ValKind(fv3) == VALSTR) ? ValToStrPtr(fv3)->data : "";
    const char *ads_s2 =
        (ValKind(sv3) == VALSTR) ? ValToStrPtr(sv3)->data : "default";
    char ads_path2[MAX_PATH * 2];
    snprintf(ads_path2, sizeof(ads_path2), "%s:%s", ads_f2, ads_s2);
    HANDLE ads_h2 = CreateFileA(ads_path2, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (ads_h2 != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER ads_sz2;
      GetFileSizeEx(ads_h2, &ads_sz2);
      if (ads_sz2.QuadPart > 0 && ads_sz2.QuadPart < 1048576) {
        char *ads_buf = (char *)malloc((size_t)ads_sz2.QuadPart + 1);
        DWORD ads_rd = 0;
        if (ReadFile(ads_h2, ads_buf, (DWORD)ads_sz2.QuadPart, &ads_rd, NULL)) {
          ads_buf[ads_rd] = 0;
          ads_res2 = ValStrLen(ads_buf, (int)ads_rd);
        }
        free(ads_buf);
      }
      CloseHandle(ads_h2);
    }
#endif
    ValFree(&sv3);
    ValFree(&fv3);
    PUSH(ads_res2);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GdiBitmapCreateC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: GdiBitmapCreateC() expects 2 args (w, h)\n");
      return -1;
    }
    AKSValue hv2 = POP(), wv = POP();
    int gdi_w = (int)ValToInt(wv), gdi_h = (int)ValToInt(hv2);
    if (gdi_w <= 0)
      gdi_w = 1;
    if (gdi_h <= 0)
      gdi_h = 1;
#ifdef _WIN32
    BITMAPINFO gdi_bmi;
    memset(&gdi_bmi, 0, sizeof(gdi_bmi));
    gdi_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    gdi_bmi.bmiHeader.biWidth = gdi_w;
    gdi_bmi.bmiHeader.biHeight = -gdi_h;
    gdi_bmi.bmiHeader.biPlanes = 1;
    gdi_bmi.bmiHeader.biBitCount = 32;
    gdi_bmi.bmiHeader.biCompression = BI_RGB;
    void *gdi_bits = NULL;
    HDC gdi_dc = CreateCompatibleDC(NULL);
    HBITMAP gdi_bmp =
        CreateDIBSection(gdi_dc, &gdi_bmi, DIB_RGB_COLORS, &gdi_bits, NULL, 0);
#else
    void *gdi_bmp = calloc((size_t)gdi_w * (size_t)gdi_h * 4, 1);
    void *gdi_dc = NULL;
#endif
    AKSValue gdi_res = ValStruct("GdiBitmap");
    AKSStruct *gdi_s = ValToObj(gdi_res);
    gdi_s->fieldnames = malloc(4 * sizeof(char *));
    gdi_s->fieldvals = malloc(4 * sizeof(AKSValue));
    gdi_s->fieldnames[0] = strdup("w");
    gdi_s->fieldvals[0] = ValInt(gdi_w);
    gdi_s->fieldnames[1] = strdup("h");
    gdi_s->fieldvals[1] = ValInt(gdi_h);
    gdi_s->fieldnames[2] = strdup("hbmp");
    gdi_s->fieldvals[2] = ValInt((int64_t)(uintptr_t)gdi_bmp);
    gdi_s->fieldnames[3] = strdup("hdc");
    gdi_s->fieldvals[3] = ValInt((int64_t)(uintptr_t)gdi_dc);
    gdi_s->fieldcount = 4;
    ValFree(&hv2);
    ValFree(&wv);
    PUSH(gdi_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GdiBitmapSetPixelC") == 0) {
    if (argc != 4) {
      fprintf(stderr, "Runtime error: GdiBitmapSetPixelC() expects 4 args "
                      "(bmp, x, y, color)\n");
      return -1;
    }
    AKSValue cv = POP(), yv = POP(), xv = POP(), bv = POP();
    AKSStruct *gdi_s2 = ValToObj(bv);
#ifdef _WIN32
    HDC gdi_dc2 = (HDC)(uintptr_t)ValToInt(gdi_s2->fieldvals[3]);
    HGDIOBJ gdi_old = SelectObject(
        gdi_dc2, (HGDIOBJ)(uintptr_t)ValToInt(gdi_s2->fieldvals[2]));
    SetPixel(gdi_dc2, (int)ValToInt(xv), (int)ValToInt(yv),
             (COLORREF)ValToInt(cv));
    SelectObject(gdi_dc2, gdi_old);
#else
    int px = (int)ValToInt(xv), py = (int)ValToInt(yv);
    int w = (int)ValToInt(gdi_s2->fieldvals[0]), h = (int)ValToInt(gdi_s2->fieldvals[1]);
    uint8_t *buf = (uint8_t *)(uintptr_t)ValToInt(gdi_s2->fieldvals[2]);
    if (buf && px >= 0 && px < w && py >= 0 && py < h) {
      uint32_t cval = (uint32_t)ValToInt(cv);
      uint8_t *ppx = buf + (py * w + px) * 4;
      ppx[0] = (uint8_t)(cval & 0xFF);
      ppx[1] = (uint8_t)((cval >> 8) & 0xFF);
      ppx[2] = (uint8_t)((cval >> 16) & 0xFF);
      ppx[3] = 255;
    }
#endif
    ValFree(&cv);
    ValFree(&yv);
    ValFree(&xv);
    PUSH(bv);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GdiBitmapSaveC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: GdiBitmapSaveC() expects 2 args (bmp, path)\n");
      return -1;
    }
    AKSValue pv4 = POP(), bv = POP();
    const char *gdi_path =
        (ValKind(pv4) == VALSTR) ? ValToStrPtr(pv4)->data : "out.bmp";
    AKSStruct *gdi_s3 = ValToObj(bv);
    int gdi_w3 = (int)ValToInt(gdi_s3->fieldvals[0]),
        gdi_h3 = (int)ValToInt(gdi_s3->fieldvals[1]);
    int gdi_stride = ((gdi_w3 * 3 + 3) / 4) * 4;
    int gdi_fsz = 54 + gdi_stride * gdi_h3;
    uint8_t *gdi_data = calloc(gdi_fsz, 1);
    gdi_data[0] = 'B';
    gdi_data[1] = 'M';
    *(uint32_t *)(gdi_data + 2) = (uint32_t)gdi_fsz;
    *(uint32_t *)(gdi_data + 10) = 54;
    *(uint32_t *)(gdi_data + 14) = 40;
    *(int32_t *)(gdi_data + 18) = gdi_w3;
    *(int32_t *)(gdi_data + 22) = -gdi_h3;
    *(uint16_t *)(gdi_data + 26) = 1;
    *(uint16_t *)(gdi_data + 28) = 24;
    *(uint32_t *)(gdi_data + 34) = (uint32_t)(gdi_stride * gdi_h3);
#ifdef _WIN32
    HBITMAP gdi_hbmp = (HBITMAP)(uintptr_t)ValToInt(gdi_s3->fieldvals[2]);
    BITMAP gdi_bm;
    GetObject(gdi_hbmp, sizeof(gdi_bm), &gdi_bm);
    HDC gdi_dc3 = (HDC)(uintptr_t)ValToInt(gdi_s3->fieldvals[3]);
    BITMAPINFO gdi_bi2;
    memset(&gdi_bi2, 0, sizeof(gdi_bi2));
    gdi_bi2.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    gdi_bi2.bmiHeader.biWidth = gdi_w3;
    gdi_bi2.bmiHeader.biHeight = gdi_h3;
    gdi_bi2.bmiHeader.biPlanes = 1;
    gdi_bi2.bmiHeader.biBitCount = 24;
    gdi_bi2.bmiHeader.biCompression = BI_RGB;
    GetDIBits(gdi_dc3, gdi_hbmp, 0, (UINT)gdi_h3, gdi_data + 54, &gdi_bi2,
              DIB_RGB_COLORS);
#else
    uint8_t *srcbuf = (uint8_t *)(uintptr_t)ValToInt(gdi_s3->fieldvals[2]);
    if (srcbuf) {
      for (int y = 0; y < gdi_h3; y++) {
        for (int x = 0; x < gdi_w3; x++) {
          uint8_t *sp = srcbuf + (y * gdi_w3 + x) * 4;
          uint8_t *dp = gdi_data + 54 + y * gdi_stride + x * 3;
          dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0];
        }
      }
    }
#endif
    FILE *gdi_fp = fopen(gdi_path, "wb");
    int gdi_ok = 0;
    if (gdi_fp) {
      fwrite(gdi_data, 1, (size_t)gdi_fsz, gdi_fp);
      fclose(gdi_fp);
      gdi_ok = 1;
    }
    free(gdi_data);
    ValFree(&pv4);
    ValFree(&bv);
    PUSH(ValBool(gdi_ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "PdbSymbolResolveC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: PdbSymbolResolveC() expects 1 arg (addr)\n");
      return -1;
    }
    AKSValue AV7 = POP();
    uint64_t pdb_addr = (uint64_t)ValToInt(AV7);
    ValFree(&AV7);
    char pdb_name[256] = "?";
#ifdef _WIN32
    HANDLE pdb_proc = GetCurrentProcess();
    SymInitialize(pdb_proc, NULL, TRUE);
    char pdb_buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *pdb_sym = (SYMBOL_INFO *)pdb_buf;
    pdb_sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    pdb_sym->MaxNameLen = 255;
    DWORD64 pdb_disp = 0;
    if (SymFromAddr(pdb_proc, (DWORD64)pdb_addr, &pdb_disp, pdb_sym))
      strncpy(pdb_name, pdb_sym->Name, 255);
#else
    Dl_info dli;
    if (dladdr((void *)(uintptr_t)pdb_addr, &dli) && dli.dli_sname) {
      snprintf(pdb_name, sizeof(pdb_name), "%s", dli.dli_sname);
    }
#endif
    PUSH(ValStr(pdb_name));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "EtwEmitC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: EtwEmitC() expects 2 args "
                      "(provider_guid_str, message)\n");
      return -1;
    }
    AKSValue mv = POP(), gv = POP();
#ifdef _WIN32
    REGHANDLE etw_h = 0;
    GUID etw_guid = {0x12345678,
                     0x1234,
                     0x1234,
                     {0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc}};
    EventRegister(&etw_guid, NULL, NULL, &etw_h);
    if (ValKind(mv) == VALSTR) {
      EVENT_DESCRIPTOR etw_ed;
      memset(&etw_ed, 0, sizeof(etw_ed));
      etw_ed.Id = 1;
      etw_ed.Level = 4;
      EventWrite(etw_h, &etw_ed, 0, NULL);
    }
    EventUnregister(etw_h);
#endif
    ValFree(&mv);
    ValFree(&gv);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "PdhQueryC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: PdhQueryC() expects 1 arg (counter_path)\n");
      return -1;
    }
    AKSValue CV2 = POP();
    double pdh_val = 0.0;
#ifdef _WIN32
    const char *pdh_path = (ValKind(CV2) == VALSTR)
                               ? ValToStrPtr(CV2)->data
                               : "\\Processor(_Total)\\% Processor Time";
    PDH_HQUERY pdh_q = NULL;
    PDH_HCOUNTER pdh_c = NULL;
    if (PdhOpenQuery(NULL, 0, &pdh_q) == ERROR_SUCCESS) {
      if (PdhAddCounterA(pdh_q, pdh_path, 0, &pdh_c) == ERROR_SUCCESS) {
        PdhCollectQueryData(pdh_q);
        Sleep(100);
        PdhCollectQueryData(pdh_q);
        PDH_FMT_COUNTERVALUE pdh_fv;
        memset(&pdh_fv, 0, sizeof(pdh_fv));
        if (PdhGetFormattedCounterValue(pdh_c, PDH_FMT_DOUBLE, NULL, &pdh_fv) ==
            ERROR_SUCCESS)
          pdh_val = pdh_fv.doubleValue;
      }
      PdhCloseQuery(pdh_q);
    }
#endif
    ValFree(&CV2);
    PUSH(ValFloat(pdh_val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WinHttpGetC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: WinHttpGetC() expects 1 arg (url)\n");
      return -1;
    }
    AKSValue UV = POP();
    AKSValue wh_res = ValNull();
#ifdef _WIN32
    const char *wh_url = (ValKind(UV) == VALSTR) ? ValToStrPtr(UV)->data : "";
    wchar_t wh_wurl[2048];
    MultiByteToWideChar(CP_UTF8, 0, wh_url, -1, wh_wurl, 2048);
    URL_COMPONENTSW wh_uc;
    memset(&wh_uc, 0, sizeof(wh_uc));
    wh_uc.dwStructSize = sizeof(wh_uc);
    wchar_t wh_host[512] = {0};
    wchar_t wh_path2[1024] = {0};
    wh_uc.lpszHostName = wh_host;
    wh_uc.dwHostNameLength = 511;
    wh_uc.lpszUrlPath = wh_path2;
    wh_uc.dwUrlPathLength = 1023;
    WinHttpCrackUrl(wh_wurl, 0, 0, &wh_uc);
    HINTERNET wh_sess =
        WinHttpOpen(L"AksVM/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (wh_sess) {
      BOOL wh_https = wh_uc.nScheme == INTERNET_SCHEME_HTTPS;
      HINTERNET wh_conn =
          WinHttpConnect(wh_sess, wh_host,
                         wh_uc.nPort ? wh_uc.nPort : (wh_https ? 443 : 80), 0);
      if (wh_conn) {
        HINTERNET wh_req = WinHttpOpenRequest(
            wh_conn, L"GET", wh_path2[0] ? wh_path2 : L"/", NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            wh_https ? WINHTTP_FLAG_SECURE : 0);
        if (wh_req) {
          if (WinHttpSendRequest(wh_req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpReceiveResponse(wh_req, NULL);
            char *wh_body = NULL;
            size_t wh_blen = 0;
            DWORD wh_avail = 0, wh_rd = 0;
            while (WinHttpQueryDataAvailable(wh_req, &wh_avail) &&
                   wh_avail > 0) {
              wh_body = realloc(wh_body, wh_blen + wh_avail + 1);
              WinHttpReadData(wh_req, wh_body + wh_blen, wh_avail, &wh_rd);
              wh_blen += wh_rd;
            }
            if (wh_body) {
              wh_body[wh_blen] = 0;
              wh_res = ValStrLen(wh_body, (int)wh_blen);
              free(wh_body);
            }
          }
          WinHttpCloseHandle(wh_req);
        }
        WinHttpCloseHandle(wh_conn);
      }
      WinHttpCloseHandle(wh_sess);
    }
#endif
    ValFree(&UV);
    PUSH(wh_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WinHttpPostC") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: WinHttpPostC() expects 3 args (url, "
                      "content_type, body)\n");
      return -1;
    }
    AKSValue bv2 = POP(), ctv = POP(), uv2 = POP();
    AKSValue wp_res = ValNull();
#ifdef _WIN32
    const char *wp_url = (ValKind(uv2) == VALSTR) ? ValToStrPtr(uv2)->data : "";
    const char *wp_ct =
        (ValKind(ctv) == VALSTR) ? ValToStrPtr(ctv)->data : "application/json";
    const char *wp_body2 =
        (ValKind(bv2) == VALSTR) ? ValToStrPtr(bv2)->data : "";
    int wp_blen = (ValKind(bv2) == VALSTR) ? ValToStrPtr(bv2)->len : 0;
    wchar_t wp_wurl[2048];
    MultiByteToWideChar(CP_UTF8, 0, wp_url, -1, wp_wurl, 2048);
    wchar_t wp_wct[256];
    MultiByteToWideChar(CP_UTF8, 0, wp_ct, -1, wp_wct, 256);
    URL_COMPONENTSW wp_uc;
    memset(&wp_uc, 0, sizeof(wp_uc));
    wp_uc.dwStructSize = sizeof(wp_uc);
    wchar_t wp_host[512] = {0};
    wchar_t wp_path3[1024] = {0};
    wp_uc.lpszHostName = wp_host;
    wp_uc.dwHostNameLength = 511;
    wp_uc.lpszUrlPath = wp_path3;
    wp_uc.dwUrlPathLength = 1023;
    WinHttpCrackUrl(wp_wurl, 0, 0, &wp_uc);
    HINTERNET wp_sess =
        WinHttpOpen(L"AksVM/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (wp_sess) {
      BOOL wp_https = wp_uc.nScheme == INTERNET_SCHEME_HTTPS;
      HINTERNET wp_conn =
          WinHttpConnect(wp_sess, wp_host,
                         wp_uc.nPort ? wp_uc.nPort : (wp_https ? 443 : 80), 0);
      if (wp_conn) {
        HINTERNET wp_req = WinHttpOpenRequest(
            wp_conn, L"POST", wp_path3[0] ? wp_path3 : L"/", NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            wp_https ? WINHTTP_FLAG_SECURE : 0);
        if (wp_req) {
          wchar_t wp_hdrs[512];
          swprintf(wp_hdrs, 512, L"Content-Type: %s\r\n", wp_wct);
          if (WinHttpSendRequest(wp_req, wp_hdrs, (DWORD)-1L, (LPVOID)wp_body2,
                                 (DWORD)wp_blen, (DWORD)wp_blen, 0)) {
            WinHttpReceiveResponse(wp_req, NULL);
            char *wp_rbody = NULL;
            size_t wp_rblen = 0;
            DWORD wp_avail2 = 0, wp_rd2 = 0;
            while (WinHttpQueryDataAvailable(wp_req, &wp_avail2) &&
                   wp_avail2 > 0) {
              wp_rbody = realloc(wp_rbody, wp_rblen + wp_avail2 + 1);
              WinHttpReadData(wp_req, wp_rbody + wp_rblen, wp_avail2, &wp_rd2);
              wp_rblen += wp_rd2;
            }
            if (wp_rbody) {
              wp_rbody[wp_rblen] = 0;
              wp_res = ValStrLen(wp_rbody, (int)wp_rblen);
              free(wp_rbody);
            }
          }
          WinHttpCloseHandle(wp_req);
        }
        WinHttpCloseHandle(wp_conn);
      }
      WinHttpCloseHandle(wp_sess);
    }
#endif
    ValFree(&bv2);
    ValFree(&ctv);
    ValFree(&uv2);
    PUSH(wp_res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ShmRingNewC") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ShmRingNewC() expects 2 args (name, "
                      "capacity_bytes)\n");
      return -1;
    }
    AKSValue cv = POP(), nv = POP();
    const char *rname =
        (ValKind(nv) == VALSTR) ? ValToStrPtr(nv)->data : "aks_shm_ring";
    uint64_t cap = (uint64_t)ValToInt(cv);
    if (cap < 64)
      cap = 64;
    size_t total_sz = sizeof(ShmRingHdr) + cap;
    void *view = NULL;
    int is_creator = 0;
#ifdef _WIN32
    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                     0, (DWORD)total_sz, rname);
    if (hMap) {
      if (GetLastError() != ERROR_ALREADY_EXISTS)
        is_creator = 1;
      view = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    }
#else
    char shm_path[256];
    snprintf(shm_path, sizeof(shm_path), "/%s", (rname[0] == '/') ? rname + 1 : rname);
    int shm_fd = shm_open(shm_path, O_CREAT | O_RDWR, 0666);
    HANDLE hMap = NULL;
    if (shm_fd >= 0) {
      struct stat shm_st;
      if (fstat(shm_fd, &shm_st) == 0 && (size_t)shm_st.st_size < total_sz) {
        if (ftruncate(shm_fd, (off_t)total_sz) == 0)
          is_creator = 1;
      }
      view = mmap(NULL, total_sz, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
      if (view == MAP_FAILED)
        view = NULL;
      hMap = (HANDLE)(uintptr_t)(shm_fd + 1);
    }
#endif
    if (view && is_creator) {
      ShmRingHdr *hdr = (ShmRingHdr *)view;
      hdr->head = 0;
      hdr->tail = 0;
      hdr->capacity = cap;
    }
    AKSValue sval = ValStruct("ShmRing");
    AKSStruct *s = ValToObj(sval);
    s->fieldnames = malloc(4 * sizeof(char *));
    s->fieldvals = malloc(4 * sizeof(AKSValue));
    s->fieldnames[0] = strdup("name");
    s->fieldvals[0] = ValStr(rname);
    s->fieldnames[1] = strdup("capacity");
    s->fieldvals[1] = ValInt((int64_t)cap);
    s->fieldnames[2] = strdup("hmap");
    s->fieldvals[2] = ValInt((int64_t)(uintptr_t)hMap);
    s->fieldnames[3] = strdup("view");
    s->fieldvals[3] = ValInt((int64_t)(uintptr_t)view);
    s->fieldcount = 4;
    ValFree(&cv);
    ValFree(&nv);
    PUSH(sval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ShmRingPushC") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: ShmRingPushC() expects 2 args (shm, data_str)\n");
      return -1;
    }
    AKSValue dv = POP(), sv = POP();
    AKSStruct *s = ValToObj(sv);
    void *view = (void *)(uintptr_t)ValToInt(s->fieldvals[3]);
    int pushed = 0;
    if (view && ValKind(dv) == VALSTR) {
      ShmRingHdr *hdr = (ShmRingHdr *)view;
      uint8_t *buf = (uint8_t *)(hdr + 1);
      const char *src = ValToStrPtr(dv)->data;
      uint32_t len = (uint32_t)ValToStrPtr(dv)->len;
      uint64_t cap = hdr->capacity;
      uint64_t head = hdr->head;
      uint64_t tail = hdr->tail;
      uint64_t used = tail - head;
      if (cap - used >= len + 4) {
        uint8_t lb[4];
        memcpy(lb, &len, 4);
        for (int i = 0; i < 4; i++) {
          buf[(tail + i) % cap] = lb[i];
        }
        for (uint32_t i = 0; i < len; i++) {
          buf[(tail + 4 + i) % cap] = (uint8_t)src[i];
        }
        MemoryBarrier();
        hdr->tail = tail + len + 4;
        pushed = 1;
      }
    }
    ValFree(&dv);
    ValFree(&sv);
    PUSH(ValBool(pushed));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ShmRingPopC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ShmRingPopC() expects 1 arg (shm)\n");
      return -1;
    }
    AKSValue sv = POP();
    AKSStruct *s = ValToObj(sv);
    void *view = (void *)(uintptr_t)ValToInt(s->fieldvals[3]);
    AKSValue res = ValNull();
    if (view) {
      ShmRingHdr *hdr = (ShmRingHdr *)view;
      uint8_t *buf = (uint8_t *)(hdr + 1);
      uint64_t cap = hdr->capacity;
      uint64_t head = hdr->head;
      uint64_t tail = hdr->tail;
      if (tail > head && (tail - head) >= 4) {
        uint8_t lb[4];
        for (int i = 0; i < 4; i++) {
          lb[i] = buf[(head + i) % cap];
        }
        uint32_t len = 0;
        memcpy(&len, lb, 4);
        if ((tail - head) >= len + 4) {
          char *out = malloc(len + 1);
          if (out) {
            for (uint32_t i = 0; i < len; i++) {
              out[i] = (char)buf[(head + 4 + i) % cap];
            }
            out[len] = '\0';
            MemoryBarrier();
            hdr->head = head + len + 4;
            res = ValStrLen(out, (int)len);
            free(out);
          }
        }
      }
    }
    ValFree(&sv);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ShmRingAvailableC") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ShmRingAvailableC() expects 1 arg (shm)\n");
      return -1;
    }
    AKSValue sv = POP();
    AKSStruct *s = ValToObj(sv);
    void *view = (void *)(uintptr_t)ValToInt(s->fieldvals[3]);
    int64_t avail = 0;
    if (view) {
      ShmRingHdr *hdr = (ShmRingHdr *)view;
      avail = (int64_t)(hdr->tail - hdr->head);
      if (avail < 0)
        avail = 0;
    }
    ValFree(&sv);
    PUSH(ValInt(avail));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ShmRingCloseC") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ShmRingCloseC() expects 1 arg (shm)\n");
      return -1;
    }
    AKSValue sv = POP();
    AKSStruct *s = ValToObj(sv);
    HANDLE hMap = (HANDLE)(uintptr_t)ValToInt(s->fieldvals[2]);
    void *view = (void *)(uintptr_t)ValToInt(s->fieldvals[3]);
#ifdef _WIN32
    if (view)
      UnmapViewOfFile(view);
    if (hMap && hMap != INVALID_HANDLE_VALUE)
      CloseHandle(hMap);
#else
    if (view) {
      ShmRingHdr *hdr = (ShmRingHdr *)view;
      munmap(view, sizeof(ShmRingHdr) + (size_t)hdr->capacity);
    }
    int shm_fd = (int)((intptr_t)hMap) - 1;
    if (shm_fd >= 0)
      close(shm_fd);
#endif
    s->fieldvals[2] = ValInt(0);
    s->fieldvals[3] = ValInt(0);
    ValFree(&sv);
    PUSH(ValBool(1));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DnsResolve") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: DnsResolve() expects 1 argument, got %d\n", argc);
      return -1;
    }
    AKSValue hostnameval = POP();
    if (ValKind(hostnameval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: DnsResolve() argument must be a string\n");
      ValFree(&hostnameval);
      return -1;
    }
    const char *hostname = ValToStrPtr(hostnameval)->data;
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(hostname, NULL, &hints, &res);
    if (err != 0 || res == NULL) {
      PUSH(ValNull());
    } else {
      struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
      char ipstr[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, sizeof(ipstr));
      PUSH(ValStr(ipstr));
      freeaddrinfo(res);
    }
    ValFree(&hostnameval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TlsHandshake") == 0) {
    if (argc != 3) {
      fprintf(stderr,
              "Runtime error: TlsHandshake() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue is_server_val = POP();
    AKSValue hostname_val = POP();
    AKSValue sock_val = POP();

    int sock = (int)ValToInt(sock_val);
    const char *hostname = ValToStrPtr(hostname_val)->data;
    int is_server = ValToBool(is_server_val);

    int tls_id = CreateTLSContext((SOCKET)sock, is_server);
    if (tls_id < 0) {
      PUSH(ValInt(-1));
    } else {
      int hs_res = PerformTLSHandshake(tls_id, hostname);
      if (hs_res < 0) {
        TLSCloseContext(tls_id);
        PUSH(ValInt(-1));
      } else {
        PUSH(ValInt(tls_id));
      }
    }
    ValFree(&is_server_val);
    ValFree(&hostname_val);
    ValFree(&sock_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TlsSend") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: TlsSend() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue dataval = POP();
    AKSValue tls_id_val = POP();
    int tls_id = (int)ValToInt(tls_id_val);
    const char *data = ValToStrPtr(dataval)->data;
    int len = ValToStrPtr(dataval)->len;

    int sent = TLSSendData(tls_id, data, len);
    PUSH(ValInt(sent));
    ValFree(&dataval);
    ValFree(&tls_id_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TlsReceive") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: TlsReceive() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue maxval = POP();
    AKSValue tls_id_val = POP();
    int tls_id = (int)ValToInt(tls_id_val);
    int max_len = (int)ValToInt(maxval);

    char *buf = malloc(max_len + 1);
    int recvd = TLSRecvData(tls_id, buf, max_len);
    if (recvd <= 0) {
      PUSH(ValStr(""));
    } else {
      buf[recvd] = '\0';
      AKSValue strval = ValStr(buf);
      ValToStrPtr(strval)->len = recvd;
      PUSH(strval);
    }
    free(buf);
    ValFree(&maxval);
    ValFree(&tls_id_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TlsClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: TlsClose() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue tls_id_val = POP();
    int tls_id = (int)ValToInt(tls_id_val);
    TLSCloseContext(tls_id);
    ValFree(&tls_id_val);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ShmOpen") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: ShmOpen() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sizeval = POP();
    AKSValue nameval = POP();
    if (ValKind(nameval) != VALSTR || ValKind(sizeval) != VALINT) {
      fprintf(stderr,
              "Runtime error: ShmOpen() arguments must be (string, int)\n");
      ValFree(&nameval);
      ValFree(&sizeval);
      return -1;
    }
    const char *shmname = ValToStrPtr(nameval)->data;
    int64_t shmsize = ValToInt(sizeval);

#ifdef _WIN32
    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)shmsize, shmname);
    if (hMapFile == NULL) {
      hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmname);
    }
    if (hMapFile == NULL) {
      PUSH(ValNull());
      ValFree(&nameval);
      ValFree(&sizeval);
      *stacktopptr = stacktop;
      return 1;
    }
    void *pBuf = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, shmsize);
    if (pBuf == NULL) {
      CloseHandle(hMapFile);
      PUSH(ValNull());
      ValFree(&nameval);
      ValFree(&sizeval);
      *stacktopptr = stacktop;
      return 1;
    }

    AKSValue arrelems = ValArr(3);
    ValToArr(arrelems)->items[0] = ValInt((int64_t)pBuf);
    ValToArr(arrelems)->items[1] = ValInt((int64_t)hMapFile);
    ValToArr(arrelems)->items[2] = ValInt(shmsize);
    ValToArr(arrelems)->count = 3;

    PUSH(arrelems);
#else
    char shm_path[256];
    if (shmname[0] == '/') {
      strncpy(shm_path, shmname, sizeof(shm_path) - 1);
    } else {
      snprintf(shm_path, sizeof(shm_path), "/%s", shmname);
    }
    shm_path[sizeof(shm_path) - 1] = '\0';
    int fd = shm_open(shm_path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
      fd = shm_open(shm_path, O_RDWR, 0666);
    }
    if (fd < 0) {
      PUSH(ValNull());
      ValFree(&nameval);
      ValFree(&sizeval);
      *stacktopptr = stacktop;
      return 1;
    }
    if (ftruncate(fd, shmsize) != 0) {
      close(fd);
      PUSH(ValNull());
      ValFree(&nameval);
      ValFree(&sizeval);
      *stacktopptr = stacktop;
      return 1;
    }
    void *pBuf = mmap(NULL, shmsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (pBuf == MAP_FAILED) {
      PUSH(ValNull());
      ValFree(&nameval);
      ValFree(&sizeval);
      *stacktopptr = stacktop;
      return 1;
    }

    AKSValue arrelems = ValArr(3);
    ValToArr(arrelems)->items[0] = ValInt((int64_t)(uintptr_t)pBuf);
    ValToArr(arrelems)->items[1] = ValInt(-1);
    ValToArr(arrelems)->items[2] = ValInt(shmsize);
    ValToArr(arrelems)->count = 3;

    PUSH(arrelems);
#endif
    ValFree(&nameval);
    ValFree(&sizeval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ShmWrite") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: ShmWrite() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue dataval = POP();
    AKSValue offsetval = POP();
    AKSValue shmval = POP();

    if (ValKind(shmval) != VALARR || ValToArr(shmval)->count < 3 ||
        ValKind(offsetval) != VALINT || ValKind(dataval) != VALSTR) {
      fprintf(
          stderr,
          "Runtime error: ShmWrite() arguments must be (array, int, string)\n");
      ValFree(&shmval);
      ValFree(&offsetval);
      ValFree(&dataval);
      return -1;
    }

    void *pBuf = (void *)ValToInt(ValToArr(shmval)->items[0]);
    int64_t maxsize = ValToInt(ValToArr(shmval)->items[2]);
    int64_t offset = ValToInt(offsetval);
    const char *data = ValToStrPtr(dataval)->data;
    size_t len = ValToStrPtr(dataval)->len;

    if (offset + len > (size_t)maxsize) {
      len = (size_t)(maxsize - offset);
    }
    if (len > 0) {
      memcpy((char *)pBuf + offset, data, len);
    }

    PUSH(ValInt(len));
    ValFree(&shmval);
    ValFree(&offsetval);
    ValFree(&dataval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ShmRead") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: ShmRead() expects 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue sizeval = POP();
    AKSValue offsetval = POP();
    AKSValue shmval = POP();

    if (ValKind(shmval) != VALARR || ValToArr(shmval)->count < 3 ||
        ValKind(offsetval) != VALINT || ValKind(sizeval) != VALINT) {
      fprintf(stderr,
              "Runtime error: ShmRead() arguments must be (array, int, int)\n");
      ValFree(&shmval);
      ValFree(&offsetval);
      ValFree(&sizeval);
      return -1;
    }

    void *pBuf = (void *)ValToInt(ValToArr(shmval)->items[0]);
    int64_t maxsize = ValToInt(ValToArr(shmval)->items[2]);
    int64_t offset = ValToInt(offsetval);
    int64_t size = ValToInt(sizeval);

    if (offset + size > maxsize) {
      size = maxsize - offset;
    }
    if (size < 0)
      size = 0;

    char *buf = malloc(size + 1);
    if (size > 0) {
      memcpy(buf, (char *)pBuf + offset, size);
    }
    buf[size] = '\0';

    PUSH(ValStr(buf));
    free(buf);
    ValFree(&shmval);
    ValFree(&offsetval);
    ValFree(&sizeval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ShmClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: ShmClose() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue shmval = POP();
    if (ValKind(shmval) == VALARR && ValToArr(shmval)->count >= 3) {
      void *pBuf = (void *)(uintptr_t)ValToInt(ValToArr(shmval)->items[0]);
#ifdef _WIN32
      HANDLE hMapFile = (HANDLE)ValToInt(ValToArr(shmval)->items[1]);
      UnmapViewOfFile(pBuf);
      CloseHandle(hMapFile);
#else
      int64_t shmsize = ValToInt(ValToArr(shmval)->items[2]);
      munmap(pBuf, shmsize);
#endif
    }
    PUSH(ValNull());
    ValFree(&shmval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "html") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: html() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: html() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    const char *src = ValToStrPtr(arg)->data;
    size_t newlen = 0;
    for (int i = 0; src[i] != '\0'; i++) {
      if (src[i] == '<')
        newlen += 4;
      else if (src[i] == '>')
        newlen += 4;
      else if (src[i] == '&')
        newlen += 5;
      else if (src[i] == '"')
        newlen += 6;
      else if (src[i] == '\'')
        newlen += 6;
      else
        newlen++;
    }
    char *dest = malloc(newlen + 1);
    size_t d = 0;
    for (int i = 0; src[i] != '\0'; i++) {
      if (src[i] == '<') {
        strcpy(dest + d, "&lt;");
        d += 4;
      } else if (src[i] == '>') {
        strcpy(dest + d, "&gt;");
        d += 4;
      } else if (src[i] == '&') {
        strcpy(dest + d, "&amp;");
        d += 5;
      } else if (src[i] == '"') {
        strcpy(dest + d, "&quot;");
        d += 6;
      } else if (src[i] == '\'') {
        strcpy(dest + d, "&#x27;");
        d += 6;
      } else {
        dest[d++] = src[i];
      }
    }
    dest[d] = '\0';
    PUSH(ValStr(dest));
    free(dest);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "shell") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: shell() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: shell() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    const char *src = ValToStrPtr(arg)->data;
    size_t newlen = 0;
    for (int i = 0; src[i] != '\0'; i++) {
      char c = src[i];
      if (c == ';' || c == '&' || c == '|' || c == '$' || c == '`' ||
          c == '>' || c == '<' || c == '\n' || c == '\r' || c == '\'' ||
          c == '"') {
        continue;
      }
      newlen++;
    }
    char *dest = malloc(newlen + 1);
    size_t d = 0;
    for (int i = 0; src[i] != '\0'; i++) {
      char c = src[i];
      if (c == ';' || c == '&' || c == '|' || c == '$' || c == '`' ||
          c == '>' || c == '<' || c == '\n' || c == '\r' || c == '\'' ||
          c == '"') {
        continue;
      }
      dest[d++] = c;
    }
    dest[d] = '\0';
    PUSH(ValStr(dest));
    free(dest);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "path") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: path() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: path() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    const char *src = ValToStrPtr(arg)->data;
    char *temp = strdup(src);
    char *p;
    while ((p = strstr(temp, ".."))) {
      memmove(p, p + 2, strlen(p + 2) + 1);
    }
    char *start = temp;
    while (*start == '/' || *start == '\\') {
      start++;
    }
    if (isalpha((unsigned char)start[0]) && start[1] == ':') {
      start += 2;
      while (*start == '/' || *start == '\\') {
        start++;
      }
    }
    PUSH(ValStr(start));
    free(temp);
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ClipboardSet") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ClipboardSet() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr,
              "Runtime error: ClipboardSet() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    SysClipSetText(ValToStrPtr(arg)->data);
    ValFree(&arg);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ClipboardGet") == 0) {
    char *txt = SysClipGetText();
    PUSH(ValStr(txt));
    free(txt);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "notify") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: notify() expects 2 arguments\n");
      return -1;
    }
    AKSValue msg = POP();
    AKSValue title = POP();
    if (ValKind(title) != VALSTR || ValKind(msg) != VALSTR) {
      fprintf(stderr, "Runtime error: notify() arguments must be strings\n");
      ValFree(&title);
      ValFree(&msg);
      return -1;
    }
    SysNotifyShow(ValToStrPtr(title)->data, ValToStrPtr(msg)->data);
    ValFree(&title);
    ValFree(&msg);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TrayAdd") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: TrayAdd() expects 2 arguments\n");
      return -1;
    }
    AKSValue tooltip = POP();
    AKSValue icon = POP();
    if (ValKind(icon) != VALSTR || ValKind(tooltip) != VALSTR) {
      fprintf(stderr, "Runtime error: TrayAdd() arguments must be strings\n");
      ValFree(&icon);
      ValFree(&tooltip);
      return -1;
    }
    SysTrayAdd(ValToStrPtr(icon)->data, ValToStrPtr(tooltip)->data);
    ValFree(&icon);
    ValFree(&tooltip);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "TrayRemove") == 0) {
    SysTrayRemove();
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "win") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: win() expects 3 arguments\n");
      return -1;
    }
    AKSValue hval = POP();
    AKSValue wval = POP();
    AKSValue title = POP();
    if (ValKind(title) != VALSTR || ValKind(wval) != VALINT ||
        ValKind(hval) != VALINT) {
      fprintf(stderr, "Runtime error: win() invalid argument types\n");
      ValFree(&title);
      ValFree(&wval);
      ValFree(&hval);
      return -1;
    }
    AKSValue win = SysGUINewWin(ValToStrPtr(title)->data, (int)ValToInt(wval),
                                (int)ValToInt(hval));
    ValFree(&title);
    ValFree(&wval);
    ValFree(&hval);
    PUSH(win);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Button") == 0) {
    if (argc != 6) {
      fprintf(stderr, "Runtime error: Button() expects 6 arguments\n");
      return -1;
    }
    AKSValue hval = POP();
    AKSValue wval = POP();
    AKSValue yval = POP();
    AKSValue xval = POP();
    AKSValue text = POP();
    AKSValue parent = POP();
    if (ValKind(text) != VALSTR || ValKind(parent) != VALINT) {
      fprintf(stderr, "Runtime error: Button() invalid types\n");
      ValFree(&parent);
      ValFree(&text);
      ValFree(&xval);
      ValFree(&yval);
      ValFree(&wval);
      ValFree(&hval);
      return -1;
    }
    AKSValue btn = SysGUINewBtn(parent, ValToStrPtr(text)->data,
                                (int)ValToInt(xval), (int)ValToInt(yval),
                                (int)ValToInt(wval), (int)ValToInt(hval));
    ValFree(&parent);
    ValFree(&text);
    ValFree(&xval);
    ValFree(&yval);
    ValFree(&wval);
    ValFree(&hval);
    PUSH(btn);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MessageBox") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: MessageBox() expects 2 arguments\n");
      return -1;
    }
    AKSValue msg = POP();
    AKSValue title = POP();
    if (ValKind(title) != VALSTR || ValKind(msg) != VALSTR) {
      fprintf(stderr,
              "Runtime error: MessageBox() arguments must be strings\n");
      ValFree(&title);
      ValFree(&msg);
      return -1;
    }
    SysGUIMsgBox(ValToStrPtr(title)->data, ValToStrPtr(msg)->data);
    ValFree(&title);
    ValFree(&msg);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "loop") == 0) {
    int res = SysGUIMsgLoop();
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlInit") == 0) {
    int res = SysSDLInit();
    PUSH(ValBool(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlWin") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SdlWin() expects 3 arguments\n");
      return -1;
    }
    AKSValue hval = POP();
    AKSValue wval = POP();
    AKSValue title = POP();
    AKSValue win = SysSDLNewWin(ValToStrPtr(title)->data, (int)ValToInt(wval),
                                (int)ValToInt(hval));
    ValFree(&title);
    ValFree(&wval);
    ValFree(&hval);
    PUSH(win);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlClear") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: SdlClear() expects 3 arguments\n");
      return -1;
    }
    AKSValue bval = POP();
    AKSValue gval = POP();
    AKSValue rval = POP();
    SysSDLClear((int)ValToInt(rval), (int)ValToInt(gval), (int)ValToInt(bval));
    ValFree(&rval);
    ValFree(&gval);
    ValFree(&bval);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlRect") == 0) {
    if (argc != 7) {
      fprintf(stderr, "Runtime error: SdlRect() expects 7 arguments\n");
      return -1;
    }
    AKSValue bval = POP();
    AKSValue gval = POP();
    AKSValue rval = POP();
    AKSValue hval = POP();
    AKSValue wval = POP();
    AKSValue yval = POP();
    AKSValue xval = POP();
    SysSDLDrawRect((int)ValToInt(xval), (int)ValToInt(yval),
                   (int)ValToInt(wval), (int)ValToInt(hval),
                   (int)ValToInt(rval), (int)ValToInt(gval),
                   (int)ValToInt(bval));
    ValFree(&xval);
    ValFree(&yval);
    ValFree(&wval);
    ValFree(&hval);
    ValFree(&rval);
    ValFree(&gval);
    ValFree(&bval);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlCircle") == 0) {
    if (argc != 6) {
      fprintf(stderr, "Runtime error: SdlCircle() expects 6 arguments\n");
      return -1;
    }
    AKSValue bval = POP();
    AKSValue gval = POP();
    AKSValue rval = POP();
    AKSValue radval = POP();
    AKSValue yval = POP();
    AKSValue xval = POP();
    SysSDLDrawCircle((int)ValToInt(xval), (int)ValToInt(yval),
                     (int)ValToInt(radval), (int)ValToInt(rval),
                     (int)ValToInt(gval), (int)ValToInt(bval));
    ValFree(&xval);
    ValFree(&yval);
    ValFree(&radval);
    ValFree(&rval);
    ValFree(&gval);
    ValFree(&bval);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SdlEvent") == 0) {
    int res = SysSDLPollEvent();
    PUSH(ValInt(res));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GuiDrag") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: GuiDrag() expects 1 argument\n");
      return -1;
    }
    AKSValue win = POP();
    SysGUIEnableDragDrop(win);
    ValFree(&win);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GuiDrop") == 0) {
    char *files = SysGUIGetDroppedFiles();
    PUSH(ValStr(files));
    free(files);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DomGet") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: DomGet() expects 1 argument\n");
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: DomGet() argument must be a string\n");
      ValFree(&arg);
      return -1;
    }
    AKSValue el = SysDOMGetElementByID(ValToStrPtr(arg)->data);
    ValFree(&arg);
    PUSH(el);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DomSet") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: DomSet() expects 2 arguments\n");
      return -1;
    }
    AKSValue html = POP();
    AKSValue el = POP();
    if (ValKind(html) != VALSTR) {
      fprintf(stderr,
              "Runtime error: DomSet() second argument must be a string\n");
      ValFree(&el);
      ValFree(&html);
      return -1;
    }
    SysDOMSetInnerHtml(el, ValToStrPtr(html)->data);
    ValFree(&el);
    ValFree(&html);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "clock") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: clock() expects 0 arguments, got %d\n",
              argc);
      return -1;
    }
    double secs = (double)clock() / CLOCKS_PER_SEC;
    PUSH(ValFloat(secs));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WriteFile") == 0) {
    AKSValue cap = ValNull();
    if (argc == 3) {
      cap = POP();
    } else if (argc != 2) {
      fprintf(stderr,
              "Runtime error: WriteFile() expects 2 or 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue content = POP();
    AKSValue path = POP();
    if (ValKind(path) != VALSTR ||
        (ValKind(content) != VALSTR && ValKind(content) != VALARR)) {
      fprintf(stderr, "Runtime error: WriteFile() arguments must be string or "
                      "byte array\n");
      ValFree(&content);
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      ValFree(&content);
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMWRITE)) {
      fprintf(stderr, "Security error: write permission denied\n");
      ValFree(&content);
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(path)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&content);
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "fs", "write", ValToStrPtr(path)->data)) {
      fprintf(stderr, "Security error: invalid write capability for path\n");
      ValFree(&content);
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    FILE *f = fopen(ValToStrPtr(path)->data, "wb");
    int success = 0;
    if (f) {
      if (ValKind(content) == VALSTR) {
        fwrite(ValToStrPtr(content)->data, 1, ValToStrPtr(content)->len, f);
      } else if (ValKind(content) == VALARR) {
        AKSArray *a = ValToArr(content);
        for (int i = 0; i < a->count; i++) {
          uint8_t b = (uint8_t)ValToInt(a->items[i]);
          fputc(b, f);
        }
      }
      fclose(f);
      success = 1;
    }
    PUSH(ValBool(success));
    ValFree(&content);
    ValFree(&path);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadFileAsync") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ReadFileAsync() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue pathval = POP();
    if (ValKind(pathval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: ReadFileAsync() argument must be a string\n");
      ValFree(&pathval);
      return -1;
    }
    if (!(vm->permissions & PERMREAD)) {
      fprintf(stderr, "Security error: read permission denied\n");
      ValFree(&pathval);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(pathval)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&pathval);
      return -1;
    }
    const char *path = ValToStrPtr(pathval)->data;

    VMTask *t = CreateAsyncPendingTask(vm);

#ifdef _WIN32
    HANDLE hFile =
        CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
      t->result = ValNull();
      t->state = 3;
      ValFree(&pathval);
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }

    CreateIoCompletionPort(hFile, vm->iocp, (ULONG_PTR)hFile, 0);

    LARGE_INTEGER size;
    GetFileSizeEx(hFile, &size);
    int file_size = (int)size.QuadPart;

    char *buf = malloc(file_size + 1);

    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = hFile;
    ctx->buffer = buf;
    ctx->op_type = 0; // read

    BOOL read_res = ReadFile(hFile, buf, file_size, NULL, &ctx->overlapped);
    if (!read_res && GetLastError() != ERROR_IO_PENDING) {
      CloseHandle(hFile);
      free(buf);
      free(ctx);
      t->result = ValNull();
      t->state = 3;
    }
#else
    int fd = AksPosixOpen(path, O_RDONLY, 0);
    if (fd < 0) {
      t->result = ValNull();
      t->state = 3;
      ValFree(&pathval);
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }
    struct stat st;
    size_t file_size = 0;
    if (fstat(fd, &st) == 0 && st.st_size > 0) {
      file_size = (size_t)st.st_size;
    }
    char *buf = malloc(file_size + 1);

#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)fd;
    ctx->buffer = buf;
    ctx->op_type = 0;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_read(sqe, fd, buf, file_size, 0);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    int bytes_read = read(fd, buf, file_size);
    if (bytes_read >= 0) {
      buf[bytes_read] = '\0';
      t->result = ValStrLen(buf, bytes_read);
    } else {
      t->result = ValNull();
    }
    t->state = 3;
    free(buf);
    close(fd);
#endif
#endif

    ValFree(&pathval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "WriteFileAsync") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: WriteFileAsync() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue contentval = POP();
    AKSValue pathval = POP();
    if (ValKind(pathval) != VALSTR || ValKind(contentval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: WriteFileAsync() arguments must be strings\n");
      ValFree(&pathval);
      ValFree(&contentval);
      return -1;
    }
    if (vm->inrestrictedeval) {
      ValFree(&pathval);
      ValFree(&contentval);
      return -1;
    }
    if (!(vm->permissions & PERMWRITE)) {
      fprintf(stderr, "Security error: write permission denied\n");
      ValFree(&pathval);
      ValFree(&contentval);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(pathval)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&pathval);
      ValFree(&contentval);
      return -1;
    }
    const char *path = ValToStrPtr(pathval)->data;
    const char *content = ValToStrPtr(contentval)->data;
    int len = ValToStrPtr(contentval)->len;

    VMTask *t = CreateAsyncPendingTask(vm);

#ifdef _WIN32
    HANDLE hFile =
        CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
      t->result = ValBool(0);
      t->state = 3;
      ValFree(&pathval);
      ValFree(&contentval);
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }

    CreateIoCompletionPort(hFile, vm->iocp, (ULONG_PTR)hFile, 0);

    char *buf = malloc(len + 1);
    memcpy(buf, content, len);
    buf[len] = '\0';

    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = hFile;
    ctx->buffer = buf;
    ctx->op_type = 1; // write

    BOOL write_res = WriteFile(hFile, buf, len, NULL, &ctx->overlapped);
    if (!write_res && GetLastError() != ERROR_IO_PENDING) {
      CloseHandle(hFile);
      free(buf);
      free(ctx);
      t->result = ValBool(0);
      t->state = 3;
    }
#else
    int fd = AksPosixOpen(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
      t->result = ValBool(0);
      t->state = 3;
      ValFree(&pathval);
      ValFree(&contentval);
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }
    char *buf = malloc(len + 1);
    memcpy(buf, content, len);
    buf[len] = '\0';

#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)fd;
    ctx->buffer = buf;
    ctx->op_type = 1;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_write(sqe, fd, buf, len, 0);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    int bytes_written = write(fd, buf, len);
    if (bytes_written >= 0) {
      t->result = ValBool(1);
    } else {
      t->result = ValBool(0);
    }
    t->state = 3;
    free(buf);
    close(fd);
#endif
#endif

    ValFree(&pathval);
    ValFree(&contentval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketListen") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SocketListen() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue portval = POP();
    int port = (int)ValToInt(portval);
    ValFree(&portval);

#ifdef _WIN32
    SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                         WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
      closesocket(s);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
      closesocket(s);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    CreateIoCompletionPort((HANDLE)s, vm->iocp, (ULONG_PTR)s, 0);
    PUSH(ValInt((int64_t)s));
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(s, 128) < 0) {
      close(s);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    PUSH(ValInt(s));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketAccept") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SocketAccept() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue sockval = POP();
    int64_t listen_s = ValToInt(sockval);
    ValFree(&sockval);

    VMTask *t = CreateAsyncPendingTask(vm);

    if (listen_s < 0) {
      t->result = ValInt(-1);
      t->state = 3; // completed
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }

#ifdef _WIN32
    SOCKET client_s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                                WSA_FLAG_OVERLAPPED);

    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    LPFN_ACCEPTEX lpfnAcceptEx = NULL;
    DWORD dwBytes = 0;
    int ioctl_res =
        WSAIoctl((SOCKET)listen_s, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &GuidAcceptEx, sizeof(GuidAcceptEx), &lpfnAcceptEx,
                 sizeof(lpfnAcceptEx), &dwBytes, NULL, NULL);

    if (ioctl_res != 0 || !lpfnAcceptEx) {
      if (client_s != INVALID_SOCKET)
        closesocket(client_s);
      t->result = ValInt(-1);
      t->state = 3;
      PUSH(ValTask(t));
      *stacktopptr = stacktop;
      return 1;
    }

    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)listen_s;
    ctx->buffer = malloc(1024); // buffer for addresses
    ctx->op_type = 4;           // accept
    ctx->extra = (void *)(uintptr_t)client_s;

    BOOL res = lpfnAcceptEx((SOCKET)listen_s, client_s, ctx->buffer, 0,
                            sizeof(struct sockaddr_in) + 16,
                            sizeof(struct sockaddr_in) + 16, &dwBytes,
                            &ctx->overlapped);
    if (!res && GetLastError() != ERROR_IO_PENDING) {
      closesocket(client_s);
      free(ctx->buffer);
      free(ctx);
      t->result = ValInt(-1);
      t->state = 3;
    }
#else
#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)listen_s;
    ctx->op_type = 4;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_accept(sqe, (int)listen_s, NULL, NULL, 0);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    int client_fd = accept((int)listen_s, NULL, NULL);
    if (client_fd >= 0) {
      t->result = ValInt(client_fd);
    } else {
      t->result = ValInt(-1);
    }
    t->state = 3;
#endif
#endif

    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketConnect") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SocketConnect() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue portval = POP();
    AKSValue hostval = POP();
    int port = (int)ValToInt(portval);
    const char *host =
        (ValKind(hostval) == VALSTR) ? ValToStrPtr(hostval)->data : "127.0.0.1";
    VMTask *t = CreateAsyncPendingTask(vm);

#ifdef _WIN32
    SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                         WSA_FLAG_OVERLAPPED);
    if (s != INVALID_SOCKET) {
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = htons((u_short)port);
      sin.sin_addr.s_addr = inet_addr(host);
      if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        CreateIoCompletionPort((HANDLE)s, vm->iocp, (ULONG_PTR)s, 0);
        t->result = ValInt((int64_t)s);
      } else {
        closesocket(s);
        t->result = ValInt(-1);
      }
    } else {
      t->result = ValInt(-1);
    }
    t->state = 3;
#else
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = htons((u_short)port);
      sin.sin_addr.s_addr = inet_addr(host);
      if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        t->result = ValInt((int64_t)s);
      } else {
        close((int)s);
        t->result = ValInt(-1);
      }
    } else {
      t->result = ValInt(-1);
    }
    t->state = 3;
#endif

    ValFree(&hostval);
    ValFree(&portval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketReceive") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SocketReceive() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue maxval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    int max_bytes = (int)ValToInt(maxval);
    ValFree(&maxval);
    ValFree(&sockval);

    VMTask *t = CreateAsyncPendingTask(vm);
    char *buf = malloc(max_bytes + 1);

#ifdef _WIN32
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)sock;
    ctx->buffer = buf;
    ctx->op_type = 2; // recv

    WSABUF wsabuf;
    wsabuf.buf = buf;
    wsabuf.len = max_bytes;
    DWORD flags = 0;
    int res =
        WSARecv((SOCKET)sock, &wsabuf, 1, NULL, &flags, &ctx->overlapped, NULL);
    if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      free(buf);
      free(ctx);
      t->result = ValStr("");
      t->state = 3;
    }
#else
#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)sock;
    ctx->buffer = buf;
    ctx->op_type = 2;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_recv(sqe, (int)sock, buf, max_bytes, 0);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    int bytes_recv = recv((int)sock, buf, max_bytes, 0);
    if (bytes_recv >= 0) {
      buf[bytes_recv] = '\0';
      t->result = ValStrLen(buf, bytes_recv);
    } else {
      t->result = ValStr("");
    }
    t->state = 3;
    free(buf);
#endif
#endif

    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketSend") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SocketSend() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue strval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    const char *str = ValToStrPtr(strval)->data;
    int len = ValToStrPtr(strval)->len;

    VMTask *t = CreateAsyncPendingTask(vm);
    char *buf = malloc(len + 1);
    memcpy(buf, str, len);
    buf[len] = '\0';

#ifdef _WIN32
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)sock;
    ctx->buffer = buf;
    ctx->op_type = 3; // send

    WSABUF wsabuf;
    wsabuf.buf = buf;
    wsabuf.len = len;
    int res =
        WSASend((SOCKET)sock, &wsabuf, 1, NULL, 0, &ctx->overlapped, NULL);
    if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      free(buf);
      free(ctx);
      t->result = ValInt(0);
      t->state = 3;
    }
#else
#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)sock;
    ctx->buffer = buf;
    ctx->op_type = 3;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_send(sqe, (int)sock, buf, len, 0);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    int bytes_sent = send((int)sock, buf, len, 0);
    if (bytes_sent >= 0) {
      t->result = ValInt(bytes_sent);
    } else {
      t->result = ValInt(0);
    }
    t->state = 3;
    free(buf);
#endif
#endif

    ValFree(&strval);
    ValFree(&sockval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketAcceptSync") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: SocketAcceptSync() expects 1 argument\n");
      return -1;
    }
    AKSValue sockval = POP();
    int64_t listen_s = ValToInt(sockval);
    ValFree(&sockval);
    if (listen_s < 0) {
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
#ifdef _WIN32
    SOCKET client_s = accept((SOCKET)listen_s, NULL, NULL);
    PUSH(ValInt((client_s != INVALID_SOCKET) ? (int64_t)client_s : -1));
#else
    int client_s = accept((int)listen_s, NULL, NULL);
    PUSH(ValInt(client_s));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketConnectSync") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SocketConnectSync() expects 2 arguments\n");
      return -1;
    }
    AKSValue portval = POP();
    AKSValue hostval = POP();
    const char *host =
        (ValKind(hostval) == VALSTR) ? ValToStrPtr(hostval)->data : "127.0.0.1";
    int port = (int)ValToInt(portval);
#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = htons((u_short)port);
      sin.sin_addr.s_addr = inet_addr(host);
      if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        PUSH(ValInt((int64_t)s));
      } else {
        closesocket(s);
        PUSH(ValInt(-1));
      }
    } else {
      PUSH(ValInt(-1));
    }
#else
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s >= 0) {
      struct sockaddr_in sin;
      sin.sin_family = AF_INET;
      sin.sin_port = htons((u_short)port);
      sin.sin_addr.s_addr = inet_addr(host);
      if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        PUSH(ValInt(s));
      } else {
        close(s);
        PUSH(ValInt(-1));
      }
    } else {
      PUSH(ValInt(-1));
    }
#endif
    ValFree(&hostval);
    ValFree(&portval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketSendSync") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: SocketSendSync() expects 2 arguments\n");
      return -1;
    }
    AKSValue strval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    const char *str =
        (ValKind(strval) == VALSTR) ? ValToStrPtr(strval)->data : "";
    int len = (ValKind(strval) == VALSTR) ? ValToStrPtr(strval)->len : 0;
#ifdef _WIN32
    int res = send((SOCKET)sock, str, len, 0);
    PUSH(ValInt(res >= 0 ? res : 0));
#else
    int res = send((int)sock, str, len, 0);
    PUSH(ValInt(res >= 0 ? res : 0));
#endif
    ValFree(&strval);
    ValFree(&sockval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketReceiveSync") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SocketReceiveSync() expects 2 arguments\n");
      return -1;
    }
    AKSValue maxval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    int max_bytes = (int)ValToInt(maxval);
    ValFree(&maxval);
    ValFree(&sockval);
    if (max_bytes <= 0)
      max_bytes = 4096;
    char *buf = malloc(max_bytes + 1);
#ifdef _WIN32
    int res = recv((SOCKET)sock, buf, max_bytes, 0);
#else
    int res = recv((int)sock, buf, max_bytes, 0);
#endif
    if (res > 0) {
      buf[res] = '\0';
      PUSH(ValStrLen(buf, res));
    } else {
      PUSH(ValStr(""));
    }
    free(buf);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SocketClose") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: SocketClose() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    ValFree(&sockval);

#ifdef _WIN32
    closesocket((SOCKET)sock);
#else
    close((int)sock);
#endif
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UdpSocket") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: UdpSocket() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue portval = POP();
    int port = (int)ValToInt(portval);
    ValFree(&portval);

#ifdef _WIN32
    SOCKET s = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0,
                         WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
      closesocket(s);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    CreateIoCompletionPort((HANDLE)s, vm->iocp, (ULONG_PTR)s, 0);
    PUSH(ValInt((int64_t)s));
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      close(s);
      PUSH(ValInt(-1));
      *stacktopptr = stacktop;
      return 1;
    }
    PUSH(ValInt(s));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UdpSendTo") == 0) {
    if (argc != 4) {
      fprintf(stderr,
              "Runtime error: UdpSendTo() expects 4 arguments, got %d\n", argc);
      return -1;
    }
    AKSValue strval = POP();
    AKSValue portval = POP();
    AKSValue hostval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    const char *host = ValToStrPtr(hostval)->data;
    int port = (int)ValToInt(portval);
    const char *str = ValToStrPtr(strval)->data;
    int len = ValToStrPtr(strval)->len;

    VMTask *t = CreateAsyncPendingTask(vm);
    char *buf = malloc(len + 1);
    memcpy(buf, str, len);
    buf[len] = '\0';

    struct sockaddr_in *target_addr = malloc(sizeof(struct sockaddr_in));
    memset(target_addr, 0, sizeof(struct sockaddr_in));
    target_addr->sin_family = AF_INET;
    target_addr->sin_port = htons(port);
    inet_pton(AF_INET, host, &target_addr->sin_addr);

#ifdef _WIN32
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)sock;
    ctx->buffer = buf;
    ctx->op_type = 3; // send
    ctx->extra = target_addr;

    WSABUF wsabuf;
    wsabuf.buf = buf;
    wsabuf.len = len;
    int res = WSASendTo((SOCKET)sock, &wsabuf, 1, NULL, 0,
                        (struct sockaddr *)target_addr,
                        sizeof(struct sockaddr_in), &ctx->overlapped, NULL);
    if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      free(buf);
      free(target_addr);
      free(ctx);
      t->result = ValInt(0);
      t->state = 3;
    }
#else
#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)sock;
    ctx->buffer = buf;
    ctx->op_type = 3;
    ctx->extra = target_addr;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_sendto(sqe, (int)sock, buf, len, 0,
                         (struct sockaddr *)target_addr,
                         sizeof(struct sockaddr_in));
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)sock;
    ctx->buffer = buf;
    ctx->op_type = 3;
    ctx->extra = target_addr;
    ctx->io_len = len;
    ctx->is_udp = 1;
    if (!PosixRegisterAsyncIo(vm, (int)sock, ctx, 1)) {
      int sent = (int)sendto((int)sock, buf, len, 0,
                             (struct sockaddr *)target_addr,
                             sizeof(struct sockaddr_in));
      t->result = ValInt(sent >= 0 ? sent : 0);
      t->state = 3;
      free(buf);
      free(target_addr);
      free(ctx);
    }
#endif
#endif

    ValFree(&strval);
    ValFree(&portval);
    ValFree(&hostval);
    ValFree(&sockval);
    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "UdpReceiveFrom") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: UdpReceiveFrom() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue maxval = POP();
    AKSValue sockval = POP();
    int64_t sock = ValToInt(sockval);
    int max_bytes = (int)ValToInt(maxval);
    ValFree(&maxval);
    ValFree(&sockval);

    VMTask *t = CreateAsyncPendingTask(vm);
    char *buf = malloc(max_bytes + 1);

    struct sockaddr_in *src_addr = malloc(sizeof(struct sockaddr_in));
    memset(src_addr, 0, sizeof(struct sockaddr_in));
    int *addr_len = malloc(sizeof(int));
    *addr_len = sizeof(struct sockaddr_in);

#ifdef _WIN32
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)sock;
    ctx->buffer = buf;
    ctx->op_type = 2; // recv
    ctx->extra = src_addr;

    WSABUF wsabuf;
    wsabuf.buf = buf;
    wsabuf.len = max_bytes;
    DWORD flags = 0;
    int res = WSARecvFrom((SOCKET)sock, &wsabuf, 1, NULL, &flags,
                          (struct sockaddr *)src_addr, addr_len,
                          &ctx->overlapped, NULL);
    if (res == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      free(buf);
      free(src_addr);
      free(addr_len);
      free(ctx);
      t->result = ValStr("");
      t->state = 3;
    }
#else
#ifdef AKS_USE_IO_URING
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)sock;
    ctx->buffer = buf;
    ctx->op_type = 2;
    ctx->extra = src_addr;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&vm->ring);
    io_uring_prep_recvfrom(sqe, (int)sock, buf, max_bytes, 0,
                           (struct sockaddr *)src_addr, (socklen_t *)addr_len);
    io_uring_sqe_set_data(sqe, ctx);
    io_uring_submit(&vm->ring);
#else
    AsyncIOContext *ctx = calloc(1, sizeof(AsyncIOContext));
    ctx->task = t;
    ctx->handle = (HANDLE)(uintptr_t)sock;
    ctx->buffer = buf;
    ctx->op_type = 2;
    ctx->extra = src_addr;
    ctx->io_len = max_bytes;
    ctx->is_udp = 1;
    free(addr_len);
    if (!PosixRegisterAsyncIo(vm, (int)sock, ctx, 0)) {
      socklen_t sl = sizeof(struct sockaddr_in);
      int recvd = (int)recvfrom((int)sock, buf, max_bytes, 0,
                                (struct sockaddr *)src_addr, &sl);
      if (recvd >= 0) {
        buf[recvd] = '\0';
        t->result = ValStrLen(buf, recvd);
      } else {
        t->result = ValStr("");
      }
      t->state = 3;
      free(buf);
      free(src_addr);
      free(ctx);
    }
#endif
#endif

    PUSH(ValTask(t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "ReadFile") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: ReadFile() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue path = POP();
    if (ValKind(path) != VALSTR) {
      fprintf(stderr, "Runtime error: ReadFile() argument must be a string\n");
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMREAD)) {
      fprintf(stderr, "Security error: read permission denied\n");
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(path)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "fs", "read", ValToStrPtr(path)->data)) {
      fprintf(stderr, "Security error: invalid read capability for path\n");
      ValFree(&path);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    FILE *f = fopen(ValToStrPtr(path)->data, "rb");
    if (!f) {
      PUSH(ValNull());
    } else {
      fseek(f, 0, SEEK_END);
      long sz = ftell(f);
      rewind(f);
      char *buf = malloc(sz + 1);
      if (buf) {
        size_t rb = fread(buf, 1, sz, f);
        buf[rb] = '\0';
        PUSH(ValStr(buf));
        free(buf);
      } else {
        PUSH(ValNull());
      }
      fclose(f);
    }
    ValFree(&path);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "Execute") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: Execute() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue cmd = POP();
    if (ValKind(cmd) != VALSTR) {
      fprintf(stderr, "Runtime error: Execute() argument must be a string\n");
      ValFree(&cmd);
      ValFree(&cap);
      return -1;
    }
    if (vm->sandboxenabled) {
      fprintf(stderr,
              "Security error: command execution disabled in sandbox mode\n");
      ValFree(&cmd);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      fprintf(stderr,
              "Security error: exec() disabled in restricted eval mode\n");
      ValFree(&cmd);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMEXEC)) {
      fprintf(stderr, "Security error: execution permission denied\n");
      ValFree(&cmd);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "exec", "execute", NULL)) {
      fprintf(stderr, "Security error: invalid execution capability\n");
      ValFree(&cmd);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);
#ifdef _WIN32
    FILE *f = _popen(ValToStrPtr(cmd)->data, "r");
#else
    FILE *f = popen(ValToStrPtr(cmd)->data, "r");
#endif
    if (!f) {
      PUSH(ValNull());
    } else {
      size_t cap = 256;
      size_t count = 0;
      char *buf = malloc(cap);
      char chunk[256];
      while (fgets(chunk, sizeof(chunk), f)) {
        size_t len = strlen(chunk);
        if (count + len >= cap) {
          cap = cap * 2 + len;
          buf = realloc(buf, cap);
        }
        memcpy(buf + count, chunk, len);
        count += len;
      }
      buf[count] = '\0';
#ifdef _WIN32
      _pclose(f);
#else
      pclose(f);
#endif
      PUSH(ValStr(buf));
      free(buf);
    }
    ValFree(&cmd);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "exit") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: exit() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    int code = (int)ValToInt(arg);
    ValFree(&arg);
    exit(code);
    return 1;
  }
  if (strcmp(name, "FileOpen") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: FileOpen() expects 2 arguments (path, mode)\n");
      return -1;
    }
    AKSValue modeval = POP();
    AKSValue pathval = POP();
    int slot = -1;
    if (ValKind(pathval) == VALSTR && ValToStrPtr(pathval) &&
        ValKind(modeval) == VALSTR && ValToStrPtr(modeval)) {
      slot =
          AksFileOpen(ValToStrPtr(pathval)->data, ValToStrPtr(modeval)->data);
    }
    ValFree(&modeval);
    ValFree(&pathval);
    PUSH(ValInt(slot));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileClose") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileClose() expects 1 argument (id)\n");
      return -1;
    }
    AKSValue idval = POP();
    int ok = 0;
    if (ValKind(idval) == VALINT) {
      ok = AksFileClose((int)ValToInt(idval));
    }
    ValFree(&idval);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileReadLine") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FileReadLine() expects 1 argument (id)\n");
      return -1;
    }
    AKSValue idval = POP();
    AKSValue res = ValNull();
    if (ValKind(idval) == VALINT) {
      char *line = AksFileReadLine((int)ValToInt(idval));
      if (line) {
        res = ValStr(line);
        free(line);
      }
    }
    ValFree(&idval);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileReadBytes") == 0) {
    if (argc != 2) {
      fprintf(
          stderr,
          "Runtime error: FileReadBytes() expects 2 arguments (id, count)\n");
      return -1;
    }
    AKSValue countval = POP();
    AKSValue idval = POP();
    AKSValue res = ValNull();
    if (ValKind(idval) == VALINT && ValKind(countval) == VALINT) {
      size_t count = (size_t)ValToInt(countval);
      size_t out_len = 0;
      char *data = AksFileReadBytes((int)ValToInt(idval), count, &out_len);
      if (data) {
        res = ValStrLen(data, (int)out_len);
        free(data);
      }
    }
    ValFree(&countval);
    ValFree(&idval);
    PUSH(res);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileWrite") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: FileWrite() expects 2 arguments (id, str)\n");
      return -1;
    }
    AKSValue strval = POP();
    AKSValue idval = POP();
    int ok = 0;
    if (ValKind(idval) == VALINT && ValKind(strval) == VALSTR &&
        ValToStrPtr(strval)) {
      AKSString *s = ValToStrPtr(strval);
      ok = AksFileWrite((int)ValToInt(idval), s->data, s->len);
    }
    ValFree(&strval);
    ValFree(&idval);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileWriteLine") == 0) {
    if (argc != 2) {
      fprintf(stderr,
              "Runtime error: FileWriteLine() expects 2 arguments (id, str)\n");
      return -1;
    }
    AKSValue strval = POP();
    AKSValue idval = POP();
    int ok = 0;
    if (ValKind(idval) == VALINT && ValKind(strval) == VALSTR &&
        ValToStrPtr(strval)) {
      AKSString *s = ValToStrPtr(strval);
      ok = AksFileWriteLine((int)ValToInt(idval), s->data, s->len);
    }
    ValFree(&strval);
    ValFree(&idval);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSeek") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Runtime error: FileSeek() expects 3 arguments (id, "
                      "offset, whence)\n");
      return -1;
    }
    AKSValue whenceval = POP();
    AKSValue offsetval = POP();
    AKSValue idval = POP();
    int ok = 0;
    if (ValKind(idval) == VALINT && ValKind(offsetval) == VALINT &&
        ValKind(whenceval) == VALINT) {
      ok = AksFileSeek((int)ValToInt(idval), (long)ValToInt(offsetval),
                       (int)ValToInt(whenceval));
    }
    ValFree(&whenceval);
    ValFree(&offsetval);
    ValFree(&idval);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileTell") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileTell() expects 1 argument (id)\n");
      return -1;
    }
    AKSValue idval = POP();
    long pos = -1;
    if (ValKind(idval) == VALINT) {
      pos = AksFileTell((int)ValToInt(idval));
    }
    ValFree(&idval);
    PUSH(ValInt((int64_t)pos));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileFlush") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileFlush() expects 1 argument (id)\n");
      return -1;
    }
    AKSValue idval = POP();
    int ok = 0;
    if (ValKind(idval) == VALINT) {
      ok = AksFileFlush((int)ValToInt(idval));
    }
    ValFree(&idval);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileEof") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FileEof() expects 1 argument (id)\n");
      return -1;
    }
    AKSValue idval = POP();
    int is_eof = 1;
    if (ValKind(idval) == VALINT) {
      is_eof = AksFileEof((int)ValToInt(idval));
    }
    ValFree(&idval);
    PUSH(ValBool(is_eof));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemExists") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FileSystemExists() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue pval = POP();
    int exists = 0;
    if (ValKind(pval) == VALSTR && ValToStrPtr(pval)) {
#ifdef _WIN32
      DWORD attr = GetFileAttributesA(ValToStrPtr(pval)->data);
      exists = (attr != INVALID_FILE_ATTRIBUTES);
#else
      exists = (access(ValToStrPtr(pval)->data, 0) == 0);
#endif
    }
    ValFree(&pval);
    PUSH(ValBool(exists));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemListDir") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FileSystemListDir() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue pval = POP();
    AKSValue arrval = ValArr(4);
    if (ValKind(pval) == VALSTR && ValToStrPtr(pval)) {
#ifdef _WIN32
      const char *dir = ValToStrPtr(pval)->data;
      char pattern[MAX_PATH];
      snprintf(pattern, sizeof(pattern), "%s\\*", dir);
      WIN32_FIND_DATAA fd;
      HANDLE hFind = FindFirstFileA(pattern, &fd);
      if (hFind != INVALID_HANDLE_VALUE) {
        do {
          if (strcmp(fd.cFileName, ".") != 0 &&
              strcmp(fd.cFileName, "..") != 0) {
            AKSValue item = ValStr(fd.cFileName);
            AKSArray *a = ValToArr(arrval);
            if (a->count >= a->capacity) {
              a->capacity = a->capacity == 0 ? 4 : a->capacity * 2;
              a->items =
                  (AKSValue *)realloc(a->items, a->capacity * sizeof(AKSValue));
            }
            a->items[a->count++] = item;
          }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
      }
#endif
    }
    ValFree(&pval);
    PUSH(arrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemMakeDir") == 0) {
    if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FileSystemMakeDir() expects 1 argument (path)\n");
      return -1;
    }
    AKSValue pval = POP();
    int ok = 0;
    if (ValKind(pval) == VALSTR && ValToStrPtr(pval)) {
#ifdef _WIN32
      ok = CreateDirectoryA(ValToStrPtr(pval)->data, NULL) != 0;
#endif
    }
    ValFree(&pval);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FileSystemAppendText") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: FileSystemAppendText() expects 2 "
                      "arguments (path, content)\n");
      return -1;
    }
    AKSValue cval = POP();
    AKSValue pval = POP();
    int ok = 0;
    if (ValKind(pval) == VALSTR && ValToStrPtr(pval) &&
        ValKind(cval) == VALSTR && ValToStrPtr(cval)) {
      FILE *f = fopen(ValToStrPtr(pval)->data, "ab");
      if (f) {
        fwrite(ValToStrPtr(cval)->data, 1, ValToStrPtr(cval)->len, f);
        fclose(f);
        ok = 1;
      }
    }
    ValFree(&cval);
    ValFree(&pval);
    PUSH(ValBool(ok));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "HttpFetch") == 0) {
    if (argc < 2) {
      fprintf(stderr, "Runtime error: HttpFetch() expects at least 2 arguments "
                      "(method, url)\n");
      return -1;
    }
    AKSValue bodyval = (argc >= 4) ? POP() : ValStr("");
    AKSValue hdrval = (argc >= 3) ? POP() : ValStr("");
    AKSValue urlval = POP();
    AKSValue methodval = POP();
    int status_code = 0;
    char *resp_body = NULL;
    int resp_len = 0;
    if (ValKind(urlval) == VALSTR && ValToStrPtr(urlval) &&
        ValKind(methodval) == VALSTR && ValToStrPtr(methodval)) {
#ifdef _WIN32
      const char *url_str = ValToStrPtr(urlval)->data;
      const char *method_str = ValToStrPtr(methodval)->data;
      WCHAR wurl[2048];
      MultiByteToWideChar(CP_UTF8, 0, url_str, -1, wurl, 2048);
      URL_COMPONENTS urlComp;
      ZeroMemory(&urlComp, sizeof(urlComp));
      urlComp.dwStructSize = sizeof(urlComp);
      WCHAR hostName[512] = {0};
      WCHAR urlPath[1024] = {0};
      urlComp.lpszHostName = hostName;
      urlComp.dwHostNameLength = 512;
      urlComp.lpszUrlPath = urlPath;
      urlComp.dwUrlPathLength = 1024;
      if (WinHttpCrackUrl(wurl, (DWORD)wcslen(wurl), 0, &urlComp)) {
        HINTERNET hSession = WinHttpOpen(
            L"AKS-HttpClient/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
          HINTERNET hConnect =
              WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
          if (hConnect) {
            DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS)
                              ? WINHTTP_FLAG_SECURE
                              : 0;
            WCHAR wmethod[32];
            MultiByteToWideChar(CP_UTF8, 0, method_str, -1, wmethod, 32);
            HINTERNET hRequest = WinHttpOpenRequest(
                hConnect, wmethod, urlPath, NULL, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (hRequest) {
              LPCWSTR req_hdrs = WINHTTP_NO_ADDITIONAL_HEADERS;
              DWORD req_hdrs_len = 0;
              WCHAR whdrs[2048];
              if (ValKind(hdrval) == VALSTR && ValToStrPtr(hdrval) &&
                  ValToStrPtr(hdrval)->len > 0) {
                MultiByteToWideChar(CP_UTF8, 0, ValToStrPtr(hdrval)->data, -1,
                                    whdrs, 2048);
                req_hdrs = whdrs;
                req_hdrs_len = (DWORD)wcslen(whdrs);
              }
              LPVOID pdata = WINHTTP_NO_REQUEST_DATA;
              DWORD data_len = 0;
              if (ValKind(bodyval) == VALSTR && ValToStrPtr(bodyval)) {
                pdata = (LPVOID)ValToStrPtr(bodyval)->data;
                data_len = (DWORD)ValToStrPtr(bodyval)->len;
              }
              if (WinHttpSendRequest(hRequest, req_hdrs, req_hdrs_len, pdata,
                                     data_len, data_len, 0)) {
                if (WinHttpReceiveResponse(hRequest, NULL)) {
                  DWORD status = 0;
                  DWORD statusSize = sizeof(status);
                  if (WinHttpQueryHeaders(
                          hRequest,
                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                          WINHTTP_NO_HEADER_INDEX)) {
                    status_code = (int)status;
                  }
                  DWORD dwSize = 0;
                  DWORD dwDownloaded = 0;
                  resp_body = (char *)malloc(1);
                  if (resp_body) {
                    resp_body[0] = '\0';
                    resp_len = 0;
                    do {
                      dwSize = 0;
                      if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
                        break;
                      if (dwSize == 0)
                        break;
                      char *buf = (char *)malloc(dwSize);
                      if (buf) {
                        if (WinHttpReadData(hRequest, buf, dwSize,
                                            &dwDownloaded)) {
                          char *new_body = (char *)realloc(
                              resp_body, resp_len + dwDownloaded + 1);
                          if (new_body) {
                            resp_body = new_body;
                            memcpy(resp_body + resp_len, buf, dwDownloaded);
                            resp_len += dwDownloaded;
                            resp_body[resp_len] = '\0';
                          }
                        }
                        free(buf);
                      }
                    } while (dwSize > 0);
                  }
                }
              }
              WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
          }
          WinHttpCloseHandle(hSession);
        }
      }
#endif
    }
    ValFree(&bodyval);
    ValFree(&hdrval);
    ValFree(&urlval);
    ValFree(&methodval);
    AKSValue res_obj = ValStruct("HttpResponse");
    StructSetFieldNative(ValToObj(res_obj), "status", ValInt(status_code));
    AKSValue body_val = resp_body ? ValStrLen(resp_body, resp_len) : ValStr("");
    StructSetFieldNative(ValToObj(res_obj), "body", body_val);
    ValFree(&body_val);
    StructSetFieldNative(ValToObj(res_obj), "ok",
                         ValBool(status_code >= 200 && status_code < 300));
    if (resp_body)
      free(resp_body);
    PUSH(res_obj);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "time") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: time() expects 0 arguments, got %d\n",
              argc);
      return -1;
    }
    time_t t = time(NULL);
    PUSH(ValFloat((double)t));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "sleep") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: sleep() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    int ms = (int)(ValKind(arg) == VALINT ? ValToInt(arg)
                                          : (int64_t)ValToFloat(arg));
    ValFree(&arg);
    SleepMs(ms);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "exists") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: exists() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: exists() argument must be a string\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMREAD)) {
      fprintf(stderr, "Security error: read permission denied\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "fs", "read", ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: invalid read capability for path\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    int ex = FileExists(ValToStrPtr(arg)->data);
    PUSH(ValBool(ex));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "RemoveFile") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: RemoveFile() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr,
              "Runtime error: RemoveFile() argument must be a string\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      fprintf(stderr,
              "Security error: rmfile() disabled in restricted eval mode\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMWRITE)) {
      fprintf(stderr, "Security error: write permission denied\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "fs", "write", ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: invalid write capability for path\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    int status = remove(ValToStrPtr(arg)->data) == 0;
    PUSH(ValBool(status));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MakeDirectory") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(
          stderr,
          "Runtime error: MakeDirectory() expects 1 or 2 arguments, got %d\n",
          argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr,
              "Runtime error: MakeDirectory() argument must be a string\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      fprintf(stderr,
              "Security error: mkdir() disabled in restricted eval mode\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMWRITE)) {
      fprintf(stderr, "Security error: write permission denied\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!IsPathSafe(vm, ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: sandbox path traversal blocked\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "fs", "write", ValToStrPtr(arg)->data)) {
      fprintf(stderr, "Security error: invalid write capability for path\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    int status = NewDir(ValToStrPtr(arg)->data);
    PUSH(ValBool(status));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "GetEnv") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: GetEnv() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: GetEnv() argument must be a string\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMENV)) {
      fprintf(stderr, "Security error: env permission denied\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "env", "env", NULL)) {
      fprintf(stderr, "Security error: invalid env capability\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    const char *val = getenv(ValToStrPtr(arg)->data);
    if (val) {
      PUSH(ValStr(val));
    } else {
      PUSH(ValNull());
    }
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SetEnv") == 0) {
    AKSValue cap = ValNull();
    if (argc == 3) {
      cap = POP();
    } else if (argc != 2) {
      fprintf(stderr,
              "Runtime error: SetEnv() expects 2 or 3 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue val = POP();
    AKSValue nameval = POP();
    if (ValKind(nameval) != VALSTR || ValKind(val) != VALSTR) {
      fprintf(stderr, "Runtime error: SetEnv() arguments must be strings\n");
      ValFree(&val);
      ValFree(&nameval);
      ValFree(&cap);
      return -1;
    }
    if (vm->sandboxenabled) {
      fprintf(stderr, "Security error: modifying environment variables "
                      "disabled in sandbox mode\n");
      ValFree(&val);
      ValFree(&nameval);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      fprintf(stderr,
              "Security error: setenv() disabled in restricted eval mode\n");
      ValFree(&val);
      ValFree(&nameval);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMENV)) {
      fprintf(stderr, "Security error: env permission denied\n");
      ValFree(&val);
      ValFree(&nameval);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "env", "env", NULL)) {
      fprintf(stderr, "Security error: invalid env capability\n");
      ValFree(&val);
      ValFree(&nameval);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);

    int status =
        SetEnvironmentVar(ValToStrPtr(nameval)->data, ValToStrPtr(val)->data);
    PUSH(ValBool(status));
    ValFree(&val);
    ValFree(&nameval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "date") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: date() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue fmtval = POP();
    AKSValue tsval = POP();
    if (ValKind(fmtval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: date() format argument must be a string\n");
      ValFree(&fmtval);
      ValFree(&tsval);
      return -1;
    }
    double tsdouble = ValKind(tsval) == VALFLOAT ? ValToFloat(tsval)
                                                 : (double)ValToInt(tsval);
    time_t t = (time_t)tsdouble;
    struct tm *timeinfo = localtime(&t);
    char buf[256];
    if (timeinfo &&
        strftime(buf, sizeof(buf), ValToStrPtr(fmtval)->data, timeinfo)) {
      PUSH(ValStr(buf));
    } else {
      PUSH(ValStr(""));
    }
    ValFree(&fmtval);
    ValFree(&tsval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "date_utc") == 0) {
    if (argc != 2) {
      fprintf(stderr, "Runtime error: date_utc() expects 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue fmtval = POP();
    AKSValue tsval = POP();
    if (ValKind(fmtval) != VALSTR) {
      fprintf(stderr,
              "Runtime error: date_utc() format argument must be a string\n");
      ValFree(&fmtval);
      ValFree(&tsval);
      return -1;
    }
    double tsdouble = ValKind(tsval) == VALFLOAT ? ValToFloat(tsval)
                                                 : (double)ValToInt(tsval);
    time_t t = (time_t)tsdouble;
    struct tm *timeinfo = gmtime(&t);
    char buf[256];
    if (timeinfo &&
        strftime(buf, sizeof(buf), ValToStrPtr(fmtval)->data, timeinfo)) {
      PUSH(ValStr(buf));
    } else {
      PUSH(ValStr(""));
    }
    ValFree(&fmtval);
    ValFree(&tsval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "DateComponentsUtc") == 0 ||
      strcmp(name, "DateComponentsLocal") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: %s() expects 1 argument (timestamp)\n",
              name);
      return -1;
    }
    AKSValue tsval = POP();
    double tsdouble = ValKind(tsval) == VALFLOAT ? ValToFloat(tsval)
                                                 : (double)ValToInt(tsval);
    time_t t = (time_t)tsdouble;
    struct tm *ti =
        (strcmp(name, "DateComponentsUtc") == 0) ? gmtime(&t) : localtime(&t);
    AKSValue struct_val = ValStruct("DateParts");
    AKSStruct *s = ValToObj(struct_val);
    s->fieldcount = 7;
    s->fieldnames = malloc(7 * sizeof(char *));
    s->fieldvals = malloc(7 * sizeof(AKSValue));
    s->fieldnames[0] = strdup("year");
    s->fieldvals[0] = ValInt(ti ? ti->tm_year + 1900 : 1970);
    s->fieldnames[1] = strdup("month");
    s->fieldvals[1] = ValInt(ti ? ti->tm_mon + 1 : 1);
    s->fieldnames[2] = strdup("day");
    s->fieldvals[2] = ValInt(ti ? ti->tm_mday : 1);
    s->fieldnames[3] = strdup("hour");
    s->fieldvals[3] = ValInt(ti ? ti->tm_hour : 0);
    s->fieldnames[4] = strdup("minute");
    s->fieldvals[4] = ValInt(ti ? ti->tm_min : 0);
    s->fieldnames[5] = strdup("second");
    s->fieldvals[5] = ValInt(ti ? ti->tm_sec : 0);
    s->fieldnames[6] = strdup("weekday");
    s->fieldvals[6] = ValInt(ti ? ti->tm_wday : 0);
    ValFree(&tsval);
    PUSH(struct_val);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "platform") == 0) {
    if (argc != 0) {
      fprintf(stderr, "Runtime error: platform() expects 0 arguments, got %d\n",
              argc);
      return -1;
    }
#ifdef _WIN32
    PUSH(ValStr("windows"));
#elif __APPLE__
    PUSH(ValStr("macos"));
#elif __linux__
    PUSH(ValStr("linux"));
#else
    PUSH(ValStr("unknown"));
#endif
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CurrentWorkingDirectory") == 0) {
    if (argc != 0) {
      fprintf(stderr,
              "Runtime error: CurrentWorkingDirectory() expects 0 arguments, "
              "got %d\n",
              argc);
      return -1;
    }
    char *dir = GetCwdStr();
    PUSH(ValStr(dir));
    free(dir);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FFILoad") == 0) {
    AKSValue cap = ValNull();
    if (argc == 2) {
      cap = POP();
    } else if (argc != 1) {
      fprintf(stderr,
              "Runtime error: FFILoad() expects 1 or 2 arguments, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALSTR) {
      fprintf(stderr, "Runtime error: FFILoad() argument must be a string\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (vm->sandboxenabled) {
      fprintf(stderr, "Security error: FFI disabled in sandbox mode\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (vm->inrestrictedeval) {
      fprintf(stderr, "Security error: FFI disabled in restricted eval mode\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!(vm->permissions & PERMFFI)) {
      fprintf(stderr, "Security error: FFI permission denied\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    if (!ValidateCap(vm, cap, "ffi", "ffi", NULL)) {
      fprintf(stderr, "Security error: invalid FFI capability\n");
      ValFree(&arg);
      ValFree(&cap);
      return -1;
    }
    ValFree(&cap);
    int64_t libhandle = 0;
#ifdef _WIN32
    libhandle = (int64_t)LoadLibraryA(ValToStrPtr(arg)->data);
#else
    libhandle = (int64_t)dlopen(ValToStrPtr(arg)->data, RTLD_LAZY);
#endif
    PUSH(ValInt(libhandle));
    ValFree(&arg);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FFIFree") == 0) {
    if (argc != 1) {
      fprintf(stderr, "Runtime error: FFIFree() expects 1 argument, got %d\n",
              argc);
      return -1;
    }
    AKSValue arg = POP();
    if (ValKind(arg) != VALINT) {
      fprintf(stderr, "Runtime error: FFIFree() handle must be an integer\n");
      return -1;
    }
    int64_t libhandle = ValToInt(arg);
    if (libhandle != 0) {
#ifdef _WIN32
      FreeLibrary((HMODULE)libhandle);
#else
      dlclose((void *)libhandle);
#endif
    }
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FFICall") == 0) {
    if (argc != 4) {
      fprintf(stderr,
              "Runtime error: FFICall() expects 4 arguments (handle, "
              "symbolname, signature, argsarr), got %d\n",
              argc);
      return -1;
    }
    AKSValue argsarrval = POP();
    AKSValue sigval = POP();
    AKSValue symval = POP();
    AKSValue handleval = POP();

    if (ValKind(handleval) != VALINT || ValKind(symval) != VALSTR ||
        ValKind(sigval) != VALSTR || ValKind(argsarrval) != VALARR) {
      fprintf(stderr, "Runtime error: FFICall() invalid argument types\n");
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&symval);
      ValFree(&handleval);
      return -1;
    }

    int64_t handle = ValToInt(handleval);
    const char *symbolname = ValToStrPtr(symval)->data;
    const char *sig = ValToStrPtr(sigval)->data;
    AKSArray *argsarr = ValToArr(argsarrval);

    if (handle == 0) {
      fprintf(stderr, "Runtime error: FFICall() library handle is null\n");
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&symval);
      ValFree(&handleval);
      return -1;
    }

    void *proc = NULL;
#ifdef _WIN32
    proc = (void *)GetProcAddress((HMODULE)handle, symbolname);
#else
    proc = dlsym((void *)handle, symbolname);
#endif
    if (!proc) {
      fprintf(stderr, "Runtime error: FFICall() symbol '%s' not found\n",
              symbolname);
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&symval);
      ValFree(&handleval);
      return -1;
    }

    char paramtypes[16];
    int paramcount = 0;
    char rettype = sig[0];
    const char *psig = strchr(sig, '(');
    if (psig) {
      psig++;
      while (*psig && *psig != ')') {
        char t = *psig++;
        if (t == 'i' || t == 's' || t == 'p') {
          paramtypes[paramcount++] = 'i';
        } else if (t == 'd' || t == 'f') {
          paramtypes[paramcount++] = 'd';
        }
      }
    }

    if (paramcount != argsarr->count) {
      fprintf(stderr,
              "Runtime error: FFICall() signature expects %d arguments, but "
              "arguments array has %d elements\n",
              paramcount, argsarr->count);
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&symval);
      ValFree(&handleval);
      return -1;
    }

    union FFIVal {
      int64_t i;
      double d;
    } ffiargs[6];
    int mask = 0;

    for (int i = 0; i < paramcount && i < 6; i++) {
      AKSValue v = argsarr->items[i];
      if (paramtypes[i] == 'i') {
        if (ValKind(v) == VALSTR) {
          ffiargs[i].i = (int64_t)ValToStrPtr(v)->data;
        } else if (ValKind(v) == VALINT) {
          ffiargs[i].i = ValToInt(v);
        } else if (ValKind(v) == VALNULL) {
          ffiargs[i].i = 0;
        } else if (ValKind(v) == VALBOOL) {
          ffiargs[i].i = ValToBool(v);
        } else {
          ffiargs[i].i = 0;
        }
      } else {
        if (ValKind(v) == VALFLOAT) {
          ffiargs[i].d = ValToFloat(v);
          mask |= (1 << i);
        } else if (ValKind(v) == VALINT) {
          ffiargs[i].d = (double)ValToInt(v);
          mask |= (1 << i);
        } else {
          ffiargs[i].d = 0.0;
          mask |= (1 << i);
        }
      }
    }

    union FFIVal retval;
    retval.i = 0;

    if (paramcount == 0) {
      if (rettype == 'd' || rettype == 'f') {
        retval.d = ((double (*)(void))proc)();
      } else {
        retval.i = ((int64_t (*)(void))proc)();
      }
    } else if (paramcount == 1) {
      if (mask == 0) {
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(int64_t))proc)(ffiargs[0].i);
        else
          retval.i = ((int64_t (*)(int64_t))proc)(ffiargs[0].i);
      } else {
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(double))proc)(ffiargs[0].d);
        else
          retval.i = ((int64_t (*)(double))proc)(ffiargs[0].d);
      }
    } else if (paramcount == 2) {
      switch (mask) {
      case 0:
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(int64_t, int64_t))proc)(ffiargs[0].i, ffiargs[1].i);
        else
          retval.i =
              ((int64_t (*)(int64_t, int64_t))proc)(ffiargs[0].i, ffiargs[1].i);
        break;
      case 1:
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(double, int64_t))proc)(ffiargs[0].d, ffiargs[1].i);
        else
          retval.i =
              ((int64_t (*)(double, int64_t))proc)(ffiargs[0].d, ffiargs[1].i);
        break;
      case 2:
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(int64_t, double))proc)(ffiargs[0].i, ffiargs[1].d);
        else
          retval.i =
              ((int64_t (*)(int64_t, double))proc)(ffiargs[0].i, ffiargs[1].d);
        break;
      case 3:
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(double, double))proc)(ffiargs[0].d, ffiargs[1].d);
        else
          retval.i =
              ((int64_t (*)(double, double))proc)(ffiargs[0].d, ffiargs[1].d);
        break;
      }
    } else if (paramcount == 3) {
      switch (mask) {
      case 0:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(int64_t, int64_t, int64_t))proc)(
              ffiargs[0].i, ffiargs[1].i, ffiargs[2].i);
        else
          retval.i = ((int64_t (*)(int64_t, int64_t, int64_t))proc)(
              ffiargs[0].i, ffiargs[1].i, ffiargs[2].i);
        break;
      case 7:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(double, double, double))proc)(
              ffiargs[0].d, ffiargs[1].d, ffiargs[2].d);
        else
          retval.i = ((int64_t (*)(double, double, double))proc)(
              ffiargs[0].d, ffiargs[1].d, ffiargs[2].d);
        break;
      default:
        if (rettype == 'd' || rettype == 'f') {
          retval.d = ((double (*)(double, double, double))proc)(
              (mask & 1) ? ffiargs[0].d : (double)ffiargs[0].i,
              (mask & 2) ? ffiargs[1].d : (double)ffiargs[1].i,
              (mask & 4) ? ffiargs[2].d : (double)ffiargs[2].i);
        } else {
          retval.i = ((int64_t (*)(int64_t, int64_t, int64_t))proc)(
              (mask & 1) ? (int64_t)ffiargs[0].d : ffiargs[0].i,
              (mask & 2) ? (int64_t)ffiargs[1].d : ffiargs[1].i,
              (mask & 4) ? (int64_t)ffiargs[2].d : ffiargs[2].i);
        }
        break;
      }
    } else {
      if (rettype == 'd' || rettype == 'f') {
        retval.d = ((double (*)(double, double, double, double))proc)(
            (mask & 1) ? ffiargs[0].d : (double)ffiargs[0].i,
            (mask & 2) ? ffiargs[1].d : (double)ffiargs[1].i,
            (mask & 4) ? ffiargs[2].d : (double)ffiargs[2].i,
            (mask & 8) ? ffiargs[3].d : (double)ffiargs[3].i);
      } else {
        retval.i = ((int64_t (*)(int64_t, int64_t, int64_t, int64_t))proc)(
            (mask & 1) ? (int64_t)ffiargs[0].d : ffiargs[0].i,
            (mask & 2) ? (int64_t)ffiargs[1].d : ffiargs[1].i,
            (mask & 4) ? (int64_t)ffiargs[2].d : ffiargs[2].i,
            (mask & 8) ? (int64_t)ffiargs[3].d : ffiargs[3].i);
      }
    }

    if (rettype == 'v') {
      PUSH(ValNull());
    } else if (rettype == 'd' || rettype == 'f') {
      PUSH(ValFloat(retval.d));
    } else if (rettype == 's') {
      PUSH(ValStr((const char *)retval.i));
    } else {
      PUSH(ValInt(retval.i));
    }

    ValFree(&argsarrval);
    ValFree(&sigval);
    ValFree(&symval);
    ValFree(&handleval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "COMCall") == 0) {
    if (argc != 4) {
      fprintf(stderr,
              "Runtime error: COMCall() expects 4 arguments (interface_ptr, "
              "method_index, signature, argsarr), got %d\n",
              argc);
      return -1;
    }
    AKSValue argsarrval = POP();
    AKSValue sigval = POP();
    AKSValue idxval = POP();
    AKSValue ptrval = POP();

    if ((ValKind(ptrval) != VALINT && ValKind(ptrval) != VALNULL) ||
        ValKind(idxval) != VALINT || ValKind(sigval) != VALSTR ||
        ValKind(argsarrval) != VALARR) {
      fprintf(stderr, "Runtime error: COMCall() invalid argument types\n");
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&idxval);
      ValFree(&ptrval);
      return -1;
    }

    void *interface_ptr = (void *)ValToInt(ptrval);
    int method_index = (int)ValToInt(idxval);
    const char *sig = ValToStrPtr(sigval)->data;
    AKSArray *argsarr = ValToArr(argsarrval);

    if (!interface_ptr) {
      fprintf(stderr, "Runtime error: COMCall() interface pointer is null\n");
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&idxval);
      ValFree(&ptrval);
      return -1;
    }

    void **vtable = *(void ***)interface_ptr;
    void *proc = vtable[method_index];
    if (!proc) {
      fprintf(
          stderr,
          "Runtime error: COMCall() method at index %d not found in vtable\n",
          method_index);
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&idxval);
      ValFree(&ptrval);
      return -1;
    }

    char paramtypes[16];
    int paramcount = 0;
    char rettype = sig[0];
    const char *psig = strchr(sig, '(');
    if (psig) {
      psig++;
      while (*psig && *psig != ')') {
        char t = *psig++;
        if (t == 'i' || t == 's' || t == 'p') {
          paramtypes[paramcount++] = 'i';
        } else if (t == 'd' || t == 'f') {
          paramtypes[paramcount++] = 'd';
        }
      }
    }

    if (paramcount != argsarr->count) {
      fprintf(stderr,
              "Runtime error: COMCall() signature expects %d arguments, but "
              "arguments array has %d elements\n",
              paramcount, argsarr->count);
      ValFree(&argsarrval);
      ValFree(&sigval);
      ValFree(&idxval);
      ValFree(&ptrval);
      return -1;
    }

    union FFIVal {
      int64_t i;
      double d;
    } ffiargs[6];
    int mask = 0;

    for (int i = 0; i < paramcount && i < 6; i++) {
      AKSValue v = argsarr->items[i];
      if (paramtypes[i] == 'i') {
        if (ValKind(v) == VALSTR) {
          ffiargs[i].i = (int64_t)ValToStrPtr(v)->data;
        } else if (ValKind(v) == VALINT) {
          ffiargs[i].i = ValToInt(v);
        } else if (ValKind(v) == VALNULL) {
          ffiargs[i].i = 0;
        } else if (ValKind(v) == VALBOOL) {
          ffiargs[i].i = ValToBool(v);
        } else {
          ffiargs[i].i = 0;
        }
      } else {
        if (ValKind(v) == VALFLOAT) {
          ffiargs[i].d = ValToFloat(v);
          mask |= (1 << i);
        } else if (ValKind(v) == VALINT) {
          ffiargs[i].d = (double)ValToInt(v);
          mask |= (1 << i);
        } else {
          ffiargs[i].d = 0.0;
          mask |= (1 << i);
        }
      }
    }

    union FFIVal retval;
    retval.i = 0;

    if (paramcount == 0) {
      if (rettype == 'd' || rettype == 'f') {
        retval.d = ((double (*)(void *))proc)(interface_ptr);
      } else {
        retval.i = ((int64_t (*)(void *))proc)(interface_ptr);
      }
    } else if (paramcount == 1) {
      if (mask == 0) {
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(void *, int64_t))proc)(interface_ptr, ffiargs[0].i);
        else
          retval.i =
              ((int64_t (*)(void *, int64_t))proc)(interface_ptr, ffiargs[0].i);
      } else {
        if (rettype == 'd' || rettype == 'f')
          retval.d =
              ((double (*)(void *, double))proc)(interface_ptr, ffiargs[0].d);
        else
          retval.i =
              ((int64_t (*)(void *, double))proc)(interface_ptr, ffiargs[0].d);
      }
    } else if (paramcount == 2) {
      switch (mask) {
      case 0:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, int64_t, int64_t))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].i);
        else
          retval.i = ((int64_t (*)(void *, int64_t, int64_t))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].i);
        break;
      case 1:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, double, int64_t))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].i);
        else
          retval.i = ((int64_t (*)(void *, double, int64_t))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].i);
        break;
      case 2:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, int64_t, double))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].d);
        else
          retval.i = ((int64_t (*)(void *, int64_t, double))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].d);
        break;
      case 3:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, double, double))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].d);
        else
          retval.i = ((int64_t (*)(void *, double, double))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].d);
        break;
      }
    } else if (paramcount == 3) {
      switch (mask) {
      case 0:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, int64_t, int64_t, int64_t))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].i, ffiargs[2].i);
        else
          retval.i = ((int64_t (*)(void *, int64_t, int64_t, int64_t))proc)(
              interface_ptr, ffiargs[0].i, ffiargs[1].i, ffiargs[2].i);
        break;
      case 7:
        if (rettype == 'd' || rettype == 'f')
          retval.d = ((double (*)(void *, double, double, double))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].d, ffiargs[2].d);
        else
          retval.i = ((int64_t (*)(void *, double, double, double))proc)(
              interface_ptr, ffiargs[0].d, ffiargs[1].d, ffiargs[2].d);
        break;
      default:
        if (rettype == 'd' || rettype == 'f') {
          retval.d = ((double (*)(void *, double, double, double))proc)(
              interface_ptr, (mask & 1) ? ffiargs[0].d : (double)ffiargs[0].i,
              (mask & 2) ? ffiargs[1].d : (double)ffiargs[1].i,
              (mask & 4) ? ffiargs[2].d : (double)ffiargs[2].i);
        } else {
          retval.i = ((int64_t (*)(void *, int64_t, int64_t, int64_t))proc)(
              interface_ptr, (mask & 1) ? (int64_t)ffiargs[0].d : ffiargs[0].i,
              (mask & 2) ? (int64_t)ffiargs[1].d : ffiargs[1].i,
              (mask & 4) ? (int64_t)ffiargs[2].d : ffiargs[2].i);
        }
        break;
      }
    } else {
      if (rettype == 'd' || rettype == 'f') {
        retval.d = ((double (*)(void *, double, double, double, double))proc)(
            interface_ptr, (mask & 1) ? ffiargs[0].d : (double)ffiargs[0].i,
            (mask & 2) ? ffiargs[1].d : (double)ffiargs[1].i,
            (mask & 4) ? ffiargs[2].d : (double)ffiargs[2].i,
            (mask & 8) ? ffiargs[3].d : (double)ffiargs[3].i);
      } else {
        retval.i =
            ((int64_t (*)(void *, int64_t, int64_t, int64_t, int64_t))proc)(
                interface_ptr,
                (mask & 1) ? (int64_t)ffiargs[0].d : ffiargs[0].i,
                (mask & 2) ? (int64_t)ffiargs[1].d : ffiargs[1].i,
                (mask & 4) ? (int64_t)ffiargs[2].d : ffiargs[2].i,
                (mask & 8) ? (int64_t)ffiargs[3].d : ffiargs[3].i);
      }
    }

    if (rettype == 'v') {
      PUSH(ValNull());
    } else if (rettype == 'd' || rettype == 'f') {
      PUSH(ValFloat(retval.d));
    } else if (rettype == 's') {
      PUSH(ValStr((const char *)retval.i));
    } else {
      PUSH(ValInt(retval.i));
    }

    ValFree(&argsarrval);
    ValFree(&sigval);
    ValFree(&idxval);
    ValFree(&ptrval);
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioWrite8") == 0) {
    if (argc < 2)
      return -1;
    uint8_t val = (uint8_t)ValToInt(POP());
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    *(volatile uint8_t *)addr = val;
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioRead8") == 0) {
    if (argc < 1)
      return -1;
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    uint8_t val = *(volatile uint8_t *)addr;
    PUSH(ValInt(val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioWrite16") == 0) {
    if (argc < 2)
      return -1;
    uint16_t val = (uint16_t)ValToInt(POP());
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    *(volatile uint16_t *)addr = val;
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioRead16") == 0) {
    if (argc < 1)
      return -1;
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    uint16_t val = *(volatile uint16_t *)addr;
    PUSH(ValInt(val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioWrite32") == 0) {
    if (argc < 2)
      return -1;
    uint32_t val = (uint32_t)ValToInt(POP());
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    *(volatile uint32_t *)addr = val;
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioRead32") == 0) {
    if (argc < 1)
      return -1;
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    uint32_t val = *(volatile uint32_t *)addr;
    PUSH(ValInt((int64_t)val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioWrite64") == 0 || strcmp(name, "VolatileWrite") == 0) {
    if (argc < 2)
      return -1;
    uint64_t val = (uint64_t)ValToInt(POP());
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    *(volatile uint64_t *)addr = val;
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "MmioRead64") == 0 || strcmp(name, "VolatileRead") == 0) {
    if (argc < 1)
      return -1;
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    uint64_t val = *(volatile uint64_t *)addr;
    PUSH(ValInt((int64_t)val));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FreestandingMemSet") == 0) {
    if (argc < 3)
      return -1;
    int count = (int)ValToInt(POP());
    uint8_t byte_val = (uint8_t)ValToInt(POP());
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    volatile uint8_t *p = (volatile uint8_t *)addr;
    for (int i = 0; i < count; i++) {
      p[i] = byte_val;
    }
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FreestandingMemCopy") == 0) {
    if (argc < 3)
      return -1;
    int count = (int)ValToInt(POP());
    uintptr_t src_addr = (uintptr_t)ValToInt(POP());
    uintptr_t dst_addr = (uintptr_t)ValToInt(POP());
    volatile uint8_t *dst = (volatile uint8_t *)dst_addr;
    const volatile uint8_t *src = (const volatile uint8_t *)src_addr;
    for (int i = 0; i < count; i++) {
      dst[i] = src[i];
    }
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FreestandingAlloc") == 0) {
    if (argc < 1)
      return -1;
    size_t size = (size_t)ValToInt(POP());
    void *ptr = calloc(1, size > 0 ? size : 1);
    PUSH(ValInt((int64_t)(uintptr_t)ptr));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "FreestandingFree") == 0) {
    if (argc < 1)
      return -1;
    uintptr_t addr = (uintptr_t)ValToInt(POP());
    if (addr)
      free((void *)addr);
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuNop") == 0) {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    __nop();
#elif defined(__GNUC__)
    __asm__ __volatile__("nop");
#endif
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuPause") == 0) {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    _mm_pause();
#elif defined(__GNUC__)
    __asm__ __volatile__("pause");
#endif
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuHalt") == 0) {
#if defined(_WIN32)
    Sleep(1);
#else
    usleep(1000);
#endif
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "InPort8") == 0 || strcmp(name, "InPortByte") == 0 ||
      strcmp(name, "InPort16") == 0 || strcmp(name, "InPort32") == 0) {
    if (argc < 1)
      return -1;
    (void)POP();
    PUSH(ValInt(0));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "OutPort8") == 0 || strcmp(name, "OutPortByte") == 0 ||
      strcmp(name, "OutPort16") == 0 || strcmp(name, "OutPort32") == 0) {
    if (argc < 2)
      return -1;
    (void)POP();
    (void)POP();
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuCli") == 0 || strcmp(name, "CpuSti") == 0) {
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuLidt") == 0 || strcmp(name, "CpuLgdt") == 0 ||
      strcmp(name, "CpuWriteCr3") == 0) {
    if (argc < 1)
      return -1;
    (void)POP();
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CpuReadCr3") == 0) {
    PUSH(ValInt(0x1000));
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "VgaPrintChar") == 0) {
    if (argc < 4)
      return -1;
    (void)POP();
    (void)POP();
    (void)POP();
    (void)POP();
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "SerialWrite") == 0) {
    if (argc < 2)
      return -1;
    (void)POP();
    (void)POP();
    PUSH(ValNull());
    *stacktopptr = stacktop;
    return 1;
  }
  if (strcmp(name, "CImportParseHeaderC") == 0) {
    if (argc < 1)
      return -1;
    AKSValue path_or_src = POP();
    if (ValKind(path_or_src) != VALSTR) {
      ValFree(&path_or_src);
      PUSH(ValNull());
      *stacktopptr = stacktop;
      return 1;
    }
    const char *raw_input = ValToStrPtr(path_or_src)->data;
    char *header_text = NULL;
    FILE *f = fopen(raw_input, "rb");
    if (f) {
      fseek(f, 0, SEEK_END);
      long sz = ftell(f);
      if (sz < 0) sz = 0;
      fseek(f, 0, SEEK_SET);
      header_text = (char *)malloc((size_t)sz + 1);
      if (header_text) {
        size_t nr = fread(header_text, 1, (size_t)sz, f);
        header_text[nr] = '\0';
      }
      fclose(f);
    } else {
      header_text = strdup(raw_input);
    }
    ValFree(&path_or_src);

    if (!header_text) {
      PUSH(ValStruct("CHeader"));
      *stacktopptr = stacktop;
      return 1;
    }

    size_t len = strlen(header_text);
    char *clean = (char *)malloc(len + 1);
    size_t c_idx = 0;
    int in_scomment = 0, in_bcomment = 0;
    for (size_t i = 0; i < len; i++) {
      if (in_scomment) {
        if (header_text[i] == '\n')
          in_scomment = 0;
        continue;
      }
      if (in_bcomment) {
        if (header_text[i] == '*' && i + 1 < len && header_text[i + 1] == '/') {
          in_bcomment = 0;
          i++;
        }
        continue;
      }
      if (header_text[i] == '/' && i + 1 < len && header_text[i + 1] == '/') {
        in_scomment = 1;
        i++;
        continue;
      }
      if (header_text[i] == '/' && i + 1 < len && header_text[i + 1] == '*') {
        in_bcomment = 1;
        i++;
        continue;
      }
      if (header_text[i] == '#') {
        while (i < len && header_text[i] != '\n')
          i++;
        continue;
      }
      clean[c_idx++] = (header_text[i] == '\n' || header_text[i] == '\r' ||
                        header_text[i] == '\t')
                           ? ' '
                           : header_text[i];
    }
    clean[c_idx] = '\0';
    free(header_text);

    AKSValue res_struct = ValStruct("CHeader");
    AKSStruct *st = ValToObj(res_struct);
    st->fieldcount = 0;
    st->fieldnames = NULL;
    st->fieldvals = NULL;

    char *p = clean;
    while (*p) {
      char *semi = strchr(p, ';');
      if (!semi)
        break;
      *semi = '\0';
      char *decl = p;
      p = semi + 1;

      while (*decl && isspace((unsigned char)*decl))
        decl++;
      char *dend = decl + strlen(decl);
      while (dend > decl && isspace((unsigned char)*(dend - 1))) {
        *(--dend) = '\0';
      }

      char *lparen = strchr(decl, '(');
      char *rparen = strrchr(decl, ')');
      if (lparen && rparen && lparen < rparen) {
        *lparen = '\0';
        *rparen = '\0';
        char *params_str = lparen + 1;

        char *name_end = decl + strlen(decl);
        while (name_end > decl && isspace((unsigned char)*(name_end - 1))) {
          *(--name_end) = '\0';
        }
        char *name_start = name_end;
        while (name_start > decl &&
               (isalnum((unsigned char)*(name_start - 1)) ||
                *(name_start - 1) == '_')) {
          name_start--;
        }

        if (name_start < name_end &&
            (isalpha((unsigned char)*name_start) || *name_start == '_')) {
          char fn_name[128];
          size_t fn_len = name_end - name_start;
          if (fn_len >= sizeof(fn_name))
            fn_len = sizeof(fn_name) - 1;
          memcpy(fn_name, name_start, fn_len);
          fn_name[fn_len] = '\0';

          char ret_type[128] = "int";
          size_t ret_len = name_start - decl;
          if (ret_len > 0) {
            if (ret_len >= sizeof(ret_type))
              ret_len = sizeof(ret_type) - 1;
            memcpy(ret_type, decl, ret_len);
            ret_type[ret_len] = '\0';
          }

          char ret_sig = 'i';
          if (strstr(ret_type, "double") || strstr(ret_type, "float"))
            ret_sig = 'd';
          else if (strstr(ret_type, "void"))
            ret_sig = 'v';
          else if (strstr(ret_type, "char*") || strstr(ret_type, "char *"))
            ret_sig = 's';
          else if (strchr(ret_type, '*'))
            ret_sig = 'p';

          char param_sigs[32];
          int param_count = 0;
          char *tok = strtok(params_str, ",");
          while (tok && param_count < 16) {
            while (*tok && isspace((unsigned char)*tok))
              tok++;
            if (strcmp(tok, "void") == 0) {
              tok = strtok(NULL, ",");
              continue;
            }
            if (strstr(tok, "double") || strstr(tok, "float")) {
              param_sigs[param_count++] = 'd';
            } else if (strstr(tok, "char*") || strstr(tok, "char *")) {
              param_sigs[param_count++] = 's';
            } else if (strchr(tok, '*')) {
              param_sigs[param_count++] = 'p';
            } else {
              param_sigs[param_count++] = 'i';
            }
            tok = strtok(NULL, ",");
          }
          param_sigs[param_count] = '\0';

          char sig_buf[64];
          snprintf(sig_buf, sizeof(sig_buf), "%c(%s)", ret_sig, param_sigs);

          st->fieldcount++;
          st->fieldnames =
              (char **)realloc(st->fieldnames, st->fieldcount * sizeof(char *));
          st->fieldvals = (AKSValue *)realloc(
              st->fieldvals, st->fieldcount * sizeof(AKSValue));
          st->fieldnames[st->fieldcount - 1] = strdup(fn_name);
          st->fieldvals[st->fieldcount - 1] = ValStr(sig_buf);
        }
      }
    }
    free(clean);

    PUSH(res_struct);
    *stacktopptr = stacktop;
    return 1;
  }

#undef PUSH
#undef POP
  return 0;
}

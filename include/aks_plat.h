// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef AKS_PLAT_H
#define AKS_PLAT_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <io.h>
#include <process.h>
#define SleepMs(ms) Sleep((DWORD)(ms))
#else
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <locale.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#endif
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <signal.h>

#ifndef MemoryBarrier
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

#ifndef _popen
#define _popen(cmd, mode) popen(cmd, mode)
#endif
#ifndef _pclose
#define _pclose(stream) pclose(stream)
#endif

#define SleepMs(ms) usleep((ms) * 1000)
#define Sleep(ms) usleep((ms) * 1000)

typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(s) close(s)
#define WSAGetLastError() errno
static inline int AksWsaStartupNoop(unsigned short wVersionRequired, void *lpWSAData) {
    (void)wVersionRequired; (void)lpWSAData; return 0;
}
static inline int AksWsaCleanupNoop(void) { return 0; }
#define WSAStartup(wVersionRequired, lpWSAData) AksWsaStartupNoop((wVersionRequired), (lpWSAData))
#define WSACleanup() AksWsaCleanupNoop()
typedef struct { int dummy; } WSADATA;
#define MAKEWORD(a, b) 0

#define CRITICAL_SECTION pthread_mutex_t
#define InitializeCriticalSection(lock) pthread_mutex_init(lock, NULL)
#define DeleteCriticalSection(lock) pthread_mutex_destroy(lock)
#define EnterCriticalSection(lock) pthread_mutex_lock(lock)
#define LeaveCriticalSection(lock) pthread_mutex_unlock(lock)

#define CONDITIONVAR pthread_cond_t
#define InitializeConditionVariable(cond) pthread_cond_init(cond, NULL)

static inline int LnxSleepConditionVariableCS(pthread_cond_t *cond, pthread_mutex_t *lock, unsigned long timeout_ms) {
    if (timeout_ms == 0xFFFFFFFFUL || timeout_ms == 0) {
        return pthread_cond_wait(cond, lock) == 0;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }
    return pthread_cond_timedwait(cond, lock, &ts) == 0;
}
#define SleepConditionVariableCS(cond, lock, timeout) LnxSleepConditionVariableCS(cond, lock, timeout)
#define WakeAllConditionVariable(cond) pthread_cond_broadcast(cond)

#define InterlockedDecrement(ptr) __sync_sub_and_fetch(ptr, 1)
#define InterlockedIncrement(ptr) __sync_add_and_fetch(ptr, 1)

#ifndef BOOL
#define BOOL int
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define DWORD unsigned long
#define WINAPI
#define LPVOID void*
#define HANDLE void*
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif
#define CloseHandle(h) LnxCloseHandle(h)
#define INFINITE 0xFFFFFFFFUL
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define WAIT_FAILED 0xFFFFFFFFUL
#define CreateEvent(attr, manual, initial, name) LnxCreateEvent(manual, initial)
#define CreateEventA(attr, manual, initial, name) LnxCreateEvent(manual, initial)
#define SetEvent(h) LnxSetEvent(h)
#define ResetEvent(h) LnxResetEvent(h)
#define WaitForSingleObject(h, timeout) LnxWaitHandle(h, timeout)

typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int signaled;
    int is_thread;
    int joined;
    int manual_reset;
} LinuxHandle;

typedef struct {
    void* (*func)(void*);
    void* arg;
    LinuxHandle *h;
} LnxThreadArg;

static inline void* LnxThreadProc(void *p) {
    LnxThreadArg *ta = (LnxThreadArg*)p;
    void* (*func)(void*) = ta->func;
    void *arg = ta->arg;
    LinuxHandle *h = ta->h;
    free(ta);

    void *res = func(arg);

    pthread_mutex_lock(&h->mutex);
    h->signaled = 1;
    pthread_cond_broadcast(&h->cond);
    pthread_mutex_unlock(&h->mutex);

    return res;
}

static inline void* LnxCreateEvent(int manual_reset, int initial_state) {
    LinuxHandle *h = calloc(1, sizeof(LinuxHandle));
    if (!h) return NULL;
    h->is_thread = 0;
    h->joined = 0;
    h->manual_reset = manual_reset ? 1 : 0;
    h->signaled = initial_state ? 1 : 0;
    pthread_mutex_init(&h->mutex, NULL);
    pthread_cond_init(&h->cond, NULL);
    return h;
}

static inline BOOL LnxSetEvent(void *handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE) return FALSE;
    LinuxHandle *h = (LinuxHandle*)handle;
    pthread_mutex_lock(&h->mutex);
    h->signaled = 1;
    pthread_cond_broadcast(&h->cond);
    pthread_mutex_unlock(&h->mutex);
    return TRUE;
}

static inline BOOL LnxResetEvent(void *handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE) return FALSE;
    LinuxHandle *h = (LinuxHandle*)handle;
    pthread_mutex_lock(&h->mutex);
    h->signaled = 0;
    pthread_mutex_unlock(&h->mutex);
    return TRUE;
}

static inline BOOL LnxCloseHandle(void *handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE) return FALSE;
    LinuxHandle *h = (LinuxHandle*)handle;
    if (h->is_thread) {
        pthread_mutex_lock(&h->mutex);
        if (!h->joined) {
            h->joined = 1;
            pthread_detach(h->thread);
        }
        pthread_mutex_unlock(&h->mutex);
        pthread_mutex_destroy(&h->mutex);
        pthread_cond_destroy(&h->cond);
    } else {
        pthread_mutex_destroy(&h->mutex);
        pthread_cond_destroy(&h->cond);
    }
    free(h);
    return TRUE;
}

static inline DWORD LnxWaitHandle(void *handle, DWORD timeout_ms) {
    if (!handle) return WAIT_FAILED;
    LinuxHandle *h = (LinuxHandle*)handle;

    pthread_mutex_lock(&h->mutex);
    if (h->signaled) {
        if (!h->is_thread) {
            if (!h->manual_reset) h->signaled = 0;
        } else if (!h->joined) {
            h->joined = 1;
            pthread_join(h->thread, NULL);
        }
        pthread_mutex_unlock(&h->mutex);
        return WAIT_OBJECT_0;
    }

    if (timeout_ms == INFINITE) {
        while (!h->signaled) {
            pthread_cond_wait(&h->cond, &h->mutex);
        }
        if (!h->is_thread) {
            if (!h->manual_reset) h->signaled = 0;
        } else if (!h->joined) {
            h->joined = 1;
            pthread_join(h->thread, NULL);
        }
        pthread_mutex_unlock(&h->mutex);
        return WAIT_OBJECT_0;
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }
        int rc = 0;
        while (!h->signaled && rc == 0) {
            rc = pthread_cond_timedwait(&h->cond, &h->mutex, &ts);
        }
        if (h->signaled) {
            if (!h->is_thread) {
                if (!h->manual_reset) h->signaled = 0;
            } else if (!h->joined) {
                h->joined = 1;
                pthread_join(h->thread, NULL);
            }
            pthread_mutex_unlock(&h->mutex);
            return WAIT_OBJECT_0;
        }
        pthread_mutex_unlock(&h->mutex);
        return WAIT_TIMEOUT;
    }
}

static inline void* LnxCreateThread(void* unused, size_t stack_size, void* func, void* arg, DWORD flags, void* unused2) {
    (void)unused; (void)flags; (void)unused2;
    LinuxHandle *h = calloc(1, sizeof(LinuxHandle));
    if (!h) return NULL;
    h->is_thread = 1;
    h->signaled = 0;
    h->joined = 0;
    pthread_mutex_init(&h->mutex, NULL);
    pthread_cond_init(&h->cond, NULL);

    LnxThreadArg *ta = malloc(sizeof(LnxThreadArg));
    if (!ta) {
        pthread_mutex_destroy(&h->mutex);
        pthread_cond_destroy(&h->cond);
        free(h);
        return NULL;
    }
    ta->func = (void* (*)(void*))func;
    ta->arg = arg;
    ta->h = h;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack_size > 0) {
        pthread_attr_setstacksize(&attr, stack_size);
    }
    int rc = pthread_create(&h->thread, &attr, LnxThreadProc, ta);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(ta);
        pthread_mutex_destroy(&h->mutex);
        pthread_cond_destroy(&h->cond);
        free(h);
        return NULL;
    }
    return h;
}

#define CreateThread(attr, stack, func, arg, flags, tid) LnxCreateThread(attr, stack, (void*)func, arg, flags, tid)

typedef struct {
    int dwNumberOfProcessors;
} SYSTEM_INFO;

static inline void GetSystemInfo(SYSTEM_INFO *si) {
    si->dwNumberOfProcessors = sysconf(_SC_NPROCESSORS_ONLN);
    if (si->dwNumberOfProcessors < 1) si->dwNumberOfProcessors = 1;
}

#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif
#ifndef LPCWSTR
typedef const wchar_t *LPCWSTR;
#endif
#ifndef ULONG
typedef uint32_t ULONG;
#endif

#ifndef BCRYPT_SHA256_ALGORITHM
#define BCRYPT_SHA256_ALGORITHM L"SHA256"
#endif
#ifndef BCRYPT_SHA512_ALGORITHM
#define BCRYPT_SHA512_ALGORITHM L"SHA512"
#endif
#ifndef BCRYPT_AES_ALGORITHM
#define BCRYPT_AES_ALGORITHM L"AES"
#endif
#ifndef BCRYPT_ECDSA_ALGORITHM
#define BCRYPT_ECDSA_ALGORITHM L"ECDSA"
#endif
#ifndef BCRYPT_ECDSA_P256_ALGORITHM
#define BCRYPT_ECDSA_P256_ALGORITHM L"ECDSA_P256"
#endif
#ifndef BCRYPT_ECDH_ALGORITHM
#define BCRYPT_ECDH_ALGORITHM L"ECDH"
#endif
#ifndef BCRYPT_ECC_CURVE_25519
#define BCRYPT_ECC_CURVE_25519 L"curve25519"
#endif

#ifndef MAX_PATH
#ifdef PATH_MAX
#define MAX_PATH PATH_MAX
#else
#define MAX_PATH 4096
#endif
#endif

typedef int64_t LONG64;
#define InterlockedCompareExchange64(dest, exch, comp) __sync_val_compare_and_swap((dest), (comp), (exch))
#define InterlockedExchange64(dest, val) __sync_lock_test_and_set((dest), (val))
#define InterlockedOr64(dest, val) __sync_fetch_and_or((dest), (val))

static inline void *_aligned_malloc(size_t size, size_t alignment) {
    void *ptr = NULL;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (posix_memalign(&ptr, alignment, size ? size : 1) != 0) return NULL;
    return ptr;
}
#define _aligned_free(ptr) free(ptr)

#define _byteswap_ushort(x) __builtin_bswap16((uint16_t)(x))
#define _byteswap_ulong(x)  __builtin_bswap32((uint32_t)(x))
#define _byteswap_uint64(x) __builtin_bswap64((uint64_t)(x))

static inline unsigned char _BitScanForward(unsigned long *index, unsigned long mask) {
    if (mask == 0) return 0;
    *index = (unsigned long)__builtin_ctzl(mask);
    return 1;
}
static inline unsigned char _BitScanForward64(unsigned long *index, unsigned long long mask) {
    if (mask == 0) return 0;
    *index = (unsigned long)__builtin_ctzll(mask);
    return 1;
}
static inline unsigned char _BitScanReverse64(unsigned long *index, unsigned long long mask) {
    if (mask == 0) return 0;
    *index = (unsigned long)(63 - __builtin_clzll(mask));
    return 1;
}

#endif // _win32

#endif // Aks_plat_h

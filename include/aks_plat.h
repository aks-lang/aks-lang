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
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>

#define SleepMs(ms) usleep((ms) * 1000)
#define Sleep(ms) usleep((ms) * 1000)

typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(s) close(s)
#define WSAGetLastError() errno
#define WSAStartup(wVersionRequired, lpWSAData) (0)
#define WSACleanup() (0)
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

#endif // _win32

#endif // Aks_plat_h

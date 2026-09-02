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
#define SleepConditionVariableCS(cond, lock, timeout) pthread_cond_wait(cond, lock)
#define WakeAllConditionVariable(cond) pthread_cond_broadcast(cond)

#define InterlockedDecrement(ptr) __sync_sub_and_fetch(ptr, 1)
#define InterlockedIncrement(ptr) __sync_add_and_fetch(ptr, 1)

#define DWORD unsigned long
#define WINAPI
#define LPVOID void*
#define HANDLE void*
#define CloseHandle(h) LnxCloseHandle(h)
#define INFINITE 0
#define CreateEvent(attr, manual, initial, name) LnxCreateEvent()
#define SetEvent(h) LnxSetEvent(h)
#define WaitForSingleObject(h, timeout) (LnxWaitHandle(h), 0)
#define WAIT_TIMEOUT 258

typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int signaled;
    int is_thread;
} LinuxHandle;

static inline void* LnxCreateEvent(void) {
    LinuxHandle *h = calloc(1, sizeof(LinuxHandle));
    h->is_thread = 0;
    pthread_mutex_init(&h->mutex, NULL);
    pthread_cond_init(&h->cond, NULL);
    h->signaled = 0;
    return h;
}

static inline void LnxSetEvent(void *handle) {
    if (!handle) return;
    LinuxHandle *h = (LinuxHandle*)handle;
    pthread_mutex_lock(&h->mutex);
    h->signaled = 1;
    pthread_cond_broadcast(&h->cond);
    pthread_mutex_unlock(&h->mutex);
}

static inline void LnxCloseHandle(void *handle) {
    if (!handle) return;
    LinuxHandle *h = (LinuxHandle*)handle;
    if (h->is_thread) {
        pthread_detach(h->thread);
    } else {
        pthread_mutex_destroy(&h->mutex);
        pthread_cond_destroy(&h->cond);
    }
    free(h);
}

static inline void LnxWaitHandle(void *handle) {
    if (!handle) return;
    LinuxHandle *h = (LinuxHandle*)handle;
    if (h->is_thread) {
        pthread_join(h->thread, NULL);
    } else {
        pthread_mutex_lock(&h->mutex);
        while (!h->signaled) {
            pthread_cond_wait(&h->cond, &h->mutex);
        }
        h->signaled = 0;
        pthread_mutex_unlock(&h->mutex);
    }
}

static inline void* LnxCreateThread(void* unused, size_t stack_size, void* func, void* arg, DWORD flags, void* unused2) {
    LinuxHandle *h = calloc(1, sizeof(LinuxHandle));
    h->is_thread = 1;
    
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack_size > 0) {
        pthread_attr_setstacksize(&attr, stack_size);
    }
    pthread_create(&h->thread, &attr, (void* (*)(void*))func, arg);
    pthread_attr_destroy(&attr);
    
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

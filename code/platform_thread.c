/* platform_thread.c - Linux/FreeBSD (pthreads) e Windows (Win32) */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform_thread.h"
#include <stdint.h>

#ifdef _WIN32

#include <windows.h>
#include <stdlib.h>

/* ---------- Threads ---------- */

/* Adaptador: CreateThread exige DWORD WINAPI f(LPVOID),
   mas a API do projeto usa void *(*)(void *). */
typedef struct {
    pt_thread_fn fn;
    void        *arg;
} pt_start_t;

static DWORD WINAPI pt_thread_trampoline(LPVOID p)
{
    pt_start_t s = *(pt_start_t *)p;
    free(p);
    s.fn(s.arg);            /* retorno da thread é descartado */
    return 0;
}

int pt_thread_create(pt_thread_t *t, pt_thread_fn fn, void *arg)
{
    pt_start_t *s = (pt_start_t *)malloc(sizeof *s);
    if (!s) return -1;
    s->fn  = fn;
    s->arg = arg;

    t->h = CreateThread(NULL, 0, pt_thread_trampoline, s, 0, NULL);
    if (t->h == NULL) {
        free(s);
        return -1;
    }
    return 0;
}

int pt_thread_join(pt_thread_t *t)
{
    if (WaitForSingleObject(t->h, INFINITE) != WAIT_OBJECT_0)
        return -1;
    CloseHandle(t->h);
    t->h = NULL;
    return 0;
}

/* ---------- Mutex (CRITICAL_SECTION) ---------- */

int pt_mutex_init(pt_mutex_t *m)
{
    InitializeCriticalSection(&m->cs);
    return 0;
}

void pt_mutex_lock(pt_mutex_t *m)    { EnterCriticalSection(&m->cs); }
void pt_mutex_unlock(pt_mutex_t *m)  { LeaveCriticalSection(&m->cs); }
void pt_mutex_destroy(pt_mutex_t *m) { DeleteCriticalSection(&m->cs); }

/* ---------- Spinlock (TTAS com Interlocked) ---------- */

int pt_spin_init(pt_spinlock_t *s)
{
    s->flag = 0;
    return 0;
}

void pt_spin_lock(pt_spinlock_t *s)
{
    for (;;) {
        /* tenta adquirir: troca atômica, devolve o valor anterior */
        if (InterlockedExchange(&s->flag, 1) == 0)
            return;
        /* espera local (só leitura) até parecer livre */
        while (s->flag)
            YieldProcessor();       /* instrução PAUSE */
    }
}

void pt_spin_unlock(pt_spinlock_t *s)
{
    InterlockedExchange(&s->flag, 0);   /* barreira completa */
}

void pt_spin_destroy(pt_spinlock_t *s)
{
    (void)s;                            /* nada a liberar */
}

/* ---------- Tempo ---------- */

uint64_t pt_now_ns(void)
{
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER c;

    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);   /* constante após o boot */
    QueryPerformanceCounter(&c);

    /* evita overflow de c.QuadPart * 1e9 */
    uint64_t sec  = (uint64_t)(c.QuadPart / freq.QuadPart);
    uint64_t rem  = (uint64_t)(c.QuadPart % freq.QuadPart);
    return sec * 1000000000ULL + (rem * 1000000000ULL) / (uint64_t)freq.QuadPart;
}

#else /* _WIN32 */

#include <pthread.h>
#include <time.h>

/* ---------- Threads ---------- */

int pt_thread_create(pt_thread_t *t, pt_thread_fn fn, void *arg) {
    return pthread_create(&t->t, NULL, fn, arg);   /* 0 = sucesso */
}

int pt_thread_join(pt_thread_t *t) {
    return pthread_join(t->t, NULL);
}

/* ---------- Mutex ---------- */

int pt_mutex_init(pt_mutex_t *m) {
    return pthread_mutex_init(&m->m, NULL);
}

void pt_mutex_lock(pt_mutex_t *m) {
    pthread_mutex_lock(&m->m);
}

void pt_mutex_unlock(pt_mutex_t *m) {
    pthread_mutex_unlock(&m->m);
}

void pt_mutex_destroy(pt_mutex_t *m) {
    pthread_mutex_destroy(&m->m);
}

/* ---------- Spinlock ---------- */

int pt_spin_init(pt_spinlock_t *s) {
    return pthread_spin_init(&s->s, PTHREAD_PROCESS_PRIVATE);
}

void pt_spin_lock(pt_spinlock_t *s) {
    pthread_spin_lock(&s->s);
}

void pt_spin_unlock(pt_spinlock_t *s) {
    pthread_spin_unlock(&s->s);
}

void pt_spin_destroy(pt_spinlock_t *s) {
    pthread_spin_destroy(&s->s);
}

/* ---------- Tempo ---------- */

uint64_t pt_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#endif /* !_WIN32 */
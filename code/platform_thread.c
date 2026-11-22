/* platform_thread.c - implementação Linux/FreeBSD (pthreads) */
#define _POSIX_C_SOURCE 200809L   /* precisa vir ANTES de qualquer #include */

#include "platform_thread.h"
#include <time.h>

#ifndef _WIN32

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
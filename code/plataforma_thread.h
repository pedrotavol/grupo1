#ifndef PLATFORM_THREAD_H
#define PLATFORM_THREAD_H

#include <stdint.h>

/* Definições específicas de cada sistema operacional */
#ifdef _WIN32

  #include <windows.h>

  typedef struct { HANDLE h; }            pt_thread_t;
  typedef struct { CRITICAL_SECTION cs; } pt_mutex_t;
  typedef struct { volatile LONG flag; }  pt_spinlock_t;

#else

  #include <pthread.h>

  typedef struct { pthread_t t; }          pt_thread_t;
  typedef struct { pthread_mutex_t m; }    pt_mutex_t;
  typedef struct { pthread_spinlock_t s; } pt_spinlock_t;

#endif

/* Tipo de função executada pela thread */
typedef void *(*pt_thread_fn)(void *);

/* Criação e encerramento de threads */
int pt_thread_create(pt_thread_t *t, pt_thread_fn fn, void *arg);
int pt_thread_join(pt_thread_t *t);

/* Controle de acesso a recursos compartilhados */
int  pt_mutex_init(pt_mutex_t *m);
void pt_mutex_lock(pt_mutex_t *m);
void pt_mutex_unlock(pt_mutex_t *m);
void pt_mutex_destroy(pt_mutex_t *m);

/* Controle de acesso com spinlock */
int  pt_spin_init(pt_spinlock_t *s);
void pt_spin_lock(pt_spinlock_t *s);
void pt_spin_unlock(pt_spinlock_t *s);
void pt_spin_destroy(pt_spinlock_t *s);

/* Retorna o tempo atual em nanossegundos */
uint64_t pt_now_ns(void);

#endif

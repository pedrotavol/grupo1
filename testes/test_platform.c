#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "platform_thread.h"   /* compile com -I apontando para a pasta do .h */

#define N_THREADS_TEST   8
#define ITERS_POR_THREAD 50000L

static int falhas = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "[FALHA] %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        falhas++; \
    } \
} while (0)

/* ---------------- [1/5] threads ---------------- */
typedef struct {
    int id;
    int marcacao;
} arg_simples_t;

static void *fn_thread_simples(void *p) {
    arg_simples_t *a = (arg_simples_t *)p;
    if (a != NULL) a->marcacao = a->id * 10 + 1;
    return NULL;
}

static void testar_threads_basicas(void) {
    printf("[1/5] Testando pt_thread_create e pt_thread_join...\n");

    pt_thread_t th[N_THREADS_TEST];
    arg_simples_t args[N_THREADS_TEST];
    int criada[N_THREADS_TEST];

    for (int i = 0; i < N_THREADS_TEST; i++) {
        args[i].id = i;
        args[i].marcacao = 0;
        criada[i] = (pt_thread_create(&th[i], fn_thread_simples, &args[i]) == 0);
        CHECK(criada[i], "pt_thread_create retornou erro");
    }

    for (int i = 0; i < N_THREADS_TEST; i++) {
        if (!criada[i]) continue;               /* nao da join em handle invalido */
        CHECK(pt_thread_join(&th[i]) == 0, "pt_thread_join retornou erro");
        CHECK(args[i].marcacao == args[i].id * 10 + 1,
              "thread nao executou trabalho esperado");
    }
}

/* ---------------- [2/5] mutex ---------------- */
typedef struct {
    pt_mutex_t *mtx;
    long *contador;
    long n_iter;
} arg_mutex_t;

static void *fn_worker_mutex(void *p) {
    arg_mutex_t *a = (arg_mutex_t *)p;
    for (long i = 0; i < a->n_iter; i++) {
        pt_mutex_lock(a->mtx);
        (*a->contador)++;
        pt_mutex_unlock(a->mtx);
    }
    return NULL;
}

static void testar_mutex(void) {
    printf("[2/5] Testando pt_mutex_init, lock, unlock e destroy...\n");

    pt_mutex_t mtx;
    int rc_init = pt_mutex_init(&mtx);
    CHECK(rc_init == 0, "pt_mutex_init retornou erro");
    if (rc_init != 0) return;

#ifdef TESTAR_TRYLOCK
    CHECK(pt_mutex_trylock(&mtx) == 0, "mutex livre: trylock deveria adquirir");
    CHECK(pt_mutex_trylock(&mtx) != 0, "mutex ocupado: trylock deveria falhar");
    pt_mutex_unlock(&mtx);
#endif

    long contador = 0;
    pt_thread_t th[N_THREADS_TEST];
    arg_mutex_t args[N_THREADS_TEST];
    int criada[N_THREADS_TEST];

    for (int i = 0; i < N_THREADS_TEST; i++) {
        args[i].mtx = &mtx;
        args[i].contador = &contador;
        args[i].n_iter = ITERS_POR_THREAD;
        criada[i] = (pt_thread_create(&th[i], fn_worker_mutex, &args[i]) == 0);
        CHECK(criada[i], "pt_thread_create falhou no teste de mutex");
    }

    for (int i = 0; i < N_THREADS_TEST; i++) {
        if (!criada[i]) continue;
        CHECK(pt_thread_join(&th[i]) == 0, "pt_thread_join falhou no teste de mutex");
    }

    long esperado = (long)N_THREADS_TEST * ITERS_POR_THREAD;
    CHECK(contador == esperado, "contador do mutex divergiu do valor esperado");

    pt_mutex_destroy(&mtx);
}

/* ---------------- [3/5] spinlock ---------------- */
typedef struct {
    pt_spinlock_t *spn;
    long *contador;
    long n_iter;
} arg_spin_t;

static void *fn_worker_spin(void *p) {
    arg_spin_t *a = (arg_spin_t *)p;
    for (long i = 0; i < a->n_iter; i++) {
        pt_spin_lock(a->spn);
        (*a->contador)++;
        pt_spin_unlock(a->spn);
    }
    return NULL;
}

static void testar_spinlock(void) {
    printf("[3/5] Testando pt_spin_init, lock, unlock e destroy...\n");

    pt_spinlock_t spn;
    int rc_init = pt_spin_init(&spn);
    CHECK(rc_init == 0, "pt_spin_init retornou erro");
    if (rc_init != 0) return;

    long contador = 0;
    pt_thread_t th[N_THREADS_TEST];
    arg_spin_t args[N_THREADS_TEST];
    int criada[N_THREADS_TEST];

    for (int i = 0; i < N_THREADS_TEST; i++) {
        args[i].spn = &spn;
        args[i].contador = &contador;
        args[i].n_iter = ITERS_POR_THREAD;
        criada[i] = (pt_thread_create(&th[i], fn_worker_spin, &args[i]) == 0);
        CHECK(criada[i], "pt_thread_create falhou no teste de spinlock");
    }

    for (int i = 0; i < N_THREADS_TEST; i++) {
        if (!criada[i]) continue;
        CHECK(pt_thread_join(&th[i]) == 0, "pt_thread_join falhou no teste de spinlock");
    }

    long esperado = (long)N_THREADS_TEST * ITERS_POR_THREAD;
    CHECK(contador == esperado, "contador do spinlock divergiu do valor esperado");

    pt_spin_destroy(&spn);
}

/* ---------------- [4/5] relogio monotônico ---------------- */
static void testar_relogio(void) {
    printf("[4/5] Testando pt_now_ns...\n");

    uint64_t t0 = pt_now_ns();
    volatile uint64_t soma = 0;
    for (uint64_t i = 0; i < 200000ULL; i++) {
        soma += i;
    }
    (void)soma;
    uint64_t t1 = pt_now_ns();

    CHECK(t1 >= t0, "pt_now_ns retrocedeu ou retornou valor inconsistente");
    CHECK((t1 - t0) > 0, "intervalo medido por pt_now_ns foi zero");
}

/* ---------------- [5/5] reutilizacao de locks ---------------- */
static void testar_reutilizacao_locks(void) {
    printf("[5/5] Testando reinicializacao de mutex e spinlock...\n");

    pt_mutex_t mtx;
    CHECK(pt_mutex_init(&mtx) == 0, "1a inicializacao de mutex falhou");
    pt_mutex_lock(&mtx);
    pt_mutex_unlock(&mtx);
    pt_mutex_destroy(&mtx);

    CHECK(pt_mutex_init(&mtx) == 0, "2a inicializacao de mutex falhou");
    pt_mutex_lock(&mtx);
    pt_mutex_unlock(&mtx);
    pt_mutex_destroy(&mtx);

    pt_spinlock_t spn;
    CHECK(pt_spin_init(&spn) == 0, "1a inicializacao de spinlock falhou");
    pt_spin_lock(&spn);
    pt_spin_unlock(&spn);
    pt_spin_destroy(&spn);

    CHECK(pt_spin_init(&spn) == 0, "2a inicializacao de spinlock falhou");
    pt_spin_lock(&spn);
    pt_spin_unlock(&spn);
    pt_spin_destroy(&spn);
}

int main(void) {
    testar_threads_basicas();
    testar_mutex();
    testar_spinlock();
    testar_relogio();
    testar_reutilizacao_locks();

    if (falhas == 0) {
        printf("\nTodos os testes passaram com sucesso!\n");
        return 0;
    }

    fprintf(stderr, "\nTotal de falhas: %d\n", falhas);
    return 1;
}

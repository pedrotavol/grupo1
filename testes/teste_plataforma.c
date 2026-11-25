/*
 * teste_plataforma.c - testa a camada de abstração (platform_thread.h/.c)
 *
 * Usa SOMENTE a API pt_* (mesmo código em Linux, Windows e FreeBSD).
 * Cada teste imprime [OK] ou [FALHA]; o programa devolve 0 se tudo passou.
 *
 *   1. teste_tempo         pt_now_ns
 *   2. teste_uma_thread    pt_thread_create / pt_thread_join
 *   3. teste_varias_threads
 *   4. teste_travamento    pt_mutex_*  (rodado com o mutex)
 *                          pt_spin_*   (rodado com o spinlock)
 *
 * Compilar (a partir da raiz do repositório):
 *   Linux:   gcc -O2 -Wall -Wextra -pthread -Icode testes/teste_plataforma.c \
 *                code/platform_thread.c -o testes/teste_plataforma
 *   Windows: cl /nologo /O2 /W4 /Icode testes\teste_plataforma.c code\platform_thread.c /Fe:testes\teste_plataforma.exe
 *
 * Se a biblioteca estiver quebrada, um teste pode travar (ex.: lock que nunca
 * solta). Linux: use  timeout 60 ./testes/teste_plataforma
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "platform_thread.h"

#define NUM_THREADS_TESTE   8
#define INCREMENTOS_POR_THREAD 20000

static int falhas = 0;

#define VERIFICAR(condicao, descricao) do { \
    if (condicao) printf("  [OK]    %s\n", descricao); \
    else { printf("  [FALHA] %s (linha %d)\n", descricao, __LINE__); falhas++; } \
} while (0)

/* Espera ocupada, usando o próprio relógio da biblioteca. */
static void esperar_ms(int ms) {
    uint64_t fim = pt_now_ns() + (uint64_t)ms * 1000000ULL;
    while (pt_now_ns() < fim) { /* gira */ }
}

/* ------------------------------------------------------------------ */
/* 1. Relógio                                                          */
/* ------------------------------------------------------------------ */

static void teste_tempo(void) {
    printf("pt_now_ns\n");

    uint64_t anterior = pt_now_ns();
    int monotono = 1;
    for (int i = 0; i < 100000; i++) {
        uint64_t agora = pt_now_ns();
        if (agora < anterior) monotono = 0;
        anterior = agora;
    }
    VERIFICAR(monotono, "o relógio nunca anda para trás");

    /* Confere a ESCALA (nanossegundos) com um relógio independente da
     * biblioteca: clock() da libc. Numa espera ocupada de 100 ms, os dois
     * devem concordar de forma grosseira (margem de 50 a 200 ms). */
    clock_t c0 = clock();
    uint64_t t0 = pt_now_ns();
    esperar_ms(100);
    uint64_t dt_ns = pt_now_ns() - t0;
    double dt_clock_ms = 1000.0 * (double)(clock() - c0) / CLOCKS_PER_SEC;
    VERIFICAR(dt_ns >= 100000000ULL, "espera de 100 ms mede pelo menos 100 ms");
    VERIFICAR(dt_clock_ms > 50.0 && dt_clock_ms < 200.0,
              "escala em ns concorda com clock() da libc");
}

/* ------------------------------------------------------------------ */
/* 2. Threads                                                          */
/* ------------------------------------------------------------------ */

static void *escrever_42(void *arg) {
    *(int *)arg = 42;
    return NULL;
}

static void teste_uma_thread(void) {
    printf("pt_thread_create / pt_thread_join (1 thread)\n");

    int valor = 0;
    pt_thread_t t;
    VERIFICAR(pt_thread_create(&t, escrever_42, &valor) == 0, "create devolve 0");
    VERIFICAR(pt_thread_join(&t) == 0, "join devolve 0");
    VERIFICAR(valor == 42, "a thread executou e recebeu o argumento");
}

static int resultado_da_thread[NUM_THREADS_TESTE];

static void *escrever_indice(void *arg) {
    int i = (int)(intptr_t)arg;
    resultado_da_thread[i] = i + 100;
    return NULL;
}

static void teste_varias_threads(void) {
    printf("pt_thread_create / pt_thread_join (%d threads)\n", NUM_THREADS_TESTE);

    pt_thread_t t[NUM_THREADS_TESTE];
    int criou_todas = 1, juntou_todas = 1;
    for (int i = 0; i < NUM_THREADS_TESTE; i++) {
        resultado_da_thread[i] = 0;
        if (pt_thread_create(&t[i], escrever_indice, (void *)(intptr_t)i) != 0) criou_todas = 0;
    }
    for (int i = 0; i < NUM_THREADS_TESTE; i++)
        if (pt_thread_join(&t[i]) != 0) juntou_todas = 0;
    VERIFICAR(criou_todas, "todos os create devolvem 0");
    VERIFICAR(juntou_todas, "todos os join devolvem 0");

    int todas_certas = 1;
    for (int i = 0; i < NUM_THREADS_TESTE; i++)
        if (resultado_da_thread[i] != i + 100) todas_certas = 0;
    VERIFICAR(todas_certas, "cada thread recebeu o seu próprio argumento");
}

/* ------------------------------------------------------------------ */
/* 3. Mutex e spinlock (mesmos testes para as duas primitivas)         */
/* ------------------------------------------------------------------ */

static pt_mutex_t    mutex;
static pt_spinlock_t spinlock;

static void travar(int usa_spin)    { if (usa_spin) pt_spin_lock(&spinlock);   else pt_mutex_lock(&mutex); }
static void destravar(int usa_spin) { if (usa_spin) pt_spin_unlock(&spinlock); else pt_mutex_unlock(&mutex); }

static volatile long contador;

/* Incremento NÃO atômico (ler, esperar um instante, gravar): sem exclusão
 * mútua, outra thread altera o contador no meio e incrementos se perdem. A
 * pausa alarga essa janela para o erro aparecer de forma confiável. */
static void *incrementar(void *arg) {
    int usa_spin = (int)(intptr_t)arg;
    for (int i = 0; i < INCREMENTOS_POR_THREAD; i++) {
        travar(usa_spin);
        long lido = contador;
        for (volatile int k = 0; k < 20; k++) { /* pausa curta */ }
        contador = lido + 1;
        destravar(usa_spin);
    }
    return NULL;
}

static volatile int outra_chegou, outra_entrou;

static void *tentar_entrar(void *arg) {
    int usa_spin = (int)(intptr_t)arg;
    outra_chegou = 1;
    travar(usa_spin);
    outra_entrou = 1;
    destravar(usa_spin);
    return NULL;
}

static void teste_travamento(int usa_spin) {
    printf(usa_spin ? "pt_spin_*  (spinlock)\n" : "pt_mutex_*  (mutex)\n");

    int ret = usa_spin ? pt_spin_init(&spinlock) : pt_mutex_init(&mutex);
    VERIFICAR(ret == 0, "init devolve 0");

    /* lock/unlock repetidos numa só thread: não pode travar nem falhar */
    for (int i = 0; i < 1000; i++) { travar(usa_spin); destravar(usa_spin); }
    VERIFICAR(1, "1000 pares lock/unlock numa thread terminaram");

    /* exclusão mútua: várias threads incrementando o mesmo contador */
    pt_thread_t t[NUM_THREADS_TESTE];
    contador = 0;
    for (int i = 0; i < NUM_THREADS_TESTE; i++)
        pt_thread_create(&t[i], incrementar, (void *)(intptr_t)usa_spin);
    for (int i = 0; i < NUM_THREADS_TESTE; i++) pt_thread_join(&t[i]);
    VERIFICAR(contador == (long)NUM_THREADS_TESTE * INCREMENTOS_POR_THREAD,
              "nenhum incremento perdido (exclusão mútua)");

    /* bloqueio: com o lock preso, outra thread não pode entrar */
    pt_thread_t outra;
    outra_chegou = 0;
    outra_entrou = 0;
    travar(usa_spin);
    pt_thread_create(&outra, tentar_entrar, (void *)(intptr_t)usa_spin);
    esperar_ms(50);
    VERIFICAR(outra_chegou == 1, "a outra thread chegou até o lock");
    VERIFICAR(outra_entrou == 0, "com o lock preso, a outra thread NÃO entra");
    destravar(usa_spin);
    pt_thread_join(&outra);
    VERIFICAR(outra_entrou == 1, "ao destravar, a outra thread entra");

    if (usa_spin) pt_spin_destroy(&spinlock); else pt_mutex_destroy(&mutex);
}

/* ------------------------------------------------------------------ */

int main(void) {
    teste_tempo();
    teste_uma_thread();
    teste_varias_threads();
    teste_travamento(0);   /* mutex    */
    teste_travamento(1);   /* spinlock */

    printf("\n%s (%d falha%s)\n", falhas ? "TESTES FALHARAM" : "TODOS OS TESTES PASSARAM",
           falhas, falhas == 1 ? "" : "s");
    return falhas ? 1 : 0;
}

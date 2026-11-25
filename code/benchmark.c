#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "platform_thread.h"

#define MAX_THREADS 16
#define CACHE_LINE  64

#if defined(_WIN32)
  #define PLAT_PADRAO "windows"
#elif defined(__linux__)
  #define PLAT_PADRAO "linux"
#elif defined(__FreeBSD__)
  #define PLAT_PADRAO "freebsd"
#else
  #define PLAT_PADRAO "desconhecida"
#endif

/* ---------------- Estado compartilhado ----------------
- Lock, contador e demais campos em linhas de cache distintas para evitar
- false sharing entre eles. */
typedef struct {
    pt_mutex_t    mtx;
    char          pad0[CACHE_LINE];
    pt_spinlock_t spn;
    char          pad1[CACHE_LINE];
    volatile long contador;
    char          pad2[CACHE_LINE];
} shared_t;

typedef struct {
    shared_t *sh;
    int       use_spin;
    long      n_iter;          /* aquisições desta thread */
    uint64_t  trabalho_iters;  /* iterações calibradas na seção crítica (0 = só incrementa) */
    char      pad[CACHE_LINE];
} worker_args_t;

/* Executa um laço de CPU sem chamar pt_now_ns() a cada aquisição. */
static void burn_iters(uint64_t iters) {
    volatile uint64_t acc = 0;
    for (uint64_t i = 0; i < iters; i++) {
        acc += (i & 1u) + 1u;
    }
    (void)acc;
}

/* Estima quantas iterações de burn_iters correspondem a 1 ns na máquina atual. */
static double calibrar_iters_por_ns(void) {
    const uint64_t amostra_base = 50000ULL;

    /* Pequeno aquecimento antes de cronometrar a calibração */
    burn_iters(amostra_base);

    uint64_t iters = amostra_base;
    uint64_t dt_ns = 0;

    /* Garante uma janela de medição grande o suficiente para reduzir ruído do timer */
    for (int tentativa = 0; tentativa < 8; tentativa++) {
        uint64_t t0 = pt_now_ns();
        burn_iters(iters);
        uint64_t t1 = pt_now_ns();
        dt_ns = (t1 > t0) ? (t1 - t0) : 0;
        if (dt_ns >= 5000000ULL) { /* >= 5 ms */
            break;
        }
        iters *= 2ULL;
    }

    if (dt_ns == 0) {
        return 1.0; /* fallback seguro caso o relógio tenha resolução grossa */
    }
    return (double)iters / (double)dt_ns;
}

static uint64_t ns_para_iters(uint64_t ns, double iters_por_ns) {
    if (ns == 0) return 0;
    double raw = (double)ns * iters_por_ns;
    if (raw < 1.0) return 1ULL;
    return (uint64_t)raw;
}

static void *worker(void *p) {
    worker_args_t *a = (worker_args_t *)p;
    shared_t *sh = a->sh;
    if (a->use_spin) {
        for (long i = 0; i < a->n_iter; i++) {
            pt_spin_lock(&sh->spn);
            sh->contador++;
            if (a->trabalho_iters) burn_iters(a->trabalho_iters);
            pt_spin_unlock(&sh->spn);
        }
    } else {
        for (long i = 0; i < a->n_iter; i++) {
            pt_mutex_lock(&sh->mtx);
            sh->contador++;
            if (a->trabalho_iters) burn_iters(a->trabalho_iters);
            pt_mutex_unlock(&sh->mtx);
        }
    }
    return NULL;
}

/* ---------------- Configurações ---------------- */
typedef struct { const char *nome; uint64_t ns; } secao_t;

static const secao_t SECOES_BASE[] = {
    { "curta", 0 },        /* só incrementa o contador */
    { "media", 1000 },     /* ~1 us   */
    { "longa", 100000 },   /* ~100 us */
};
static const secao_t SECOES_EXTRA[] = {
    { "10us", 10000 },
    { "30us", 30000 },
};
static const int THREADS[] = { 1, 2, 4, 8, 16 };
#define N_THREADS_CFG (int)(sizeof THREADS / sizeof THREADS[0])
#define MAX_SECOES 8

typedef struct {
    int       nthreads;
    int       use_spin;
    secao_t   secao;
    long      total;           /* total de aquisições (fixo p/ a seção) */
    uint64_t  trabalho_iters;  /* iterações calibradas p/ a seção */
} config_t;

/* Total fixo por seção: ~0.4 s de trabalho serializado, limitado a 2M.
- É o MESMO para todas as threads e primitivas de uma mesma seção, então
- as vazões são comparáveis entre si. */
static long total_para(uint64_t ns, int quick) {
    double t = 0.4e9 / ((double)ns + 50.0);
    if (t > 2000000.0) t = 2000000.0;
    if (t < 2000.0)    t = 2000.0;
    if (quick) { t /= 20.0; if (t < 200.0) t = 200.0; }
    return (long)t;
}

/* Executa UMA repetição. Devolve tempo em segundos (ou <0 em erro). */
static double run_once(const config_t *c) {
    shared_t *sh = (shared_t *)calloc(1, sizeof(shared_t) + CACHE_LINE);
    if (!sh) return -1.0;
    /* alinha em linha de cache */
    uintptr_t raw = (uintptr_t)sh;
    shared_t *al = (shared_t *)((raw + CACHE_LINE - 1) & ~(uintptr_t)(CACHE_LINE - 1));

    pt_mutex_init(&al->mtx);
    pt_spin_init(&al->spn);
    al->contador = 0;

    worker_args_t args[MAX_THREADS];
    pt_thread_t   th[MAX_THREADS];
    long base = c->total / c->nthreads;
    long resto = c->total % c->nthreads;
    for (int i = 0; i < c->nthreads; i++) {
        args[i].sh = al;
        args[i].use_spin = c->use_spin;
        args[i].n_iter = base + (i < resto ? 1 : 0);
        args[i].trabalho_iters = c->trabalho_iters;
    }

    uint64_t t0 = pt_now_ns();
    for (int i = 0; i < c->nthreads; i++) {
        if (pt_thread_create(&th[i], worker, &args[i]) != 0) {
            fprintf(stderr, "erro: pt_thread_create falhou\n");
            exit(3);
        }
    }
    for (int i = 0; i < c->nthreads; i++) pt_thread_join(&th[i]);
    uint64_t t1 = pt_now_ns();

    long final = al->contador;
    pt_mutex_destroy(&al->mtx);
    pt_spin_destroy(&al->spn);
    free(sh);

    if (final != c->total) {
        fprintf(stderr, "ERRO DE CORRECAO: contador=%ld esperado=%ld (%s, %d thr, %s)\n",
                final, c->total, c->use_spin ? "spinlock" : "mutex",
                c->nthreads, c->secao.nome);
        exit(2);
    }
    return (double)(t1 - t0) / 1e9;
}

int main(int argc, char **argv) {
    const char *out_path = "resultados.csv";
    const char *plataforma = PLAT_PADRAO;
    int reps = 30, quick = 0, extra = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc)               out_path = argv[++i];
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc)         reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--plataforma") && i + 1 < argc) plataforma = argv[++i];
        else if (!strcmp(argv[i], "--quick"))                        quick = 1;
        else if (!strcmp(argv[i], "--extra"))                        extra = 1;
        else { fprintf(stderr, "argumento desconhecido: %s\n", argv[i]); return 1; }
    }
    if (quick && reps == 30) reps = 3;
    if (reps < 1) reps = 1;
    if (!quick && reps < 30)
        fprintf(stderr, "AVISO: menos de 30 repetições exige justificativa no relatório.\n");

    /* monta a lista de configurações */
    secao_t secoes[MAX_SECOES];
    int nsec = 0;
    for (size_t i = 0; i < sizeof SECOES_BASE / sizeof SECOES_BASE[0]; i++) secoes[nsec++] = SECOES_BASE[i];
    if (extra)
        for (size_t i = 0; i < sizeof SECOES_EXTRA / sizeof SECOES_EXTRA[0]; i++) secoes[nsec++] = SECOES_EXTRA[i];

    double iters_por_ns = calibrar_iters_por_ns();

    int ncfg = nsec * N_THREADS_CFG * 2;
    config_t *cfg = (config_t *)malloc(sizeof(config_t) * (size_t)ncfg);
    if (!cfg) { perror("malloc"); return 1; }

    int k = 0;
    for (int s = 0; s < nsec; s++) {
        uint64_t iters_secao = ns_para_iters(secoes[s].ns, iters_por_ns);
        for (int t = 0; t < N_THREADS_CFG; t++) {
            for (int p = 0; p < 2; p++) {
                cfg[k].nthreads = THREADS[t];
                cfg[k].use_spin = p;
                cfg[k].secao = secoes[s];
                cfg[k].total = total_para(secoes[s].ns, quick);
                cfg[k].trabalho_iters = iters_secao;
                k++;
            }
        }
    }

    FILE *f = fopen(out_path, "w");
    if (!f) { perror("fopen"); free(cfg); return 1; }
    fprintf(f, "plataforma,primitiva,threads,secao,secao_ns,total_aquisicoes,repeticao,tempo_s,vazao_aq_s\n");
    fflush(f);

    fprintf(stderr, "plataforma=%s configs=%d reps=%d iters/ns=%.4f%s -> %s\n",
            plataforma, ncfg, reps, iters_por_ns, quick ? " (QUICK)" : "", out_path);

    /* aquecimento (descartado) */
    for (int i = 0; i < ncfg; i++) run_once(&cfg[i]);
    fprintf(stderr, "aquecimento ok\n");

    /* repetições intercaladas */
    for (int r = 1; r <= reps; r++) {
        for (int i = 0; i < ncfg; i++) {
            double tempo = run_once(&cfg[i]);
            if (tempo <= 0) { fprintf(stderr, "erro na execução\n"); fclose(f); free(cfg); return 3; }
            fprintf(f, "%s,%s,%d,%s,%llu,%ld,%d,%.9f,%.2f\n",
                    plataforma, cfg[i].use_spin ? "spinlock" : "mutex",
                    cfg[i].nthreads, cfg[i].secao.nome,
                    (unsigned long long)cfg[i].secao.ns, cfg[i].total, r,
                    tempo, (double)cfg[i].total / tempo);
            fflush(f);
        }
        fprintf(stderr, "repeticao %d/%d concluida\n", r, reps);
    }
    fclose(f);
    free(cfg);
    return 0;
}
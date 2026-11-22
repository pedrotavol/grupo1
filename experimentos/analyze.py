#!/usr/bin/env python3
"""Análise do benchmark Mutex x Spinlock.

Uso:  python3 analyze.py dados/linux_resultados.csv [dados/windows_resultados.csv ...]
                         [--out experimentos]

Lê os CSVs brutos (sem alterá-los) e gera:
  - resumo.csv                 média e desvio padrão por configuração
  - grafico1_vazao_<plat>.png  vazão x nº de threads (uma curva por primitiva,
                               um painel por duração da seção crítica)
  - grafico2_cruzamento_<plat>.png  razão spinlock/mutex x duração da seção
                               (cruzamento em 1.0 = ponto de inversão)
  - cruzamentos.txt            ponto de inversão estimado, por plataforma/threads
"""
import sys, os
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

args = sys.argv[1:]
out = "experimentos"
if "--out" in args:
    i = args.index("--out"); out = args[i + 1]; del args[i:i + 2]
if not args:
    sys.exit(__doc__)
os.makedirs(out, exist_ok=True)

df = pd.concat([pd.read_csv(p) for p in args], ignore_index=True)

# ---------- resumo ----------
keys = ["plataforma", "primitiva", "secao", "secao_ns", "threads"]
g = df.groupby(keys)
res = g.agg(n=("repeticao", "count"),
            tempo_medio_s=("tempo_s", "mean"), tempo_dp_s=("tempo_s", lambda x: x.std(ddof=1)),
            vazao_media=("vazao_aq_s", "mean"), vazao_dp=("vazao_aq_s", lambda x: x.std(ddof=1)),
            ).reset_index().sort_values(keys)
res["cv_pct"] = 100 * res["vazao_dp"] / res["vazao_media"]
res.to_csv(os.path.join(out, "resumo.csv"), index=False, float_format="%.6g")

COL = {"mutex": "#1f77b4", "spinlock": "#d95f02"}
secoes = (df[["secao", "secao_ns"]].drop_duplicates().sort_values("secao_ns"))

report = []
for plat, dp in res.groupby("plataforma"):
    # ---- Gráfico 1: vazão x threads ----
    ns = len(secoes)
    fig, axs = plt.subplots(1, ns, figsize=(4.2 * ns, 3.8), squeeze=False)
    for ax, (_, srow) in zip(axs[0], secoes.iterrows()):
        d = dp[dp.secao == srow.secao]
        for prim, dd in d.groupby("primitiva"):
            dd = dd.sort_values("threads")
            ax.errorbar(dd.threads, dd.vazao_media, yerr=dd.vazao_dp, marker="o",
                        capsize=3, color=COL[prim], label=prim)
        ax.set_xscale("log", base=2)
        ax.set_xticks(sorted(dp.threads.unique())); ax.set_xticklabels(sorted(dp.threads.unique()))
        ax.set_yscale("log")
        ax.set_title(f"seção {srow.secao}")
        ax.set_xlabel("nº de threads"); ax.grid(alpha=.3)
    axs[0][0].set_ylabel("vazão (aquisições/s)")
    axs[0][0].legend()
    fig.suptitle(f"Vazão x threads — {plat} (média ± desvio padrão)")
    fig.tight_layout()
    fig.savefig(os.path.join(out, f"grafico1_vazao_{plat}.png"), dpi=150)
    plt.close(fig)

    # ---- Gráfico 2: razão spin/mutex x duração da seção ----
    piv = dp.pivot_table(index=["threads", "secao_ns"], columns="primitiva", values="vazao_media")
    piv["razao"] = piv["spinlock"] / piv["mutex"]
    fig, ax = plt.subplots(figsize=(6.2, 4.2))
    for thr, dd in piv.reset_index().groupby("threads"):
        dd = dd.sort_values("secao_ns")
        x = dd.secao_ns.replace(0, 100) / 1000.0     # "curta" (0 ns) plotada em 0.1 us
        ax.plot(x, dd.razao, marker="o", label=f"{thr} thr")
        # cruzamento por interpolação log-log entre pontos consecutivos
        r = dd.razao.values; xs = x.values
        for j in range(len(r) - 1):
            if (r[j] - 1) * (r[j + 1] - 1) < 0:
                lx = np.log(xs[j]) + (np.log(xs[j + 1]) - np.log(xs[j])) * \
                     (0 - np.log(r[j])) / (np.log(r[j + 1]) - np.log(r[j]))
                report.append(f"{plat}: {thr} threads -> inversão ~ {np.exp(lx):.2f} us de seção crítica")
    ax.axhline(1, color="k", ls="--", lw=1)
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("duração da seção crítica (µs; 'curta' plotada em 0,1)")
    ax.set_ylabel("vazão spinlock / vazão mutex")
    ax.set_title(f"Ponto de cruzamento — {plat}\n(>1: spinlock melhor; <1: mutex melhor)")
    ax.grid(alpha=.3); ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out, f"grafico2_cruzamento_{plat}.png"), dpi=150)
    plt.close(fig)

    # quem vence em cada ponto
    for (thr, sns), row in piv.iterrows():
        v = "spinlock" if row.razao > 1 else "mutex"
        report.append(f"{plat}: {thr:>2} thr, seção {sns/1000:g} us -> razão spin/mutex = {row.razao:.2f} ({v} vence)")

with open(os.path.join(out, "cruzamentos.txt"), "w") as fh:
    fh.write("\n".join(report) + "\n")
print(f"{len(df)} linhas lidas; resultados em {out}/")
print("\n".join(report))

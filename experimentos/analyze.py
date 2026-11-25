#!/usr/bin/env python3
"""Análise do benchmark Mutex x Spinlock.

Uso:  python3 analyze.py dados/linux_resultados.csv [dados/windows_resultados.csv ...]
                         [--out experimentos]

Lê os CSVs brutos (sem alterá-los) e gera:
- resumo.csv                      média e desvio padrão por configuração
- grafico1_vazao_<plat>.png       vazão x nº de threads (uma curva por primitiva,
                                    um painel por duração da seção crítica)
- grafico2_cruzamento_<plat>.png  razão spinlock/mutex x duração da seção
                                    (linha 1.0 = ponto de inversão)
- cruzamentos.txt                 inversões estimadas e razão por configuração
"""
import os
import sys
import numpy as np
import pandas as pd
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, NullLocator

COLUNAS_OBRIGATORIAS = {
    "plataforma", "primitiva", "threads", "secao",
    "secao_ns", "total_aquisicoes", "repeticao", "tempo_s", "vazao_aq_s"
}

COR_PRIMITIVA = {"mutex": "#1f77b4", "spinlock": "#d95f02"}
PALETA_THREADS = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b"]


def aplicar_estilo_base():
    plt.rcParams.update({
        "font.size": 10,
        "axes.titlesize": 11,
        "axes.labelsize": 10,
        "xtick.labelsize": 9.5,
        "ytick.labelsize": 9.5,
        "legend.fontsize": 8.5,
        "axes.edgecolor": "#444444",
        "axes.linewidth": 0.8,
    })


def limpar_bordas(ax):
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)


def formatar_vazao_tick(val, _pos):
    if val <= 0:
        return ""
    if val >= 1_000_000:
        m = val / 1_000_000.0
        return f"{m:.1f}M" if m % 1 != 0 else f"{int(round(m))}M"
    if val >= 1_000:
        k = val / 1_000.0
        return f"{k:.1f}k" if k % 1 != 0 else f"{int(round(k))}k"
    return f"{val:g}"


def ticks_log_vazao(v_min, v_max):
    lo = max(1.0, float(v_min) * 0.85)
    hi = float(v_max) * 1.15
    exp_min = int(np.floor(np.log10(lo)))
    exp_max = int(np.ceil(np.log10(hi)))
    ticks = []
    for exp in range(exp_min, exp_max + 1):
        base = 10.0 ** exp
        for mult in (1.0, 1.5, 2.0, 3.0, 5.0, 7.5):
            val = mult * base
            if lo <= val <= hi:
                ticks.append(val)
    if not ticks:
        ticks = [float(v_min), float(v_max)]
    return ticks


def formatar_razao_tick(val, _pos):
    if val <= 0:
        return ""
    if abs(val - round(val)) < 1e-6 and val >= 1:
        return f"{int(round(val))}.0" if val < 10 else f"{int(round(val))}"
    return f"{val:g}"


def parse_args(argv):
    out_dir = "experimentos"
    csv_paths = []
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--out":
            if i + 1 >= len(argv):
                sys.exit("Erro: --out exige um caminho de diretório.")
            out_dir = argv[i + 1]
            i += 2
        elif arg.startswith("--"):
            sys.exit(f"Erro: opção desconhecida '{arg}'.")
        else:
            csv_paths.append(arg)
            i += 1
    return csv_paths, out_dir


def formatar_rotulo_secao(secao_nome, secao_ns):
    if secao_ns == 0:
        return f"{secao_nome}\n(0 ns)"
    if secao_ns % 1000 == 0:
        return f"{secao_nome}\n({secao_ns // 1000} µs)"
    return f"{secao_nome}\n({secao_ns / 1000.0:g} µs)"


def formatar_plat(plat):
    s = str(plat).strip()
    return s.capitalize() if s.islower() else s


def main():
    aplicar_estilo_base()
    csv_paths, out_dir = parse_args(sys.argv[1:])
    if not csv_paths:
        sys.exit(__doc__)

    for path in csv_paths:
        if not os.path.isfile(path):
            sys.exit(f"Erro: arquivo CSV não encontrado: {path}")

    os.makedirs(out_dir, exist_ok=True)

    frames = []
    for path in csv_paths:
        df_part = pd.read_csv(path)
        faltando = COLUNAS_OBRIGATORIAS - set(df_part.columns)
        if faltando:
            sys.exit(f"Erro: {path} está sem as colunas: {sorted(faltando)}")
        frames.append(df_part)

    df = pd.concat(frames, ignore_index=True)

    # ---------- Resumo estatístico (dados brutos preservados) ----------
    keys = ["plataforma", "primitiva", "secao", "secao_ns", "threads"]
    res = (
        df.groupby(keys, as_index=False)
        .agg(
            n=("repeticao", "count"),
            tempo_medio_s=("tempo_s", "mean"),
            tempo_dp_s=("tempo_s", lambda x: x.std(ddof=1)),
            vazao_media=("vazao_aq_s", "mean"),
            vazao_dp=("vazao_aq_s", lambda x: x.std(ddof=1)),
        )
        .sort_values(["plataforma", "secao_ns", "threads", "primitiva"])
    )
    res["cv_pct"] = np.where(
        res["vazao_media"] > 0,
        100.0 * res["vazao_dp"] / res["vazao_media"],
        np.nan,
    )
    res.to_csv(os.path.join(out_dir, "resumo.csv"), index=False, float_format="%.6g")

    secoes = (
        df[["secao", "secao_ns"]]
        .drop_duplicates()
        .sort_values(["secao_ns", "secao"])
        .reset_index(drop=True)
    )
    rotulos_secao = [
        formatar_rotulo_secao(r.secao, r.secao_ns) for _, r in secoes.iterrows()
    ]

    inversoes = []
    detalhes = []

    for plat, dp in res.groupby("plataforma"):
        plat_label = formatar_plat(plat)
        threads_sorted = sorted(dp["threads"].unique())

        # ---- Gráfico 1: Vazão x Número de Threads ----
        n_sec = len(secoes)
        fig, axs = plt.subplots(
            1, n_sec, figsize=(5.0 * n_sec, 4.5), squeeze=False, constrained_layout=True
        )

        handles_prim = []
        labels_prim = []

        for idx, (ax, (_, srow)) in enumerate(zip(axs[0], secoes.iterrows())):
            d_sec = dp[dp["secao"] == srow["secao"]]
            v_low = []
            v_high = []
            for prim, dd in d_sec.groupby("primitiva"):
                dd = dd.sort_values("threads")
                cor = COR_PRIMITIVA.get(prim, None)
                eb = ax.errorbar(
                    dd["threads"],
                    dd["vazao_media"],
                    yerr=dd["vazao_dp"],
                    marker="o",
                    markersize=5,
                    linewidth=1.7,
                    capsize=3,
                    color=cor,
                    label=prim,
                )
                if prim not in labels_prim:
                    handles_prim.append(eb[0])
                    labels_prim.append(prim)

                v_low.append(np.min(dd["vazao_media"] - dd["vazao_dp"]))
                v_high.append(np.max(dd["vazao_media"] + dd["vazao_dp"]))

            limpar_bordas(ax)
            ax.set_xscale("log", base=2)
            ax.xaxis.set_minor_locator(NullLocator())
            ax.set_xticks(threads_sorted)
            ax.set_xticklabels([str(t) for t in threads_sorted])

            ax.set_yscale("log")
            ax.yaxis.set_minor_locator(NullLocator())
            ax.yaxis.set_major_formatter(FuncFormatter(formatar_vazao_tick))
            if v_low and v_high:
                ax.set_yticks(ticks_log_vazao(min(v_low), max(v_high)))

            ax.set_title(formatar_rotulo_secao(srow["secao"], srow["secao_ns"]).replace("\n", " "))
            ax.set_xlabel("Nº de threads")
            if idx == 0:
                ax.set_ylabel("Vazão (aquisições/s)")
            ax.grid(True, which="major", linestyle="--", linewidth=0.6, alpha=0.35)

        if handles_prim:
            fig.legend(
                handles_prim,
                labels_prim,
                loc="upper center",
                bbox_to_anchor=(0.5, 0.93),
                ncol=len(handles_prim),
                frameon=True,
                facecolor="white",
                edgecolor="#dddddd",
            )

        fig.suptitle(f"Vazão vs. Threads — {plat_label} (média ± dp)", fontweight="bold")
        fig.savefig(os.path.join(out_dir, f"grafico1_vazao_{plat}.png"), dpi=300)
        plt.close(fig)

        # ---- Gráfico 2: Razão spinlock/mutex x Duração da seção crítica ----
        piv = dp.pivot_table(
            index=["threads", "secao_ns", "secao"],
            columns="primitiva",
            values="vazao_media",
        ).reset_index()

        if "spinlock" in piv.columns and "mutex" in piv.columns:
            piv["razao"] = piv["spinlock"] / piv["mutex"]
            sec_ordem = {row["secao_ns"]: idx for idx, row in secoes.iterrows()}
            piv["pos_x"] = piv["secao_ns"].map(sec_ordem)

            fig, ax = plt.subplots(figsize=(7.2, 4.8), constrained_layout=True)

            for idx_t, (thr, dd) in enumerate(piv.groupby("threads")):
                dd = dd.sort_values("pos_x")
                xs = dd["pos_x"].to_numpy(dtype=float)
                rs = dd["razao"].to_numpy(dtype=float)
                ns_vals = dd["secao_ns"].to_numpy(dtype=float)
                cor_t = PALETA_THREADS[idx_t % len(PALETA_THREADS)]

                ax.plot(
                    xs,
                    rs,
                    marker="o",
                    markersize=5.5,
                    linewidth=1.7,
                    color=cor_t,
                    label=f"{thr} thr",
                )

                for j in range(len(rs) - 1):
                    if rs[j] > 0 and rs[j + 1] > 0 and (rs[j] - 1.0) * (rs[j + 1] - 1.0) < 0:
                        if ns_vals[j] > 0 and ns_vals[j + 1] > 0:
                            frac = (0.0 - np.log(rs[j])) / (np.log(rs[j + 1]) - np.log(rs[j]))
                            ns_est = np.exp(
                                np.log(ns_vals[j]) + frac * (np.log(ns_vals[j + 1]) - np.log(ns_vals[j]))
                            )
                            inversoes.append(
                                f"{plat}: {thr} threads -> cruzamento estimado ~ {ns_est / 1000.0:.2f} µs "
                                f"(entre {ns_vals[j] / 1000.0:g} µs e {ns_vals[j + 1] / 1000.0:g} µs)"
                            )
                        else:
                            inversoes.append(
                                f"{plat}: {thr} threads -> cruzamento entre seção curta (0 ns) e {ns_vals[j + 1] / 1000.0:g} µs"
                            )

            limpar_bordas(ax)
            ax.axhline(1.0, color="#222222", linestyle="--", linewidth=1.2, label="limiar 1.0")
            ax.set_yscale("log")
            ax.yaxis.set_minor_locator(NullLocator())
            ax.yaxis.set_major_formatter(FuncFormatter(formatar_razao_tick))

            r_min = max(0.1, float(piv["razao"].min()) * 0.85)
            r_max = float(piv["razao"].max()) * 1.15
            candidatos = [0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0]
            ticks_y = [v for v in candidatos if r_min <= v <= r_max]
            if 1.0 not in ticks_y:
                ticks_y.append(1.0)
                ticks_y = sorted(ticks_y)
            if ticks_y:
                ax.set_yticks(ticks_y)

            ax.set_xticks(range(len(rotulos_secao)))
            ax.set_xticklabels(rotulos_secao)
            ax.set_xlabel("Duração da seção crítica")
            ax.set_ylabel("Razão de vazão (spinlock / mutex)")
            ax.set_title(
                f"Spinlock vs. Mutex — {plat_label}\n"
                "(> 1.0: spinlock maior vazão; < 1.0: mutex maior vazão)",
                fontweight="bold",
            )
            ax.grid(True, which="major", linestyle="--", linewidth=0.6, alpha=0.35)
            ax.legend(
                loc="upper center",
                bbox_to_anchor=(0.5, -0.18),
                ncol=3,
                frameon=True,
                facecolor="white",
                edgecolor="#dddddd",
            )
            fig.savefig(os.path.join(out_dir, f"grafico2_cruzamento_{plat}.png"), dpi=300)
            plt.close(fig)

            piv_sorted = piv.sort_values(["threads", "pos_x"])
            for _, r in piv_sorted.iterrows():
                vencedor = "spinlock" if r["razao"] > 1.0 else "mutex"
                detalhes.append(
                    f"{plat}: {int(r['threads']):>2} thr, seção {r['secao']} ({r['secao_ns'] / 1000.0:g} µs) "
                    f"-> spin/mutex = {r['razao']:.2f} ({vencedor})"
                )

    linhas_txt = ["=== CRUZAMENTOS ESTIMADOS ==="]
    linhas_txt.extend(inversoes if inversoes else ["Nenhum cruzamento detectado entre pontos consecutivos."])
    linhas_txt.append("")
    linhas_txt.append("=== DETALHE POR CONFIGURAÇÃO ===")
    linhas_txt.extend(detalhes)

    txt_path = os.path.join(out_dir, "cruzamentos.txt")
    with open(txt_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(linhas_txt) + "\n")

    print(f"{len(df)} linhas brutas processadas; resultados salvos em {out_dir}/")
    print("\n".join(linhas_txt[:15]))


if __name__ == "__main__":
    main()
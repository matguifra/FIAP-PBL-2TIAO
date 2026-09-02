"""
CardioIA - Fase 1 (Batimentos de Dados)
Gera um conjunto balanceado de imagens de ECG de 12 derivacoes a partir do
PTB-XL (PhysioNet, open access, CC BY 4.0).

Fonte: https://physionet.org/content/ptb-xl/1.0.3/
Saida: data/ecg_images/*.jpg + data/ecg_images/manifest.csv

Uso: .venv/bin/python scripts/gerar_ecg_imagens.py [--por-classe 24]
"""

import argparse
import ast
import io
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import wfdb

BASE = "https://physionet.org/files/ptb-xl/1.0.3/"
RAIZ = Path(__file__).resolve().parent.parent
DIR_DADOS = RAIZ / "data"
DIR_IMGS = DIR_DADOS / "ecg_images"
DIR_WFDB = DIR_DADOS / "wfdb_cache"

# ordem das derivacoes no PTB-XL
DERIVACOES = ["I", "II", "III", "aVR", "aVL", "aVF", "V1", "V2", "V3", "V4", "V5", "V6"]
# layout classico 3x4 + tira de ritmo (DII longo)
COLUNAS = [["I", "II", "III"], ["aVR", "aVL", "aVF"], ["V1", "V2", "V3"], ["V4", "V5", "V6"]]

CLASSES = ["NORM", "MI", "STTC", "CD", "HYP"]
SEXO = {0: "M", 1: "F"}


def carregar_metadados():
    db = pd.read_csv(DIR_DADOS / "ptbxl_database.csv", index_col="ecg_id")
    scp = pd.read_csv(DIR_DADOS / "scp_statements.csv", index_col=0)
    scp = scp[scp.diagnostic == 1]
    mapa = scp.diagnostic_class.to_dict()

    def superclasses(codigos):
        d = ast.literal_eval(codigos)
        return sorted({mapa[c] for c in d if c in mapa})

    db["superclasses"] = db.scp_codes.apply(superclasses)
    return db


def selecionar(db, por_classe):
    """Amostra deterministica, rotulo unico, laudo conferido por humano, sem marca-passo."""
    ok = db[
        (db.superclasses.apply(len) == 1)
        & (db.validated_by_human)
        & (db.pacemaker.isna())
        & (db.age.between(18, 95))
        & (db.sex.isin([0, 1]))
    ].copy()
    ok["classe"] = ok.superclasses.str[0]

    partes = []
    for classe in CLASSES:
        sub = ok[ok.classe == classe]
        # balanceia sexo dentro de cada classe (mitigacao de vies)
        for sexo in (0, 1):
            s = sub[sub.sex == sexo].sort_index()
            n = por_classe // 2
            if len(s) < n:
                raise SystemExit(f"amostra insuficiente: {classe}/{SEXO[sexo]} tem {len(s)}")
            partes.append(s.sample(n=n, random_state=42))
    return pd.concat(partes).sort_index()


def baixar(nome_arquivo):
    """Baixa .hea/.dat do PhysioNet para o cache local."""
    for ext in (".hea", ".dat"):
        destino = DIR_WFDB / (nome_arquivo + ext)
        if destino.exists() and destino.stat().st_size > 0:
            continue
        destino.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(BASE + nome_arquivo + ext, timeout=120) as r:
            destino.write_bytes(r.read())


def desenhar_grade(ax, largura_mm, altura_mm):
    """Papel de ECG: 1 mm fino, 5 mm grosso, 25 mm/s e 10 mm/mV."""
    for passo, cor, lw in ((1, "#f2b8b5", 0.4), (5, "#e07a75", 0.8)):
        ax.set_xticks(np.arange(0, largura_mm + passo, passo), minor=(passo == 1))
        ax.set_yticks(np.arange(0, altura_mm + passo, passo), minor=(passo == 1))
        ax.grid(which="minor" if passo == 1 else "major", color=cor, lw=lw)
    ax.set_xlim(0, largura_mm)
    ax.set_ylim(0, altura_mm)
    ax.set_xticklabels([])
    ax.set_yticklabels([])
    ax.tick_params(length=0)
    for lado in ax.spines.values():
        lado.set_visible(False)


def plotar_ecg(sinal, fs, ecg_id, destino):
    """sinal: (n_amostras, 12) em mV. Salva JPG com layout 3x4 + tira de ritmo."""
    mm_s, mm_mv = 25.0, 10.0
    dur = sinal.shape[0] / fs
    largura_mm, altura_mm = dur * mm_s, 160.0
    bases = [140.0, 100.0, 60.0, 20.0]  # 3 linhas + tira de ritmo

    fig, ax = plt.subplots(figsize=(largura_mm / 25.4, altura_mm / 25.4), dpi=150)
    fig.subplots_adjust(0, 0, 1, 1)
    desenhar_grade(ax, largura_mm, altura_mm)

    t = np.arange(sinal.shape[0]) / fs
    seg = dur / len(COLUNAS)
    for c, coluna in enumerate(COLUNAS):
        m = (t >= c * seg) & (t < (c + 1) * seg)
        for r, nome in enumerate(coluna):
            y = sinal[m, DERIVACOES.index(nome)]
            ax.plot(t[m] * mm_s, bases[r] + y * mm_mv, color="#111", lw=0.8)
            ax.text(c * seg * mm_s + 4, bases[r] + 12, nome, fontsize=7, weight="bold", color="#111")
        if c:  # separador vertical entre colunas
            ax.plot([c * seg * mm_s] * 2, [bases[2] - 12, bases[0] + 10], color="#111", lw=0.5)

    ritmo = sinal[:, DERIVACOES.index("II")]
    ax.plot(t * mm_s, bases[3] + ritmo * mm_mv, color="#111", lw=0.8)
    ax.text(2, bases[3] + 12, "II", fontsize=7, weight="bold", color="#111")
    # rotulo tecnico apenas: diagnostico NUNCA vai para o pixel (evita vazamento de rotulo)
    ax.text(largura_mm - 2, 3, f"PTB-XL ecg_id {ecg_id} | 25 mm/s | 10 mm/mV | {fs} Hz",
            fontsize=6, color="#333", ha="right")

    buf = io.BytesIO()
    fig.savefig(buf, format="jpg", pil_kwargs={"quality": 80}, facecolor="white")
    plt.close(fig)
    destino.write_bytes(buf.getvalue())


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--por-classe", type=int, default=24, help="imagens por superclasse (par)")
    args = p.parse_args()
    assert args.por_classe % 2 == 0, "--por-classe deve ser par (balanceio por sexo)"

    DIR_IMGS.mkdir(parents=True, exist_ok=True)
    db = carregar_metadados()
    amostra = selecionar(db, args.por_classe)
    print(f"selecionados {len(amostra)} registros")

    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(baixar, amostra.filename_hr))

    linhas = []
    for ecg_id, linha in amostra.iterrows():
        registro = wfdb.rdrecord(str(DIR_WFDB / linha.filename_hr))
        nome = f"{linha.classe}_{ecg_id:05d}.jpg"
        plotar_ecg(registro.p_signal, int(registro.fs), ecg_id, DIR_IMGS / nome)
        linhas.append({
            "arquivo": nome, "ecg_id": ecg_id, "classe": linha.classe,
            "sexo": SEXO[int(linha.sex)], "idade": int(linha.age),
            "dispositivo": linha.device, "ano": str(linha.recording_date)[:4],
            # registro WFDB = par de arquivos; sem a extensao a URL nao existe
            "fonte_hea": f"{BASE}{linha.filename_hr}.hea",
            "fonte_dat": f"{BASE}{linha.filename_hr}.dat",
        })

    man = pd.DataFrame(linhas)
    man.to_csv(DIR_IMGS / "manifest.csv", index=False)
    print(man.groupby(["classe", "sexo"]).size().unstack(fill_value=0))
    print(f"\n{len(man)} imagens em {DIR_IMGS}")


if __name__ == "__main__":
    main()

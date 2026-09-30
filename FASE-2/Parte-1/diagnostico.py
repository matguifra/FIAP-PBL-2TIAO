"""
CardioIA — Parte 1: extração de sintomas e sugestão de diagnóstico.

Lê as frases dos pacientes (frases_sintomas.txt), procura nelas as expressões
do mapa de conhecimento (mapa_conhecimento.csv), descarta os sintomas negados
("não tenho febre") e monta um ranking com as 3 doenças mais prováveis.

Uso: python3 diagnostico.py
"""
import csv
import re
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path

PASTA = Path(__file__).parent

# Palavras que negam o sintoma logo depois delas ("não tenho febre", "sem tosse")
NEGACAO = re.compile(r"\b(?:nao|nem|sem|nunca|nenhum|nenhuma|jamais)\b")
# A negação não atravessa pontuação nem conjunção adversativa:
# em "não tenho febre, mas sinto tontura" só a febre é negada
FIM_ORACAO = re.compile(r"[,.;:!?]|\b(?:mas|porem|contudo|entretanto|todavia)\b")


def normalizar(texto):
    # Minúsculas e sem acento: "Tórax" e "torax" passam a ser a mesma coisa
    sem_acento = unicodedata.normalize("NFKD", texto).encode("ascii", "ignore").decode()
    return " ".join(sem_acento.lower().split())


def carregar_mapa(caminho):
    """Cada linha do CSV vira (doença, [sintoma 1, sintoma 2])."""
    # utf-8-sig tolera o BOM que o Excel coloca ao salvar CSV
    with open(caminho, encoding="utf-8-sig", newline="") as f:
        return [(linha["Doença Associada"], [linha["Sintoma 1"], linha["Sintoma 2"]])
                for linha in csv.DictReader(f)]


def negado(texto, achado, expressao):
    # Negado quando há negação até 2 palavras antes do sintoma, na mesma oração
    # ("não tenho febre"), ou intercalada nele ("a fala não ficou enrolada").
    # Janela curta de propósito: "não aguento mais essa dor" NÃO nega a dor.
    antes = FIM_ORACAO.split(texto[:achado.start()])[-1].split()[-3:]
    trecho = " ".join(antes) + " " + achado.group()
    # Desconta a negação que faz parte da própria expressão ("dor que não passa")
    return len(NEGACAO.findall(trecho)) > len(NEGACAO.findall(expressao))


def identificar(frase, mapa):
    # Devolve {doença: [sintomas presentes na frase]} e a lista de sintomas negados.
    texto = normalizar(frase)
    achados, negados = defaultdict(list), {}
    for doenca, expressoes in mapa:
        for expressao in expressoes:
            norma = normalizar(expressao)
            # \b impede casar pedaço de palavra ("perna" dentro de "pernas");
            # entre as palavras aceita até 2 intercaladas ("fala ficou enrolada")
            padrao = r"\b" + r"(?:\s+\w+){0,2}\s+".join(map(re.escape, norma.split())) + r"\b"
            ocorrencias = list(re.finditer(padrao, texto))
            if not ocorrencias:
                continue
            # Uma menção afirmada basta: "não tive febre ontem, mas hoje tenho febre"
            if any(not negado(texto, m, norma) for m in ocorrencias):
                achados[doenca].append(expressao)
            else:
                negados[expressao] = None  # dict como conjunto que mantém a ordem
            break  # os dois sinônimos da linha contam como um sintoma só
    return achados, list(negados)


def main():
    mapa = carregar_mapa(PASTA / "mapa_conhecimento.csv")
    total = Counter(doenca for doenca, _ in mapa)  # sintomas cadastrados por doença
    linhas = (PASTA / "frases_sintomas.txt").read_text(encoding="utf-8").splitlines()
    frases = [linha.strip() for linha in linhas if linha.strip()]

    for n, frase in enumerate(frases, 1):
        print(f"\nPaciente {n}: {frase}")
        achados, negados = identificar(frase, mapa)
        if not achados:
            print("  Nenhum sintoma do mapa identificado.")
            continue

        # Ranking: mais sintomas encontrados primeiro; no empate, vence a doença
        # que teve a maior fração do seu quadro encontrada (ex.: 3 de 4 > 3 de 8)
        def chave(item):
            doenca, lista = item
            return len(lista), len(lista) / total[doenca]

        ranking = sorted(achados.items(), key=chave, reverse=True)
        sintomas = dict.fromkeys(s for _, lista in ranking for s in lista)  # sem repetir
        sugeridas = [d for d, lista in ranking if chave((d, lista)) == chave(ranking[0])]

        print("  Sintomas identificados:", ", ".join(sintomas))
        if negados:
            print("  Sintomas negados (ignorados):", ", ".join(negados))
        print("  Diagnóstico sugerido:", " / ".join(sugeridas))
        print("  Top 3 suspeitas:")
        for posicao, (doenca, lista) in enumerate(ranking[:3], 1):
            print(f"    {posicao}. {doenca} — {len(lista)} de {total[doenca]} sintomas do mapa")

    print("\nAviso: sugestão automática por palavras-chave, não substitui avaliação médica.")


if __name__ == "__main__":
    main()

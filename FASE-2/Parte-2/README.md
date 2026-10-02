# FIAP - Faculdade de Informática e Administração Paulista

<p align="center">
<a href= "https://www.fiap.com.br/"><img src="../../logo-fiap.png" alt="FIAP - Faculdade de Informática e Administração Paulista" border="0" width=40% height=40%></a>
</p>

<br>

# CardioIA — Fase 2, Parte 2: Classificador de Risco por Texto

## Integrantes:
- <a href="https://github.com/joaorafa-ramos">João Rafael Gonçalves Ramos</a> RM567908
- <a href="https://github.com/leticiaguerrasoares">Leticia Angelim Guerra</a> RM567501
- <a href="https://github.com/matguifra">Matheus Guimarães França</a> RM567144
- <a href="https://github.com/RivandoNeto">Rivando Bezerra Cavalcanti Neto</a> RM568235

## Professores:
### Tutor(a)
- <a href="https://br.linkedin.com/in/leonardoorabona">Leonardo Ruiz Orabona</a>
### Coordenador(a)
- <a href="https://www.linkedin.com/in/andregodoichiovato/">André Godoi Chiovatto</a>


## Descrição

Classificador básico de texto que lê o relato de sintomas de um paciente e o classifica como
**alto risco** ou **baixo risco**, simulando a priorização de uma triagem clínica.

A base tem **100 frases curtas** com queixas comuns do dia a dia (50 de alto risco e 50 de baixo
risco). As frases viram vetores com **TF-IDF** e são classificadas por **Regressão Logística**,
escolhida depois de comparada com uma Árvore de Decisão. Além da acurácia, o notebook testa o
modelo com 10 frases-desafio (negação, quadro atípico, escrita de celular etc.), mostra quais
palavras pesam na decisão e mede o desempenho por subgrupo para discutir vieses.


## Resultados

| Métrica | Resultado |
|---|---|
| Acurácia na validação cruzada do treino (Regressão Logística × Árvore de Decisão) | **0,79** × 0,67 |
| Acurácia no conjunto de teste (25 frases) | **0,76** |
| Acurácia na validação cruzada com as 100 frases | **0,82** |
| Recall (sensibilidade) de alto risco: teste / validação cruzada | 0,69 (9 de 13) / 0,82 (41 de 50) |
| Acerto nas frases-desafio | 6 de 10 |
| Relatos da Parte 1 marcados como alto risco (todos são graves) | 8 de 10 |

**Distorções encontradas:** o modelo não entende negação (*"coriza e tosse, sem febre e sem falta
de ar"* vira alto risco), trata como leve um sinal de alarme que "já passou" (*"minha boca ficou
torta por uns minutos, mas já passou"*), depende muito da palavra "peito", aprende atalhos do jeito
de escrever (*"por causa"* e *"depois de"* puxam para baixo risco) e erra quadros atípicos, como o
cansaço extremo de um idoso.

**Viés de representação:** na validação cruzada com as 100 frases, o modelo detecta 17 de 17 casos
graves que citam o peito, mas só 24 de 33 dos que não citam (desmaio, pressão muito alta, AVC, perna
inchada). Quadros sem dor no peito são mais frequentes em mulheres, idosos e diabéticos (Canto et
al., 2000; Mehta et al., 2016), então são esses pacientes que o modelo mais deixaria no fim da fila.


## Como funciona

1. **Base:** `frases_risco.csv`, no formato `frase,situacao`. As duas primeiras frases são os
   exemplos do enunciado; nenhuma frase da Parte 1 foi usada no treino. As frases em que o paciente
   fala de si no masculino ou no feminino ("cansado" / "cansada") estão equilibradas em cada classe,
   o mesmo cuidado de paridade que a Fase 1 teve com os ECGs.
2. **Separação:** 75 frases de treino e 25 de teste, estratificadas (`random_state=42`).
3. **TF-IDF:** `TfidfVectorizer(strip_accents="unicode", ngram_range=(1, 2))`, que converte para
   minúsculas, remove acentos (`coração` = `coracao`) e conta palavras e pares de palavras
   (`falta de`, `no peito`). As stopwords não são removidas, porque "não" e "sem" mudam o sentido.
4. **Modelo:** Regressão Logística e Árvore de Decisão comparadas por validação cruzada no treino
   (5 partes, repetida 10 vezes). O vetorizador fica dentro de um `Pipeline`, sem vazamento de dados.
5. **Avaliação:** acurácia, precisão, recall, matriz de confusão, erros do teste, validação cruzada
   com as 100 frases, termos que mais pesam em cada classe e frases-desafio.
6. **Vieses:** desempenho separado para casos graves que citam ou não citam o peito.
7. **Integração com a Parte 1:** o classificador atribui o nível de risco aos 10 relatos de
   [`../Parte-1/frases_sintomas.txt`](../Parte-1/frases_sintomas.txt). A Parte 1 indica **o que**
   pode ser; a Parte 2 indica **com que urgência** o paciente deve ser atendido.


## Critério de rotulagem

**Alto risco:** pelo menos um sinal de alarme, como: dor, aperto ou peso no peito (em repouso, forte,
que irradia para braço, pescoço, queixo ou costas, ou com suor frio, enjoo, tontura ou falta de ar);
dor no peito nova ou que aparece com esforços cada vez menores; falta de ar súbita, em repouso, ao
deitar, com pouco esforço ou com as pernas inchadas; desmaio ou quase desmaio; coração disparado com
tontura ou falta de ar, ou que não acalma; boca torta, fala enrolada, perda súbita da visão, fraqueza
ou dormência de um lado do corpo; pressão muito alta com sintomas; perna inchada e quente de um lado
só; quadros atípicos (cansaço extremo súbito, enjoo ou dor nas costas com suor frio, dor no queixo e
no braço com suor frio); lábios ou dedos roxos. Um sinal de alarme que já passou (desmaio, boca torta
por alguns minutos) continua sendo alto risco.

**Baixo risco:** sintoma leve e sem sinal de alarme, em geral com causa clara: dor muscular ou de postura,
resfriado, alergia, dor de dente, azia, cansaço por dormir mal, pequenos machucados, pedidos de
rotina. A base inclui de propósito casos com palavras de alarme em contexto benigno, como *"dor no
peito só quando aperto o músculo, depois da academia"* e *"minha boca está dormente por causa da
anestesia do dentista"*.

Os sinais de dor no peito se baseiam na *III Diretriz sobre Tratamento do Infarto Agudo do Miocárdio*
(SBC, 2004), do corpus da [Fase 1](../../FASE-1/docs/): dor forte ou em repouso, que irradia para
braço e pescoço, com falta de ar, enjoo ou vômito. Os demais, incluindo queixo, costas e suor frio,
são sinais de alarme de uso geral em triagem, reunidos pelo grupo sem validação clínica.


## Limitações conhecidas

- **Base pequena e simulada:** 100 frases escritas e rotuladas pelo grupo, sem revisão de um
  profissional de saúde. A proporção 50/50 não reflete uma triagem real.
- **Saco de palavras:** o TF-IDF só enxerga palavras isoladas e pares vizinhos, então negação,
  minimização e contexto passam despercebidos.
- **Atalhos de escrita:** palavras como "por causa", "depois de", "já passou", "no" e "hora" ganharam
  peso por causa do jeito como as frases foram escritas, e não por motivo clínico.
- **Casos graves perdidos:** o recall de alto risco foi de 0,69 no teste (4 de 13 perdidos) e de 0,82
  na validação cruzada (9 de 50 perdidos), ou seja, de 1 em cada 5 a 1 em cada 3 casos graves
  ficaria no fim da fila.


## Próximas melhorias

1. Ampliar a base com mais variedade de sinais de alarme e, se possível, relatos reais anonimizados
   (dado de saúde é dado sensível, art. 11 da LGPD), com rótulos revisados por profissionais.
2. Tratar a negação antes do TF-IDF, reaproveitando a regra de negação da Parte 1
   (`sem falta de ar` → `NEG_falta_de_ar`).
3. Baixar o limiar de decisão para priorizar o recall de alto risco, escolhendo o valor num conjunto
   de validação separado.
4. Comparar com modelos de linguagem em português que entendem contexto, como o BERTimbau.


## Estrutura de pastas

- <b>frases_risco.csv</b>: 100 frases rotuladas (`frase,situacao`), 50 de alto risco e 50 de baixo risco.
- <b>classificador_risco.ipynb</b>: notebook com TF-IDF, treino, avaliação, frases-desafio, análise de
  vieses e integração com a Parte 1 (já executado, com as saídas salvas).
- <b>README.md</b>: este guia.


## Como executar o código

Pré-requisitos: Python 3.10+ com `pandas`, `scikit-learn`, `matplotlib` e Jupyter (testado com
Python 3.13, scikit-learn 1.7, pandas 2.3 e matplotlib 3.10).

```bash
git clone https://github.com/matguifra/FIAP-PBL-2TIAO.git
cd FIAP-PBL-2TIAO/FASE-2/Parte-2
pip install pandas scikit-learn matplotlib jupyter
jupyter notebook classificador_risco.ipynb
```

Execute as células em ordem (*Run All*). A seção 8 lê `../Parte-1/frases_sintomas.txt`, então rode o
notebook a partir do repositório clonado.

> **Aviso:** protótipo acadêmico. A classificação de risco é automática e não substitui a avaliação
> de um profissional de saúde.


## Referências

1. Sociedade Brasileira de Cardiologia. *III Diretriz sobre Tratamento do Infarto Agudo do Miocárdio*.
   Arq. Bras. Cardiol., v. 83, supl. 4, 2004. (Texto 02 do corpus da Fase 1.)
2. Canto, J. G. et al. *Prevalence, clinical characteristics, and mortality among patients with
   myocardial infarction presenting without chest pain*. JAMA, v. 283, n. 24, p. 3223-3229, 2000.
3. Mehta, L. S. et al. *Acute Myocardial Infarction in Women: A Scientific Statement From the American
   Heart Association*. Circulation, v. 133, n. 9, p. 916-947, 2016.
4. Pedregosa, F. et al. *Scikit-learn: Machine Learning in Python*. JMLR, v. 12, p. 2825-2830, 2011.


## Histórico de lançamentos

* 0.1.0 - 02/10/2026
    * Base com 100 frases rotuladas, classificador TF-IDF + Regressão Logística, frases-desafio e análise de vieses.

## Licença

<img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/cc.svg?ref=chooser-v1"><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/by.svg?ref=chooser-v1"><p xmlns:cc="http://creativecommons.org/ns#" xmlns:dct="http://purl.org/dc/terms/"><a property="dct:title" rel="cc:attributionURL" href="https://github.com/agodoi/template">MODELO GIT FIAP</a> por <a rel="cc:attributionURL dct:creator" property="cc:attributionName" href="https://fiap.com.br">Fiap</a> está licenciado sob <a href="http://creativecommons.org/licenses/by/4.0/?ref=chooser-v1" target="_blank" rel="license noopener noreferrer" style="display:inline-block;">Attribution 4.0 International</a>.</p>

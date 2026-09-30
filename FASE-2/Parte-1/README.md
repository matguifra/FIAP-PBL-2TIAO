# FIAP - Faculdade de Informática e Administração Paulista

<p align="center">
<a href= "https://www.fiap.com.br/"><img src="../../logo-fiap.png" alt="FIAP - Faculdade de Informática e Admnistração Paulista" border="0" width=40% height=40%></a>
</p>

<br>

# CardioIA — Fase 2, Parte 1: Frases de Sintomas e Extração de Informações

## 👨‍🎓 Integrantes:
- <a href="https://github.com/joaorafa-ramos">João Rafael Gonçalves Ramos</a> RM567908
- <a href="https://github.com/leticiaguerrasoares">Leticia Angelim Guerra</a> RM567501
- <a href="https://github.com/matguifra">Matheus Guimarães França</a> RM567144
- <a href="https://github.com/RivandoNeto">Rivando Bezerra Cavalcanti Neto</a> RM568235

## 👩‍🏫 Professores:
### Tutor(a)
- <a href="https://br.linkedin.com/in/leonardoorabona">Leonardo Ruiz Orabona</a>
### Coordenador(a)
- <a href="https://www.linkedin.com/in/andregodoichiovato/">André Godoi Chiovatto</a>


## 📜 Descrição

Sistema simples de apoio ao diagnóstico: lê relatos de pacientes em linguagem natural,
identifica os sintomas com base em um mapa de conhecimento e sugere as doenças mais prováveis.

Os relatos simulam o que um paciente diria numa triagem: o que sente, desde quando e como isso
afeta a rotina (*"Há dois dias estou com uma dor no peito que piora quando faço esforço
físico..."*). O mapa de conhecimento associa expressões comuns a 12 doenças cardiovasculares.
Sintomas compartilhados, como `dor no peito`, `falta de ar` e `febre`, aparecem em mais de uma
doença de propósito: o diagnóstico sai da **combinação** de sintomas, não de um sintoma isolado.

Para cada relato, o programa mostra os sintomas identificados, os sintomas que o paciente negou,
o diagnóstico sugerido e o **top 3 de suspeitas**.


## ⚙️ Como funciona

1. **Normalização:** frase e mapa passam para minúsculas e sem acento (`Tórax` = `torax`).
2. **Busca dos sintomas:** cada expressão do mapa é procurada como palavra inteira (`perna` não casa
   com `pernas`), aceitando até 2 palavras no meio (`fala ficou enrolada` casa com `fala enrolada`).
   Os dois sinônimos de uma mesma linha contam como um único sintoma.
3. **Negação:** um sintoma é ignorado quando `não`, `nem`, `sem`, `nunca`, `nenhum(a)` ou `jamais`
   aparece até 2 palavras antes dele, na mesma oração. A negação é interrompida por pontuação e por
   conjunções adversativas (`mas`, `porém`...). Assim, em *"não tenho febre, mas sinto tontura"* só a
   febre é descartada, e em *"não aguento mais essa dor no peito"* a dor continua valendo.
4. **Ranking:** as doenças são ordenadas pelo número de sintomas encontrados. Em caso de empate,
   vence a que teve a maior fração do próprio quadro encontrada (3 de 4 vence 3 de 8).

Exemplo de saída:

```
Paciente 2: Há dois dias estou com uma dor no peito que piora quando faço esforço físico, ...
  Sintomas identificados: dor no peito, piora quando faço esforço, subir escadas, alivia quando paro
  Sintomas negados (ignorados): falta de ar, tontura
  Diagnóstico sugerido: Angina
  Top 3 suspeitas:
    1. Angina — 4 de 7 sintomas do mapa
    2. Pericardite — 1 de 6 sintomas do mapa
    3. Infarto agudo do miocárdio — 1 de 8 sintomas do mapa
```


## ⚠️ Limitações conhecidas

- **Negação por regras:** *"não consigo subir escada sem falta de ar"* descarta a falta de ar por
  engano (o `sem` ali confirma o sintoma). Em listas com vírgula, como *"não tenho febre, tosse ou
  falta de ar"*, só a febre é negada. Quando há dúvida, a regra prefere **manter** o sintoma, porque
  deixar de ver um sintoma real é o erro mais grave numa triagem.
- **Todos os sintomas têm o mesmo peso:** `cansaço` vale tanto quanto `dor irradiando para o braço
  esquerdo`, embora o segundo seja muito mais específico de infarto.
- **Variações de escrita precisam estar no mapa:** flexões (`inchado`/`inchada`), sinônimos novos e
  erros de digitação não são reconhecidos.


## 🚀 Próximas melhorias

1. **Peso por sintoma:** adicionar uma coluna `peso` ao CSV, ou calcular a especificidade de cada
   sintoma pelo número de doenças em que ele aparece (lógica do TF-IDF). Sintomas raros e típicos
   passariam a contar mais.
2. **Classificação de urgência:** marcar no mapa as doenças que exigem emergência imediata (infarto,
   AVC, embolia pulmonar) e destacar esses casos na saída, independentemente da posição no ranking.
3. **Extração estruturada completa:** capturar também o **início** (*"há dois dias"*, *"desde
   ontem"*), a **intensidade** e o **impacto na rotina**, gerando um registro JSON por paciente. O
   tempo de início ajuda a separar quadros agudos de crônicos (ex.: infarto × angina estável).
4. **Negação e incerteza mais robustas:** adaptar o algoritmo NegEx/ConText ao português, tratando
   pseudo-negações (*"sem"* depois de verbo negado) e incerteza (*"acho que"*, *"talvez"*), ou usar
   análise sintática com spaCy (`pt_core_news_sm`).
5. **Tolerância a variações de escrita:** lematização (spaCy) ou stemming (RSLP, do NLTK) para as
   flexões, e busca aproximada (`difflib`, `rapidfuzz`) para erros de digitação.
6. **Ontologia padronizada:** trocar o CSV plano por códigos **CID-10** e termos **SNOMED CT** /
   **DeCS**, com hierarquia (sintoma → sistema do corpo → doença), em um formato como OWL/RDF.
7. **Contexto do paciente:** considerar idade, sexo e fatores de risco (tabagismo, diabetes,
   hipertensão) para ajustar a probabilidade de cada doença, por exemplo com um modelo bayesiano.
8. **Aprendizado de máquina e avaliação:** montar um conjunto maior de relatos com gabarito, medir
   acurácia, top-3 accuracy e matriz de confusão, e comparar esta abordagem por regras com um
   classificador treinado (TF-IDF + Regressão Logística, ou um modelo de linguagem como o BERTimbau).
9. **Validação clínica e LGPD:** revisar o mapa com um profissional de saúde e, se houver relatos
   reais, anonimizá-los antes do processamento (dado de saúde é dado sensível, art. 11 da LGPD).


## 📁 Estrutura de pastas

- <b>frases_sintomas.txt</b>: 10 relatos de pacientes (o que sentem, desde quando e como isso afeta a rotina).
- <b>mapa_conhecimento.csv</b>: 79 associações `Sintoma 1, Sintoma 2 → Doença Associada`, cobrindo 12 doenças cardiovasculares.
- <b>diagnostico.py</b>: lê as frases, extrai os sintomas, descarta os negados e monta o ranking de suspeitas.
- <b>README.md</b>: este guia.


## 🔧 Como executar o código

Pré-requisito: Python 3 (testado com Python 3.14). O código usa apenas a biblioteca padrão, então
não há dependências para instalar.

```bash
git clone https://github.com/matguifra/FIAP-PBL-2TIAO.git
cd FIAP-PBL-2TIAO/FASE-2/Parte-1
python3 diagnostico.py
```

> **Aviso:** protótipo acadêmico. As sugestões são geradas por palavras-chave e não substituem
> avaliação médica.


## 🗃 Histórico de lançamentos

* 0.1.0 - 30/09/2026
    * Extração de sintomas, tratamento de negação e ranking top 3 de suspeitas.

## 📋 Licença

<img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/cc.svg?ref=chooser-v1"><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/by.svg?ref=chooser-v1"><p xmlns:cc="http://creativecommons.org/ns#" xmlns:dct="http://purl.org/dc/terms/"><a property="dct:title" rel="cc:attributionURL" href="https://github.com/agodoi/template">MODELO GIT FIAP</a> por <a rel="cc:attributionURL dct:creator" property="cc:attributionName" href="https://fiap.com.br">Fiap</a> está licenciado sobre <a href="http://creativecommons.org/licenses/by/4.0/?ref=chooser-v1" target="_blank" rel="license noopener noreferrer" style="display:inline-block;">Attribution 4.0 International</a>.</p>

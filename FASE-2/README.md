# FIAP - Faculdade de Informática e Administração Paulista

<p align="center">
<a href= "https://www.fiap.com.br/"><img src="../logo-fiap.png" alt="FIAP - Faculdade de Informática e Admnistração Paulista" border="0" width=40% height=40%></a>
</p>

<br>

# CardioIA — Fase 2: Triagem e Apoio ao Diagnóstico por Linguagem Natural

## 👨‍🎓 Integrantes: 
| Nome | RM |
|---|:---:|
| <a href="https://github.com/joaorafa-ramos">João Rafael Gonçalves Ramos</a> | RM567908 |
| <a href="https://github.com/leticiaguerrasoares">Letícia Angelim Guerra</a> | RM567501 |
| <a href="https://github.com/matguifra">Matheus Guimarães França</a> | RM567144 |
| <a href="https://github.com/RivandoNeto">Rivando Bezerra Cavalcanti Neto</a> | RM568235 |

## 👩‍🏫 Professores:
### Tutor(a) 
- <a href="https://br.linkedin.com/in/leonardoorabona">Leonardo Ruiz Orabona</a>
### Coordenador(a)
- <a href="https://www.linkedin.com/in/andregodoichiovato/">André Godoi Chiovatto</a>


## 📜 Descrição

O **CardioIA — Fase 2** expande o ecossistema cardiológico inteligente desenvolvido na Fase 1, implementando soluções de **Processamento de Linguagem Natural (NLP)** e **Aprendizado de Máquina** para automatizar a triagem clínica e apoiar a formulação de hipóteses diagnósticas a partir de relatos livres de sintomas.

O projeto é estruturado em duas etapas complementares e integradas:

1. **Parte 1 — Extração de Informações e Hipóteses Diagnósticas por Regras:**
   Processa relatos em linguagem natural simulando queixas de pacientes em triagem hospitalar. O sistema normaliza o texto (remoção de acentos e padronização para minúsculas) e realiza o casamento com um mapa de conhecimento estruturado de 79 regras clínicas, cobrindo 12 patologias cardiovasculares (ex.: Infarto Agudo do Miocárdio, Angina, Insuficiência Cardíaca, Miocardite e Pericardite). Possui um motor determinístico de tratamento de negações (*"não sinto febre"*, *"sem falta de ar"*), que desconsidera sintomas negados respeitando fronteiras sintáticas (pontuação e conjunções adversativas). Ao final, gera um ranking das doenças mais prováveis ordenado pela quantidade e cobertura proporcional de sintomas (Top 3 hipóteses diagnósticas).

2. **Parte 2 — Classificação de Risco Clínico com Aprendizado de Máquina:**
   Implementa a priorização da fila de acolhimento classificando os relatos em **alto risco** ou **baixo risco**. Utiliza uma base balanceada de 100 relatos clínicos curtos (50 de alto risco e 50 de baixo risco), com rigorosa paridade de gênero e fundamentada nos sinais de alarme da *III Diretriz sobre Tratamento do Infarto Agudo do Miocárdio* (SBC, 2004). O pipeline combina extração de características via **TF-IDF** (unigramas e bigramas, preservando marcadores de negação) com **Regressão Logística**, modelo que superou a Árvore de Decisão em validação cruzada repetida (acurácia de 0,79 vs 0,67). O módulo conta com testes em frases-desafio, explicabilidade de termos de peso e auditoria de viés algorítmico para apresentações atípicas (sem dor no peito, frequentes em mulheres, idosos e diabéticos).

**Integração do Pipeline:**
As duas partes operam de forma sinérgica: a **Parte 1** identifica **o que** o paciente provavelmente apresenta (conjunto de sintomas ativos e suspeitas diagnósticas), enquanto a **Parte 2** determina a **urgência** do atendimento (estratificação de risco na triagem).


## 📁 Estrutura de pastas

Dentre os arquivos e pastas presentes na raiz da Fase 2, definem-se:

- <b>Parte-1/</b>: Módulo de extração de sintomas e apoio ao diagnóstico baseado em regras.
  - <b>diagnostico.py</b>: Script em Python para normalização textual, detecção de sintomas, descarte de negações e ranking top-3 de hipóteses diagnósticas.
  - <b>frases_sintomas.txt</b>: Arquivo com 10 relatos clínicos simulados de pacientes para triagem cardiológica.
  - <b>mapa_conhecimento.csv</b>: Base de conhecimento com 79 pares de expressões clínicas associadas a 12 doenças cardiovasculares.
  - <b>README.md</b>: Documentação detalhada da Parte 1, regras sintáticas, limitações conhecidas e próximos passos.
- <b>Parte-2/</b>: Módulo de aprendizado de máquina para classificação de risco clínico.
  - <b>classificador_risco.ipynb</b>: Jupyter Notebook documentado com pipeline TF-IDF + Regressão Logística, validação cruzada, análise de vieses e inferência integrada sobre os relatos da Parte 1.
  - <b>frases_risco.csv</b>: Dataset rotulado com 100 frases de sintomas (50 de alto risco e 50 de baixo risco) e balanceamento de gênero.
  - <b>README.md</b>: Documentação técnica da Parte 2, critérios de rotulagem clínica, métricas e referências bibliográficas.
- <b>template-README.md</b>: Documento consolidado e guia geral da Fase 2 do projeto CardioIA.


## 🔧 Como executar o código

### Pré-requisitos
- Python 3.10 ou superior (testado com Python 3.13 e 3.14).
- Gerenciador [uv](https://docs.astral.sh/uv/) (recomendado) ou `pip`/`venv`.
- Dependências da Parte 2: `pandas`, `scikit-learn`, `matplotlib`, `jupyter`.

---

### Executando a Parte 1 (Extração de Sintomas e Diagnóstico)
A Parte 1 foi desenvolvida utilizando exclusivamente a biblioteca padrão do Python, sem necessidade de instalar dependências externas:

```bash
# Navegar até a pasta da Parte 1
cd FASE-2/Parte-1

# Executar com uv:
uv run python diagnostico.py

# Ou com Python nativo:
python3 diagnostico.py
```

---

### Executando a Parte 2 (Classificador de Risco)
A Parte 2 contém o notebook de pré-processamento, modelagem e avaliação:

```bash
# Navegar até a pasta da Parte 2
cd FASE-2/Parte-2

# Executar o Jupyter com uv (instalação e execução sob demanda):
uv run --with pandas,scikit-learn,matplotlib,jupyter jupyter notebook classificador_risco.ipynb

# Ou instalação convencional via pip:
pip install pandas scikit-learn matplotlib jupyter
jupyter notebook classificador_risco.ipynb
```

Execute as células em ordem (*Run All*). A seção 8 consome diretamente o arquivo `../Parte-1/frases_sintomas.txt`, integrando os dois módulos.


## 🗃 Histórico de lançamentos

* 0.2.0 - 02/10/2026
    * Conclusão da Parte 2: dataset com 100 frases rotuladas, pipeline TF-IDF + Regressão Logística, testes com frases-desafio, auditoria de vieses de representação e integração com os relatos da Parte 1.
* 0.1.0 - 30/09/2026
    * Conclusão da Parte 1: motor de extração de sintomas por regras léxicas, algoritmo contextual de descarte de negações e ranking top-3 de hipóteses diagnósticas.



## 📋 Licença

<img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/cc.svg?ref=chooser-v1"><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/by.svg?ref=chooser-v1"><p xmlns:cc="http://creativecommons.org/ns#" xmlns:dct="http://purl.org/dc/terms/"><a property="dct:title" rel="cc:attributionURL" href="https://github.com/agodoi/template">MODELO GIT FIAP</a> por <a rel="cc:attributionURL dct:creator" property="cc:attributionName" href="https://fiap.com.br">Fiap</a> está licenciado sobre <a href="http://creativecommons.org/licenses/by/4.0/?ref=chooser-v1" target="_blank" rel="license noopener noreferrer" style="display:inline-block;">Attribution 4.0 International</a>.</p>



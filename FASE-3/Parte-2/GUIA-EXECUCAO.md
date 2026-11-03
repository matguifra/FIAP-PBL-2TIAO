# Guia de execução — Fase 3, Parte 2

Passo a passo para rodar o protótipo, ver o dashboard funcionando e tirar os prints da entrega. Três blocos: (A) ESP32 no Wokwi, (B) dashboard no Node-RED, (C) Grafana (opcional).

---

## A) Rodar o ESP32 no Wokwi

1. Acesse https://wokwi.com e faça login.
2. Crie um novo projeto de **ESP32** (menu New Project → ESP32).
3. Substitua o conteúdo de:
   - `sketch.ino` pelo nosso `sketch.ino`;
   - `diagram.json` pelo nosso `diagram.json`.
4. Clique na aba **Library Manager** (ícone de biblioteca) e adicione:
   - **DHT sensor library for ESPx**
   - **PubSubClient**
5. Clique em ▶️ para iniciar a simulação. No Monitor Serial devem aparecer:
   - a conexão ao Wi-Fi (`[REDE] Conectando ao Wi-Fi Wokwi-GUEST ... OK!`);
   - a conexão ao broker (`[MQTT] ... CONECTADO.`);
   - as leituras publicadas (`[NUVEM] {"seq":1,...}`).
6. **Gerando dados para o dashboard:**
   - **Batimentos:** clique no botão vermelho no ritmo desejado. ~1 clique por segundo ≈ 60 bpm; mais de 2 por segundo gera **taquicardia** (BPM > 120).
   - **Febre:** clique no DHT22 e suba a temperatura para 38 °C ou mais. O LED vermelho acende.
   - **Offline/online:** mova a chave deslizante. Offline, as leituras entram na fila; ao voltar, são reenviadas (aparecem com `"origem":"buffer"`).
7. Depois de salvar o projeto, copie o link e cole no `README.md` (campo "Simulação no Wokwi").

> **Print 1:** o Monitor Serial mostrando as linhas `[NUVEM] {...}` sendo publicadas.

---

## B) Dashboard no Node-RED

### B.1 Instalar o Node-RED (uma vez só)

No terminal do seu computador, com o Node.js instalado:

```
npm install -g --unsafe-perm node-red
```

Depois inicie com:

```
node-red
```

Abra o navegador em **http://127.0.0.1:1880** (o editor do Node-RED).

### B.2 Instalar a paleta do dashboard

No editor: menu (canto superior direito) → **Manage palette** → aba **Install** → procure **node-red-dashboard** → **Install**.

### B.3 Importar o nosso dashboard

No editor: menu → **Import** → **select a file to import** → escolha o `flows.json` → **Import** → clique em **Deploy** (botão vermelho no canto superior direito).

O fluxo já vem configurado com o broker e o tópico certos. Não precisa mexer em nada.

### B.4 Abrir o painel

Abra **http://127.0.0.1:1880/ui**. Com a simulação do Wokwi rodando (bloco A), o painel começa a se mexer:

- o **gráfico de BPM** desenha a linha dos batimentos;
- o **gauge de temperatura** e o **gauge de BPM** acompanham os valores;
- a **faixa de alerta** fica verde (Normal) e muda para vermelha quando você força febre (≥ 38 °C) ou taquicardia (> 120 bpm).

> **Print 2:** o painel (`/ui`) em estado **normal** — tudo verde.
> **Print 3:** o painel em estado de **alerta** — a faixa vermelha e os gauges na zona vermelha (force uma febre ou uma taquicardia no Wokwi antes de capturar).
> **Print 4 (opcional):** o fluxo aberto no editor do Node-RED, mostrando os nós ligados.

---

## C) Grafana Cloud (visualização avançada — opcional)

Há dois caminhos. O mais simples usa o Node-RED como ponte; o outro liga o Grafana direto no MQTT.

**Caminho 1 — via Node-RED + InfluxDB (recomendado):**
1. Crie uma conta gratuita no **Grafana Cloud** (grafana.com) — ele já vem com um banco de séries temporais.
2. No Node-RED, instale a paleta `node-red-contrib-influxdb` e adicione um nó **influxdb out** depois do `mqtt in`, gravando cada leitura.
3. No Grafana, adicione a fonte de dados do seu InfluxDB e monte um painel com a série de BPM e de temperatura, com uma régua de limite (threshold) em 120 bpm e 38 °C.

**Caminho 2 — Grafana direto no MQTT:**
1. No Grafana Cloud, instale o plugin **Grafana MQTT data source**.
2. Configure o broker `broker.hivemq.com:1883` e o tópico `cardioia/leticia567501/telemetria`.
3. Monte um painel de série temporal com os campos `bpm` e `temp`.

> **Print 5 (opcional):** o painel do Grafana com os dados.

---

## Checklist de entrega da Parte 2

- [ ] Link do projeto no Wokwi (no README).
- [ ] `sketch.ino` comentado.
- [ ] `flows.json` (export do dashboard) **ou** prints do dashboard — de preferência os dois.
- [ ] Print do Monitor Serial publicando no MQTT.
- [ ] Print do painel em estado normal e em estado de alerta.
- [ ] `relatorio-parte2.md` (2+ páginas).
- [ ] (Opcional) Print do Grafana.

---

## B-bis) Atalho sem instalar nada: dashboard web

Se você não quiser instalar o Node-RED agora, dá para ver os dados ao vivo e tirar
os prints com a página `dashboard-web/dashboard.html`:

1. Com o Wokwi rodando e publicando (bloco A), baixe o `dashboard.html` e abra com
   dois cliques no navegador (precisa de internet).
2. A bolinha fica verde ("conectado ao broker") e o painel começa a se mexer.
3. Tire os prints em estado normal e em alerta (force febre ou taquicardia no Wokwi).

Lembre: para a entrega formal, o dashboard pedido é o do **Node-RED** (`flows.json`).
Esta página é um complemento para facilitar a visualização.

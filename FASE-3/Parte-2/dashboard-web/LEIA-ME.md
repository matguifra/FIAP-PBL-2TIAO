# Dashboard web (jeito fácil, sem instalar nada)

Esta é uma forma rápida de VER os dados do ESP32 ao vivo e tirar os prints, sem
precisar instalar o Node-RED. Ela conecta no mesmo broker MQTT (HiveMQ) por
WebSocket e mostra gráfico de BPM, medidor de temperatura, BPM e o alerta.

> Observação para a entrega: o enunciado pede o dashboard no **Node-RED** — e ele
> está pronto no `flows.json` (seção B do guia). Esta página web é um COMPLEMENTO
> para facilitar a visualização e os prints; não substitui o Node-RED na entrega.

## Como usar
1. Abra o Wokwi e rode o `sketch.ino` (deixe publicando — ver o guia).
2. Baixe este `dashboard.html` e abra com dois cliques no navegador (Chrome).
   Precisa de internet (ele baixa as bibliotecas de gráfico e de MQTT).
3. A bolinha no topo fica verde ("conectado ao broker"). Quando você apertar o
   botão no Wokwi e mexer na temperatura, os valores aparecem aqui ao vivo.
4. Tire o print em estado **normal** (faixa verde) e force uma febre (≥38 °C) ou
   taquicardia (>120 bpm) no Wokwi para tirar o print em **alerta** (faixa vermelha).

Importante: o tópico aqui (no topo do arquivo, `TOPICO`) tem que ser o MESMO do
`sketch.ino`. Já estão iguais: cardioia/leticia567501/telemetria

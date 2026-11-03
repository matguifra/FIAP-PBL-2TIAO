/*
 * =============================================================================
 *  CardioIA — Fase 3, Parte 2: Transmissão para a nuvem e visualização
 *  (Fog/Cloud Computing) — monitor cardíaco vestível simulado com ESP32
 * =============================================================================
 *
 *  Continuação da Parte 1 (Edge Computing, feita pelo João). Aqui o dispositivo
 *  deixa de apenas guardar os dados localmente e passa a PUBLICÁ-LOS NA NUVEM
 *  via protocolo MQTT, para serem exibidos em tempo real num dashboard (Node-RED
 *  e, opcionalmente, Grafana).
 *
 *  O que este programa faz:
 *    1. Conecta-se ao Wi-Fi (no Wokwi, a rede aberta "Wokwi-GUEST" dá acesso à
 *       internet) e a um broker MQTT público (broker.hivemq.com, porta 1883).
 *    2. Lê dois sensores a cada 3 segundos:
 *         - DHT22 (obrigatório): temperatura e umidade;
 *         - Botão de pressão: cada aperto simula um batimento; o programa calcula
 *           os batimentos por minuto (BPM).
 *    3. Classifica cada leitura na própria borda (NORMAL, FEBRE, TAQUICARDIA...).
 *    4. Publica a leitura como um JSON no tópico MQTT de telemetria. O dashboard
 *       assina esse tópico e mostra gráfico, medidor (gauge) e alertas.
 *    5. Resiliência: a chave deslizante simula a conectividade do paciente.
 *       Quando "offline", as leituras NÃO são perdidas: entram numa fila em
 *       memória. Quando a conexão volta, a fila é esvaziada (reenviada) antes
 *       das leituras novas — o padrão store-and-forward herdado da Parte 1.
 *
 *  Sensores, pinos e circuito: IDÊNTICOS aos da Parte 1 (mesmo diagram.json).
 *    DHT22 -> GPIO 15 | Botão "batimento" -> GPIO 4 | Chave "Wi-Fi" -> GPIO 27
 *    LED verde (publicando na nuvem) -> GPIO 25 | LED vermelho (alerta) -> GPIO 26
 *
 *  A lógica de leitura dos sensores, cálculo de BPM e classificação clínica foi
 *  reaproveitada da Parte 1 (João), para as duas partes ficarem coerentes.
 *
 *  Bibliotecas (Wokwi -> libraries.txt):
 *    - DHT sensor library for ESPx   (DHTesp)
 *    - PubSubClient                  (cliente MQTT)
 * =============================================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"

// -----------------------------------------------------------------------------
// 1) PINOS (iguais aos da Parte 1)
// -----------------------------------------------------------------------------
const int PINO_DHT             = 15;
const int PINO_BOTAO_BATIMENTO = 4;
const int PINO_CHAVE_WIFI      = 27;
const int PINO_LED_NUVEM       = 25;  // aceso = publicando na nuvem
const int PINO_LED_ALERTA      = 26;  // aceso = alerta clínico na última leitura

// -----------------------------------------------------------------------------
// 2) REDE E MQTT
// -----------------------------------------------------------------------------
const char* WIFI_SSID   = "Wokwi-GUEST";     // rede aberta do simulador (dá internet)
const char* WIFI_SENHA  = "";
const char* MQTT_BROKER = "broker.hivemq.com"; // broker público de teste
const int   MQTT_PORTA  = 1883;

// Base de tópico. O sufixo deixa o tópico "único" no broker público, evitando
// que dados de outras pessoas se misturem com os nossos.
const char* TOPICO_TELEMETRIA = "cardioia/leticia567501/telemetria";
const char* TOPICO_ALERTA     = "cardioia/leticia567501/alerta";
const char* MQTT_CLIENT_ID    = "cardioia-esp32-leticia567501";

WiFiClient   clienteWiFi;
PubSubClient mqtt(clienteWiFi);

// -----------------------------------------------------------------------------
// 3) COLETA E LIMITES CLÍNICOS (definidos pelo grupo, iguais à Parte 1)
// -----------------------------------------------------------------------------
const uint32_t INTERVALO_LEITURA_MS = 3000;  // 1 leitura a cada 3 s (dashboard em tempo real)
const float    TEMP_FEBRE      = 38.0;       // °C
const float    TEMP_HIPOTERMIA = 35.0;       // °C
const int      BPM_TAQUICARDIA = 120;        // bpm
const int      BPM_BRADICARDIA = 50;         // bpm

// -----------------------------------------------------------------------------
// 4) CÁLCULO DE BPM (botão) — mesmos parâmetros da Parte 1
// -----------------------------------------------------------------------------
const uint32_t INTERVALO_MIN_BATIMENTO_MS = 250;
const uint32_t INTERVALO_MAX_BATIMENTO_MS = 2000;
const uint32_t TEMPO_SEM_PULSO_MS         = 10000;
const uint8_t  BATIMENTOS_NA_MEDIA        = 5;

// -----------------------------------------------------------------------------
// 5) RESILIÊNCIA OFFLINE — fila em RAM
// -----------------------------------------------------------------------------
// Modelo de negócio: vestível domiciliar. Se a conexão cair, guardamos as
// leituras e reenviamos ao reconectar. Como a Parte 1 já cobre o armazenamento
// longo (12 h em SPIFFS), aqui mantemos uma fila curta em memória, suficiente
// para quedas rápidas de rede (300 amostras x 3 s = 15 min).
const uint16_t MAX_FILA = 300;

// -----------------------------------------------------------------------------
// 6) CÓDIGOS DE ALERTA (iguais à Parte 1)
// -----------------------------------------------------------------------------
const uint8_t ALERTA_FEBRE       = 1 << 0;
const uint8_t ALERTA_HIPOTERMIA  = 1 << 1;
const uint8_t ALERTA_TAQUICARDIA = 1 << 2;
const uint8_t ALERTA_BRADICARDIA = 1 << 3;
const uint8_t ALERTA_SEM_PULSO   = 1 << 4;
const uint8_t FALHA_DHT          = 1 << 5;
const uint8_t ALERTAS_CLINICOS   = ALERTA_FEBRE | ALERTA_HIPOTERMIA | ALERTA_TAQUICARDIA |
                                   ALERTA_BRADICARDIA | ALERTA_SEM_PULSO;

struct Leitura {
  uint32_t seq;
  uint32_t tempoS;
  float    temp;
  float    umid;
  uint16_t bpm;
  uint8_t  flags;
};

// -----------------------------------------------------------------------------
// 7) ESTADO GLOBAL
// -----------------------------------------------------------------------------
DHTesp   dht;
bool     conectividadeSimulada = false; // a chave deslizante (paciente online?)
uint32_t ultimaLeituraMs       = 0;
uint32_t proximoSeq            = 1;
float    ultimaTempValida      = NAN;
float    ultimaUmidValida      = NAN;

Leitura  fila[MAX_FILA];     // fila circular de pendentes (resiliência)
uint16_t filaInicio = 0;
uint16_t filaQtd    = 0;
uint32_t descartadas = 0;

// Batimento (interrupção do botão) — reaproveitado da Parte 1
volatile uint32_t ultimoBatimentoMs = 0;
volatile uint32_t intervalos[BATIMENTOS_NA_MEDIA];
volatile uint8_t  idxIntervalo      = 0;
volatile uint8_t  qtdIntervalos     = 0;
volatile bool     pulsoJaDetectado  = false;
portMUX_TYPE      travaBatimento    = portMUX_INITIALIZER_UNLOCKED;

// =============================================================================
// BATIMENTOS (idêntico à Parte 1)
// =============================================================================
void IRAM_ATTR aoDetectarBatimento() {
  uint32_t agora = millis();
  portENTER_CRITICAL_ISR(&travaBatimento);
  uint32_t dt = agora - ultimoBatimentoMs;
  if (pulsoJaDetectado && dt < INTERVALO_MIN_BATIMENTO_MS) {
    portEXIT_CRITICAL_ISR(&travaBatimento);
    return;
  }
  if (pulsoJaDetectado && dt <= INTERVALO_MAX_BATIMENTO_MS) {
    intervalos[idxIntervalo] = dt;
    idxIntervalo = (idxIntervalo + 1) % BATIMENTOS_NA_MEDIA;
    if (qtdIntervalos < BATIMENTOS_NA_MEDIA) qtdIntervalos++;
  } else {
    qtdIntervalos = 0;
    idxIntervalo  = 0;
  }
  ultimoBatimentoMs = agora;
  pulsoJaDetectado  = true;
  portEXIT_CRITICAL_ISR(&travaBatimento);
}

uint16_t calcularBPM(bool &semPulso) {
  portENTER_CRITICAL(&travaBatimento);
  bool     detectado = pulsoJaDetectado;
  uint32_t ultimo    = ultimoBatimentoMs;
  uint8_t  qtd       = qtdIntervalos;
  uint32_t soma      = 0;
  for (uint8_t i = 0; i < qtd; i++) soma += intervalos[i];
  portEXIT_CRITICAL(&travaBatimento);

  semPulso = false;
  if (!detectado) return 0;
  if (millis() - ultimo > TEMPO_SEM_PULSO_MS) { semPulso = true; return 0; }
  if (qtd == 0) return 0;
  return (uint16_t)(60000UL / (soma / qtd));
}

// =============================================================================
// LEITURA E CLASSIFICAÇÃO (idêntico à Parte 1)
// =============================================================================
Leitura lerSensores() {
  Leitura l;
  l.seq    = proximoSeq++;
  l.tempoS = millis() / 1000;
  l.flags  = 0;

  TempAndHumidity dados = dht.getTempAndHumidity();
  if (dht.getStatus() == DHTesp::ERROR_NONE && !isnan(dados.temperature)) {
    ultimaTempValida = dados.temperature;
    ultimaUmidValida = dados.humidity;
  } else {
    l.flags |= FALHA_DHT;
  }
  l.temp = ultimaTempValida;
  l.umid = ultimaUmidValida;

  bool semPulso;
  l.bpm = calcularBPM(semPulso);

  if (!isnan(l.temp) && l.temp >= TEMP_FEBRE)     l.flags |= ALERTA_FEBRE;
  if (!isnan(l.temp) && l.temp < TEMP_HIPOTERMIA) l.flags |= ALERTA_HIPOTERMIA;
  if (l.bpm > BPM_TAQUICARDIA)                    l.flags |= ALERTA_TAQUICARDIA;
  if (l.bpm > 0 && l.bpm < BPM_BRADICARDIA)       l.flags |= ALERTA_BRADICARDIA;
  if (semPulso)                                   l.flags |= ALERTA_SEM_PULSO;
  return l;
}

void statusComoTexto(uint8_t flags, char *saida, size_t tamanho) {
  if (flags == 0) { snprintf(saida, tamanho, "NORMAL"); return; }
  saida[0] = '\0';
  const char *nomes[] = {"FEBRE", "HIPOTERMIA", "TAQUICARDIA", "BRADICARDIA", "SEM_PULSO", "FALHA_DHT"};
  for (uint8_t i = 0; i < 6; i++) {
    if (flags & (1 << i)) {
      if (saida[0] != '\0') strncat(saida, "|", tamanho - strlen(saida) - 1);
      strncat(saida, nomes[i], tamanho - strlen(saida) - 1);
    }
  }
}

// Monta o JSON publicado na nuvem. Campos "planos" para o dashboard ler fácil.
void leituraComoJSON(const Leitura &l, const char *origem, char *saida, size_t tamanho) {
  char status[64];
  statusComoTexto(l.flags, status, sizeof(status));
  bool alerta = (l.flags & ALERTAS_CLINICOS) != 0;
  snprintf(saida, tamanho,
    "{\"seq\":%lu,\"t\":%lu,\"temp\":%.1f,\"umid\":%.1f,\"bpm\":%u,"
    "\"status\":\"%s\",\"alerta\":%d,\"febre\":%d,\"taqui\":%d,\"origem\":\"%s\"}",
    (unsigned long)l.seq, (unsigned long)l.tempoS,
    isnan(l.temp) ? 0.0 : l.temp, isnan(l.umid) ? 0.0 : l.umid, l.bpm,
    status, alerta ? 1 : 0,
    (l.flags & ALERTA_FEBRE) ? 1 : 0,
    (l.flags & ALERTA_TAQUICARDIA) ? 1 : 0,
    origem);
}

// =============================================================================
// FILA DE RESILIÊNCIA
// =============================================================================
void enfileirar(const Leitura &l) {
  if (filaQtd == MAX_FILA) {        // fila cheia: descarta a mais antiga
    filaInicio = (filaInicio + 1) % MAX_FILA;
    filaQtd--;
    descartadas++;
  }
  fila[(filaInicio + filaQtd) % MAX_FILA] = l;
  filaQtd++;
}

// =============================================================================
// REDE E MQTT
// =============================================================================
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("[REDE] Conectando ao Wi-Fi ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  uint8_t tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(300); Serial.print("."); tentativas++;
  }
  Serial.println(WiFi.status() == WL_CONNECTED
    ? "  OK! IP: " + WiFi.localIP().toString()
    : "  FALHOU (seguimos coletando mesmo assim).");
}

// Tenta (re)conectar ao broker MQTT, sem travar o programa.
bool conectarMQTT() {
  if (mqtt.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.print("[MQTT] Conectando ao broker ");
  Serial.print(MQTT_BROKER);
  // clientId com sufixo aleatório evita conflito de dois clientes iguais no broker público
  String id = String(MQTT_CLIENT_ID) + "-" + String(random(0xffff), HEX);
  if (mqtt.connect(id.c_str())) {
    Serial.println("  CONECTADO.");
    // tópico de status "retido": quem abrir o dashboard vê o último estado
    mqtt.publish(TOPICO_ALERTA, "{\"estado\":\"online\"}", true);
    return true;
  }
  Serial.print("  falhou (rc="); Serial.print(mqtt.state()); Serial.println("). Tenta de novo depois.");
  return false;
}

// Publica uma leitura no tópico de telemetria. Retorna true se o broker aceitou.
bool publicar(const Leitura &l, const char *origem) {
  if (!mqtt.connected()) return false;
  char payload[200];
  leituraComoJSON(l, origem, payload, sizeof(payload));
  bool ok = mqtt.publish(TOPICO_TELEMETRIA, payload);
  if (ok) { Serial.print("[NUVEM] "); Serial.println(payload); }
  // Alerta vai também para um tópico próprio (útil para Grafana/automação)
  if (ok && (l.flags & ALERTAS_CLINICOS)) {
    char status[64];
    statusComoTexto(l.flags, status, sizeof(status));
    char msg[96];
    snprintf(msg, sizeof(msg), "{\"estado\":\"ALERTA\",\"status\":\"%s\",\"bpm\":%u,\"temp\":%.1f}",
             status, l.bpm, isnan(l.temp) ? 0.0 : l.temp);
    mqtt.publish(TOPICO_ALERTA, msg, true);
  }
  return ok;
}

// Esvazia a fila de pendentes (mais antigas primeiro) quando a conexão volta.
void esvaziarFila() {
  uint16_t enviados = 0;
  while (filaQtd > 0 && mqtt.connected()) {
    if (!publicar(fila[filaInicio], "buffer")) break;  // broker recusou: tenta depois
    filaInicio = (filaInicio + 1) % MAX_FILA;
    filaQtd--;
    enviados++;
    mqtt.loop();
    delay(20);  // não satura o broker público
  }
  if (enviados > 0)
    Serial.printf("[SYNC] %u leitura(s) pendente(s) reenviada(s) apos reconexao.\n", enviados);
  if (descartadas > 0) {
    Serial.printf("[NUVEM] AVISO: %lu leitura(s) descartada(s) por limite de buffer.\n",
                  (unsigned long)descartadas);
    char aviso[96];
    snprintf(aviso, sizeof(aviso), "{\"estado\":\"aviso\",\"descartadas\":%lu}", (unsigned long)descartadas);
    mqtt.publish(TOPICO_ALERTA, aviso, true);
    descartadas = 0;
  }
}

// Lê a chave deslizante -> conectividade simulada do paciente.
void atualizarConectividade() {
  bool estado = digitalRead(PINO_CHAVE_WIFI) == HIGH;
  if (estado != conectividadeSimulada) {
    conectividadeSimulada = estado;
    Serial.println(conectividadeSimulada
      ? "[REDE] Paciente ONLINE. Publicando na nuvem; fila pendente sera reenviada."
      : String("[REDE] Paciente OFFLINE. Leituras vao para a fila (resiliencia)."));
  }
}

// =============================================================================
// SETUP E LOOP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("====================================================");
  Serial.println(" CardioIA - Parte 2: transmissao MQTT + dashboard   ");
  Serial.println("====================================================");

  pinMode(PINO_BOTAO_BATIMENTO, INPUT_PULLUP);
  pinMode(PINO_CHAVE_WIFI, INPUT);
  pinMode(PINO_LED_NUVEM, OUTPUT);
  pinMode(PINO_LED_ALERTA, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(PINO_BOTAO_BATIMENTO), aoDetectarBatimento, FALLING);

  dht.setup(PINO_DHT, DHTesp::DHT22);

  conectarWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORTA);
  mqtt.setBufferSize(300);
  conectarMQTT();

  conectividadeSimulada = digitalRead(PINO_CHAVE_WIFI) == HIGH;
  ultimaLeituraMs = millis();
  Serial.println("Aperte o botao no ritmo do coracao. Use a chave para simular online/offline.");
}

void loop() {
  // Mantém Wi-Fi e MQTT vivos (sem travar a coleta)
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  conectarMQTT();
  mqtt.loop();
  atualizarConectividade();

  // Está "publicando na nuvem" quando: paciente online + MQTT conectado
  bool online = conectividadeSimulada && mqtt.connected();
  digitalWrite(PINO_LED_NUVEM, online ? HIGH : LOW);

  // Assim que reconecta, reenvia o que ficou pendente
  if (online && filaQtd > 0) esvaziarFila();

  // Coleta periódica
  if (millis() - ultimaLeituraMs >= INTERVALO_LEITURA_MS) {
    ultimaLeituraMs = millis();

    Leitura l = lerSensores();

    // LED de alerta decide na borda, sem depender da nuvem
    digitalWrite(PINO_LED_ALERTA, (l.flags & ALERTAS_CLINICOS) ? HIGH : LOW);

    char status[64];
    statusComoTexto(l.flags, status, sizeof(status));
    Serial.printf("[LEITURA] #%lu | %.1f C | %.1f %% | %u bpm | %s | %s\n",
                  (unsigned long)l.seq, l.temp, l.umid, l.bpm, status,
                  online ? "online" : "OFFLINE");

    // Online: publica agora. Offline: guarda na fila (reenviada ao reconectar).
    if (online) {
      if (!publicar(l, "live")) enfileirar(l);  // se o publish falhar, não perde
    } else {
      enfileirar(l);
    }
  }

  delay(10);
}

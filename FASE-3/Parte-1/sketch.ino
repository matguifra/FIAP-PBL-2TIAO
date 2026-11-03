/*
 * CardioIA, Fase 3, Parte 1: monitor cardíaco vestível com ESP32 (Edge Computing).
 *
 * A cada 5 s o ESP32 lê o DHT22 e calcula o BPM pelos apertos do botão. Cada leitura
 * é classificada aqui mesmo e gravada no SPIFFS antes de qualquer envio. Com a chave
 * Wi-Fi ligada, o arquivo local vai para a Serial (a nuvem simulada) e é apagado.
 *
 * Limite: 8.640 amostras, ou 12 h sem conexão. Quando lota, saem as 10% amostras
 * normais mais antigas e as leituras de alerta ficam.
 *
 * No Wokwi o SPIFFS some quando a simulação para. Se ele não montar, as leituras
 * vão para uma fila em RAM.
 *
 * Pinos: DHT22 15, botão 4, chave Wi-Fi 27, LED verde 25, LED vermelho 26.
 */

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "DHTesp.h"   // biblioteca "DHT sensor library for ESPx"

const int PINO_DHT             = 15;
const int PINO_BOTAO_BATIMENTO = 4;
const int PINO_CHAVE_WIFI      = 27;
const int PINO_LED_WIFI        = 25;
const int PINO_LED_ALERTA      = 26;

const uint32_t INTERVALO_LEITURA_MS     = 5000;
const uint32_t MAX_AMOSTRAS             = 8640;              // 12 h de leituras a cada 5 s
const uint32_t AMOSTRAS_POR_COMPACTACAO = MAX_AMOSTRAS / 10;
const float    USO_MAXIMO_FLASH         = 0.90;              // compacta também se a flash passar disso
const uint16_t LINHAS_POR_LOTE          = 20;                // lotes pequenos para não travar a coleta
const uint16_t MAX_AMOSTRAS_RAM         = 600;               // fila em RAM se o SPIFFS falhar (50 min)

// Limites clínicos definidos pelo grupo.
const float TEMP_FEBRE      = 38.0;  // °C
const float TEMP_HIPOTERMIA = 35.0;  // °C
const int   BPM_TAQUICARDIA = 120;
const int   BPM_BRADICARDIA = 50;

const uint32_t INTERVALO_MIN_BATIMENTO_MS = 250;   // abaixo disso é trepidação do botão (> 240 bpm)
const uint32_t INTERVALO_MAX_BATIMENTO_MS = 2000;  // pausa maior recomeça a média
const uint32_t TEMPO_SEM_PULSO_MS         = 10000;
const uint8_t  BATIMENTOS_NA_MEDIA        = 5;

const char* ARQ_PENDENTES = "/pendentes.csv";
const char* ARQ_ENVIANDO  = "/enviando.csv";  // lote congelado enquanto é enviado
const char* ARQ_TEMP      = "/temp.csv";      // usado na compactação
const char* CABECALHO_CSV = "seq,tempo_s,temp_c,umid_pct,bpm,status";

// Um bit por alerta, para uma leitura poder ter vários ao mesmo tempo.
const uint8_t ALERTA_FEBRE       = 1 << 0;
const uint8_t ALERTA_HIPOTERMIA  = 1 << 1;
const uint8_t ALERTA_TAQUICARDIA = 1 << 2;
const uint8_t ALERTA_BRADICARDIA = 1 << 3;
const uint8_t ALERTA_SEM_PULSO   = 1 << 4;
const uint8_t FALHA_DHT          = 1 << 5;  // falha técnica: não acende o LED de alerta
const uint8_t ALERTAS_CLINICOS   = ALERTA_FEBRE | ALERTA_HIPOTERMIA | ALERTA_TAQUICARDIA |
                                   ALERTA_BRADICARDIA | ALERTA_SEM_PULSO;

struct Leitura {
  uint32_t seq;      // a nuvem usa para descartar linhas repetidas
  uint32_t tempoS;   // segundos desde que o ESP32 ligou
  float    temp;     // °C
  float    umid;     // %
  uint16_t bpm;      // 0 = sem leitura
  uint8_t  flags;
};

DHTesp dht;

bool     spiffsOk         = false; // false = usando a fila em RAM
bool     wifiConectado    = false; // o Wi-Fi simulado; segue a chave deslizante
uint32_t ultimaLeituraMs  = 0;
uint32_t proximoSeq       = 1;
float    ultimaTempValida = NAN;
float    ultimaUmidValida = NAN;

// Contadores do arquivo pendente, para não reler o arquivo a cada leitura.
uint32_t pendentesTotal   = 0;
uint32_t pendentesNormais = 0;
uint32_t pendentesAlertas = 0;
uint32_t descartadas      = 0;  // amostras perdidas por falta de espaço

bool     enviando          = false;
bool     arquivoEnvioAberto = false;
File     arquivoEnvio;
size_t   posicaoEnvio      = 0;  // onde o envio parou se a conexão cair
uint32_t linhasEnviadas    = 0;

// Fila circular usada quando o SPIFFS não monta.
Leitura  bufferRam[MAX_AMOSTRAS_RAM];
uint16_t inicioRam = 0;
uint16_t qtdRam    = 0;

// Alteradas dentro da interrupção do botão.
volatile uint32_t ultimoBatimentoMs = 0;
volatile uint32_t intervalos[BATIMENTOS_NA_MEDIA];
volatile uint8_t  idxIntervalo      = 0;
volatile uint8_t  qtdIntervalos     = 0;
volatile bool     pulsoJaDetectado  = false;
portMUX_TYPE      travaBatimento    = portMUX_INITIALIZER_UNLOCKED;

// Roda a cada aperto do botão e guarda o intervalo desde o batimento anterior.
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
    qtdIntervalos = 0;                                   // pausa longa: recomeça a média
    idxIntervalo  = 0;
  }
  ultimoBatimentoMs = agora;
  pulsoJaDetectado  = true;
  portEXIT_CRITICAL_ISR(&travaBatimento);
}

// BPM = 60.000 ms dividido pela média dos últimos intervalos.
// semPulso fica true quando os batimentos pararam por mais de 10 s.
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
  if (millis() - ultimo > TEMPO_SEM_PULSO_MS) {
    semPulso = true;
    return 0;
  }
  if (qtd == 0) return 0;                         // só um batimento até agora, sem intervalo
  return (uint16_t)(60000UL / (soma / qtd));
}

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
    l.flags |= FALHA_DHT;  // repete o último valor válido
  }
  l.temp = ultimaTempValida;
  l.umid = ultimaUmidValida;

  bool semPulso;
  l.bpm = calcularBPM(semPulso);

  if (!isnan(l.temp) && l.temp >= TEMP_FEBRE)      l.flags |= ALERTA_FEBRE;
  if (!isnan(l.temp) && l.temp < TEMP_HIPOTERMIA)  l.flags |= ALERTA_HIPOTERMIA;
  if (l.bpm > BPM_TAQUICARDIA)                     l.flags |= ALERTA_TAQUICARDIA;
  if (l.bpm > 0 && l.bpm < BPM_BRADICARDIA)        l.flags |= ALERTA_BRADICARDIA;
  if (semPulso)                                    l.flags |= ALERTA_SEM_PULSO;
  return l;
}

// Gera "NORMAL" ou os alertas separados por "|", como "FEBRE|TAQUICARDIA".
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

void leituraComoCSV(const Leitura &l, char *saida, size_t tamanho) {
  char status[64];
  statusComoTexto(l.flags, status, sizeof(status));
  snprintf(saida, tamanho, "%lu,%lu,%.1f,%.1f,%u,%s",
           (unsigned long)l.seq, (unsigned long)l.tempoS, l.temp, l.umid, l.bpm, status);
}

bool linhaEhAlerta(const String &linha) {
  return !linha.endsWith(",NORMAL");
}

// Usado ao ligar: num ESP32 real os arquivos sobrevivem a um desligamento.
void examinarArquivo(const char *caminho, uint32_t &total, uint32_t &normais,
                     uint32_t &alertas, uint32_t &maiorSeq) {
  total = normais = alertas = 0;
  File f = SPIFFS.open(caminho, FILE_READ);
  if (!f) return;
  while (f.available()) {
    String linha = f.readStringUntil('\n');
    linha.trim();
    if (linha.length() == 0) continue;
    total++;
    if (linhaEhAlerta(linha)) alertas++; else normais++;
    uint32_t seq = linha.substring(0, linha.indexOf(',')).toInt();
    if (seq > maiorSeq) maiorSeq = seq;
  }
  f.close();
}

void inicializarArmazenamento() {
  spiffsOk = SPIFFS.begin(true);  // true = formata se for o primeiro uso
  if (!spiffsOk) {
    Serial.println("[ARMAZ] SPIFFS indisponível. Usando plano B: fila em RAM com " +
                   String(MAX_AMOSTRAS_RAM) + " amostras.");
    return;
  }

  // Se a energia caiu no meio de uma compactação, o temp.csv pode ser a única cópia.
  if (!SPIFFS.exists(ARQ_PENDENTES) && SPIFFS.exists(ARQ_TEMP)) {
    SPIFFS.rename(ARQ_TEMP, ARQ_PENDENTES);
    Serial.println("[ARMAZ] Compactação interrompida recuperada.");
  } else if (SPIFFS.exists(ARQ_TEMP)) {
    SPIFFS.remove(ARQ_TEMP);
  }

  uint32_t maiorSeq = 0;
  examinarArquivo(ARQ_PENDENTES, pendentesTotal, pendentesNormais, pendentesAlertas, maiorSeq);

  uint32_t tEnv, nEnv, aEnv;
  examinarArquivo(ARQ_ENVIANDO, tEnv, nEnv, aEnv, maiorSeq);
  if (tEnv > 0) {
    Serial.println("[ARMAZ] Envio interrompido encontrado (" + String(tEnv) +
                   " amostras). Será retomado quando houver conexão.");
  }
  proximoSeq = maiorSeq + 1;

  Serial.printf("[ARMAZ] SPIFFS montado: %u de %u bytes usados.\n",
                (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
  Serial.printf("[ARMAZ] Amostras pendentes de execuções anteriores: %lu\n",
                (unsigned long)pendentesTotal);
}

// Descarta as amostras normais mais antigas. Alertas só saem se faltarem normais.
void compactarArquivo() {
  uint32_t remover = min(AMOSTRAS_POR_COMPACTACAO, pendentesTotal);
  uint32_t cotaNormais = min(remover, pendentesNormais);
  uint32_t cotaAlertas = remover - cotaNormais;

  File origem  = SPIFFS.open(ARQ_PENDENTES, FILE_READ);
  File destino = SPIFFS.open(ARQ_TEMP, FILE_WRITE);
  if (!origem || !destino) {
    Serial.println("[ARMAZ] Erro ao abrir arquivos para compactação.");
    if (origem) origem.close();
    if (destino) destino.close();
    return;
  }

  uint32_t removidasN = 0, removidasA = 0;
  while (origem.available()) {
    String linha = origem.readStringUntil('\n');
    linha.trim();
    if (linha.length() == 0) continue;
    bool alerta = linhaEhAlerta(linha);
    if (!alerta && removidasN < cotaNormais) { removidasN++; continue; }
    if (alerta && removidasA < cotaAlertas)  { removidasA++; continue; }
    destino.println(linha);
  }
  origem.close();
  destino.close();

  SPIFFS.remove(ARQ_PENDENTES);
  SPIFFS.rename(ARQ_TEMP, ARQ_PENDENTES);

  pendentesNormais -= removidasN;
  pendentesAlertas -= removidasA;
  pendentesTotal   -= (removidasN + removidasA);
  descartadas      += (removidasN + removidasA);

  Serial.printf("[ARMAZ] Limite atingido. Compactação: %lu normais e %lu alertas antigos descartados.\n",
                (unsigned long)removidasN, (unsigned long)removidasA);
}

void armazenarLeitura(const Leitura &l) {
  if (!spiffsOk) {
    // Fila cheia: sobrescreve a leitura mais antiga.
    if (qtdRam == MAX_AMOSTRAS_RAM) {
      inicioRam = (inicioRam + 1) % MAX_AMOSTRAS_RAM;
      qtdRam--;
      descartadas++;
    }
    bufferRam[(inicioRam + qtdRam) % MAX_AMOSTRAS_RAM] = l;
    qtdRam++;
    return;
  }

  bool flashCheia = SPIFFS.usedBytes() > SPIFFS.totalBytes() * USO_MAXIMO_FLASH;
  if (pendentesTotal >= MAX_AMOSTRAS || flashCheia) compactarArquivo();

  char linha[96];
  leituraComoCSV(l, linha, sizeof(linha));
  File f = SPIFFS.open(ARQ_PENDENTES, FILE_APPEND);
  if (!f) {
    Serial.println("[ARMAZ] Erro ao gravar no SPIFFS.");
    return;
  }
  f.println(linha);
  f.close();

  pendentesTotal++;
  if (l.flags == 0) pendentesNormais++; else pendentesAlertas++;
}

uint32_t totalPendente() {
  return spiffsOk ? pendentesTotal : qtdRam;
}

void atualizarWifi() {
  bool estado = digitalRead(PINO_CHAVE_WIFI) == HIGH;
  if (estado != wifiConectado) {
    wifiConectado = estado;
    Serial.println(wifiConectado
      ? "[REDE] Wi-Fi CONECTADO. Pendentes: " + String(totalPendente())
      : String("[REDE] Wi-Fi DESCONECTADO. Leituras seguem sendo guardadas localmente."));
  }
  digitalWrite(PINO_LED_WIFI, wifiConectado ? HIGH : LOW);
}

void informarDescartes() {
  if (descartadas == 0) return;
  Serial.printf("[NUVEM] AVISO: %lu amostras foram descartadas por limite de armazenamento.\n",
                (unsigned long)descartadas);
  descartadas = 0;
}

void sincronizarRam() {
  if (qtdRam == 0) return;
  if (!enviando) {
    enviando = true;
    linhasEnviadas = 0;
    Serial.println("[SYNC] Iniciando envio da fila em RAM.");
    Serial.print("[NUVEM] "); Serial.println(CABECALHO_CSV);
  }
  char linha[96];
  for (uint16_t i = 0; i < LINHAS_POR_LOTE && qtdRam > 0; i++) {
    leituraComoCSV(bufferRam[inicioRam], linha, sizeof(linha));
    Serial.print("[NUVEM] "); Serial.println(linha);
    inicioRam = (inicioRam + 1) % MAX_AMOSTRAS_RAM;
    qtdRam--;
    linhasEnviadas++;
  }
  if (qtdRam == 0) {
    Serial.printf("[SYNC] Concluído: %lu amostras enviadas. Fila em RAM esvaziada.\n",
                  (unsigned long)linhasEnviadas);
    informarDescartes();
    enviando = false;
  }
}

// Envia em lotes para não travar a coleta. O pendentes.csv vira enviando.csv,
// e as leituras novas vão para um pendentes.csv novo enquanto o envio acontece.
void gerenciarSincronizacao() {
  if (!wifiConectado) {
    if (arquivoEnvioAberto) {
      posicaoEnvio = arquivoEnvio.position();
      arquivoEnvio.close();
      arquivoEnvioAberto = false;
      Serial.printf("[SYNC] Conexão perdida no meio do envio (%lu linhas já enviadas). "
                    "O restante será enviado quando a conexão voltar.\n",
                    (unsigned long)linhasEnviadas);
    }
    return;
  }

  if (!spiffsOk) { sincronizarRam(); return; }

  if (!enviando) {
    if (SPIFFS.exists(ARQ_ENVIANDO)) {
      posicaoEnvio = 0;  // sobrou de um reinício: reenvia desde o começo
    } else if (pendentesTotal > 0) {
      SPIFFS.rename(ARQ_PENDENTES, ARQ_ENVIANDO);
      pendentesTotal = pendentesNormais = pendentesAlertas = 0;
      posicaoEnvio = 0;
    } else {
      return;
    }
    enviando = true;
    linhasEnviadas = 0;
    Serial.print("[NUVEM] "); Serial.println(CABECALHO_CSV);
  }

  // Reabre na posição em que o envio parou, se a conexão caiu antes.
  if (!arquivoEnvioAberto) {
    arquivoEnvio = SPIFFS.open(ARQ_ENVIANDO, FILE_READ);
    if (!arquivoEnvio) { enviando = false; return; }
    arquivoEnvio.seek(posicaoEnvio);
    arquivoEnvioAberto = true;
    if (posicaoEnvio > 0) Serial.println("[SYNC] Retomando envio de onde parou.");
  }

  for (uint16_t i = 0; i < LINHAS_POR_LOTE && arquivoEnvio.available(); i++) {
    String linha = arquivoEnvio.readStringUntil('\n');
    linha.trim();
    if (linha.length() == 0) continue;
    Serial.print("[NUVEM] "); Serial.println(linha);  // na Parte 2, este envio vira MQTT
    linhasEnviadas++;
  }

  if (!arquivoEnvio.available()) {
    arquivoEnvio.close();
    arquivoEnvioAberto = false;
    SPIFFS.remove(ARQ_ENVIANDO);
    enviando = false;
    posicaoEnvio = 0;
    Serial.printf("[SYNC] Concluído: %lu amostras enviadas. Arquivo local apagado.\n",
                  (unsigned long)linhasEnviadas);
    informarDescartes();
  }
}

void imprimirLeitura(const Leitura &l) {
  char status[64];
  statusComoTexto(l.flags, status, sizeof(status));
  Serial.printf("[LEITURA] #%lu | %.1f C | %.1f %% | %u bpm | %s | %s | pendentes: %lu/%lu\n",
                (unsigned long)l.seq, l.temp, l.umid, l.bpm, status,
                wifiConectado ? "online" : "OFFLINE",
                (unsigned long)totalPendente(),
                (unsigned long)(spiffsOk ? MAX_AMOSTRAS : MAX_AMOSTRAS_RAM));
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("==============================================");
  Serial.println(" CardioIA - Monitor vestivel (Edge Computing) ");
  Serial.println("==============================================");
  Serial.printf("Leitura a cada %lu s | capacidade offline: %lu amostras (~%lu h)\n",
                (unsigned long)(INTERVALO_LEITURA_MS / 1000), (unsigned long)MAX_AMOSTRAS,
                (unsigned long)(MAX_AMOSTRAS * INTERVALO_LEITURA_MS / 3600000UL));
  Serial.printf("Alertas: temp >= %.1f C ou < %.1f C | bpm > %d ou < %d | sem pulso por %lu s\n",
                TEMP_FEBRE, TEMP_HIPOTERMIA, BPM_TAQUICARDIA, BPM_BRADICARDIA,
                (unsigned long)(TEMPO_SEM_PULSO_MS / 1000));
  Serial.println("Aperte o botao no ritmo do coracao. Use a chave para ligar/desligar o Wi-Fi.");

  pinMode(PINO_BOTAO_BATIMENTO, INPUT_PULLUP);
  pinMode(PINO_CHAVE_WIFI, INPUT);
  pinMode(PINO_LED_WIFI, OUTPUT);
  pinMode(PINO_LED_ALERTA, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(PINO_BOTAO_BATIMENTO), aoDetectarBatimento, FALLING);

  dht.setup(PINO_DHT, DHTesp::DHT22);
  inicializarArmazenamento();

  wifiConectado = !(digitalRead(PINO_CHAVE_WIFI) == HIGH); // força o aviso inicial
  atualizarWifi();
  ultimaLeituraMs = millis();  // primeira leitura em 5 s: o DHT22 precisa de ~2 s para estabilizar
}

void loop() {
  atualizarWifi();

  if (millis() - ultimaLeituraMs >= INTERVALO_LEITURA_MS) {
    ultimaLeituraMs = millis();

    Leitura l = lerSensores();
    armazenarLeitura(l);   // grava antes de tentar enviar
    imprimirLeitura(l);
    digitalWrite(PINO_LED_ALERTA, (l.flags & ALERTAS_CLINICOS) ? HIGH : LOW);
  }

  gerenciarSincronizacao();

  delay(10);
}

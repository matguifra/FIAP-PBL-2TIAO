/*
 * =============================================================================
 *  CardioIA — Fase 3, Parte 1: Armazenamento e processamento local
 *  (Edge Computing) em um monitor cardíaco vestível simulado com ESP32
 * =============================================================================
 *
 *  O que este programa faz:
 *    1. Lê dois sensores a cada 5 segundos:
 *         - DHT22: temperatura e umidade (sensor obrigatório);
 *         - Botão de pressão: cada aperto simula um batimento cardíaco, e o
 *           programa calcula os batimentos por minuto (BPM).
 *    2. Classifica cada leitura (normal, febre, taquicardia etc.) direto no
 *       dispositivo, sem depender da nuvem. Isso é o "processamento na borda".
 *    3. Guarda TODA leitura primeiro no armazenamento local (SPIFFS), em um
 *       arquivo CSV. Só depois ela é enviada. Esse padrão se chama
 *       "store-and-forward" (guarda e depois encaminha).
 *    4. Simula o Wi-Fi com uma variável booleana controlada por uma chave
 *       deslizante. Quando "conectado", envia o que estava guardado para a
 *       "nuvem" (Serial.println) e apaga o arquivo local.
 *    5. Garante resiliência offline: sem conexão, continua coletando e
 *       guardando; quando a conexão volta, sincroniza tudo o que ficou pendente.
 *
 *  Estratégia de armazenamento limitado (modelo de negócio):
 *    - Paciente cardiológico monitorado em casa por um vestível.
 *    - 1 leitura a cada 5 s = 720 leituras por hora.
 *    - Capacidade para 12 h sem conexão = 8.640 amostras (~285 KB em CSV).
 *    - Quando o limite é atingido, o arquivo é "compactado": as 10% amostras
 *      NORMAIS mais antigas são descartadas. Leituras com alerta são
 *      preservadas, porque são as que o médico mais precisa ver.
 *
 *  Observação sobre o simulador:
 *    No Wokwi o SPIFFS funciona durante a simulação, mas é apagado quando ela
 *    termina (em um ESP32 real ele persiste). Se o SPIFFS não puder ser
 *    montado, o programa passa a usar um buffer em memória RAM como plano B,
 *    e o Monitor Serial continua registrando tudo.
 *
 *  Componentes (veja diagram.json):
 *    DHT22 -> GPIO 15 | Botão "batimento" -> GPIO 4 | Chave Wi-Fi -> GPIO 27
 *    LED verde (Wi-Fi) -> GPIO 25 | LED vermelho (alerta) -> GPIO 26
 * =============================================================================
 */

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "DHTesp.h"   // biblioteca "DHT sensor library for ESPx"

// -----------------------------------------------------------------------------
// 1) PINOS
// -----------------------------------------------------------------------------
const int PINO_DHT             = 15;
const int PINO_BOTAO_BATIMENTO = 4;
const int PINO_CHAVE_WIFI      = 27;
const int PINO_LED_WIFI        = 25;
const int PINO_LED_ALERTA      = 26;

// -----------------------------------------------------------------------------
// 2) CONFIGURAÇÃO DA COLETA E DO ARMAZENAMENTO
// -----------------------------------------------------------------------------
const uint32_t INTERVALO_LEITURA_MS     = 5000;              // 1 leitura a cada 5 s
const uint32_t MAX_AMOSTRAS             = 8640;              // 12 h x 720 leituras/h
const uint32_t AMOSTRAS_POR_COMPACTACAO = MAX_AMOSTRAS / 10; // libera 10% quando lota
const float    USO_MAXIMO_FLASH         = 0.90;              // compacta também se a flash passar de 90%
const uint16_t LINHAS_POR_LOTE          = 20;                // linhas enviadas por volta do loop
const uint16_t MAX_AMOSTRAS_RAM         = 600;               // plano B: 50 min em RAM

// -----------------------------------------------------------------------------
// 3) LIMITES CLÍNICOS (definidos pelo grupo)
// -----------------------------------------------------------------------------
const float TEMP_FEBRE      = 38.0;  // °C  — febre
const float TEMP_HIPOTERMIA = 35.0;  // °C  — hipotermia
const int   BPM_TAQUICARDIA = 120;   // bpm — acima disso: taquicardia
const int   BPM_BRADICARDIA = 50;    // bpm — abaixo disso: bradicardia

// -----------------------------------------------------------------------------
// 4) PARÂMETROS DO CÁLCULO DE BPM (botão de pressão)
// -----------------------------------------------------------------------------
const uint32_t INTERVALO_MIN_BATIMENTO_MS = 250;   // < 250 ms (> 240 bpm) = ruído/trepidação do botão
const uint32_t INTERVALO_MAX_BATIMENTO_MS = 2000;  // > 2 s entre apertos = pausa, recomeça a média
const uint32_t TEMPO_SEM_PULSO_MS         = 10000; // 10 s sem batimento = alerta "sem pulso"
const uint8_t  BATIMENTOS_NA_MEDIA        = 5;     // média dos últimos 5 intervalos

// -----------------------------------------------------------------------------
// 5) ARQUIVOS NO SPIFFS
// -----------------------------------------------------------------------------
const char* ARQ_PENDENTES = "/pendentes.csv"; // leituras ainda não enviadas
const char* ARQ_ENVIANDO  = "/enviando.csv";  // lote "congelado" que está sendo enviado
const char* ARQ_TEMP      = "/temp.csv";      // arquivo auxiliar da compactação
const char* CABECALHO_CSV = "seq,tempo_s,temp_c,umid_pct,bpm,status";

// -----------------------------------------------------------------------------
// 6) CÓDIGOS DE ALERTA (cada bit representa uma condição)
// -----------------------------------------------------------------------------
const uint8_t ALERTA_FEBRE       = 1 << 0;
const uint8_t ALERTA_HIPOTERMIA  = 1 << 1;
const uint8_t ALERTA_TAQUICARDIA = 1 << 2;
const uint8_t ALERTA_BRADICARDIA = 1 << 3;
const uint8_t ALERTA_SEM_PULSO   = 1 << 4;
const uint8_t FALHA_DHT          = 1 << 5;  // falha técnica, não clínica
const uint8_t ALERTAS_CLINICOS   = ALERTA_FEBRE | ALERTA_HIPOTERMIA | ALERTA_TAQUICARDIA |
                                   ALERTA_BRADICARDIA | ALERTA_SEM_PULSO;

// Uma leitura completa dos sensores
struct Leitura {
  uint32_t seq;      // número sequencial (permite à nuvem descartar duplicatas)
  uint32_t tempoS;   // segundos desde que o dispositivo ligou
  float    temp;     // °C
  float    umid;     // %
  uint16_t bpm;      // batimentos por minuto (0 = sem leitura)
  uint8_t  flags;    // combinação dos códigos de alerta acima
};

// -----------------------------------------------------------------------------
// 7) ESTADO GLOBAL
// -----------------------------------------------------------------------------
DHTesp dht;

bool     spiffsOk         = false; // false = usando o plano B em RAM
bool     wifiConectado    = false; // a "variável booleana" que simula o Wi-Fi
uint32_t ultimaLeituraMs  = 0;
uint32_t proximoSeq       = 1;
float    ultimaTempValida = NAN;
float    ultimaUmidValida = NAN;

// Contadores do arquivo de pendentes (evitam reler o arquivo a cada leitura)
uint32_t pendentesTotal   = 0;
uint32_t pendentesNormais = 0;
uint32_t pendentesAlertas = 0;
uint32_t descartadas      = 0;  // amostras perdidas por falta de espaço

// Controle do envio em lotes
bool     enviando          = false;
bool     arquivoEnvioAberto = false;
File     arquivoEnvio;
size_t   posicaoEnvio      = 0;  // onde o envio parou, se a conexão cair no meio
uint32_t linhasEnviadas    = 0;

// Plano B: fila circular em RAM
Leitura  bufferRam[MAX_AMOSTRAS_RAM];
uint16_t inicioRam = 0;
uint16_t qtdRam    = 0;

// Variáveis usadas pela interrupção do botão (volatile = podem mudar a qualquer momento)
volatile uint32_t ultimoBatimentoMs = 0;
volatile uint32_t intervalos[BATIMENTOS_NA_MEDIA];
volatile uint8_t  idxIntervalo      = 0;
volatile uint8_t  qtdIntervalos     = 0;
volatile bool     pulsoJaDetectado  = false;
portMUX_TYPE      travaBatimento    = portMUX_INITIALIZER_UNLOCKED;

// =============================================================================
// BATIMENTOS: interrupção do botão e cálculo de BPM
// =============================================================================

// Executa a cada aperto do botão. Guarda o tempo entre um batimento e outro.
void IRAM_ATTR aoDetectarBatimento() {
  uint32_t agora = millis();
  portENTER_CRITICAL_ISR(&travaBatimento);
  uint32_t dt = agora - ultimoBatimentoMs;

  // Ignora apertos rápidos demais (trepidação mecânica do botão)
  if (pulsoJaDetectado && dt < INTERVALO_MIN_BATIMENTO_MS) {
    portEXIT_CRITICAL_ISR(&travaBatimento);
    return;
  }

  if (pulsoJaDetectado && dt <= INTERVALO_MAX_BATIMENTO_MS) {
    intervalos[idxIntervalo] = dt;                       // intervalo válido entra na média
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

// Calcula o BPM pela média dos últimos intervalos entre batimentos.
// semPulso = true quando já houve batimentos, mas eles pararam há mais de 10 s.
uint16_t calcularBPM(bool &semPulso) {
  portENTER_CRITICAL(&travaBatimento);
  bool     detectado = pulsoJaDetectado;
  uint32_t ultimo    = ultimoBatimentoMs;
  uint8_t  qtd       = qtdIntervalos;
  uint32_t soma      = 0;
  for (uint8_t i = 0; i < qtd; i++) soma += intervalos[i];
  portEXIT_CRITICAL(&travaBatimento);

  semPulso = false;
  if (!detectado) return 0;                       // ainda aguardando o primeiro batimento
  if (millis() - ultimo > TEMPO_SEM_PULSO_MS) {   // batimentos pararam
    semPulso = true;
    return 0;
  }
  if (qtd == 0) return 0;                         // só 1 batimento até agora
  return (uint16_t)(60000UL / (soma / qtd));      // 60.000 ms / intervalo médio
}

// =============================================================================
// LEITURA E CLASSIFICAÇÃO (processamento na borda)
// =============================================================================

Leitura lerSensores() {
  Leitura l;
  l.seq    = proximoSeq++;
  l.tempoS = millis() / 1000;
  l.flags  = 0;

  // --- DHT22: temperatura e umidade ---
  TempAndHumidity dados = dht.getTempAndHumidity();
  if (dht.getStatus() == DHTesp::ERROR_NONE && !isnan(dados.temperature)) {
    ultimaTempValida = dados.temperature;
    ultimaUmidValida = dados.humidity;
  } else {
    l.flags |= FALHA_DHT;  // mantém o último valor válido e marca a falha
  }
  l.temp = ultimaTempValida;
  l.umid = ultimaUmidValida;

  // --- Botão: batimentos por minuto ---
  bool semPulso;
  l.bpm = calcularBPM(semPulso);

  // --- Regras de alerta ---
  if (!isnan(l.temp) && l.temp >= TEMP_FEBRE)      l.flags |= ALERTA_FEBRE;
  if (!isnan(l.temp) && l.temp < TEMP_HIPOTERMIA)  l.flags |= ALERTA_HIPOTERMIA;
  if (l.bpm > BPM_TAQUICARDIA)                     l.flags |= ALERTA_TAQUICARDIA;
  if (l.bpm > 0 && l.bpm < BPM_BRADICARDIA)        l.flags |= ALERTA_BRADICARDIA;
  if (semPulso)                                    l.flags |= ALERTA_SEM_PULSO;
  return l;
}

// Converte os bits de alerta em texto. Ex.: "FEBRE|TAQUICARDIA" ou "NORMAL".
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

// Monta a linha CSV: seq,tempo_s,temp_c,umid_pct,bpm,status
void leituraComoCSV(const Leitura &l, char *saida, size_t tamanho) {
  char status[64];
  statusComoTexto(l.flags, status, sizeof(status));
  snprintf(saida, tamanho, "%lu,%lu,%.1f,%.1f,%u,%s",
           (unsigned long)l.seq, (unsigned long)l.tempoS, l.temp, l.umid, l.bpm, status);
}

// Uma linha do CSV é "alerta" quando o status não é NORMAL
bool linhaEhAlerta(const String &linha) {
  return !linha.endsWith(",NORMAL");
}

// =============================================================================
// ARMAZENAMENTO LOCAL (SPIFFS) E POLÍTICA DE LIMITE
// =============================================================================

// Conta as linhas de um arquivo e descobre o maior número de sequência gravado.
// Usado no boot: em um ESP32 real, o arquivo sobrevive a um desligamento.
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

  // Recuperação: se a energia caiu no meio de uma compactação, o arquivo
  // temporário pode ser o único com os dados. Nesse caso ele é restaurado.
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

// Libera espaço descartando as amostras NORMAIS mais antigas.
// Alertas só são descartados se não houver amostras normais suficientes.
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
    if (!alerta && removidasN < cotaNormais) { removidasN++; continue; } // normal antiga: sai
    if (alerta && removidasA < cotaAlertas)  { removidasA++; continue; } // só em último caso
    destino.println(linha);                                               // o resto fica
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

// Guarda uma leitura localmente (SPIFFS ou, no plano B, RAM).
void armazenarLeitura(const Leitura &l) {
  if (!spiffsOk) {
    // Plano B: fila circular. Cheia = sobrescreve a mais antiga.
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

// =============================================================================
// CONECTIVIDADE SIMULADA E SINCRONIZAÇÃO COM A "NUVEM"
// =============================================================================

// Lê a chave deslizante e atualiza a variável booleana wifiConectado.
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

// Aviso enviado à nuvem quando houve descarte por falta de espaço
void informarDescartes() {
  if (descartadas == 0) return;
  Serial.printf("[NUVEM] AVISO: %lu amostras foram descartadas por limite de armazenamento.\n",
                (unsigned long)descartadas);
  descartadas = 0;
}

// Plano B: envia a fila da RAM, das mais antigas para as mais novas
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

// Envia o arquivo local em lotes, sem travar a coleta.
// 1) "Congela" o arquivo de pendentes renomeando-o para enviando.csv;
//    novas leituras continuam indo para um pendentes.csv novo.
// 2) Envia LINHAS_POR_LOTE linhas por volta do loop.
// 3) Ao terminar, apaga o arquivo local.
// Se a conexão cair no meio, guarda a posição e continua de onde parou.
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

  // Começa um novo envio, se houver o que enviar
  if (!enviando) {
    if (SPIFFS.exists(ARQ_ENVIANDO)) {
      posicaoEnvio = 0;  // envio interrompido por reinício: recomeça o arquivo
    } else if (pendentesTotal > 0) {
      SPIFFS.rename(ARQ_PENDENTES, ARQ_ENVIANDO);
      pendentesTotal = pendentesNormais = pendentesAlertas = 0;
      posicaoEnvio = 0;
    } else {
      return;  // nada pendente
    }
    enviando = true;
    linhasEnviadas = 0;
    Serial.print("[NUVEM] "); Serial.println(CABECALHO_CSV);
  }

  // Abre (ou reabre) o arquivo na posição em que o envio parou
  if (!arquivoEnvioAberto) {
    arquivoEnvio = SPIFFS.open(ARQ_ENVIANDO, FILE_READ);
    if (!arquivoEnvio) { enviando = false; return; }
    arquivoEnvio.seek(posicaoEnvio);
    arquivoEnvioAberto = true;
    if (posicaoEnvio > 0) Serial.println("[SYNC] Retomando envio de onde parou.");
  }

  // Envia um lote
  for (uint16_t i = 0; i < LINHAS_POR_LOTE && arquivoEnvio.available(); i++) {
    String linha = arquivoEnvio.readStringUntil('\n');
    linha.trim();
    if (linha.length() == 0) continue;
    Serial.print("[NUVEM] "); Serial.println(linha);  // aqui, na Parte 2, entra o MQTT
    linhasEnviadas++;
  }

  // Terminou? Apaga o arquivo local.
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

// =============================================================================
// SAÍDA NO MONITOR SERIAL
// =============================================================================

void imprimirLeitura(const Leitura &l) {
  char status[64];
  statusComoTexto(l.flags, status, sizeof(status));
  Serial.printf("[LEITURA] #%lu | %.1f C | %.1f %% | %u bpm | %s | %s | pendentes: %lu/%lu\n",
                (unsigned long)l.seq, l.temp, l.umid, l.bpm, status,
                wifiConectado ? "online" : "OFFLINE",
                (unsigned long)totalPendente(),
                (unsigned long)(spiffsOk ? MAX_AMOSTRAS : MAX_AMOSTRAS_RAM));
}

// =============================================================================
// SETUP E LOOP
// =============================================================================

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
  ultimaLeituraMs = millis();  // primeira leitura em 5 s (o DHT22 precisa de ~2 s para estabilizar)
}

void loop() {
  atualizarWifi();

  // 1) Coleta periódica: lê, classifica, guarda localmente
  if (millis() - ultimaLeituraMs >= INTERVALO_LEITURA_MS) {
    ultimaLeituraMs = millis();

    Leitura l = lerSensores();
    armazenarLeitura(l);   // sempre guarda primeiro (store-and-forward)
    imprimirLeitura(l);
    digitalWrite(PINO_LED_ALERTA, (l.flags & ALERTAS_CLINICOS) ? HIGH : LOW);
  }

  // 2) Se houver conexão, envia o que estiver pendente (em lotes)
  gerenciarSincronizacao();

  delay(10);
}

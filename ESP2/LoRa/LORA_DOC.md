# LoRa – Pasta `LoRa/`

> **Versão:** 1.0.0
> **Data:** 2026-04-27
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O módulo `LoRa` é responsável por **transmitir dados do ESP2 para a Ground Station** via rádio LoRa na frequência de **868MHz** (banda ISM europeia).

**DIFERENÇA CRÍTICA PARA O ESP1:**

| Componente | LoRa 2.4GHz              | LoRa 868MHz            |
| -----------|--------------------------|------------------------|
| **ESP1**   | ✅ RECEBE comandos da GS | ❌ Não utiliza         |
| **ESP2**   | ❌ Não utiliza           | ✅ TRANSMITE para a GS |

**O ESP2 NUNCA recebe nada diretamente da GS.** Toda a comunicação incoming (comandos) chega via ESP1 através da UART.

---

## 2. Arquitetura

### 2.1 Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP2                                                                            │
│                                                                                 │
│ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐                 │
│ │ Telemetry   │ │ Video       │ │ Shell       │ │ Failsafe    │                 │
│ └──────┬──────┘ └──────┬──────┘ └──────┬──────┘ └──────┬──────┘                 │
│        │               │               │               │                        |
│ └──────────────────────────────┼──────────────────────────────┘                 │
│                                │                                                │
│                                ▼                                                │
│ ┌─────────────────────────────────┐                                             │
│ │ LoRa::send(priority)            │                                             │
│ │ (adiciona à fila prioritária)   │                                             │
│ └─────────────────────────────────┘                                             │
│ │                                                                               │
│ ▼                                                                               │
│ ┌─────────────────────────────────┐                                             │
│ │ Fila de Prioridades             │                                             │
│ │ (até 8 pacotes, ordenados por   │                                             │
│ │ prioridade decrescente)         │                                             │
│ └─────────────────────────────────┘                                             │
│ │                                                                               │
│ ▼ (LoRa::transmit)                                                              │
│ ┌─────────────────────────────────┐                                             │
│ │ Driver LoRa (SX1262)            │                                             │
│ │ • Configuração RF               │                                             │
│ │ • Transmissão física            │                                             │
│ │ • Estatísticas                  │                                             │
│ └─────────────────────────────────┘                                             │
│ │                                                                               │
│ ▼ (TX apenas)                                                                   │
│ ┌─────────────────────┐                                                         │
│ │ LoRa 868MHz         │                                                         │
│ │ → Ground Station    │                                                         │
│ └─────────────────────┘                                                         │
└─────────────────────────────────────────────────────────────────────────────────┘

### 2.2 Fila de Prioridades

O módulo implementa uma **fila circular com prioridade** para evitar perda de pacotes em situações de congestionamento.

| Característica | Valor                                            |
| ---------------|--------------------------------------------------|
| Tamanho máximo | 8 pacotes                                        |
| Ordenação      | Por prioridade (menor número = maior prioridade) |
| Política       | Pacote de maior prioridade é enviado primeiro    |

---

## 3. Prioridades de Transmissão

| Prioridade | Valor | Constante                | Descrição                           |
| -----------|-------|--------------------------|-------------------------------------|
| 1          | 0     | `LORA_PRIORITY_FAILSAFE` | Mensagens críticas de emergência    |
| 2          | 1     | `LORA_PRIORITY_COMMAND`  | Respostas a comandos da GS (ACKs)   |
| 3          | 2     | `LORA_PRIORITY_TELEMETRY`| Dados de voo em tempo real          |
| 4          | 3     | `LORA_PRIORITY_SHELL`    | Respostas de comandos shell remoto  |
| 5          | 4     | `LORA_PRIORITY_VIDEO`    | Stream de vídeo (prova de conceito) |
| 6          | 5     | `LORA_PRIORITY_DEBUG`    | Mensagens de debug                  |

### Comportamento em Congestionamento

- Se a fila estiver cheia (`queueCount >= LORA_TX_QUEUE_SIZE`), o pacote de **menor prioridade** é descartado
- Em caso de empate de prioridade, o mais antigo permanece

---

## 4. Módulos LoRa Suportados

| Módulo    | Constante           | Status                  | Observações
| ----------|---------------------|-------------------------|
| **SX1262**| `LORA_MODULE_SX1262`| ✅ Suportado            | Recomendado. Maior alcance, menor consumo
| **SX1276**| `LORA_MODULE_SX1276`| ✅ Suportado            | Fallback. Mais comum, ligeiramente menos eficiente
| **RAK811**| `LORA_MODULE_RAK811`| 🚧 Em desenvolvimento   | Módulo baseado em SX1262
| **Custom**| `LORA_MODULE_CUSTOM`| 🔧 Sob consulta         | Para drivers personalizados

---

## 5. Configuração RF

### 5.1 Parâmetros Padrão

| Parâmetro             | Valor          | Descrição                             |
| ----------------------|----------------|---------------------------------------|
| **Frequência**        | 868.000.000 Hz | Banda ISM europeia (868 MHz)          |
| **Potência TX**       | 20 dBm         | Máximo permitido (100 mW)             |
| **Spreading Factor**  | 9              | Equilíbrio entre alcance e velocidade |
| **Largura de banda**  | 125 kHz        | Padrão para Long Range                |
| **Coding Rate**       | 4/8            | Mais robusto para voo em movimento    |
| **CRC**               | Ativado        | Deteção de erros de transmissão       |

### 5.2 Configuração Recomendada para Voo

```cpp
LoRaConfig config;
config.frequency = 868000000;      // 868 MHz
config.txPower = 20;               // 20 dBm (máximo)
config.spreadingFactor = 9;        // SF9
config.bandwidth = 125000;         // 125 kHz
config.codingRate = 8;             // 4/8
config.crcEnabled = true;
´´´

5.3 Configuração para Testes em Solo (maior velocidade)

```cpp
LoRaConfig config;
config.frequency = 868000000;
config.txPower = 14;               // Potência reduzida
config.spreadingFactor = 7;        // SF7 (mais rápido)
config.bandwidth = 250000;         // 250 kHz (maior largura)
config.codingRate = 5;             // 4/5 (menos robusto)
config.crcEnabled = true;
´´´

6. Pinos de Hardware

6.1 Para SX1262 (Recomendado)

Pino            GPIO    Descrição
LORA_NSS_PIN    5       Chip Select (CS) - SPI
LORA_DIO1_PIN   4       DIO1 - Interrupção
LORA_RESET_PIN  2       Reset do módulo
LORA_BUSY_PIN   15      Busy (indica estado do módulo)

6.2 Para SX1276 (Fallback)
Pino            GPIO    Descrição
LORA_NSS_PIN    5       Chip Select (CS) - SPI
LORA_DIO0_PIN   4       DIO0 - Interrupção
LORA_RESET_PIN  2       Reset do módulo
Nota: Os pinos podem ser alterados no ficheiro LoRa.h conforme a PCB.

7. API Pública

7.1 Inicialização

```cpp
#include "LoRa.h"

LoRa lora;

void setup() {
    LoRaConfig config;
    config.frequency = 868000000;
    config.txPower = 20;
    config.spreadingFactor = 9;
    config.bandwidth = 125000;
    config.codingRate = 8;
    config.crcEnabled = true;
    
    if (lora.begin(LORA_MODULE_SX1262, config)) {
        Serial.println("LoRa inicializado com sucesso");
    } else {
        Serial.println("Erro ao inicializar LoRa");
    }
    
    lora.setDebug(true);
}
´´´

7.2 Transmissão (com prioridade)

```cpp
void loop() {
    // Preparar dados
    uint8_t buffer[256];
    size_t len = prepareTelemetryData(buffer);
    
    // Enviar com prioridade de telemetria
    lora.send(buffer, len, LORA_PRIORITY_TELEMETRY);
    
    // Processar fila (chamar frequentemente)
    lora.transmit();
}
´´´

7.3 Transmissão Imediata (ignora fila)

```cpp
// Para mensagens críticas (ex: failsafe)
uint8_t alert[] = {0xAA, 0x14, 0x01, 0x00};
lora.sendImmediate(alert, sizeof(alert));
´´´

7.4 Configuração Dinâmica

```cpp
// Mudar frequência durante o voo (evitar interferência)
lora.setFrequency(868500000);  // 868.5 MHz

// Reduzir potência para testes em solo
lora.setTxPower(14);  // 14 dBm
´´´

7.5 Consulta de Estado

```cpp
// Verificar se está pronto
if (lora.isReady()) {
    // Módulo disponível
}

// Obter status
LoRaStatus status = lora.getStatus();

// Obter estatísticas
const LoRaStats& stats = lora.getStats();
Serial.printf("Pacotes enviados: %lu\n", stats.packetsSent);
Serial.printf("Falhas: %lu\n", stats.packetsFailed);
Serial.printf("Overflows: %lu\n", stats.queueOverflows);
7.6 Gestão da Fila
cpp
// Limpar a fila (descartar todos os pacotes pendentes)
lora.clearQueue();

// Resetar estatísticas
lora.resetStats();
´´´

8. Exemplo de Integração com Scheduler

```cpp
#include "LoRa.h"
#include "Telemetry.h"
#include "Failsafe.h"

LoRa lora;
Telemetry telemetry;
Failsafe failsafe;

void setup() {
    LoRaConfig config = LoRaConfig::defaultConfig();
    lora.begin(LORA_MODULE_SX1262, config);
    lora.setDebug(true);
}

void loop() {
    // 1. Verificar failsafe (prioridade máxima)
    if (failsafe.isActive()) {
        uint8_t alert[32];
        size_t len = prepareFailsafePacket(alert);
        lora.send(alert, len, LORA_PRIORITY_FAILSAFE);
    }
    
    // 2. Enviar telemetria
    uint8_t telemetryBuffer[256];
    size_t telemetryLen = telemetry.buildPacket(telemetryBuffer, sizeof(telemetryBuffer));
    lora.send(telemetryBuffer, telemetryLen, LORA_PRIORITY_TELEMETRY);
    
    // 3. Processar fila (enviar pacotes)
    lora.transmit();
    
    delay(10);
}
´´´

9. Estatísticas (LoRaStats)

Campo               Descrição
packetsSent         Total de pacotes enviados com sucesso
packetsFailed       Pacotes cujo envio falhou (problema de hardware/RF)
queueOverflows      Pacotes perdidos por fila cheia
lastRSSI            RSSI do último pacote (se módulo suportar receção)
lastSNR             SNR do último pacote (se módulo suportar receção)
lastTransmitTime    Timestamp da última transmissão (ms)
queueSize           Tamanho atual da fila (0-8)

10. Debug

10.1 Ativação

```cpp
lora.setDebug(true);
´´´

10.2 Mensagens Típicas

[LoRa] begin() — Inicializando módulo LoRa 868MHz...
[LoRa] _initSX1262() — Inicializando SX1262...
[LoRa] _initSX1262() — Pinos: CS=5, DIO1=4, RESET=2, BUSY=15
[LoRa] _initSX1262() — SX1262 inicializado com sucesso
[LoRa] begin() — Módulo LoRa inicializado com sucesso
[LoRa]   Frequência: 868000000 Hz
[LoRa]   Potência: 20 dBm
[LoRa]   Spreading Factor: 9
[LoRa]   Bandwidth: 125000 Hz
[LoRa]   Coding Rate: 4/8

[LoRa] _enqueue() — Pacote adicionado à fila (prio=2, size=128)
[LoRa] _sendPacket() — Pacote enviado: 128 bytes
[LoRa] _dequeue() — Pacote removido da fila (prio=2, restam=0)

10.3 Erros Comuns

Mensagem                Causa                                               Solução
radio.begin() falhou    Módulo não conectado ou alimentação insuficiente    Verificar fiação e alimentação
Fila cheia              Congestionamento de transmissão                     Aumentar LORA_TX_QUEUE_SIZE ou reduzir taxa
Falha no envio          Interferência ou módulo ocupado                     Tentar novamente

11. Performance

Métrica                                 Valor       Nota
Taxa de transmissão (SF9, BW125)        ~1.8 kbps   Alcance máximo
Taxa de transmissão (SF7, BW250)        ~10.9 kbps  Alcance reduzido
Tempo de transmissão (32 bytes, SF9)    ~150 ms     -
Tempo de transmissão (128 bytes, SF9)   ~450 ms     -
Tamanho máximo do pacote                256 bytes   Limitado pelo buffer
Tamanho da fila                         8 pacotes   ~2 KB de RAM

12. Exemplo Completo

```cpp
#include "LoRa.h"
#include "Telemetry.h"

LoRa lora;
Telemetry telemetry;

void setup() {
    Serial.begin(115200);
    
    LoRaConfig config;
    config.frequency = 868000000;
    config.txPower = 20;
    config.spreadingFactor = 9;
    config.bandwidth = 125000;
    config.codingRate = 8;
    config.crcEnabled = true;
    
    if (lora.begin(LORA_MODULE_SX1262, config)) {
        Serial.println("LoRa OK");
    } else {
        Serial.println("LoRa FALHOU");
        while(1);
    }
    
    lora.setDebug(true);
    telemetry.begin();
}

void loop() {
    static uint32_t lastSend = 0;
    uint32_t now = millis();
    
    // Enviar telemetria a 10 Hz
    if (now - lastSend >= 100) {
        uint8_t buffer[256];
        size_t len = telemetry.buildPacket(buffer, sizeof(buffer));
        lora.send(buffer, len, LORA_PRIORITY_TELEMETRY);
        lastSend = now;
    }
    
    // Processar fila
    lora.transmit();
    
    delay(10);
}
´´´

Fim da documentação – LoRa v1.0.0

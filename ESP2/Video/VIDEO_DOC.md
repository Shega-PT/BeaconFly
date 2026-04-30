# Video – Pasta `Video/` (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-29
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O módulo `Video` é responsável por **processar e transmitir vídeo do ESP2 para a Ground Station** via LoRa 868MHz.

**NOTA IMPORTANTE:** O vídeo é uma funcionalidade de **PROVA DE CONCEITO (POC)**. Não é crítica para a operação do UAS. Perdas de pacote são aceitáveis e esperadas devido às limitações de largura de banda do LoRa.

---

## 2. Funcionalidades

| Funcionalidade          | Descrição                                               |
| ------------------------|---------------------------------------------------------|
| **Recepção de frames**  | Recebe frames de vídeo do ESP1 via UART                 |
| **Fragmentação**        | Divide frames grandes em chunks pequenos (max 128 bytes)|
| **Fila de transmissão** | Organiza chunks por ordem de chegada                    |
| **Envio LoRa**          | Transmite chunks via LoRa 868MHz com prioridade baixa   |
| **Estatísticas**        | Monitoriza chunks enviados, descartados e falhados      |

---

## 3. Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP1                                                                            │
│                                                                                 │
│ Câmera → Compressão → Frame → UART →                                            │
└─────────────────────────────────────────────────────────────────────────────────┘
│
▼ (UART RX)
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP2                                                                            │
│                                                                                 │
│ ┌─────────────────────────────────────────────────────────────────────┐         │
│ │ Video::processFrame()                                               │         │
│ │                                                                     │         │
│ │ Frame (4096 bytes) → Fragmentação em chunks (max 128 bytes)         │         │
│ │ • Chunk 0: frameID=1, chunkID=0, data[0-127]                        │         │
│ │ • Chunk 1: frameID=1, chunkID=1, data[128-255]                      │         │
│ │ • Chunk 2: frameID=1, chunkID=2, data[256-383]                      │         │
│ │ • ... (32 chunks para um frame de 4096 bytes)                       │         │
│ └─────────────────────────────────────────────────────────────────────┘         │
│ │                                                                               │
│ ▼                                                                               │
│ ┌─────────────────────────────────────┐                                         │
│ │ Fila de Transmissão                 │                                         │
│ │ (até 50 chunks, FIFO)               │                                         │
│ └─────────────────────────────────────┘                                         │
│ │                                                                               │
│ ▼                                                                               │
│ ┌──────────────────────────────────────┐                                        │
│ │ Video::sendNext()                    │                                        │
│ │ • Envia próximo chunk da fila        │                                        │
│ │ • Prioridade: LORA_PRIORITY_VIDEO (4)│                                        │
│ └──────────────────────────────────────┘                                        │
│ │                                                                               │
│ ▼                                                                               │
│ LoRa 868MHz → GS                                                                │
└─────────────────────────────────────────────────────────────────────────────────┘

---

## 4. Protocolo de Vídeo (TLV)

A mensagem `MSG_VIDEO` (ID=0x16) contém os seguintes campos TLV:

| Campo                | ID (hex) | Tipo      | Tamanho       | Descrição                 |
| ---------------------|----------|-----------|---------------|---------------------------|
| `FLD_VIDEO_FRAME_ID` | 0xB0     | uint16_t  | 2 bytes       | Número do frame (0-65535) |
| `FLD_VIDEO_CHUNK_ID` | 0xB1     | uint8_t   | 1 byte        | Índice do chunk (0-255)   |
| `FLD_VIDEO_TOTAL`    | 0xB2     | uint8_t   | 1 byte        | Total de chunks no frame  |
| `FLD_VIDEO_PAYLOAD`  | 0xB3     | uint8_t[] | até 128 bytes | Dados do vídeo            |

### Exemplo de Pacote

```text
START_BYTE: 0xAA
MSG_ID: 0x16 (MSG_VIDEO)
TLV_COUNT: 4

TLV1: ID=0xB0 LEN=2 DATA=[0x00, 0x01] (frameID=1)
TLV2: ID=0xB1 LEN=1 DATA=[0x00] (chunkID=0)
TLV3: ID=0xB2 LEN=1 DATA=[0x20] (totalChunks=32)
TLV4: ID=0xB3 LEN=128 DATA=[...payload...]

CRC8: 0xXX
´´´
---

## 5. Fragmentação de Frames

| Parâmetro              | Valor      | Descrição                                   |
| -----------------------|------------|---------------------------------------------|
| `VIDEO_MAX_CHUNK_SIZE` | 128 bytes  | Tamanho máximo de cada chunk (limite TLV)   |
| `VIDEO_MAX_FRAME_SIZE` | 4096 bytes | Tamanho máximo do frame (4KB)               |
| `VIDEO_MAX_QUEUE_SIZE` | 50 chunks  | Máximo de chunks na fila                    |

### Cálculo do Número de Chunks

NumChunks = ceil(frameSize / MAX_CHUNK_SIZE)

Exemplo:

- Frame de 500 bytes → 4 chunks (128, 128, 128, 116)
- Frame de 1024 bytes → 8 chunks (8 × 128)
- Frame de 4096 bytes → 32 chunks (32 × 128)

---

## 6. Prioridade de Envio

| Prioridade                | Valor | Descrição                 |
| --------------------------|-------|---------------------------|
| `LORA_PRIORITY_FAILSAFE`  | 0     | Emergência                |
| `LORA_PRIORITY_COMMAND`   | 1     | Respostas a comandos      |
| `LORA_PRIORITY_TELEMETRY` | 2     | Telemetria normal         |
| `LORA_PRIORITY_SHELL`     | 3     | Respostas shell           |
| **`LORA_PRIORITY_VIDEO`** | **4** | **Vídeo (esta módulo)**   |
| `LORA_PRIORITY_DEBUG`     | 5     | Debug                     |

**O vídeo tem prioridade baixa.** Em caso de congestionamento na fila LoRa, pacotes de vídeo podem ser descartados.

---

## 7. API Pública

### 7.1 Inicialização

```cpp
#include "Video.h"
#include "LoRa.h"

LoRa lora;
Video video;

void setup() {
    lora.begin(LORA_MODULE_SX1262, config);
    video.begin(&lora);
    video.setDebug(true);
}
´´´

7.2 Processamento de Frames (Recepção do ESP1)

```cpp
void loop() {
    if (Serial2.available()) {
        uint8_t buffer[4096];
        size_t len = Serial2.readBytes(buffer, sizeof(buffer));
        
        if (video.processFrame(buffer, len)) {
            // Frame processado com sucesso
        }
    }
}
´´´

7.3 Transmissão (Chamar no Loop)

```cpp
void loop() {
    // Enviar próximo chunk da fila
    video.sendNext();
    
    delay(10);
}
´´´

7.4 Configuração

```cpp
// Ativar/desativar vídeo
video.setEnabled(true);
video.setEnabled(false);

// Definir tipo de compressão (futuro)
video.setCompression(VIDEO_COMPRESS_JPEG);
´´´

7.5 Estatísticas

```cpp
// Estado atual
VideoStatus status = video.getStatus();

// Tamanho da fila
size_t queueSize = video.getQueueSize();

// Chunks enviados
uint32_t sent = video.getChunksSent();

// Chunks descartados (fila cheia)
uint32_t dropped = video.getChunksDropped();

// Resetar estatísticas
video.resetStats();
´´´

8. Exemplo de Integração

```cpp
#include "Video.h"
#include "LoRa.h"

LoRa lora;
Video video;

void setup() {
    Serial.begin(115200);
    Serial2.begin(921600);  // UART para ESP1
    
    LoRaConfig config;
    config.frequency = 868000000;
    config.txPower = 20;
    config.spreadingFactor = 9;
    config.bandwidth = 125000;
    config.codingRate = 8;
    config.crcEnabled = true;
    
    lora.begin(LORA_MODULE_SX1262, config);
    video.begin(&lora);
    video.setDebug(true);
    video.setEnabled(true);
}

void loop() {
    // 1. Receber frames de vídeo do ESP1
    while (Serial2.available()) {
        uint8_t buffer[4096];
        size_t len = Serial2.readBytes(buffer, sizeof(buffer));
        
        if (len > 0) {
            video.processFrame(buffer, len);
        }
    }
    
    // 2. Enviar chunks
    video.sendNext();
    
    // 3. Imprimir estatísticas a cada 10 segundos
    static uint32_t lastStats = 0;
    if (millis() - lastStats > 10000) {
        Serial.printf("Video: queue=%zu, sent=%lu, dropped=%lu\n",
                      video.getQueueSize(),
                      video.getChunksSent(),
                      video.getChunksDropped());
        lastStats = millis();
    }
    
    delay(10);
}
´´´

9. Estatísticas

Métrica             Descrição                           Onde consultar
_framesProcessed    Frames processados                  getFramesProcessed() (futuro)
_chunksSent         Chunks enviados com sucesso         getChunksSent()
_chunksDropped      Chunks descartados (fila cheia)     getChunksDropped()
_chunksFailed       Chunks com falha de envio           getChunksFailed() (futuro)
_queue.size()       Tamanho atual da fila               getQueueSize()

Interpretação

Situação            Possível causa
dropped > 0         Fila cheia → reduzir taxa de vídeo
failed > 0          Problema no LoRa → verificar antena/RF
queueSize grande    Envio mais lento que chegada de frames

10. Debug

Ativação

```cpp
video.setDebug(true);
´´´

Mensagens Típicas

[Video] begin() — Módulo de vídeo inicializado
[Video]   Compressão: NONE
[Video]   Max chunk size: 128 bytes
[Video]   Max queue size: 50 chunks

[Video] _fragmentFrame() — Frame 0: 1024 bytes → 8 chunks
[Video] _enqueueChunk() — Chunk 0/8 do frame 0 adicionado à fila (fila: 1)
[Video] _enqueueChunk() — Chunk 1/8 do frame 0 adicionado à fila (fila: 2)
...
[Video] sendChunk() — Chunk 0/8 do frame 0 enviado (156 bytes)
[Video] sendChunk() — Chunk 1/8 do frame 0 enviado (156 bytes)
...

11. Limitações e Considerações

Limitação           Descrição
Largura de banda    LoRa 868MHz tem taxa muito baixa (1-10 kbps)
Tamanho de pacote   Chunks limitados a 128 bytes (TLV payload)
Perda de pacote     Aceitável para POC, mas afeta qualidade
Frame rate          Máximo de ~1-2 fps para imagens pequenas
Compressão          Ainda não implementada (placeholder)

Recomendações:

- Usar compressão JPEG (implementação futura) para reduzir tamanho
- Reduzir resolução (ex: 160×120 para teste)
- Taxa de frames baixa (1-2 fps máximo)
- Aceitar perdas — vídeo não é crítico para a missão

Fim da documentação – Video v1.0.0

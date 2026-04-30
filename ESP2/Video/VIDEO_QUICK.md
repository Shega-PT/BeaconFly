# Guia Rápido – Video (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-29
> **Autor:** BeaconFly UAS Team

---

## 1. Funcionalidade (LEMBRETE)

**Vídeo é PROVA DE CONCEITO (POC).** Não é crítico. Perdas de pacote são aceitáveis.

---

## 2. Código Rápido

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

void loop() {
    // Receber frame do ESP1
    if (Serial2.available()) {
        uint8_t buffer[4096];
        size_t len = Serial2.readBytes(buffer, sizeof(buffer));
        video.processFrame(buffer, len);
    }
    
    // Enviar chunks
    video.sendNext();
}
´´´

3. Parâmetros

Parâmetro               Valor           Descrição
VIDEO_MAX_CHUNK_SIZE    128 bytes       Tamanho máximo do chunk
VIDEO_MAX_FRAME_SIZE    4096 bytes      Tamanho máximo do frame
VIDEO_MAX_QUEUE_SIZE    50 chunks       Máximo na fila
Prioridade              4 (baixa)       LORA_PRIORITY_VIDEO

4. Comandos

```cpp
// Ativar/desativar
video.setEnabled(true);
video.setEnabled(false);

// Estatísticas
uint32_t sent = video.getChunksSent();
uint32_t dropped = video.getChunksDropped();
size_t queue = video.getQueueSize();

// Reset
video.resetStats();

// Debug
video.setDebug(true);
´´´

5. Estrutura do Pacote

MSG_VIDEO (0x16)
├── FLD_VIDEO_FRAME_ID (0xB0) → uint16_t
├── FLD_VIDEO_CHUNK_ID (0xB1) → uint8_t
├── FLD_VIDEO_TOTAL (0xB2) → uint8_t
└── FLD_VIDEO_PAYLOAD (0xB3) → uint8_t[até 128]

6. Exemplo de Uso

ESP1 (envia frame)

```cpp
// Capturar frame da câmera
uint8_t frame[1024];
size_t len = captureFrame(frame);

// Enviar para ESP2 via UART
Serial2.write(frame, len);
´´´

ESP2 (recebe e transmite)

```cpp
// Receber frame
if (Serial2.available()) {
    uint8_t buffer[4096];
    size_t len = Serial2.readBytes(buffer, sizeof(buffer));
    video.processFrame(buffer, len);
}

// Enviar chunks (chamar no loop)
video.sendNext();
´´´

GS (recebe vídeo)

```cpp
// Receber pacote LoRa
uint8_t buffer[256];
size_t len = lora.receive(buffer, sizeof(buffer));

if (buffer[1] == MSG_VIDEO) {
    // Extrair campos TLV
    uint16_t frameId = extractFrameId(buffer);
    uint8_t chunkId = extractChunkId(buffer);
    uint8_t total = extractTotalChunks(buffer);
    uint8_t* payload = extractPayload(buffer);
    
    // Reconstruir frame
    reassembleFrame(frameId, chunkId, total, payload);
}
´´´

7. Debug

Mensagens:

[Video] begin() — Módulo de vídeo inicializado
[Video] _fragmentFrame() — Frame 0: 1024 bytes → 8 chunks
[Video] sendChunk() — Chunk 0/8 do frame 0 enviado (156 bytes)
[Video] sendChunk() — Chunk 1/8 do frame 0 enviado (156 bytes)

Fim do Guia Rápido – Video v1.0.0

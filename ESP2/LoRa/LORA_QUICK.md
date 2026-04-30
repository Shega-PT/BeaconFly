# Guia Rápido – LoRa

> **Versão:** 1.0.0
> **Data:** 2026-04-27
> **Autor:** BeaconFly UAS Team

---

## 1. LEMBRETE IMPORTANTE

| Componente | LoRa 2.4GHz     | LoRa 868MHz          |
|------------|-----------------|----------------------|
| **ESP1**   | ✅ RECEBE da GS | ❌ Não utiliza       |
| **ESP2**   | ❌ Não utiliza  | ✅ TRANSMITE para GS |

**ESP2 NUNCA recebe via LoRa. Apenas transmite.**

---

## 2. Pinos (Ajustar conforme PCB)

### SX1262 (Recomendado)

| Função | Pino GPIO |
|--------|-----------|
| CS     | 5         |
| DIO1   | 4         |
| RESET  | 2         |
| BUSY   | 15        |

### SX1276 (Fallback)

| Função | Pino GPIO |
| -------|-----------|
| CS     | 5         |
| DIO0   | 4         |
| RESET  | 2         |

---

## 3. Prioridades

| Prioridade | Valor | Constante                 | Uso          |
| -----------|-------|---------------------------|--------------|
| 1          | 0     | `LORA_PRIORITY_FAILSAFE`  | Emergência   |
| 2          | 1     | `LORA_PRIORITY_COMMAND`   | Respostas GS |
| 3          | 2     | `LORA_PRIORITY_TELEMETRY` | Telemetria   |
| 4          | 3     | `LORA_PRIORITY_SHELL`     | Shell remoto |
| 5          | 4     | `LORA_PRIORITY_VIDEO`     | Vídeo        |
| 6          | 5     | `LORA_PRIORITY_DEBUG`     | Debug        |

---

## 4. Configuração Rápida

```cpp
LoRa lora;

void setup() {
    LoRaConfig config;
    config.frequency = 868000000;   // 868 MHz
    config.txPower = 20;            // 20 dBm
    config.spreadingFactor = 9;     // SF9
    config.bandwidth = 125000;      // 125 kHz
    config.codingRate = 8;          // 4/8
    config.crcEnabled = true;
    
    lora.begin(LORA_MODULE_SX1262, config);
}
´´´

5. Enviar Dados

```cpp
// Com prioridade (recomendado)
uint8_t data[128];
size_t len = prepareData(data);
lora.send(data, len, LORA_PRIORITY_TELEMETRY);

// Imediato (ignora fila)
lora.sendImmediate(data, len);

// Processar fila (chamar frequentemente)
lora.transmit();
´´´

6. Consultar Estado

```cpp
// Módulo pronto?
bool ready = lora.isReady();

// Estatísticas
const LoRaStats& stats = lora.getStats();
Serial.printf("Enviados: %lu\n", stats.packetsSent);
Serial.printf("Falhas: %lu\n", stats.packetsFailed);
Serial.printf("Fila: %d\n", stats.queueSize);
´´´

7. Configuração Dinâmica

```cpp
// Mudar frequência
lora.setFrequency(868500000);  // 868.5 MHz

// Mudar potência
lora.setTxPower(14);  // 14 dBm
´´´

8. Debug

```cpp
lora.setDebug(true);
Mensagens típicas:
´´´

[LoRa] begin() — Inicializando módulo LoRa 868MHz...
[LoRa] _sendPacket() — Pacote enviado: 128 bytes

9. Exemplo Completo

```cpp
#include "LoRa.h"

LoRa lora;

void setup() {
    Serial.begin(115200);
    
    LoRaConfig config;
    config.frequency = 868000000;
    config.txPower = 20;
    config.spreadingFactor = 9;
    config.bandwidth = 125000;
    config.codingRate = 8;
    config.crcEnabled = true;
    
    lora.begin(LORA_MODULE_SX1262, config);
    lora.setDebug(true);
}

void loop() {
    static uint32_t lastSend = 0;
    
    if (millis() - lastSend >= 1000) {
        uint8_t packet[] = {0xAA, 0x11, 0x01, 0x00};
        lora.send(packet, sizeof(packet), LORA_PRIORITY_TELEMETRY);
        lastSend = millis();
    }
    
    lora.transmit();
    delay(10);
}
´´´

10. Erros Comuns

Erro                    Solução
radio.begin() falhou    Verificar fiação e alimentação
Fila cheia              Aumentar LORA_TX_QUEUE_SIZE
Falha no envio          Verificar antena e interferência

Fim do Guia Rápido – LoRa v1.0.0

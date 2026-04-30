# LoRa – Pasta `LoRa/`

> **Versão:** 2.0.0  
> **Data:** 2026-04-17  
> **Autor:** BeaconFly UAS Team

Esta documentação detalha o **driver de comunicação LoRa 2.4GHz** do sistema BeaconFly. Abrange os 2 arquivos da pasta: `LoRa.h` e `LoRa.cpp`, explicando a arquitetura, configuração, operação e integração com os restantes módulos.

---

## 1. Objetivo do Módulo

O módulo `LoRa` é responsável por **receber comandos da Ground Station (GS) via LoRa 2.4GHz** e convertê-los em bytes legíveis para o ESP1.

**Princípio fundamental:** Este módulo faz **APENAS** a camada física e de enlace. Não faz parsing, não valida segurança, não desmonta TLV.

| Responsabilidade                  | Módulo                    |
| ----------------------------------|---------------------------|
| Ondas de rádio → bytes            | **LoRa**                  |
| Bytes → mensagem TLV              | **Parser**                |
| Mensagem → autenticada/rejeitada  | **Security**              |
| Comando → ação                    | **FlightControl / Shell** |

---

## 2. Arquitetura

### 2.1 Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ GROUND STATION (GS)                                                             │
│ (Comandos em TLV + HMAC + SEQ)                                                  │
│ │                                                                               │
│ ▼ (LoRa 2.4GHz)                                                                 │
│ ┌─────────────────────────────────────────────────────────────────────────────┐ │
│ │ ESP1                                                                        │ │
│ │ ┌─────────────────────────────────────────────────────────────────────────┐ │ │
│ │ │ LORA (este módulo)                                                      │ │ │
│ │ │ • Recebe ondas de rádio                                                 │ │ │
│ │ │ • Converte para bytes crus                                              │ │ │
│ │ │ • Disponibiliza via readPacket()                                        │ │ │
│ │ └─────────────────────────────────────────────────────────────────────────┘ │ │
│ │ │                                                                           │ │
│ │ ▼ (bytes crus)                                                              │ │
│ │ ┌─────────────────────────────────────────────────────────────────────────┐ │ │
│ │ │ PARSER                                                                  │ │ │
│ │ │ • Alimenta byte a byte via feed()                                       │ │ │
│ │ │ • Reconstrói mensagem TLV                                               │ │ │
│ │ │ • Valida CRC8 e estrutura                                               │ │ │
│ │ └─────────────────────────────────────────────────────────────────────────┘ │ │
│ │ │                                                                           │ │
│ │ ▼ (mensagem TLV)                                                            │ │
│ │ ┌─────────────────────────────────────────────────────────────────────────┐ │ │
│ │ │ SECURITY                                                                │ │ │
│ │ │ • Verifica HMAC-SHA256                                                  │ │ │
│ │ │ • Verifica sequência (anti-replay)                                      │ │ │
│ │ │ • Autentica a origem (GS)                                               │ │ │
│ │ └─────────────────────────────────────────────────────────────────────────┘ │ │
│ │ │                                                                           │ │
│ │ ▼ (comando autenticado)                                                     │ │
│ │ ┌─────────────────────────────────────────────────────────────────────────┐ │ │
│ │ │ FLIGHTCONTROL / SHELL                                                   │ │ │
│ │ │ • Processa comando                                                      │ │ │
│ │ │ • Executa ação                                                          │ │ │
│ │ └─────────────────────────────────────────────────────────────────────────┘ │ │
│ └─────────────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────────┘

### 2.2 Diagrama de Sequência (Código)

```cpp
// No loop principal do ESP1
void loop() {
    // 1. LoRa recebe bytes
    lora.update();
    
    // 2. Se há pacote disponível
    if (lora.available()) {
        uint8_t raw[1024];
        size_t len = lora.readPacket(raw, sizeof(raw));
        
        // 3. Alimentar o parser byte a byte
        for (size_t i = 0; i < len; i++) {
            if (parser.feed(raw[i])) {
                // 4. Mensagem completa → Security
                TLVMessage* msg = parser.getMessage();
                if (security.verifyPacket(*msg) == SEC_OK) {
                    // 5. Comando autenticado → ação
                    flightControl.processCommand(*msg);
                }
                parser.acknowledge();
            }
        }
    }
}
´´´

3. Módulos Suportados

Módulo      Fabricante  Banda       Estado
SX1280      Semtech     2.4 GHz     ✅ Implementado
E220-2.4    Ebyte       2.4 GHz     ✅ Compatível (SX1280)
LLCC68      Semtech     2.4 GHz     🚧 Em desenvolvimento

4. Configuração

4.1 Pinos (LoRaPins)

```cpp
struct LoRaPins {
    uint8_t cs;      // Chip Select (SPI)
    uint8_t dio1;    // Interrupção de pacote
    uint8_t rst;     // Reset
    uint8_t busy;    // Busy (opcional)
    
    static LoRaPins defaultPins() {
        LoRaPins pins;
        pins.cs = 5;
        pins.dio1 = 4;
        pins.rst = 2;
        pins.busy = 15;
        return pins;
    }
};
´´´

4.2 Parâmetros RF (LoRaConfig)

Parâmetro           Descrição                           Valores típicos
frequency           Frequência central (Hz)             2.400.000.000 (2.4 GHz)
spreadingFactor     Fator de espalhamento (5-12)        9 (equilíbrio)
bandwidth           Largura de banda (0-9)              4 (400 kHz)
codingRate          Taxa de codificação (5-8)           7 (4/7)
txPower             Potência de transmissão (dBm)       10 (10 mW)
useCRC              Ativar CRC interno                  true
invertIQ            Inverter IQ (anti-interferência)    false

4.3 Configuração Padrão

```cpp
LoRaConfig config = LoRaConfig::defaultConfig();
// frequency = 2.4 GHz
// spreadingFactor = 9
// bandwidth = 4 (400 kHz)
// codingRate = 7 (4/7)
// txPower = 10 dBm
// useCRC = true
´´´

5. API Pública

5.1 Inicialização

```cpp
LoRa lora;

void setup() {
    LoRaPins pins = LoRaPins::defaultPins();
    LoRaConfig config = LoRaConfig::defaultConfig();
    
    if (!lora.begin(LORA_MODULE_SX1280, pins, config)) {
        Serial.println("Erro ao inicializar LoRa!");
    }
    
    lora.setDebug(true);
}
´´´

5.2 Operação Principal

```cpp
void loop() {
    // Processar receção (não-bloqueante)
    lora.update();
    
    // Verificar se há pacote
    if (lora.available()) {
        uint8_t buffer[1024];
        size_t len = lora.readPacket(buffer, sizeof(buffer));
        
        // Alimentar parser...
    }
}
´´´

5.3 Transmissão (ACKs)

```cpp
// Enviar resposta (bloqueante)
uint8_t ack[] = {0xAA, 0x13, 0x00, 0x00};  // ACK vazio
lora.sendPacket(ack, sizeof(ack));
´´´

5.4 Configuração Dinâmica

```cpp
// Alterar frequência em runtime
lora.setFrequency(2405000000UL);  // 2.405 GHz

// Alterar potência
lora.setTxPower(13);  // 13 dBm
´´´

5.5 Estatísticas

```cpp
const LoRaStats& stats = lora.getStats();
Serial.printf("Pacotes recebidos: %lu\n", stats.packetsReceived);
Serial.printf("Pacotes válidos: %lu\n", stats.packetsValid);
Serial.printf("Último RSSI: %d dBm\n", stats.lastRSSI);
Serial.printf("Último SNR: %d dB\n", stats.lastSNR);

// Resetar estatísticas
lora.resetStats();
´´´

6. Estatísticas (LoRaStats)

Campo               Descrição
packetsReceived     Total de pacotes recebidos (incluindo inválidos)
packetsValid        Pacotes que passaram o CRC do rádio
packetsRejected     Pacotes rejeitados (buffer cheio ou erro interno)
lastRSSI            Força do sinal do último pacote (dBm)
lastSNR             Relação sinal-ruído do último pacote (dB)
lastPacketTime      Timestamp do último pacote (millis)

Interpretação de RSSI:

RSSI (dBm)      Qualidade
> -50           Excelente
-50 a -70       Bom
-70 a -90       Razoável
-90 a -110      Fraco
< -110          Perda iminente

7. Exemplo Completo

```cpp
#include "LoRa.h"
#include "Parser.h"
#include "Security.h"
#include "FlightControl.h"

LoRa lora;
Parser parser;
Security security;
FlightControl fc;

void setup() {
    Serial.begin(115200);
    
    // Inicializar LoRa
    LoRaPins pins = LoRaPins::defaultPins();
    LoRaConfig config = LoRaConfig::defaultConfig();
    
    if (!lora.begin(LORA_MODULE_SX1280, pins, config)) {
        Serial.println("ERRO: LoRa não inicializado!");
        while(1);
    }
    
    lora.setDebug(true);
    security.setKey("minha_chave_secreta_32_bytes_!!!");
    
    Serial.println("Sistema pronto");
}

void loop() {
    lora.update();
    
    if (lora.available()) {
        uint8_t raw[1024];
        size_t len = lora.readPacket(raw, sizeof(raw));
        
        // Alimentar parser byte a byte
        for (size_t i = 0; i < len; i++) {
            if (parser.feed(raw[i])) {
                TLVMessage* msg = parser.getMessage();
                
                // Verificar autenticidade
                if (security.verifyPacket(*msg) == SEC_OK) {
                    fc.processCommand(*msg);
                }
                
                parser.acknowledge();
            }
        }
    }
    
    fc.update();
    delay(10);  // 100Hz
}
´´´

8. Debug e Diagnóstico

8.1 Ativação

```cpp
lora.setDebug(true);
´´´

8.2 Mensagens Típicas

[LoRa] begin() — Inicializando módulo tipo 0
[LoRa] _initSX1280() — CS=5, DIO1=4, RST=2, BUSY=15
[LoRa] _initSX1280() — Configuração: freq=2400.000MHz, SF=9, BW=4, CR=7, TX=10dBm
[LoRa] begin() — Módulo inicializado com sucesso
[LoRa] update() — Pacote detetado
[LoRa] update() — Pacote recebido: 32 bytes, RSSI=-65, SNR=12
[LoRa] _processRaw() — Pacote armazenado: 32 bytes
[LoRa] readPacket() — 32 bytes lidos

8.3 Erros Comuns

Mensagem                    Causa                           Solução
radio.begin() falhou        Módulo não conectado            Verificar ligações e alimentação
Pacote demasiado grande     Tamanho > 1024                  Aumentar MAX_MESSAGE_SIZE
Erro na leitura: -1         Timeout ou CRC inválido         Verificar configuração RF
Envio falhou                Módulo ocupado ou sem energia   Aguardar e tentar novamente

9. Considerações de Performance

Métrica                             Valor
Tempo de update() (sem pacote)      ~10 µs
Tempo de update() (com pacote)      ~200 µs
Tempo de sendPacket() (32 bytes)    ~50-100 ms
Buffer de receção                   1024 bytes
Suporte a múltiplos módulos         Sim (via enum)

10. Observações Finais

- O LoRa NÃO deve chamar o Parser ou Security — viola a separação de responsabilidades.
- Apenas comandos da GS são recebidos por este módulo (2.4 GHz).
- Telemetria de saída deve usar o ESP2 com 868 MHz (maior alcance).
- Em caso de perda de link, o Security ativa failsafe automaticamente.

Fim da documentação – LoRa BeaconFly v2.0.0

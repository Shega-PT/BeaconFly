# Guia Rápido – LoRa BeaconFly

> **Versão:** 2.0.0  
> **Uso:** Referência rápida para debug, manutenção e desenvolvimento

---

## 1. Responsabilidades (LEMBRETE)

| Módulo       | Faz                | NÃO faz               |
| -------------|--------------------|-----------------------|
| **LoRa**     | Bytes → bytes      | ❌ Parse, ❌ Security |
| **Parser**   | Bytes → TLV        | ❌ Security           |
| **Security** | TLV → autenticado  | ❌ Parse              |

---

## 2. Constantes Rápidas

| Constante             | Valor | Descrição        |
| ----------------------|-------|------------------|
| `LORA_MODULE_SX1280`  | 0     | Semtech 2.4GHz   |
| `LORA_MODULE_LLCC68`  | 1     | Versão económica |
| `LORA_MODULE_E220_24` | 2     | Ebyte E220-2.4   |

---

## 3. Configuração Padrão

### Pinos (PCB BeaconFly v1.0)

| Pino | Função      | Valor |
| -----|-------------|-------|
| CS   | Chip Select | 5     |
| DIO1 | Interrupção | 4     |
| RST  | Reset       | 2     |
| BUSY | Busy        | 15    |

### Parâmetros RF

| Parâmetro       | Valor            | Descrição        |
| ----------------|------------------|------------------|
| frequency       | 2.400.000.000 Hz | 2.4 GHz          |
| spreadingFactor | 9                | SF9 (equilíbrio) |
| bandwidth       | 4                | 400 kHz          |
| codingRate      | 7                | 4/7              |
| txPower         | 10 dBm           | 10 mW            |

---

## 4. Inicialização Rápida

```cpp
LoRa lora;

void setup() {
    LoRaPins pins = LoRaPins::defaultPins();
    LoRaConfig config = LoRaConfig::defaultConfig();
    
    if (lora.begin(LORA_MODULE_SX1280, pins, config)) {
        Serial.println("LoRa OK");
    }
    
    lora.setDebug(true);
}
´´´

5. Loop Principal

```cpp
void loop() {
    lora.update();
    
    if (lora.available()) {
        uint8_t buf[1024];
        size_t len = lora.readPacket(buf, sizeof(buf));
        
        // Alimentar parser...
        for (size_t i = 0; i < len; i++) {
            parser.feed(buf[i]);
        }
    }
}
´´´

6. Enviar ACK

```cpp
uint8_t ack[] = {0xAA, 0x13, 0x00, 0x00};
lora.sendPacket(ack, sizeof(ack));
´´´

7. Configuração Dinâmica

```cpp
// Mudar frequência (ex: evitar interferência)
lora.setFrequency(2405000000UL);  // 2.405 GHz

// Mudar potência
lora.setTxPower(13);  // 13 dBm
´´´

8. Estatísticas

```cpp
const LoRaStats& stats = lora.getStats();

Serial.printf("Recebidos: %lu\n", stats.packetsReceived);
Serial.printf("Válidos: %lu\n", stats.packetsValid);
Serial.printf("RSSI: %d dBm\n", stats.lastRSSI);
Serial.printf("SNR: %d dB\n", stats.lastSNR);

// Reset
lora.resetStats();
´´´

Interpretação RSSI:

dBm             Qualidade
> -50           ⭐⭐⭐⭐⭐ Excelente
-50 a -70       ⭐⭐⭐⭐ Bom
-70 a -90       ⭐⭐⭐ Razoável
-90 a -110      ⭐⭐ Fraco
< -110          ⭐ Perda iminente

9. Debug

Ativar

```cpp
lora.setDebug(true);
´´´

Mensagens típicas

[LoRa] begin() — Inicializando módulo tipo 0
[LoRa] _initSX1280() — Configuração: freq=2400.000MHz, SF=9
[LoRa] update() — Pacote recebido: 32 bytes, RSSI=-65, SNR=12
[LoRa] readPacket() — 32 bytes lidos
10. Erros Comuns

Erro                        Causa                   Solução
radio.begin() falhou        Módulo não conectado    Verificar fiação
Pacote demasiado grande     >1024 bytes             Aumentar buffer
Erro na leitura: -1         CRC inválido            Verificar config RF
sendPacket() falhou         Módulo ocupado          Tentar novamente

11. Fluxo Completo (Com Parser)

```cpp
LoRa lora;
Parser parser;
Security security;

void loop() {
    lora.update();
    
    if (lora.available()) {
        uint8_t raw[1024];
        size_t len = lora.readPacket(raw, sizeof(raw));
        
        for (size_t i = 0; i < len; i++) {
            if (parser.feed(raw[i])) {
                TLVMessage* msg = parser.getMessage();
                
                if (security.verifyPacket(*msg) == SEC_OK) {
                    // Comando autenticado!
                    processCommand(*msg);
                }
                
                parser.acknowledge();
            }
        }
    }
}
´´´

12. Verificação Rápida (Teste em Solo)

```cpp
void testLoRa() {
    LoRaPins pins = LoRaPins::defaultPins();
    LoRaConfig config = LoRaConfig::defaultConfig();
    
    if (!lora.begin(LORA_MODULE_SX1280, pins, config)) {
        Serial.println("ERRO: LoRa não responde!");
        return;
    }
    
    Serial.println("LoRa OK. Aguardando pacotes...");
    
    while (true) {
        lora.update();
        
        if (lora.available()) {
            uint8_t buf[256];
            size_t len = lora.readPacket(buf, sizeof(buf));
            
            Serial.print("Pacote recebido (");
            Serial.print(len);
            Serial.print(" bytes): ");
            
            for (size_t i = 0; i < len; i++) {
                Serial.printf("%02X ", buf[i]);
            }
            Serial.println();
        }
        
        delay(10);
    }
}
´´´

Fim do Guia Rápido – LoRa BeaconFly v2.0.0

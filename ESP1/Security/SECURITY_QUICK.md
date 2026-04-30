# Guia Rápido – Security BeaconFly

> **Versão:** 2.0.0  
> **Uso:** Referência rápida para debug, manutenção e desenvolvimento

---

## 1. Filosofia (LEMBRETE)

**FAIL SECURE:** Em caso de dúvida → BLOQUEAR.

Uma mensagem bloqueada é recuperável. Um comando perigoso executado pode ser catastrófico.

---

## 2. Níveis de Segurança

| Nível    | Violações | Roll max | Pitch max |
| ---------|-----------|----------|-----------|
| NORMAL   | 0         | 45°      | 35°       |
| CAUTION  | 1-2       | 30°      | 25°       |
| WARNING  | 3-4       | 15°      | 15°       |
| CRITICAL | 5-7       | 10°      | 10°       |
| LOCKDOWN | 8+        | —        | —         |

---

## 3. Estados de Voo

| Estado                    | Valor | Descrição             |
| --------------------------|-------|-----------------------|
| `STATE_GROUND_DISARMED`   | 0     | Em solo, desarmado    |
| `STATE_GROUND_ARMED`      | 1     | Em solo, armado       |
| `STATE_TAKEOFF`           | 2     | Descolagem            |
| `STATE_FLYING`            | 3     | Em voo                |
| `STATE_LANDING`           | 4     | Aterragem             |
| `STATE_FAILSAFE`          | 5     | Modo segurança        |

---

## 4. Códigos de Retorno (Rápidos)

| Código                | Valor | O que fazer           |
| ----------------------|-------|-----------------------|
| `SEC_OK`              | 0     | ✅ Processar          |
| `SEC_INVALID_HMAC`    | 1     | ❌ Rejeitar, log      |
| `SEC_REPLAY_DETECTED` | 2     | ❌ Rejeitar, log      |
| `SEC_RATE_LIMITED`    | 3     | ❌ Aguardar           |
| `SEC_SANITY_FAILED`   | 4     | ❌ Verificar sensor   |
| `SEC_LOCKDOWN`        | 5     | 🔒 Apenas failsafe    |
| `SEC_WRONG_STATE`     | 6     | ❌ Verificar modo     |
| `SEC_AUTH_REQUIRED`   | 7     | 🔐 Autenticar GS      |

---

## 5. Rate Limiting

| Categoria  | Comandos                                   | Intervalo |
| -----------|--------------------------------------------|-----------|
| Crítico    | ARM, DISARM, REBOOT, SHUTDOWN              | 500 ms    |
| Controlo   | SET_ROLL, SET_PITCH, SET_YAW, SET_THROTTLE | 20 ms     |
| Manutenção | SET_PARAM, CALIB                           | 5 s       |

---

## 6. Sanidade de Comandos (Limites Absolutos)

| Comando               | Limite  |
| ----------------------|---------|
| `CMD_SET_ROLL`        | ±60°    |
| `CMD_SET_PITCH`       | ±45°    |
| `CMD_SET_YAW`         | ±180°/s |
| `CMD_SET_THROTTLE`    | 0-1     |
| `CMD_SET_ALT_TARGET`  | 0-1000m |

---

## 7. Sanidade de Sensores

| Sensor        | Limite        |
| --------------|---------------|
| Roll/Pitch    | ±180°         |
| Yaw/Heading   | 0-360°        |
| Bateria       | 6-30V         |
| Temperatura   | -20 a 80°C    |
| Altitude      | -100 a 1000m  |

---

## 8. Comandos por Estado de Voo

| Comando               | Solo Desarmado | Solo Armado  | Voo |
| ----------------------|----------------|--------------|-----|
| `CMD_ARM`             | ✅             | ❌           | ❌  |
| `CMD_DISARM`          | ❌             | ✅           | ✅  |
| `CMD_SET_ROLL`        | ❌             | ✅           | ✅  |
| `CMD_SET_PITCH`       | ❌             | ✅           | ✅  |
| `CMD_SET_THROTTLE`    | ❌             | ✅           | ✅  |
| `CMD_REBOOT`          | ✅             | ✅           | ❌  |
| `CMD_RTL`             | ❌             | ❌           | ✅  |

---

## 9. Inicialização Rápida

```cpp
Security security;

void setup() {
    security.begin();
    security.setDebug(true);
    
    // Opcional: configurar geofence
    SecurityConfig cfg;
    cfg.enableGeofence = true;
    cfg.geofenceRadius = 500.0f;
    security.configure(cfg);
}
´´´

10. Verificação de Pacote

```cpp
SecurityResult result = security.verifyPacket(msg, seqNum, hmac);

if (result == SEC_OK) {
    // Pacote válido
    processCommand(msg);
} else if (result == SEC_LOCKDOWN) {
    // Apenas failsafe
    if (msg.msgID == MSG_FAILSAFE) {
        activateFailsafe();
    }
}
´´´
11. Autenticação da GS

```cpp
// ESP1 gera challenge
uint32_t challenge = security.generateChallenge();
sendToGS(challenge);

// GS responde com HMAC(challenge)
// ESP1 verifica
if (security.verifyChallengeResponse(response, len)) {
    // GS autenticada
}
´´´

12. Computação de HMAC (para envio)

```cpp
uint8_t hmac[32];
security.computeHMAC(data, len, hmac);
// Enviar data + hmac
´´´

13. Atualização do Estado de Voo

```cpp
// Quando o FlightControl muda de modo
security.setFlightState(STATE_FLYING);

// Consultar estado atual
FlightState state = security.getFlightState();
14. Estatísticas
cpp
const SecurityStats& stats = security.getStats();

Serial.printf("Pacotes verificados: %lu\n", stats.packetsVerified);
Serial.printf("HMAC falhas: %lu\n", stats.hmacFailures);
Serial.printf("Replay tentativas: %lu\n", stats.replayAttempts);
Serial.printf("Lockdown eventos: %lu\n", stats.lockdownEvents);

// Reset
security.resetStats();
´´´

15. Lockdown

```cpp
// Verificar se está em lockdown
if (security.isInLockdown()) {
    // Apenas MSG_FAILSAFE é aceite
}

// Ativar lockdown manualmente (emergência)
security.triggerLockdown("Erro crítico de sensor");
´´´

16. Debug – Ativação

```cpp
security.setDebug(true);
´´´

Mensagens típicas

[Security] begin() — Security inicializado. Challenge: 0xA4B3C2D1
[Security] verifyPacket() — Pacote 0x12 válido
[Security] verifyChallengeResponse() — GS autenticada com sucesso!
[Security] ⚠️⚠️⚠️ LOCKDOWN ATIVADO ⚠️⚠️⚠️

17. Exemplo Completo (Integração)

```cpp
#include "Security.h"
#include "LoRa.h"
#include "Parser.h"

Security security;
LoRa lora;
Parser parser;

void loop() {
    lora.update();
    
    if (lora.available()) {
        uint8_t raw[1024];
        size_t len = lora.readPacket(raw, sizeof(raw));
        
        // Extrair seqNum (bytes 0-3) e HMAC (bytes 4-35)
        uint32_t seqNum = *(uint32_t*)raw;
        uint8_t* hmac = raw + 4;
        
        // Alimentar parser com o resto (bytes 36+)
        for (size_t i = 36; i < len; i++) {
            if (parser.feed(raw[i])) {
                TLVMessage* msg = parser.getMessage();
                
                if (security.verifyPacket(*msg, seqNum, hmac) == SEC_OK) {
                    // Processar comando
                    processCommand(msg);
                }
                
                parser.acknowledge();
            }
        }
    }
    
    // Atualizar estado de voo na segurança
    security.setFlightState(getCurrentFlightState());
}
´´´

18. Verificação Rápida em Testes

```cpp
void testSecurity() {
    Security sec;
    sec.begin();
    sec.setDebug(true);
    
    // Testar sanidade
    Serial.println("Teste de sanidade:");
    Serial.printf("Roll 45°: %d\n", sec.verifyCommand(CMD_SET_ROLL, 45.0f));
    Serial.printf("Roll 70°: %d (deve falhar)\n", sec.verifyCommand(CMD_SET_ROLL, 70.0f));
    
    // Testar rate limiting
    Serial.println("\nTeste de rate limiting:");
    for (int i = 0; i < 10; i++) {
        SecurityResult r = sec.verifyPacket(testMsg, 1, testHmac);
        Serial.printf("Pacote %d: %d\n", i, r);
        delay(10);
    }
}
´´´

Fim do Guia Rápido – Security BeaconFly v2.0.0

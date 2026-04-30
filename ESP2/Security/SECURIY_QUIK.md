# Guia Rápido – Security (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-30
> **Autor:** BeaconFly UAS Team

---

## 1. Diferenças ESP1 vs ESP2

| Aspeto              | ESP1             | ESP2            |
| --------------------|------------------|-----------------|
| Origem dos comandos | LoRa 2.4GHz (GS) | UART (ESP1)     |
| Timeout de link     | ✅ Gerencia      | ❌ Não gerencia |
| HMAC                | ✅ Verifica      | ✅ Verifica     |
| Anti-replay         | ✅               | ✅              |
| Rate limiting       | ✅               | ✅              |

---

## 2. Código Rápido

```cpp
#include "Security.h"

Security security;

void setup() {
    security.begin();
    security.setDebug(true);
}

void loop() {
    // Receber comando do ESP1
    TLVMessage msg;
    uint32_t seqNum;
    uint8_t hmac[32];
    
    SecurityResult result = security.verifyPacket(msg, seqNum, hmac);
    
    if (result == SEC_OK) {
        // Processar comando
    }
}
´´´

3. Níveis de Segurança

Nível       Violações   Roll max    Pitch max
NORMAL      0           45°         35°
CAUTION     1-2         30°         25°
WARNING     3-4         15°         15°
CRITICAL    5-7         10°         10°
LOCKDOWN    8+          —           —

4. Estados de Voo

Estado                  Valor   Descrição
STATE_GROUND_DISARMED   0       Solo, desarmado
STATE_GROUND_ARMED      1       Solo, armado
STATE_TAKEOFF           2       Descolagem
STATE_FLYING            3       Em voo
STATE_LANDING           4       Aterragem
STATE_FAILSAFE          5       Failsafe

5. Códigos de Retorno

Código                  Valor   Significado
SEC_OK                  0       ✅ Válido
SEC_INVALID_HMAC        1       ❌ HMAC inválido
SEC_REPLAY_DETECTED     2       ❌ Replay detectado
SEC_RATE_LIMITED        3       ❌ Rate limit excedido
SEC_SANITY_FAILED       4       ❌ Sanidade falhou
SEC_LOCKDOWN            5       🔒 Lockdown ativo
SEC_WRONG_STATE         6       ❌ Estado errado
SEC_ENVELOPE_EXCEEDED   7       ❌ Envelope excedido

6. Comandos por Estado

Comando             Solo Desarmado      Solo Armado       Voo
CMD_ARM             ✅                  ❌                  ❌
CMD_DISARM          ❌                  ✅                  ✅
CMD_SET_ROLL        ❌                  ✅                  ✅
CMD_SET_PITCH       ❌                  ✅                  ✅
CMD_SET_THROTTLE    ❌                  ✅                  ✅
CMD_REBOOT          ✅                  ✅                  ❌

7. Rate Limiting

Categoria                               Intervalo
Crítico (ARM, DISARM, REBOOT)           500 ms
Controlo (SET_ROLL, SET_PITCH, etc.)    20 ms
Manutenção (SET_PARAM, CALIB)           5 s

8. Sanidade de Comandos

Comando             Limite
CMD_SET_ROLL        ±60°
CMD_SET_PITCH       ±45°
CMD_SET_YAW         ±180°/s
CMD_SET_THROTTLE    0-1
CMD_SET_ALT_TARGET  0-1000m

9. Debug

```cpp
security.setDebug(true);
´´´

Mensagens:

[Security] verifyPacket() — Pacote 0x12 válido
[Security] _recordViolation() — Violação #1/10
[Security] ⚠️⚠️⚠️ LOCKDOWN ATIVADO ⚠️⚠️⚠️

Fim do Guia Rápido – Security ESP2 v1.0.0

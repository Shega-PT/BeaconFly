# Security – Pasta `Security/` (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-30
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O módulo `Security` (ESP2) é responsável pela **segurança das comunicações** no ESP2.

**DIFERENÇAS PARA O ESP1:**

| Aspeto                    | ESP1                            | ESP2                           |
| --------------------------|---------------------------------|--------------------------------|
| **Origem dos comandos**   | LoRa 2.4GHz (diretamente da GS) | UART (do ESP1)                 |
| **Link com GS**           | Gerencia timeouts               | Não gerencia (feito pelo ESP1) |
| **Lockdown**              | ✅ Sim                          | ✅ Sim                         |
| **HMAC**                  | ✅ Verifica                     | ✅ Verifica                    |
| **Anti-replay**           | ✅ Sim                          | ✅ Sim                         |
| **Rate limiting**         | ✅ Sim                          | ✅ Sim                         |

**Princípio fundamental:** O ESP2 NUNCA recebe comandos diretamente da GS. Tudo vem via UART do ESP1, já validado pelo Security do ESP1, mas o ESP2 mantém a sua própria camada de segurança para proteção interna.

---

## 2. Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP1                                                                            │
│                                                                                 │
│ GS → LoRa 2.4GHz → Security (ESP1) → Comando validado → UART TX                 │
└─────────────────────────────────────────────────────────────────────────────────┘
│
▼ (UART)
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP2                                                                            │
│                                                                                 │
│ ┌─────────────────────────────────────────────────────────────────────┐         │
│ │ SECURITY (ESP2)                                                     │         │
│ │                                                                     │         │
│ │ Comando → 1. HMAC → 2. Anti-replay → 3. Rate limiting → Válido      │         │
│ │                                                                     │         │
│ │ Se válido → Encaminha para Shell / FlightControl / Failsafe         │         │
│ │ Se inválido → Rejeita, conta violação, possível lockdown            │         │
│ └─────────────────────────────────────────────────────────────────────┘         │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘

---

## 3. Mecanismos de Segurança

### 3.1 HMAC-SHA256 (Autenticação)

| Parâmetro            | Valor               |
| ---------------------|---------------------|
| **Algoritmo**        | HMAC-SHA256         |
| **Tamanho da chave** | 32 bytes (256 bits) |
| **Tamanho do HMAC**  | 32 bytes            |

**Funcionamento:**

1. O ESP1 calcula HMAC de cada mensagem
2. O HMAC é enviado juntamente com a mensagem via UART
3. O ESP2 recalcula o HMAC com a mesma chave
4. Se forem iguais → mensagem autêntica

### 3.2 Anti-Replay (Janela Deslizante)

| Parâmetro   | Valor        |
| ------------|--------------|
| **Janela**  | 32 bits      |
| **Tamanho** | 32 mensagens |

**Funcionamento:**

- Cada mensagem tem um número de sequência (seqNum)
- Mensagens com seqNum ≤ último recebido são verificadas na janela
- Se o bit correspondente já estiver marcado → replay

### 3.3 Rate Limiting

| Categoria      | Comandos                                   | Intervalo  | Taxa máxima    |
| ---------------|--------------------------------------------|------------|----------------|
| **Crítico**    | ARM, DISARM, REBOOT                        | 500 ms     | 2 por segundo  |
| **Controlo**   | SET_ROLL, SET_PITCH, SET_YAW, SET_THROTTLE | 20 ms      | 50 por segundo |
| **Manutenção** | SET_PARAM, CALIB, etc.                     | 5 segundos | 0.2 por segundo|

### 3.4 Sanidade de Comandos

| Comando              | Limite | Razão                         |
| ---------------------|--------|-------------------------------|
| `CMD_SET_ROLL`       | ±60°   | Limite estrutural da aeronave |
| `CMD_SET_PITCH`      | ±45°   | Evitar stall                  |
| `CMD_SET_YAW`        | ±180°/s| Evitar spin                   |
| `CMD_SET_THROTTLE`   | 0-1    | Normalizado                   |
| `CMD_SET_ALT_TARGET` | 0-1000m| Altitude legal (Europa)       |

### 3.5 Sanidade de Sensores

| Sensor      | Limite | Razão                |
| ------------|--------|----------------------|
| Roll/Pitch  | ±180°  | Ângulo físico máximo |
| Yaw/Heading | 0-360° | Ângulo circular      |
| Bateria     | 6-30V  | 2S a 6S LiPo         |

### 3.6 Lockdown Progressivo

| Violações | Nível    | Comportamento                 |
| ----------|----------|-------------------------------|
| 0         | NORMAL   | Operação normal               |
| 1-2       | CAUTION  | Limites reduzidos, alerta     |
| 3-4       | WARNING  | Limites muito reduzidos       |
| 5-7       | CRITICAL | Apenas comandos de emergência |
| 8+        | LOCKDOWN | Bloqueio total                |

---

## 4. Estados de Voo (`FlightState`)

| Estado                  | Valor | Descrição                  |
| ------------------------|-------|----------------------------|
| `STATE_GROUND_DISARMED` | 0     | Em solo, sistema desarmado |
| `STATE_GROUND_ARMED`    | 1     | Em solo, sistema armado    |
| `STATE_TAKEOFF`         | 2     | Descolagem em progresso    |
| `STATE_FLYING`          | 3     | Em voo normal              |
| `STATE_LANDING`         | 4     | Aterragem em progresso     |
| `STATE_FAILSAFE`        | 5     | Modo de segurança ativo    |

---

## 5. Níveis de Segurança (`SecurityLevel`)

| Nível    | Violações | Roll max | Pitch max | Comandos permitidos     |
| ---------|-----------|----------|-----------|-------------------------|
| NORMAL   | 0         | 45°      | 35°       | Todos                   |
| CAUTION  | 1-2       | 30°      | 25°       | Todos (reduzidos)       |
| WARNING  | 3-4       | 15°      | 15°       | Todos (muito reduzidos) |
| CRITICAL | 5-7       | 10°      | 10°       | Apenas emergência       |
| LOCKDOWN | 8+        | —        | —         | Nenhum                  |

---

## 6. API Pública

### 6.1 Inicialização

```cpp
#include "Security.h"

Security security;

void setup() {
    security.begin();
    security.setDebug(true);
    
    // Opcional: carregar chave personalizada
    uint8_t myKey[32] = { ... };
    security.loadKey(myKey, 32);
    
    SecurityConfig config;
    config.enableFailSecure = true;
    security.configure(config);
}
´´´

6.2 Verificação de Pacotes

```cpp
void onPacketReceived(const TLVMessage& msg, uint32_t seqNum, const uint8_t* hmac) {
    SecurityResult result = security.verifyPacket(msg, seqNum, hmac);
    
    switch (result) {
        case SEC_OK:
            // Pacote válido — processar
            processCommand(msg);
            break;
        case SEC_INVALID_HMAC:
            // Possível ataque — apenas log
            break;
        case SEC_LOCKDOWN:
            // Sistema em lockdown — ignorar
            break;
        default:
            break;
    }
}
´´´

6.3 Validação de Comandos

```cpp
void processCommand(uint8_t cmdID, float value) {
    SecurityResult result = security.verifyCommand(cmdID, value);
    
    if (result != SEC_OK) {
        // Comando rejeitado por razões de segurança
        return;
    }
    
    // Executar comando...
}
´´´

6.4 Atualização do Estado de Voo

```cpp
// Quando o FlightControl muda de estado
security.setFlightState(STATE_FLYING);

// Consultar estado atual
FlightState state = security.getFlightState();
´´´

6.5 Computação de HMAC (para envio)

```cpp
void sendPacketToESP1(const TLVMessage& msg) {
    uint8_t buffer[512];
    size_t len = serializeMessage(msg, buffer, sizeof(buffer));
    
    uint8_t hmac[SECURITY_HMAC_SIZE];
    security.computeHMAC(buffer, len, hmac);
    
    // Enviar mensagem + HMAC + seqNum
}
´´´

6.6 Consultas

```cpp
// Nível de segurança
SecurityLevel level = security.getSecurityLevel();

// Lockdown
bool lockdown = security.isInLockdown();

// Estatísticas
const SecurityStats& stats = security.getStats();
Serial.printf("HMAC failures: %lu\n", stats.hmacFailures);
´´´

7. Estatísticas (SecurityStats)

Campo               Descrição
packetsVerified     Total de pacotes verificados
hmacFailures        Falhas de HMAC (possível ataque)
replayAttempts      Tentativas de replay detectadas
rateLimitHits       Rate limit excedido
sanityFailures      Falhas de sanidade
wrongStateBlocks    Comandos bloqueados por estado errado
envelopeViolations  Violações de envelope de voo
lockdownEvents      Número de lockdowns ativados
lastViolationTime   Timestamp da última violação
lastViolationReason Razão da última violação

8. Exemplo de Integração

```cpp
#include "Security.h"
#include "FlightControl.h"

Security security;
FlightControl fc;

void setup() {
    security.begin();
    security.setDebug(true);
    
    fc.begin(&actuators, &si, &gps);
}

void loop() {
    // Receber comando do ESP1 via UART
    if (Serial2.available()) {
        uint8_t buffer[256];
        size_t len = readPacket(buffer);
        
        // Extrair seqNum e HMAC
        uint32_t seqNum = extractSeqNum(buffer);
        uint8_t hmac[32];
        extractHMAC(buffer, hmac);
        
        // Construir TLVMessage
        TLVMessage msg;
        parseTLV(buffer, len, msg);
        
        // Verificar segurança
        SecurityResult result = security.verifyPacket(msg, seqNum, hmac);
        
        if (result == SEC_OK) {
            // Processar comando
            if (msg.msgID == MSG_COMMAND) {
                fc.processCommand(msg);
            } else if (msg.msgID == MSG_SHELL_CMD) {
                shell.processCommand(msg);
            }
        }
    }
    
    // Atualizar estado de voo na segurança
    security.setFlightState(mapModeToState(fc.getMode()));
}
´´´

9. Debug

Ativação

```cpp
security.setDebug(true);
´´´

Mensagens Típicas

[Security] begin() — Security inicializado
[Security] verifyPacket() — Pacote 0x12 válido
[Security] _recordViolation() — Violação #1/10 (tipo=1)
[Security] _updateSecurityLevel() — Nível alterado: 0 → 1
[Security] ⚠️⚠️⚠️ LOCKDOWN ATIVADO ⚠️⚠️⚠️

10. Constantes de Configuração

Constante                   Valor   Descrição
SECURITY_HMAC_KEY_SIZE      32      Tamanho da chave HMAC (bytes)
SECURITY_HMAC_SIZE          32      Tamanho do HMAC (bytes)
SECURITY_SEQ_WINDOW         32      Janela anti-replay (bits)
SECURITY_MAX_VIOLATIONS     10      Máximo de violações antes do lockdown
RATE_LIMIT_CRITICAL_MS      500     Intervalo para comandos críticos (ms)
RATE_LIMIT_CONTROL_MS       20      Intervalo para comandos de controlo (ms)
RATE_LIMIT_MAINTENANCE_MS   5000    Intervalo para manutenção (ms)

Fim da documentação – Security ESP2 v1.0.0

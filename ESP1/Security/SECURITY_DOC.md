# Security – Pasta `Security/`

> **Versão:** 2.0.0  
> **Data:** 2026-04-17  
> **Autor:** BeaconFly UAS Team

Esta documentação detalha o **módulo de segurança transversal** do sistema BeaconFly. Abrange os 2 arquivos da pasta: `Security.h` e `Security.cpp`, explicando a arquitetura de segurança, mecanismos de proteção, configuração e integração com os restantes módulos.

---

## 1. Objetivo do Módulo

O módulo `Security` é a **última linha de defesa** do sistema BeaconFly. Protege contra:

| Categoria         | Ameaças                                | Mecanismos
| ------------------|----------------------------------------|---------------------------------------
| **Comunicação**   | Spoofing, replay, flood, MITM          | HMAC-SHA256, anti-replay, rate limiting, challenge-response
| **Operacional**   | Comandos perigosos, sensores corruptos | Sanidade, envelope de voo, contexto de estado
| **Sistema**       | Travamentos, violações repetidas       | Watchdog, lockdown progressivo

**Princípio fundamental — FAIL SECURE:**
> Em caso de dúvida ou anomalia → BLOQUEAR.
>
> Uma mensagem bloqueada indevidamente é recuperável (perda de pacote).
> Um comando perigoso executado pode ser catastrófico (perda da aeronave).

---

## 2. Arquitetura de Segurança

### 2.1 Camadas de Proteção

┌─────────────────────────────────────────────────────────────────────────────────┐
│ PACOTE RECEBIDO (GS → ESP1)                                                     │
└─────────────────────────────────────────────────────────────────────────────────┘
│
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ CAMADA 1: LOCKDOWN                                                              │
│ • Se lockdown ativo → apenas MSG_FAILSAFE é aceite                              │
│ • Proteção contra ataques persistentes                                          │
└─────────────────────────────────────────────────────────────────────────────────┘
│ (se não lockdown)
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ CAMADA 2: AUTENTICAÇÃO GS                                                       │
│ • GS deve autenticar-se via challenge-response antes de enviar comandos         │
│ • Exceções: MSG_FAILSAFE e MSG_HEARTBEAT (críticos para segurança)              │
└─────────────────────────────────────────────────────────────────────────────────┘
│ (se autenticada)
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ CAMADA 3: HMAC-SHA256                                                           │
│ • Verifica integridade e autenticidade da mensagem                              │
│ • Impede spoofing e adulteração                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
│ (se HMAC válido)
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ CAMADA 4: ANTI-REPLAY                                                           │
│ • Janela deslizante de 32 bits                                                  │
│ • Impede gravação e reenvio de pacotes legítimos                                │
└─────────────────────────────────────────────────────────────────────────────────┘
│ (se sequência válida)
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ CAMADA 5: RATE LIMITING                                                         │
│ • Proteção contra flood de comandos                                             │
│ • Limites diferentes por categoria (crítico, controlo, manutenção)              │
└─────────────────────────────────────────────────────────────────────────────────┘
│ (dentro dos limites)
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ PACOTE VÁLIDO → PROCESSAR                                                       │
└─────────────────────────────────────────────────────────────────────────────────┘

text

### 2.2 Validação de Comandos (Pós-autenticação)

Após o pacote ser considerado válido, cada comando individual passa por validações adicionais:

COMANDO RECEBIDO
│
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ SANIDADE FÍSICA                                                                 │
│ • Valor dentro dos limites absolutos (ex: roll ≤ 60°)                           │
│ • Protege contra comandos fisicamente impossíveis                               │
└─────────────────────────────────────────────────────────────────────────────────┘
│ (se válido)
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ CONTEXTO DE ESTADO                                                              │
│ • Comando permitido no estado atual de voo?                                     │
│ • Ex: ARM só permitido em solo                                                  │
└─────────────────────────────────────────────────────────────────────────────────┘
│ (se permitido)
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ENVELOPE DE VOO                                                                 │
│ • Limites adaptados ao nível de segurança atual                                 │
│ • Ex: roll máximo reduzido em modo WARNING                                      │
└─────────────────────────────────────────────────────────────────────────────────┘
│ (dentro do envelope)
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ COMANDO SEGURO → EXECUTAR                                                       │
└─────────────────────────────────────────────────────────────────────────────────┘

---

## 3. Mecanismos de Segurança

### 3.1 HMAC-SHA256 (Autenticação)

**O que é:** Hash-based Message Authentication Code com SHA-256.

**Como funciona:**

1. A Ground Station calcula HMAC da mensagem usando uma chave secreta partilhada
2. O HMAC é enviado juntamente com a mensagem
3. O ESP1 recalcula o HMAC com a mesma chave e compara
4. Se forem iguais → mensagem autêntica e íntegra

**Porquê HMAC-SHA256:**

- Seguro contra ataques conhecidos
- 256 bits de segurança (padrão militar)
- Hardware-accelerated no ESP32 (mbedtls)

**Chave secreta:**

- 32 bytes (256 bits)
- Deve ser única por aeronave
- Armazenada em NVS ou eFuse (nunca em código)

### 3.2 Anti-Replay (Janela Deslizante)

**Problema:** Um atacante pode gravar um pacote legítimo e reenviá-lo mais tarde.

**Solução:** Números de sequência com janela deslizante de 32 bits.
Janela de 32 bits (cada bit representa uma sequência recente):

Última sequência recebida: 1000

Bitmap: [1];[0];[1];[1];[0];[0];[0];...;[0]
↑ ↑ ↑ ↑ ↑
| | | | └── seq 999 (visto)
| | | └───── seq 998 (visto)
| | └──────── seq 997 (não visto)
| └─────────── seq 996 (visto)
└────────────── seq 995 (visto)

Se chegar seq 997 → ACEITE (bit=0)
Se chegar seq 999 → REJEITADO (bit=1, replay)
Se chegar seq 950 → REJEITADO (fora da janela)

**Vantagens:**

- Tolerância a pacotes fora de ordem
- Memória mínima (apenas 32 bits)
- Deteção eficiente de replay

### 3.3 Rate Limiting

**Problema:** Um atacante pode enviar centenas de comandos por segundo, sobrecarregando o sistema.

**Solução:** Limites por categoria de comando.

| Categoria      | Comandos                                   | Intervalo mínimo | Taxa máxima
| ---------------|--------------------------------------------|------------------|-----------------
| **Crítico**    | ARM, DISARM, REBOOT, SHUTDOWN              | 500 ms           | 2 por segundo
| **Controlo**   | SET_ROLL, SET_PITCH, SET_YAW, SET_THROTTLE | 20 ms            | 50 por segundo
| **Manutenção** | SET_PARAM, CALIB, etc.                     | 5 segundos       | 0.2 por segundo

### 3.4 Challenge-Response (Autenticação da GS)

**Problema:** Como o ESP1 sabe que a Ground Station é legítima?

**Solução:** Protocolo de desafio-resposta:
ESP1                                       GS
│                                          │
│──── generateChallenge() ────────────────►│
│ (envia número aleatório de 32 bits)      │
│                                          │
│                                          │ calcula HMAC(challenge)
│                                          │
│◄──── verifyChallengeResponse(hmac) ──────│
│                                          │
│ verifica HMAC com chave secreta          │
│                                          │
│──── GS autenticada! ─────────────────────│

**Segurança:** O challenge é diferente a cada autenticação, prevenindo replay.

### 3.5 Lockdown Progressivo

**Problema:** Violações repetidas podem indicar um ataque em curso.

**Solução:** Níveis de segurança progressivos:

| Violações | Nível    | Comportamento
| ----------|----------|-------------------------------
| 0         | NORMAL   | Operação normal
| 1-2       | CAUTION  | Limites reduzidos, alerta
| 3-4       | WARNING  | Limites muito reduzidos
| 5-7       | CRITICAL | Apenas comandos de emergência
| 8+        | LOCKDOWN | Apenas MSG_FAILSAFE aceite

**Recuperação:** O lockdown requer **reset físico** do ESP1 (segurança máxima).

### 3.6 Sanidade de Comandos

Limites absolutos que NUNCA podem ser excedidos:

| Comando              | Limite  | Razão
| ---------------------|---------|-------------------------------
| `CMD_SET_ROLL`       | ±60°    | Limite estrutural da aeronave
| `CMD_SET_PITCH`      | ±45°    | Evitar stall
| `CMD_SET_YAW`        | ±180°/s | Evitar spin
| `CMD_SET_THROTTLE`   | 0-1     | Normalizado
| `CMD_SET_ALT_TARGET` | 0-120m  | Altitude legal (Europa)

### 3.7 Contexto de Estado de Voo

Comandos só são permitidos em estados específicos:

| Comando               | Estados permitidos
| ----------------------|-------------------------------------------
| `CMD_ARM`             | GROUND_DISARMED
| `CMD_DISARM`          | Todos exceto GROUND_DISARMED
| `CMD_SET_ROLL`, etc.  | FLYING, TAKEOFF, LANDING, GROUND_ARMED
| `CMD_REBOOT`          | GROUND_DISARMED, GROUND_ARMED
| `CMD_RTL`             | FLYING, ALT_HOLD, AUTO

### 3.8 Sanidade de Sensores

Protege contra leituras corruptas ou impossíveis:

| Sensor      | Limite       | Razão
| ------------|--------------|----------------------
| Roll/Pitch  | ±180°        | Ângulo físico máximo
| Yaw/Heading | 0-360°       | Ângulo circular
| Bateria     | 6-30V        | 2S a 6S LiPo
| Temperatura | -20 a 80°C   | Operacional
| Altitude    | -100 a 1000m | Sensato

---

## 4. Estados de Voo (`FlightState`)

| Estado                  | Descrição                  | Transições
| ------------------------|----------------------------|----------------------
| `STATE_GROUND_DISARMED` | Em solo, sistema desarmado | → ARM → GROUND_ARMED
| `STATE_GROUND_ARMED`    | Em solo, sistema armado    | → TAKEOFF → FLYING
| `STATE_TAKEOFF`         | Descolagem em progresso    | → FLYING
| `STATE_FLYING`          | Em voo normal              | → LANDING, FAILSAFE
| `STATE_LANDING`         | Aterragem em progresso     | → GROUND_DISARMED
| `STATE_FAILSAFE`        | Modo de segurança ativo    | → GROUND_DISARMED

**Importante:** O `FlightControl` deve chamar `setFlightState()` sempre que o estado muda.

---

## 5. Níveis de Segurança (`SecurityLevel`)

| Nível    | Violações | Limite Roll | Limite Pitch | Comandos permitidos
| ---------|-----------|-------------|--------------|---------------------------
| NORMAL   | 0         | 45°         | 35°          | Todos
| CAUTION  | 1-2       | 30°         | 25°          | Todos (reduzidos)
| WARNING  | 3-4       | 15°         | 15°          | Todos (muito reduzidos)
| CRITICAL | 5-7       | 10°         | 10°          | Apenas RTL, LAND, DISARM
| LOCKDOWN | 8+        | —           | —            | Apenas MSG_FAILSAFE

---

## 6. API Pública

### 6.1 Inicialização

```cpp
Security security;

void setup() {
    security.begin();
    
    // Opcional: carregar chave personalizada (produção)
    uint8_t myKey[32] = { ... };
    security.loadKey(myKey, 32);
    
    // Configuração avançada
    SecurityConfig config;
    config.enableGeofence = true;
    config.geofenceRadius = 500.0f;
    config.geofenceLat = 41.1496f;
    config.geofenceLon = -8.6109f;
    security.configure(config);
    
    security.setDebug(true);
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
            logSecurityEvent("HMAC inválido");
            break;
            
        case SEC_LOCKDOWN:
            // Sistema em lockdown — ignorar
            break;
            
        default:
            // Outras violações
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

6.4 Autenticação da Ground Station

```cpp
void onAuthenticationRequest() {
    // Gerar challenge
    uint32_t challenge = security.generateChallenge();
    sendToGS(challenge);
}

void onAuthenticationResponse(const uint8_t* response, size_t len) {
    if (security.verifyChallengeResponse(response, len)) {
        Serial.println("GS autenticada com sucesso!");
    } else {
        Serial.println("Falha na autenticação da GS");
    }
}
´´´

6.5 Computação de HMAC (para envio)

```cpp
void sendPacketToGS(const TLVMessage& msg) {
    // Serializar mensagem
    uint8_t buffer[1024];
    size_t len = serializeMessage(msg, buffer, sizeof(buffer));
    
    // Calcular HMAC
    uint8_t hmac[SECURITY_HMAC_SIZE];
    security.computeHMAC(buffer, len, hmac);
    
    // Enviar mensagem + HMAC
    lora.sendPacket(buffer, len, hmac);
}
´´´

6.6 Consulta de Estado

```cpp
// Estado de voo
security.setFlightState(STATE_FLYING);
FlightState state = security.getFlightState();

// Nível de segurança
SecurityLevel level = security.getSecurityLevel();

// Lockdown
bool lockdown = security.isInLockdown();

// Estatísticas
const SecurityStats& stats = security.getStats();
Serial.printf("HMAC failures: %lu\n", stats.hmacFailures);
´´´

7. Configuração

7.1 Configuração Padrão

```cpp
SecurityConfig config;
config.enableFailSecure = true;   // Fail Secure ativo
config.enableGeofence   = false;  // Geofencing desativado
config.enableWatchdog   = true;   // Watchdog ativo
config.geofenceRadius   = 500.0f; // Raio 500m
config.geofenceLat      = 0.0f;   // A definir
config.geofenceLon      = 0.0f;   // A definir
´´´

7.2 Ativação de Geofencing

```cpp
config.enableGeofence = true;
config.geofenceRadius = 300.0f;   // 300 metros
config.geofenceLat = homeLat;
config.geofenceLon = homeLon;
security.configure(config);
´´´

8. Estatísticas (SecurityStats)

Campo                   Descrição
packetsVerified         Total de pacotes verificados
hmacFailures            Falhas de HMAC (possível ataque)
replayAttempts          Tentativas de replay detectadas
rateLimitHits           Rate limit excedido
sanityFailures          Falhas de sanidade
wrongStateBlocks        Comandos bloqueados por estado errado
envelopeViolations      Violações de envelope de voo
lockdownEvents          Número de lockdowns ativados
lastViolationTime       Timestamp da última violação
lastViolationReason     Razão da última violação

9. Exemplo Completo de Integração

```cpp
#include "Security.h"
#include "LoRa.h"
#include "Parser.h"
#include "FlightControl.h"

Security security;
LoRa lora;
Parser parser;
FlightControl fc;

void setup() {
    Serial.begin(115200);
    
    // Inicializar segurança
    security.begin();
    security.setDebug(true);
    
    // Carregar chave (em produção, viria de NVS)
    uint8_t key[32];
    loadKeyFromNVS(key);
    security.loadKey(key, 32);
    
    // Inicializar LoRa
    LoRaPins pins = LoRaPins::defaultPins();
    LoRaConfig config = LoRaConfig::defaultConfig();
    lora.begin(LORA_MODULE_SX1280, pins, config);
    
    // Inicializar FlightControl
    fc.begin();
}

void loop() {
    lora.update();
    
    if (lora.available()) {
        uint8_t raw[1024];
        size_t len = lora.readPacket(raw, sizeof(raw));
        
        // Extrair seqNum e HMAC do pacote
        uint32_t seqNum = extractSeqNum(raw, len);
        uint8_t hmac[32];
        extractHMAC(raw, len, hmac);
        
        // Construir TLVMessage (via Parser)
        for (size_t i = 0; i < len - 36; i++) {  // -36 para HMAC+SEQ
            if (parser.feed(raw[i])) {
                TLVMessage* msg = parser.getMessage();
                
                // Verificar segurança
                SecurityResult result = security.verifyPacket(*msg, seqNum, hmac);
                
                if (result == SEC_OK) {
                    // Pacote autenticado — processar
                    fc.processCommand(*msg);
                } else if (result == SEC_LOCKDOWN) {
                    // Lockdown ativo — apenas failsafe
                    if (msg->msgID == MSG_FAILSAFE) {
                        fc.setMode(MODE_RTL);
                    }
                }
                
                parser.acknowledge();
            }
        }
    }
    
    // Atualizar estado de voo na segurança
    security.setFlightState(mapModeToState(fc.getMode()));
    
    fc.update();
    delay(10);
}

FlightState mapModeToState(FlightMode mode) {
    switch (mode) {
        case MODE_MANUAL:
        case MODE_STABILIZE:
        case MODE_ALT_HOLD:
        case MODE_AUTO:
            return STATE_FLYING;
        case MODE_RTL:
            return STATE_LANDING;
        default:
            return STATE_GROUND_DISARMED;
    }
}
´´´

10. Debug e Diagnóstico

10.1 Ativação

```cpp
security.setDebug(true);
´´´

10.2 Mensagens Típicas

[Security] begin() — Inicializando módulo de segurança...
[Security] begin() — Security inicializado. Challenge: 0xA4B3C2D1
[Security] begin() — Princípio Fail Secure ativo
[Security] verifyPacket() — Pacote 0x12 válido
[Security] verifyCommand() — Sanidade falhou: cmd=0xD2, val=75.00
[Security] _recordViolation() — Violação #1/10 (tipo=4)
[Security] _updateSecurityLevel() — Nível alterado: 0 → 1 (violações=1)
[Security] verifyChallengeResponse() — GS autenticada com sucesso!
[Security] ⚠️⚠️⚠️ LOCKDOWN ATIVADO ⚠️⚠️⚠️
[Security] triggerLockdown() — Razão: Máximo de violações atingido

11. Considerações de Segurança para Produção

⚠️ Aviso                 Descrição
Chave padrão            NUNCA usar a chave DEFAULT_HMAC_KEY em produção. É pública!
Armazenamento de chave  Usar NVS (Non-Volatile Storage) ou eFuse do ESP32
Chave única             Cada aeronave deve ter uma chave única
Distribuição de chave   Partilhar chave de forma segura com a GS (ex: Bluetooth seguro no primeiro boot)
Atualização de chave    Suportar rotação periódica de chaves
Logging                 Evitar log de chaves ou HMACs em texto claro

Exemplo de carregamento seguro de chave

```cpp
#include <nvs_flash.h>
#include <nvs.h>

bool loadKeyFromNVS(uint8_t* output) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("security", NVS_READONLY, &handle);
    if (err != ESP_OK) return false;
    
    size_t len = SECURITY_HMAC_KEY_SIZE;
    err = nvs_get_blob(handle, "hmac_key", output, &len);
    nvs_close(handle);
    
    return (err == ESP_OK && len == SECURITY_HMAC_KEY_SIZE);
}
´´´

12. Resumo dos Códigos de Retorno

Código                  Valor   Significado                 Ação recomendada
SEC_OK                  0       Tudo ok                     Processar normalmente
SEC_INVALID_HMAC        1       HMAC inválido               Rejeitar, log, contar violação
SEC_REPLAY_DETECTED     2       Replay detectado            Rejeitar, log, contar violação
SEC_RATE_LIMITED        3       Rate limit excedido         Rejeitar, aguardar
SEC_SANITY_FAILED       4       Sanidade falhou             Rejeitar, verificar sensor
SEC_LOCKDOWN            5       Lockdown ativo              Apenas MSG_FAILSAFE aceite
SEC_WRONG_STATE         6       Estado errado               Rejeitar, verificar modo
SEC_AUTH_REQUIRED       7       Autenticação necessária     Iniciar challenge-response
SEC_ENVELOPE_EXCEEDED   8       Envelope excedido           Rejeitar, reduzir setpoint
SEC_GEOFENCE_VIOLATION  9       Geofence violado            RTL imediato
SEC_WATCHDOG_TIMEOUT    10      Watchdog timeout            Reiniciar sistema

Fim da documentação – Security BeaconFly v2.0.0

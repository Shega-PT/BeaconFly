# Failsafe – Pasta `Failsafe/`

> **Versão:** 1.0.0
> **Data:** 2026-04-27
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O `Failsafe` é o módulo **mais importante** de todo o sistema BeaconFly. Responsável por:

| Responsabilidade             | Descrição                                            |
| -----------------------------|------------------------------------------------------|
| **Análise constante**        | Monitoriza todos os parâmetros de voo em tempo real  |
| **Deteção de anomalias**     | Identifica condições e classifica a gravidade        |
| **Decisão autónoma**         | Escolhe o modo mais adequado para cada situação      |
| **Comando do FlightControl** | Controla o FlightControl do ESP2 para executar ações |
| **Alarmes**                  | Ativa alarmes para alertar pilotos e pessoas no solo |
| **Comunicação**              | Envia estado atual para Ground Station via telemetria|

**Princípio fundamental:** O Failsafe NUNCA controla atuadores diretamente. Apenas comanda o FlightControl do ESP2, que por sua vez controla os atuadores.

---

## 2. Níveis de Failsafe

O sistema possui 6 níveis de gravidade, do 0 (normal) ao 5 (emergência máxima).

| Nível | Nome            | Descrição                                   |
| ------|-----------------|---------------------------------------------|
| 0     | **NORMAL**      | Nenhuma anomalia detetada. Operação normal. |
| 1     | **ALERTA**      | Apenas alerta via telemetria.               |
| 2     | **RTL**         | Regressa à origem.                          |
| 3     | **ATERRAGEM**   | Desce verticalmente de forma controlada.    |
| 4     | **PLANAGEM**    | Apenas estabilidade, motor desligado.       |
| 5     | **CORTE TOTAL** | Corta tudo. Avião cai livremente.           |

---

## 3. Transições entre Níveis

┌─────────────────────────────────────────┐
│ NÍVEL 0 — NORMAL                        │
│ (operação normal, sem alarmes)          │
└─────────────────────────────────────────┘
│
▼ (deteção de anomalia)
┌─────────────────────────────────────────┐
│ NÍVEL 1 — ALERTA                        │
│ (bateria baixa, GPS lost, temp alta)    │
│ apenas telemetria, sem alarme           │
└─────────────────────────────────────────┘
│
(condição agrava ou persiste)
│
┌─────────────────────────────────────────┐
│ NÍVEL 2 — RTL                           │
│ (perda de link, bateria baixa)          │
│ Regressa à origem                       │
│ Luz azul 1Hz                            │
└─────────────────────────────────────────┘
│
(condição agrava ou persiste)
│
┌─────────────────────────────────────────┐
│ NÍVEL 3 — ATERRAGEM                     │
│ (bateria crítica, falha ESP1)           │
│ Desce verticalmente                     │
│ Luz amarela 2Hz + 1 beep/s              │
└─────────────────────────────────────────┘
│
(condição agrava ou persiste)
│
┌─────────────────────────────────────────┐
│ NÍVEL 4 — PLANAGEM                      │
│ (falha motor, ângulos extremos)         │
│ Apenas estabilidade, motor=0            │
│ Luz laranja 3Hz + 2 beeps/s             │
└─────────────────────────────────────────┘
│
(condição agrava ou persiste)
│
┌─────────────────────────────────────────┐
│ NÍVEL 5 — CORTE TOTAL                   │
│ (falha IMU, bateria morta)              │
│ Corte total, queda livre                │
│ Luz vermelha 5Hz + alarme               │
└─────────────────────────────────────────┘

---

## 4. Timeouts (Adaptáveis)

Os timeouts variam conforme o modo de voo atual do ESP1:

| Modo de Voo     | ESP1    | GS       | Justificação                             |
| ----------------|---------|-- -------|------------------------------------------|
| **MANUAL**      | 3000 ms | 2000 ms  | Piloto humano precisa de resposta rápida |
| **Automáticos** | 5000 ms | 10000 ms | Modos autónomos toleram mais latência    |

---

## 5. Limiares de Deteção

### 5.1 Análise CONSTANTE (a cada ciclo ~100Hz)

| Parâmetro           | Limiar                  | Persistência | Ação                |
| --------------------|-------------------------|--------------|---------------------|
| Ângulo de roll      | > 60°                   | 1 segundo    | Nível 4 (Planagem)  |
| Ângulo de pitch     | > 45°                   | 1 segundo    | Nível 4 (Planagem)  |
| Velocidade angular  | > 300°/s                | Imediato     | Nível 4 (Planagem)  |
| Queda vertical (vz) | < -15 m/s               | Imediato     | Nível 4 (Planagem)  |
| Heartbeat ESP1      | Timeout conforme tabela | Imediato     | Nível 3 (Aterragem) |
| Link GS             | Timeout conforme tabela | Imediato     | Nível 2 (RTL)       |

### 5.2 Análise REGULAR (a cada 5-10 segundos)

| Parâmetro             | Limiar      | Ação                                    |
| ----------------------|-------------|-----------------------------------------|
| Tensão da bateria     | < 10.5V     | Nível 1 (Alerta) → depois Nível 2 (RTL) |
| Tensão da bateria     | < 9.0V      | Nível 3 (Aterragem)                     |
| Tensão da bateria     | < 8.5V      | Nível 5 (Corte Total)                   |
| Temperatura ESC/motor | > 80°C      | Nível 3 (Aterragem)                     |
| GPS válido            | Perda > 10s | Nível 1 (Alerta)                        |

### 5.3 Análise RARA (a cada 30-60 segundos)

| Parâmetro              | Limiar                          | Ação                   |
| -----------------------|---------------------------------|------------------------|
| Integridade IMU        | Acel total desvia >0.5g de 9.81 | Nível 5 (Corte Total)  |
| Saturação de atuadores | PWM no extremo por >5s          | Nível 4 (Planagem)     |

---

## 6. Alarmes (LED + Buzzer)

### 6.1 Configuração de Hardware

| Sinal             | Pino GPIO | Descrição                             |
| ------------------|-----------|---------------------------------------|
| `FS_LED_PIN`      | 32        | LED RGB (ou LED comum para testes)    |
| `FS_BUZZER_PIN`   | 33        | Buzzer ativo (PWM para tons variados) |

### 6.2 Comportamento dos Alarmes por Nível

| Nível | Luz (LED)                                 | Som (Buzzer)                    |
| ------|-------------------------------------------|---------------------------------|
| 0     | OFF                                       | OFF                             |
| 1     | OFF                                       | OFF                             |
| 2     | 🔵 Azul piscante (1 Hz)                   | OFF                             |
| 3     | 🟡 Amarelo piscante (2 Hz)                | 🔊 1 beep curto/segundo         |
| 4     | 🟠 Laranja piscante (3 Hz)                | 🔊 2 beeps curtos/segundo       |
| 5     | 🔴 Vermelho rápido (5 Hz) + estroboscópio | 🔊 Alarme contínuo intermitente |

### 6.3 Especificação Técnica dos Alarmes

**LED - Nível 2 (RTL):**

- Ciclo: 500ms ligado / 500ms desligado
- Frequência: 1 Hz

**LED - Nível 3 (Aterragem):**

- Ciclo: 250ms ligado / 250ms desligado
- Frequência: 2 Hz

**LED - Nível 4 (Planagem):**

- Ciclo: 166ms ligado / 166ms desligado
- Frequência: 3 Hz

**LED - Nível 5 (Corte Total):**

- Ciclo: 100ms ligado / 100ms desligado
- Frequência: 5 Hz (efeito estroboscópico)

**Buzzer - Nível 3 (Aterragem):**

- 1 beep curto (100ms) a cada 1 segundo

**Buzzer - Nível 4 (Planagem):**

- 2 beeps curtos (100ms cada, intervalo 100ms) a cada 1 segundo

**Buzzer - Nível 5 (Corte Total):**

- Tom contínuo intermitente: 500ms ligado / 500ms desligado

---

## 7. Razões de Ativação (`FailsafeReason`)

| Código | Constante                | Descrição                        | Nível típico |
| -------|--------------------------|----------------------------------|--------------|
| 0      | `FS_REASON_NONE`         | Sem razão (nível normal)         | 0            |
| 1      | `FS_REASON_LOST_ESP1`    | Perda de comunicação com ESP1    | 3            |
| 2      | `FS_REASON_LOST_GS`      | Perda de link com Ground Station | 2            |
| 3      | `FS_REASON_ANGLE_ROLL`   | Ângulo de roll excessivo         | 4            |
| 4      | `FS_REASON_ANGLE_PITCH`  | Ângulo de pitch excessivo        | 4            |
| 5      | `FS_REASON_ANGULAR_RATE` | Velocidade angular excessiva     | 4            |
| 6      | `FS_REASON_SINK_RATE`    | Queda vertical rápida            | 4            |
| 7      | `FS_REASON_BATT_LOW`     | Bateria baixa                    | 2            |
| 8      | `FS_REASON_BATT_CRITICAL`| Bateria crítica                  | 3            |
| 9      | `FS_REASON_BATT_DEAD`    | Bateria morta                    | 5            |
| 10     | `FS_REASON_TEMP_CRITICAL`| Temperatura crítica              | 3            |
| 11     | `FS_REASON_GPS_LOST`     | Perda de sinal GPS               | 1            |
| 12     | `FS_REASON_IMU_FAILURE`  | Falha da IMU                     | 5            |
| 13     | `FS_REASON_ACTUATOR_SAT` | Saturação de atuadores           | 4            |
| 14     | `FS_REASON_SYSTEM_HANG`  | Sistema travado (watchdog)       | 5            |

---

## 8. API Pública

### 8.1 Inicialização

```cpp
Failsafe failsafe;

void setup() {
    failsafe.begin();
    failsafe.setDebug(true);  // Opcional: ativar debug
}
´´´

8.2 Atualização de Dados (chamar no loop)

```cpp
void loop() {
    // Atualizar dados de entrada do failsafe
    failsafe.updateInputs(
        lastESP1Heartbeat,    // timestamp do último heartbeat do ESP1
        lastGSCommand,        // timestamp do último comando da GS
        flightMode,           // modo de voo atual (para timeouts adaptativos)
        flightState,          // estado atual do FlightControl (ângulos, etc.)
        gpsValid,             // flag indicando se GPS está válido
        battVoltage,          // tensão da bateria (volts)
        motorTemperature      // temperatura do motor/ESC (°C)
    );
    
    // Processar e executar
    FailsafeLevel level = failsafe.process();
    failsafe.execute(&flightControl);
}
´´´

8.3 Consulta de Estado

```cpp
// Obter nível atual
FailsafeLevel level = failsafe.getLevel();

// Obter razão da ativação
FailsafeReason reason = failsafe.getReason();

// Verificar se está ativo (nível >= 2)
bool active = failsafe.isActive();

// Obter timestamp da última ativação
uint32_t lastTime = failsafe.getLastActivationTime();
´´´

8.4 Reset Manual

```cpp
// Resetar o failsafe (apenas quando condições normalizam)
failsafe.reset();
´´´

8.5 Debug

```cpp
failsafe.setDebug(true);   // Ativar mensagens de debug
failsafe.setDebug(false);  // Desativar mensagens de debug
´´´

9. Exemplo de Integração Completa

```cpp
#include "Failsafe.h"
#include "FlightControl.h"
#include "Protocol.h"

Failsafe failsafe;
FlightControl flightControl;

void setup() {
    Serial.begin(115200);
    
    failsafe.begin();
    flightControl.begin();
    
    failsafe.setDebug(true);
}

void loop() {
    // Obter dados atuais
    uint32_t now = millis();
    uint32_t lastESP1Heartbeat = getLastESP1Heartbeat();  // Implementar
    uint32_t lastGSCommand = getLastGSCommand();          // Implementar
    uint8_t flightMode = flightControl.getMode();
    FlightState state = flightControl.getState();
    bool gpsValid = isGPSValid();                         // Implementar
    float battVoltage = getBatteryVoltage();              // Implementar
    float motorTemp = getMotorTemperature();              // Implementar
    
    // Atualizar failsafe
    failsafe.updateInputs(lastESP1Heartbeat, lastGSCommand, flightMode,
                          state, gpsValid, battVoltage, motorTemp);
    
    // Processar e executar
    failsafe.process();
    failsafe.execute(&flightControl);
    
    // Se failsafe não estiver ativo, o FlightControl opera normalmente
    if (!failsafe.isActive()) {
        flightControl.update();
    }
    
    delay(10);  // 100Hz
}
´´´

10. Fluxo de Decisão do Failsafe

                    ┌─────────────────────────────────────────┐
                    │         DADOS DE ENTRADA                │
                    │  • ESP1 heartbeat + modo                │
                    │  • GS link (comandos recebidos)         │
                    │  • Ângulos (roll, pitch)                │
                    │  • Velocidades angulares                │
                    │  • Bateria (tensão, corrente)           │
                    │  • GPS (válido? coordenadas?)           │
                    │  • Temperatura (ESC/motor)              │
                    │  • IMU integridade                      │
                    └─────────────────────────────────────────┘
                                          │
                                          ▼
                    ┌─────────────────────────────────────────┐
                    │         AVALIAR GRAVIDADE               │
                    │  • Falha crítica IMU? → Nível 5         │
                    │  • Bateria morta? → Nível 5             │
                    └─────────────────────────────────────────┘
                                          │ (não crítico)
                                          ▼
                    ┌───────────────────────────────────────────┐
                    │         AVALIAR CONTROLÁVEIS              │
                    │  • Velocidade angular excessiva? → Nível 4│
                    │  • Ângulos excessivos? → Nível 4          │
                    │  • Queda rápida? → Nível 4                │
                    │  • Saturação atuadores? → Nível 4         │
                    └───────────────────────────────────────────┘
                                          │ (não)
                                          ▼
                    ┌─────────────────────────────────────────┐
                    │         AVALIAR RECUPERÁVEIS            │
                    │  • Bateria crítica? → Nível 3           │
                    │  • Temperatura crítica? → Nível 3       │
                    │  • Falha ESP1? → Nível 3                │
                    │  • Perda link GS? → Nível 2             │
                    │  • Bateria baixa? → Nível 2             │
                    └─────────────────────────────────────────┘
                                          │ (não)
                                          ▼
                    ┌─────────────────────────────────────────┐
                    │         ALERTA APENAS                   │
                    │  • GPS perdido? → Nível 1               │
                    └─────────────────────────────────────────┘
                                          │ (nada)
                                          ▼
                    ┌─────────────────────────────────────────┐
                    │              NÍVEL 0 — NORMAL           │
                    └─────────────────────────────────────────┘

11. Mensagens de Debug (Exemplo)

[Failsafe] begin()           — Failsafe inicializado. LED no pino 32, Buzzer no pino 33
[Failsafe] process()         — Mudança de nível: 0 → 2
[Failsafe] process()         — FAILSAFE ATIVADO! Nível: 2, Razão: 2
[Failsafe] _executeRTL()     — Comando RTL enviado ao FlightControl
[Failsafe] process()         — Mudança de nível: 2 → 3
[Failsafe] _executeLand()    — Aterragem forçada: throttle=30%, modo ALT_HOLD
[Failsafe] process()         — Mudança de nível: 3 → 4
[Failsafe] _executeGlide()   — Planagem: throttle=0%, modo STABILIZE
[Failsafe] process()         — Mudança de nível: 4 → 5
[Failsafe] _executeFreeFall()— ⚠️ CORTE TOTAL ⚠️ Modo MANUAL, throttle=0
[Failsafe] process()         — FAILSAFE DESATIVADO
[Failsafe] reset()           — Failsafe resetado para nível NORMAL

12. Considerações de Segurança

Regra                                  Descrição
Nunca controla atuadores diretamente   Apenas comanda o FlightControl
Timeouts adaptativos                   Modo MANUAL tem timeouts mais curtos
Persistência temporal                  Ângulos excessivos só ativam após 1 segundo
Fallback inteligente                   Se GPS inválido, RTL vira aterragem
Alarmes progressivos                   Quanto pior a situação, mais intensos os alarmes
Reset manual                           Só resetar quando condições normalizam

Fim da documentação – Failsafe v1.0.0

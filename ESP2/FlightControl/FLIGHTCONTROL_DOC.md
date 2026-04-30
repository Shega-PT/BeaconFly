# FlightControl – Pasta `FlightControl/` (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-30
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O módulo `FlightControl` (ESP2) é o **controlador de voo de emergência** do sistema BeaconFly. Ao contrário do ESP1 (que tem autoridade total sobre o voo), o ESP2 **apenas executa ordens do Failsafe** quando este está ativo.

**FLUXO DE AUTORIDADE:**

| Situação           | Autoridade      | Controlador ativo     |
| -------------------|-----------------|-----------------------|
| **Voo normal**     | ESP1            | FlightControl do ESP1 |
| **Failsafe ativo** | ESP2 (Failsafe) | FlightControl do ESP2 |

---

## 2. Modos de Voo (ESP2)

| Modo             | Valor | Descrição           | Quando usar                          |
| -----------------|-------|---------------------|--------------------------------------|
| `MODE_IDLE`      | 0     | Aguardando comando  | Nenhum comando recebido              |
| `MODE_RTL`       | 1     | Return To Launch    | Falha de link, bateria baixa com GPS |
| `MODE_LAND`      | 2     | Aterragem forçada   | Bateria crítica, GPS inválido        |
| `MODE_GLIDE`     | 3     | Planagem controlada | Falha de motor, ângulos extremos     |
| `MODE_FREE_FALL` | 4     | Corte total         | Falha crítica IMU, bateria morta     |

---

## 3. Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ FAILSAFE (ESP2)                                                                 │
│                                                                                 │
│ • Analisa condições críticas                                                    │
│ • Decide o modo de emergência                                                   │
│ • Chama FlightControl::setCommand()                                             │
└─────────────────────────────────────────────────────────────────────────────────┘
│
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ FLIGHTCONTROL (ESP2)                                                            │
│                                                                                 │
│ ┌─────────────────────────────────────────────────────────────────────────┐     │
│ │ FlightControl::update()                                                 │     │
│ │                                                                         │     │
│ │ 1. Atualiza estado (SIConverter + GPS)                                  │     │
│ │ 2. Executa controlador conforme modo (RTL/LAND/GLIDE/FREE_FALL)         │     │
│ │ 3. Calcula PIDs (Roll, Pitch)                                           │     │
│ │ 4. Aplica mixer de ailerons                                             │     │
│ │ 5. Gera ActuatorSignals                                                 │     │
│ └─────────────────────────────────────────────────────────────────────────┘     │
│ │                                                                               │
│ ▼                                                                               │
│ ActuatorSignals                                                                 │
│ (wingR, wingL, rudder, motor)                                                   │
└─────────────────────────────────────────────────────────────────────────────────┘
│
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ACTUATORS (ESP2)                                                                │
│                                                                                 │
│ • Aileron Direito (roll)                                                        │
│ • Aileron Esquerdo (roll)                                                       │
│ • Leme (yaw)                                                                    │
│ • Motor (throttle)                                                              │
└─────────────────────────────────────────────────────────────────────────────────┘

---

## 4. Descrição dos Modos de Emergência

### 4.1 Modo RTL (Return To Launch)

| Parâmetro         | Valor | Descrição                          |
| ------------------|-------|------------------------------------|
| **Altitude alvo** | 50m   | Altitude segura para regresso      |
| **Throttle**      | 50%   | Potência de cruzeiro               |
| **Pitch**         | 5°    | Inclinação para avanço             |
| **Tolerância**    | 10m   | Distância para considerar "chegou" |

**Comportamento:**

1. Calcula bearing (rumo) entre posição atual e home
2. Converte bearing em erro de roll
3. Aplica PID de roll para curvar na direção correta
4. Mantém altitude constante
5. Ao chegar a casa (distância < 10m), inicia aterragem

### 4.2 Modo LAND (Aterragem Forçada)

| Altitude | Throttle | Descrição           |
| ---------|----------|---------------------|
| > 10m    | 30%      | Descida controlada  |
| 5-10m    | 20%      | Redução de potência |
| 2-5m     | 10%      | Aproximação final   |
| < 2m     | 0%       | Motor desligado     |

**Comportamento:**

- Mantém asas niveladas (roll = 0°)
- Pitch = 0° (nariz nivelado)
- Throttle reduz gradualmente com a altitude
- Ao tocar o solo (<0.5m), desarma os atuadores

### 4.3 Modo GLIDE (Planagem Controlada)

| Parâmetro        | Valor | Descrição                  |
| -----------------|-------|----------------------------|
| **Throttle**     | 0%    | Motor desligado            |
| **Roll máximo**  | 10°   | Apenas correções suaves    |
| **Pitch máximo** | 5°    | Limitado para evitar stall |

**Comportamento:**

- Motor desligado (planagem)
- PIDs ativos apenas para manter asas niveladas
- Roll limitado a 10° para evitar perda de controlo
- Pitch limitado a 5° para evitar stall

### 4.4 Modo FREE_FALL (Corte Total)

| Ação          | Descrição                 |
| --------------|---------------------------|
| **Throttle**  | 0% (cortado)              |
| **Servos**    | Posição neutra (1500µs)   |
| **Actuators** | `failsafeBlock()` ativado |

**Comportamento:**

- Desliga completamente os atuadores
- Avião cai livremente
- Último recurso quando todas as outras opções falham

---

## 5. PIDs (Apenas Roll e Pitch)

| Eixo  | Kp   | Ki   | Kd   | Limite (µs) | lpfAlpha |
| ------|------|------|------|-------------|----------|
| Roll  | 1.25 | 0.06 | 0.28 | 400         | 0.72     |
| Pitch | 1.10 | 0.05 | 0.25 | 400         | 0.72     |

**Nota:** O ESP2 não tem PID para Yaw (o leme não é utilizado nos modos de emergência).

---

## 6. Mixer (Avião Convencional)

**Fórmula para ailerons:**
wingR = neutro + rollCorr
wingL = neutro - rollCorr

| rollCorr | wingR | wingL | Efeito           |
| ---------|-------|-------|------------------|
| +100     | 1600  | 1400  | Virar à direita  |
| -100     | 1400  | 1600  | Virar à esquerda |
| 0        | 1500  | 1500  | Nivelado         |

**Leme:** Não utilizado nos modos de emergência (mantém neutro).

---

## 7. Navegação RTL

### 7.1 Cálculo de Bearing

bearing = atan2(sin(Δlon) · cos(lat2),
cos(lat1) · sin(lat2) - sin(lat1) · cos(lat2) · cos(Δlon))

### 7.2 Cálculo de Distância (Haversine)

a = sin²(Δlat/2) + cos(lat1) · cos(lat2) · sin²(Δlon/2)
c = 2 · atan2(√a, √(1-a))
distância = R · c (R = 6371000 m)

### 7.3 Bearing → Roll Error

rollError = (targetBearing - currentHeading) × 0.5
rollError = constrain(rollError, -45°, 45°)

---

## 8. API Pública

### 8.1 Inicialização

```cpp
#include "FlightControl.h"
#include "Actuators.h"
#include "SIConverter.h"
#include "GPS.h"

Actuators actuators;
SIConverter si;
GPS gps;
FlightControl fc;

void setup() {
    actuators.begin();
    si.begin();
    gps.begin();
    
    fc.begin(&actuators, &si, &gps);
    fc.setDebug(true);
}
´´´

8.2 Comando do Failsafe

```cpp
// RTL
FlightSetpoints sp;
fc.setCommand(MODE_RTL, sp);

// Aterragem forçada
FlightSetpoints landSp;
landSp.altitude = 0.0f;
landSp.throttle = 0.3f;
fc.setCommand(MODE_LAND, landSp);

// Planagem
fc.setCommand(MODE_GLIDE);

// Corte total
fc.setCommand(MODE_FREE_FALL);
´´´

8.3 Loop Principal

```cpp
void loop() {
    static uint32_t lastTime = 0;
    uint32_t now = micros();
    float dt = (now - lastTime) / 1000000.0f;
    lastTime = now;
    
    dt = constrain(dt, 0.001f, 0.1f);
    
    fc.update(dt);
    
    delay(10);  // 100Hz
}
´´´

8.4 Definição do Ponto de Origem (Home)

```cpp
// Definir home a partir do GPS
if (gps.hasFix()) {
    fc.setHomePosition(gps.getData().latitude,
                       gps.getData().longitude,
                       gps.getData().altitude);
}
´´´

8.5 Consultas

```cpp
FlightMode mode = fc.getMode();
bool active = fc.isActive();
ActuatorSignals outputs = fc.getOutputs();
const FlightState& state = fc.getState();
´´´

9. Exemplo de Integração com Failsafe

```cpp
#include "Failsafe.h"
#include "FlightControl.h"

Failsafe failsafe;
FlightControl fc;

void setup() {
    failsafe.begin();
    fc.begin(&actuators, &si, &gps);
}

void loop() {
    uint32_t now = micros();
    float dt = (now - lastTime) / 1000000.0f;
    lastTime = now;
    
    // Atualizar failsafe
    failsafe.updateInputs(...);
    FailsafeLevel level = failsafe.process();
    
    // Se failsafe ativo, comandar FlightControl
    if (failsafe.isActive()) {
        switch (level) {
            case FS_LEVEL_RTL:
                fc.setCommand(MODE_RTL);
                break;
            case FS_LEVEL_LAND:
                fc.setCommand(MODE_LAND);
                break;
            case FS_LEVEL_GLIDE:
                fc.setCommand(MODE_GLIDE);
                break;
            case FS_LEVEL_FREE_FALL:
                fc.setCommand(MODE_FREE_FALL);
                break;
            default:
                break;
        }
    }
    
    // Executar FlightControl (se ativo)
    fc.update(dt);
    
    // Se não ativo, o ESP1 tem controlo
    if (!fc.isActive()) {
        // ESP1 controla...
    }
}
´´´

10. Constantes de Navegação

Constante           Valor   Descrição
MAX_ROLL_ANGLE      45°     Máximo ângulo de roll
MAX_PITCH_ANGLE     35°     Máximo ângulo de pitch
GLIDE_ROLL_ANGLE    10°     Roll máximo em planagem
GLIDE_PITCH_ANGLE   5°      Pitch máximo em planagem
RTL_ALTITUDE        50m     Altitude para RTL
RTL_TOLERANCE       10m     Distância para considerar "chegou"
LAND_DESCENT_RATE   2 m/s   Taxa de descida na aterragem

11. Debug

Ativação

```cpp
fc.setDebug(true);
´´´

Mensagens Típicas

[FlightControl] begin() — FlightControl inicializado (modo IDLE)
[FlightControl] setCommand() — Modo RTL ativado
[FlightControl] _updateRTL() — Dist=150.3m, Roll=12.5°, Throttle=0.50
[FlightControl] _updateRTL() — Dist=45.2m, Roll=5.2°, Throttle=0.50
[FlightControl] _updateRTL() — Chegou a casa! Iniciando aterragem
[FlightControl] _updateLand() — Alt=50.0m, Throttle=0.30
[FlightControl] _updateLand() — Alt=10.2m, Throttle=0.20
[FlightControl] _updateLand() — Aterragem concluída! Desarmando

Fim da documentação – FlightControl ESP2 v1.0.0

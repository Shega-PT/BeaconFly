# Guia Rápido – FlightControl (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-30
> **Autor:** BeaconFly UAS Team

---

## 1. Modos de Voo (ESP2)

| Modo             | Valor | Descrição           |
| -----------------|-------|---------------------|
| `MODE_IDLE`      | 0     | Aguardando comando  |
| `MODE_RTL`       | 1     | Return To Launch    |
| `MODE_LAND`      | 2     | Aterragem forçada   |
| `MODE_GLIDE`     | 3     | Planagem controlada |
| `MODE_FREE_FALL` | 4     | Corte total         |

---

## 2. Código Rápido

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

void loop() {
    static uint32_t lastTime = micros();
    uint32_t now = micros();
    float dt = (now - lastTime) / 1000000.0f;
    lastTime = now;
    
    fc.update(dt);
    delay(10);
}
´´´

3. Comandos do Failsafe

```cpp
// RTL
fc.setCommand(MODE_RTL);

// Aterragem forçada
FlightSetpoints sp;
sp.altitude = 0.0f;
sp.throttle = 0.3f;
fc.setCommand(MODE_LAND, sp);

// Planagem
fc.setCommand(MODE_GLIDE);

// Corte total
fc.setCommand(MODE_FREE_FALL);
´´´

4. Definir Home (RTL)

```cpp
if (gps.hasFix()) {
    fc.setHomePosition(gps.getData().latitude,
                       gps.getData().longitude,
                       gps.getData().altitude);
}
´´´

5. Consultas

```cpp
FlightMode mode = fc.getMode();
bool active = fc.isActive();
ActuatorSignals outputs = fc.getOutputs();
const FlightState& state = fc.getState();
´´´

6. Comportamento por Modo

Modo        Throttle    Roll                Pitch               GPS necessário
RTL         50%         PID navegação       5°                  ✅ Sim
LAND        30%→0%      0°                  0°                  ❌ Não
GLIDE       0%          PID estabilização   PID estabilização   ❌ Não
FREE_FALL   0%          Neutro              Neutro              ❌ Não

7. PIDs (Roll e Pitch)

Eixo    Kp      Ki      Kd
Roll    1.25    0.06    0.28
Pitch   1.10    0.05    0.25

8. Mixer Ailerons

wingR = 1500 + rollCorr
wingL = 1500 - rollCorr

9. Debug

```cpp
fc.setDebug(true);
´´´

Mensagens:

[FlightControl] setCommand() — Modo RTL ativado
[FlightControl] _updateRTL() — Dist=150.3m, Roll=12.5°
[FlightControl] _updateRTL() — Chegou a casa! Iniciando aterragem

Fim do Guia Rápido – FlightControl ESP2 v1.0.0

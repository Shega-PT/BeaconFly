# Guia Rápido – Actuators (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-30
> **Autor:** BeaconFly UAS Team

---

## 1. Atuadores (ESP2 vs ESP1)

| Atuador      | ESP1      | ESP2       |
| -------------|-----------|------------|
| Asa direita  | ✅ Elevon | ✅ Aileron |
| Asa esquerda | ✅ Elevon | ✅ Aileron |
| Leme         | ✅        | ✅         |
| Motor        | ✅        | ✅         |
| Elevon R     | ✅        | ❌         |
| Elevon L     | ✅        | ❌         |
| **Total**    | **6**     | **4**      |

---

## 2. Pinos

| Atuador           | Pino GPIO | Canal LEDC |
| ------------------|-----------|------------|
| Aileron Direito   | 13        | 0          |
| Aileron Esquerdo  | 14        | 1          |
| Leme              | 27        | 2          |
| Motor             | 33        | 3          |

---

## 3. Valores de PWM

| Situação        | Ailerons (µs) | Leme (µs) | Motor (µs)  |
| -----.----------|---------------|-----------|-------------|
| Neutro          | 1500          | 1500      | 1000 (STOP) |
| Máximo positivo | 2000          | 2000      | 1900        |
| Máximo negativo | 1000          | 1000      | —           |
| Potência mínima | —             | —         | 1050        |

---

## 4. Código Rápido

```cpp
#include "Actuators.h"

Actuators actuators;

void setup() {
    actuators.begin();
    actuators.arm();
}

void loop() {
    ActuatorSignals signals;
    signals.wingR = 1500;
    signals.wingL = 1500;
    signals.rudder = 1500;
    signals.motor = 1200;
    
    actuators.update(signals);
    delay(10);
}
´´´

5. Comandos

```cpp
// Inicialização
actuators.begin();

// Armamento
actuators.arm();

// Atualização
actuators.update(signals);

// Desarmamento
actuators.disarm();

// Posição segura
actuators.safePosition();

// Failsafe
actuators.failsafeBlock();
actuators.failsafeClear();

// Consultas
bool armed = actuators.isArmed();
bool failsafe = actuators.isFailsafeActive();
ActuatorSignals last = actuators.getLastSignals();

// Debug
actuators.setDebug(true);
´´´

6. Slew Rate

Atuador     Slew Rate       Tempo para 500µs
Servos      30 µs/ciclo     ~17 ciclos (0.34s a 50Hz)
Motor       20 µs/ciclo     ~25 ciclos (0.5s a 50Hz)

7. Estados do Sistema

Estado      isArmed()   isFailsafeActive()  update()    arm()
Desarmado   false       false               Ignorado    ✅
Armado      true        false               ✅          Ignorado
Failsafe    false       true                Bloqueado   Bloqueado

8. Debug

```cpp
actuators.setDebug(true);
´´´

Mensagens:

[Actuators] update → WR:1500 WL:1500 Rud:1500 Mot:1200
[Actuators] ⚠️⚠️⚠️ FAILSAFE BLOCK ATIVADO ⚠️⚠️⚠️

Fim do Guia Rápido – Actuators ESP2 v1.0.0

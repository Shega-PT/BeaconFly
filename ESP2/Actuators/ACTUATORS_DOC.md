# Actuators – Pasta `Actuators/` (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-30
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O módulo `Actuators` (ESP2) é responsável pelo **controlo físico dos atuadores** ligados ao ESP2.

**DIFERENÇAS PARA O ESP1:**

| Aspeto            | ESP1                          | ESP2                                   |
| ------------------|-------------------------------|----------------------------------------|
| **Configuração**  | Asa delta (flying wing)       | Avião convencional                     |
| **Atuadores**     | 5 servos + 1 motor            | 2 servos (ailerons) + 1 leme + 1 motor |
| **Mixer**         | Mixer de elevons (pitch+roll) | Sem mixer (canais independentes)       |
| **Total**         | 6 canais                      | 4 canais                               |

---

## 2. Atuadores Controlados

| Atuador              | Sinal    | Função                          | Pino GPIO | Canal LEDC |
| ---------------------|----------|---------------------------------|-----------|------------|
| **Aileron Direito**  | `wingR`  | Roll (asa direita baixa/subida) | 13        | 0          |
| **Aileron Esquerdo** | `wingL`  | Roll (asa esquerda baixa/subida)| 14        | 1          |
| **Leme**             | `rudder` | Yaw (guinada)                   | 27        | 2          |
| **Motor**            | `motor`  | Throttle (potência)             | 33        | 3          |

---

## 3. Limites Físicos (PWM)

### 3.1 Servos (Ailerons e Leme)

| Constante           | Valor (µs) | Descrição                |
| --------------------|------------|--------------------------|
| `PWM_SERVO_MIN`     | 1000       | Deflexão máxima negativa |
| `PWM_SERVO_NEUTRAL` | 1500       | Posição neutra (centro)  |
| `PWM_SERVO_MAX`     | 2000       | Deflexão máxima positiva |

### 3.2 Motor (ESC)

| Constante        | Valor (µs) | Descrição                         |
| -----------------|------------|-----------------------------------|
| `PWM_MOTOR_STOP` | 1000       | Motor parado (ESC armado)         |
| `PWM_MOTOR_MIN`  | 1050       | Potência mínima útil (zona morta) |
| `PWM_MOTOR_MAX`  | 1900       | Potência máxima segura            |
| `PWM_MOTOR_ARM`  | 1000       | Pulso de armamento (2 segundos)   |

---

## 4. Configuração PWM

| Parâmetro      | Servos   | Motor    | Descrição       |
| ---------------|----------|----------|-----------------|
| **Frequência** | 50 Hz    | 50 Hz    | Período de 20ms |
| **Resolução**  | 16 bits  | 16 bits  | 65535 níveis    |
| **Período**    | 20000 µs | 20000 µs | 20ms            |

**Conversão µs → ticks:**
ticks = us * 65535 / 20000

Exemplo: 1500µs → 1500 * 65535 / 20000 = 4915 ticks

---

## 5. Slew Rate (Suavização)

| Atuador                 | Slew Rate (µs/ciclo) | Efeito              |
| ------------------------|----------------------|---------------------|
| Servos (ailerons, leme) | 30 µs/ciclo          | ~1500 µs/s (a 50Hz) |
| Motor                   | 20 µs/ciclo          | ~1000 µs/s (a 50Hz) |

**Porquê:** Evita movimentos bruscos que podem danificar servos ou destabilizar a aeronave.

---

## 6. Estados do Sistema

┌─────────────────────────────────────┐
│ SISTEMA DESARMADO                   │
│ (após begin() ou disarm())          │
└─────────────────────────────────────┘
│
▼ (arm() bem-sucedido)
┌─────────────────────────────────────┐
│ SISTEMA ARMADO                      │
│ (update() tem efeito)               │
└─────────────────────────────────────┘
│
▼ (failsafeBlock())
┌─────────────────────────────────────┐
│ FAILSAFE ATIVO                      │
│ (bloqueio físico das saídas)        │
│ • safePosition() forçado            │
│ • Canais LEDC detachados            │
│ • update() e arm() bloqueados       │
└─────────────────────────────────────┘
│
▼ (failsafeClear())
┌─────────────────────────────────────┐
│ SISTEMA DESARMADO                   │
│ (requer novo arm())                 │
└─────────────────────────────────────┘

---

## 7. API Pública

### 7.1 Inicialização

```cpp
#include "Actuators.h"

Actuators actuators;

void setup() {
    actuators.begin();
    actuators.setDebug(true);
}
´´´

7.2 Armamento e Controlo

```cpp
// Armar o sistema (enviar pulso ao ESC)
actuators.arm();

// Atualizar atuadores (chamado a 100Hz)
ActuatorSignals signals;
signals.wingR = 1500;    // neutro
signals.wingL = 1500;    // neutro
signals.rudder = 1500;   // neutro
signals.motor = 1200;    // potência baixa
actuators.update(signals);

// Desarmar
actuators.disarm();
´´´

7.3 Failsafe

```cpp
// Ativar failsafe (emergência)
actuators.failsafeBlock();

// Desativar failsafe (volta a desarmado)
actuators.failsafeClear();
´´´

7.4 Posição Segura

```cpp
// Forçar posição segura (ailerons neutro, motor STOP)
actuators.safePosition();
´´´

7.5 Consultas de Estado

```cpp
bool armed = actuators.isArmed();
bool failsafe = actuators.isFailsafeActive();
ActuatorSignals last = actuators.getLastSignals();

uint16_t wingR, wingL, rudder, motor;
actuators.getCurrentPWM(&wingR, &wingL, &rudder, &motor);
´´´

8. Exemplo de Integração com FlightControl

```cpp
#include "Actuators.h"
#include "FlightControl.h"

Actuators actuators;
FlightControl flightControl;

void setup() {
    actuators.begin();
    flightControl.begin();
    actuators.arm();
}

void loop() {
    // FlightControl calcula os valores desejados
    flightControl.update();
    
    // Obter sinais do FlightControl
    ActuatorSignals signals = flightControl.getOutputs();
    
    // Enviar para atuadores
    actuators.update(signals);
    
    delay(10);  // 100Hz
}
´´´

9. Debug

Ativação

```cpp
actuators.setDebug(true);
´´´

Mensagens Típicas

[Actuators] begin() — Inicializando sistema de atuadores (ESP2)...
[Actuators] _setupPWM() — Configurando LEDC (ESP32)
[Actuators]   Canal 0: pino=13, freq=50 Hz
[Actuators]   Canal 1: pino=14, freq=50 Hz
[Actuators]   Canal 2: pino=27, freq=50 Hz
[Actuators]   Canal 3: pino=33, freq=50 Hz
[Actuators] begin() — concluído. Sistema DESARMADO.
[Actuators]   Atuadores: Aileron D, Aileron E, Leme, Motor
[Actuators]   Servos: 1000-2000 µs | Motor: 1000-1900 µs

[Actuators] arm() — SISTEMA ARMADO com sucesso.

[Actuators] update → WR:1500 WL:1500 Rud:1500 Mot:1200

[Actuators] ⚠️⚠️⚠️ FAILSAFE BLOCK ATIVADO ⚠️⚠️⚠️
[Actuators] failsafeBlock() — Canais LEDC detachados.

10. Performance

Métrica                 Valor
Tempo update()          ~2 µs
Tempo arm()             2000 ms (bloqueante)
Tempo failsafeBlock()   ~100 µs
Jitter PWM              < 1 µs
Canais LEDC             4
Resolução               16 bits

11. Segurança

Mecanismo               Descrição
Constrain automático    Nenhum valor fora dos limites é escrito
ARM/DISARM explícito    Sistema só responde após armamento
Slew rate               Evita movimentos bruscos
Failsafe físico         Detacha canais LEDC em emergência
Posição segura          Neutro + STOP em qualquer emergência

Fim da documentação – Actuators ESP2 v1.0.0

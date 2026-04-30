# Guia Rápido – Failsafe

> **Versão:** 1.0.0
> **Data:** 2026-04-27
> **Autor:** BeaconFly UAS Team

---

## 1. Níveis de Failsafe (Resumo)

| Nível | Nome        | Quando                        | Alarme                      |
| ------|-------------|-------------------------------|-----------------------------|
| 0     | NORMAL      | Tudo ok                       | Nenhum                      |
| 1     | ALERTA      | Bateria baixa, GPS lost       | Nenhum                      |
| 2     | RTL         | Perda de link, bateria baixa  | Luz azul 1Hz                |
| 3     | ATERRAGEM   | Bateria crítica, falha ESP1   | Luz amarela 2Hz + 1 beep/s  |
| 4     | PLANAGEM    | Falha motor, ângulos extremos | Luz laranja 3Hz + 2 beeps/s |
| 5     | CORTE TOTAL | Falha IMU, bateria morta      | Luz vermelha 5Hz + alarme   |

---

## 2. Timeouts Rápidos

| Modo de Voo       | ESP1 (heartbeat) | GS (comandos) |
| ------------------|------------------|---------------|
| **MANUAL**        | 3 segundos       | 2 segundos    |
| **Automático**    | 5 segundos       | 10 segundos   |

---

## 3. Limiares Rápidos

| Parâmetro             | Limiar        | Persistência  |
| ----------------------|---------------|---------------|
| Roll                  | > 60°         | 1 segundo     |
| Pitch                 | > 45°         | 1 segundo     |
| Velocidade angular    | > 300°/s      | Imediato      |
| Queda vertical        | < -15 m/s     | Imediato      |
| Bateria baixa         | < 10.5V       | Imediato      |
| Bateria crítica       | < 9.0V        | Imediato      |
| Bateria morta         | < 8.5V        | Imediato      |
| Temperatura           | > 80°C        | Imediato      |
| GPS perdido           | > 10 segundos | 10 segundos   |

---

## 4. Pinos de Alarme

| Sinal             | Pino GPIO | Descrição          |
| ------------------|-----------|--------------------|
| `FS_LED_PIN`      | 32        | LED (RGB ou comum) |
| `FS_BUZZER_PIN`   | 33        | Buzzer ativo       |

---

## 5. Código Rápido

```cpp
#include "Failsafe.h"
#include "FlightControl.h"

Failsafe failsafe;
FlightControl flightControl;

void setup() {
    failsafe.begin();
    flightControl.begin();
}

void loop() {
    // Atualizar dados
    failsafe.updateInputs(heartbeatESP1, lastGSCommand, flightMode,
                          state, gpsValid, battVoltage, temperature);
    
    // Processar e executar
    failsafe.process();
    failsafe.execute(&flightControl);
    
    // Se não ativo, controlo normal
    if (!failsafe.isActive()) {
        flightControl.update();
    }
    
    delay(10);
}
´´´

6. Consulta de Estado

```cpp
// Nível atual
FailsafeLevel level = failsafe.getLevel();

// Razão da ativação
FailsafeReason reason = failsafe.getReason();

// Está ativo? (nível >= 2)
bool active = failsafe.isActive();

// Timestamp da última ativação
uint32_t lastTime = failsafe.getLastActivationTime();

// Reset (apenas quando condições normalizam)
failsafe.reset();
´´´

7. Comportamento dos Alarmes

Nível   Luz                 Som
0       OFF                 OFF
1       OFF                 OFF
2       🔵 Azul (1 Hz)      OFF
3       🟡 Amarelo (2 Hz)   1 beep/segundo
4       🟠 Laranja (3 Hz)   2 beeps/segundo
5       🔴 Vermelho (5 Hz)  Alarme intermitente

8. Razões de Ativação (Códigos)

Código  Constante                   Descrição
0       FS_REASON_NONE              Sem razão
1       FS_REASON_LOST_ESP1         Perda ESP1
2       FS_REASON_LOST_GS           Perda GS
3       FS_REASON_ANGLE_ROLL        Roll excessivo
4       FS_REASON_ANGLE_PITCH       Pitch excessivo
5       FS_REASON_ANGULAR_RATE      Velocidade angular
6       FS_REASON_SINK_RATE         Queda rápida
7       FS_REASON_BATT_LOW          Bateria baixa
8       FS_REASON_BATT_CRITICAL     Bateria crítica
9       FS_REASON_BATT_DEAD         Bateria morta
10      FS_REASON_TEMP_CRITICAL     Temperatura crítica
11      FS_REASON_GPS_LOST          GPS perdido
12      FS_REASON_IMU_FAILURE       Falha IMU
13      FS_REASON_ACTUATOR_SAT      Atuador saturado
14      FS_REASON_SYSTEM_HANG       Sistema travado

9. Debug

```cpp
failsafe.setDebug(true);   // Ativar
failsafe.setDebug(false);  // Desativar
´´´

Mensagens de debug típicas:

[Failsafe] process() — Mudança de nível: 0 → 2
[Failsafe] process() — FAILSAFE ATIVADO! Nível: 2, Razão: 2
[Failsafe] _executeRTL() — Comando RTL enviado ao FlightControl

Fim do Guia Rápido – Failsafe v1.0.0

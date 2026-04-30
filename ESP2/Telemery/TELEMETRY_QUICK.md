# Guia Rápido – Telemetry (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-29
> **Autor:** BeaconFly UAS Team

---

## 1. Modos de Telemetria

| Modo      | Taxa  | Dados                | Quando usar     |
| ----------|-------|----------------------|-----------------|
| `NORMAL`  | 10 Hz | Completos            | Operação normal |
| `FAST`    | 20 Hz | Reduzidos (críticos) | Failsafe        |
| `DEBUG`   | 20 Hz | Completos + extra    | Testes          |

---

## 2. Código Rápido

```cpp
#include "Telemetry.h"

Telemetry telemetry;

void setup() {
    telemetry.begin(&si, &gps, &fc, &failsafe, &lora);
    telemetry.setDebug(true);
}

void loop() {
    telemetry.send();           // Envia para GS (10Hz)
    telemetry.sendToESP1(si.getData());  // Envia para ESP1
}
´´´

3. Comandos

```cpp
// Modos
telemetry.setMode(TELEMETRY_MODE_NORMAL);
telemetry.setMode(TELEMETRY_MODE_FAST);
telemetry.setMode(TELEMETRY_MODE_DEBUG);

// Envio imediato
telemetry.sendImmediate();

// Ativar/desativar
telemetry.setEnabled(true);

// Estatísticas
uint32_t sent = telemetry.getPacketsSent();
telemetry.resetStats();

// Enviar comando para ESP1
telemetry.sendCommandToESP1(CMD_SET_ROLL, 15.0f);
´´´

4. Campos Enviados (Modo NORMAL)

Categoria       Campos
Atitude         roll, pitch, yaw, heading, roll_rate, pitch_rate, yaw_rate
Aceleração      accel_x, accel_y, accel_z
Bateria         batt_v, batt_a, batt_w, batt_soc
Temperaturas    temp_imu, temp_baro, tc0, tc1, tc2, tc3
GPS             lat, lon, alt, sats, hdop, speed
Estado          mode, failsafe_state

5. Exemplo de Pacote (JSON)

```json
{
  "roll": 2.34,
  "pitch": -1.23,
  "yaw": 145.67,
  "heading": 145.67,
  "batt_v": 12.34,
  "batt_a": 0.12,
  "alt": 50.2,
  "mode": 1
}
´´´

6. Debug

```cpp
telemetry.setDebug(true);
´´´

Mensagens:

[Telemetry] buildTelemetryPacket() — 12 TLVs, 156 bytes
[Telemetry] sendImmediate() — Pacote enviado (156 bytes)

Fim do Guia Rápido – Telemetry v1.0.0

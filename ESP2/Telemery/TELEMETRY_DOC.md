# Telemetry – Pasta `Telemetry/` (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-29
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O módulo `Telemetry` (ESP2) é responsável por **coletar, organizar e enviar todos os dados de telemetria para a Ground Station**.

**Responsabilidades principais:**

| Responsabilidade    | Descrição                                                           |
| --------------------|---------------------------------------------------------------------|
| **Coleta de dados** | Recolhe dados do SIConverter (SI), GPS, FlightControl e Failsafe    |
| **Construção TLV**  | Constrói mensagens no formato TLV (Type-Length-Value)               |
| **Envio via LoRa**  | Envia telemetria para a GS via LoRa 868MHz                          |
| **Envio para ESP1** | Envia dados SI de volta para o ESP1 via UART                        |
| **Gestão de modos** | Altera taxa de envio conforme estado do sistema (normal/failsafe)   |

---

## 2. Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP2                                                                            │
│                                                                                 │
│ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌──────────────┐                │
│ │ SIConverter │ │ GPS         │ │ Failsafe    │ │ FlightControl│                │
│ │ (SI)        │ │ (posição)   │ │ (estado)    │ │ (modo)       │                │
│ └──────┬──────┘ └──────┬──────┘ └──────┬──────┘ └──────┬───────┘                │
│ │      │               │               │               │                        |
│ └─────────────────┼─────────────────┼──────────────────┘                        │
│                   │                 │                                           │
│                   ▼                 ▼                                           │
│ ┌─────────────────────────────────┐                                             │
│ │ Telemetry::build()              │                                             │
│ │ • Constroi mensagem TLV         │                                             │
│ │ • Adiciona todos os campos      │                                             │
│ │ • Retorna buffer serializado    │                                             │
│ └─────────────────────────────────┘                                             │
│                     │                                                           │
│ ┌───────────────────┴───────────────────┐                                       │
│ │                                       │                                       │
│ ▼                                       ▼                                       │
│ ┌─────────────┐                  ┌─────────────┐                                │
│ │ LoRa 868MHz │                  │ UART (ESP1) │                                │
│ │ (para GS)   │                  │ (SI data)   │                                │
│ └─────────────┘                  └─────────────┘                                │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘

---

## 3. Modos de Operação

| Modo       | Taxa          | Descrição                | Quando usar                       |
| -----------|---------------|--------------------------|-----------------------------------|
| **NORMAL** | 10 Hz (100ms) | Todos os campos          | Operação normal                   |
| **FAST**   | 20 Hz (50ms)  | Apenas críticos          | Failsafe ativo                    |
| **DEBUG**  | 20 Hz (50ms)  | Todos os campos + extra  | Desenvolvimento/testes/FailSafe   |

### 3.1 Modo NORMAL (10Hz)

Envia pacotes completos com todos os dados disponíveis:

- Atitude (roll, pitch, yaw, heading)
- Aceleração (X, Y, Z)
- Bateria (tensão, corrente, potência, carga)
- Temperaturas (IMU, barómetro, 4 termopares)
- GPS (latitude, longitude, altitude, satélites, HDOP, velocidade)
- Estado do sistema (modo de voo, failsafe)

### 3.2 Modo FAST (20Hz)

Envia pacotes reduzidos com apenas dados críticos:

- Roll e pitch
- Altitude fundida
- Tensão da bateria
- Estado do failsafe

**Porquê reduzir?** Durante um failsafe, a prioridade é garantir que os dados críticos chegam rapidamente à GS.

### 3.3 Modo DEBUG (20Hz)

Envia pacotes completos com campos adicionais:

- Velocidades angulares detalhadas
- Dados de failsafe completos
- Timestamp interno

---

## 4. Campos TLV Transmitidos

### 4.1 Atitude (0x30-0x36)

| Campo             | ID   | Tipo  | Unidade | Descrição                |
| ------------------|------|-------|---------|--------------------------|
| `FLD_ROLL`        | 0x30 | float | graus   | Ângulo de rolamento      |
| `FLD_PITCH`       | 0x31 | float | graus   | Ângulo de inclinação     |
| `FLD_YAW`         | 0x32 | float | graus   | Ângulo de guinada        |
| `FLD_HEADING`     | 0x36 | float | graus   | Rumo magnético           |
| `FLD_ROLL_RATE`   | 0x37 | float | °/s     | Velocidade angular roll  |
| `FLD_PITCH_RATE`  | 0x38 | float | °/s     | Velocidade angular pitch |
| `FLD_YAW_RATE`    | 0x39 | float | °/s     | Velocidade angular yaw   |

### 4.2 Aceleração (0x33-0x35)

| Campo    | ID   | Tipo  | Unidade | Descrição    |
| ---------|------|-------|---------|--------------|
| `FLD_VX` | 0x33 | float | m/s²    | Aceleração X |
| `FLD_VY` | 0x34 | float | m/s²    | Aceleração Y |
| `FLD_VZ` | 0x35 | float | m/s²    | Aceleração Z |

### 4.3 Altitude e Pressão (0x40-0x41)

| Campo           | ID   | Tipo  | Unidade | Descrição           |
| ----------------|------|-------|---------|---------------------|
| `FLD_ALT_FUSED` | -    | float | metros  | Altitude fundida    |
| `FLD_ALT_BARO`  | 0x41 | float | hPa     | Pressão barométrica |

### 4.4 Bateria (0x50-0x54)

| Campo          | ID   | Tipo  | Unidade | Descrição           |
| ---------------|------|-------|---------|---------------------|
| `FLD_BATT_V`   | 0x50 | float | volts   | Tensão da bateria   |
| `FLD_BATT_A`   | 0x51 | float | amperes | Corrente            |
| `FLD_BATT_W`   | 0x52 | float | watts   | Potência            |
| `FLD_BATT_SOC` | 0x54 | float | 0-1     | Estado de carga     |

### 4.5 Temperatura (0x60-0x65)

| Campo           | ID   | Tipo  | Unidade | Descrição            |
| ----------------|------|-------|---------|----------------------|
| `FLD_ESP2_TEMP` | 0x65 | float | °C      | Temperatura ESP2     |
| `FLD_TEMP1`     | 0x60 | float | °C      | Termopar 1 (motor 1) |
| `FLD_TEMP2`     | 0x61 | float | °C      | Termopar 2 (motor 2) |
| `FLD_TEMP3`     | 0x62 | float | °C      | Termopar 3 (motor 3) |
| `FLD_TEMP4`     | 0x63 | float | °C      | Termopar 4 (motor 4) |

### 4.6 GPS (0x20-0x26)

| Campo             | ID   | Tipo     | Unidade     | Descrição                  |
| ------------------|------|----------|-------------|----------------------------|
| `FLD_GPS_LAT`     | 0x20 | int32_t  | graus × 10⁷ | Latitude                   |
| `FLD_GPS_LON`     | 0x21 | int32_t  | graus × 10⁷ | Longitude                  |
| `FLD_GPS_ALT`     | 0x22 | int32_t  | mm          | Altitude GPS               |
| `FLD_GPS_SPEED`   | 0x23 | uint16_t | cm/s        | Velocidade GPS             |
| `FLD_GPS_SATS`    | 0x24 | uint8_t  | -           | Número de satélites        |
| `FLD_GPS_HDOP`    | 0x26 | uint16_t | ×100        | HDOP (precisão horizontal) |

### 4.7 Sistema (0x70-0x76)

| Campo             | ID   | Tipo    | Unidade | Descrição          |
| ------------------|------|---------|---------|--------------------|
| `FLD_MODE`        | 0x71 | uint8_t | -       | Modo de voo        |
| `FLD_FS_STATE`    | 0xA3 | uint8_t | -       | Failsafe ativo (1) |
| `FLD_FS_REASON`   | 0xA1 | uint8_t | -       | Razão do failsafe  |
| `FLD_RX_LINK`     | 0x73 | uint8_t | %       | Qualidade RX       |
| `FLD_TX_LINK`     | 0x74 | uint8_t | %       | Qualidade TX       |

---

## 5. API Pública

### 5.1 Inicialização

```cpp
#include "Telemetry.h"
#include "SIConverter.h"
#include "GPS.h"
#include "FlightControl.h"
#include "Failsafe.h"
#include "LoRa.h"

SIConverter si;
GPS gps;
FlightControl fc;
Failsafe failsafe;
LoRa lora;
Telemetry telemetry;

void setup() {
    si.begin();
    gps.begin();
    fc.begin();
    failsafe.begin();
    lora.begin(LORA_MODULE_SX1262, config);
    
    telemetry.begin(&si, &gps, &fc, &failsafe, &lora);
    telemetry.setDebug(true);
}
´´´

5.2 Loop Principal

```cpp
void loop() {
    // Atualizar dados
    si.update(rawData);
    gps.update();
    
    // Enviar telemetria para GS
    telemetry.send();
    
    // Enviar dados SI de volta para ESP1
    telemetry.sendToESP1(si.getData());
    
    delay(10);
}
´´´

5.3 Modos de Telemetria

```cpp
// Modo normal (10Hz)
telemetry.setMode(TELEMETRY_MODE_NORMAL);

// Modo rápido (20Hz) - durante failsafe
telemetry.setMode(TELEMETRY_MODE_FAST);

// Modo debug (20Hz + dados extras)
telemetry.setMode(TELEMETRY_MODE_DEBUG);

// Ativar/desativar automaticamente pelo failsafe
telemetry.setFailsafeMode(failsafe.isActive());
´´´

5.4 Controlo de Envio

```cpp
// Envio automático (respeita intervalo)
telemetry.send();

// Envio imediato (ignora intervalo)
telemetry.sendImmediate();

// Ativar/desativar telemetria
telemetry.setEnabled(true);
telemetry.setEnabled(false);
´´´

5.5 Envio para ESP1

```cpp
// Enviar dados SI para ESP1
SIData data = si.getData();
telemetry.sendToESP1(data);

// Enviar comando para ESP1
telemetry.sendCommandToESP1(CMD_SET_ROLL, 15.0f);
´´´

5.6 Estatísticas e Diagnóstico

```cpp
// Número de pacotes enviados
uint32_t sent = telemetry.getPacketsSent();

// Resetar estatísticas
telemetry.resetStats();

// Status do módulo
char status[128];
telemetry.getStatusString(status, sizeof(status));
Serial.println(status);
´´´

6. Exemplo de Pacote TLV (Telemetria)

Mensagem MSG_TELEMETRY serializada:

START_BYTE (0xAA)
MSG_ID (0x11)
TLV_COUNT (0x0C)
[TLV1] ID=0x30 LEN=4 DATA=... (roll)
[TLV2] ID=0x31 LEN=4 DATA=... (pitch)
[TLV3] ID=0x32 LEN=4 DATA=... (yaw)
[TLV4] ID=0x36 LEN=4 DATA=... (heading)
[TLV5] ID=0x33 LEN=4 DATA=... (accel X)
[TLV6] ID=0x34 LEN=4 DATA=... (accel Y)
[TLV7] ID=0x35 LEN=4 DATA=... (accel Z)
[TLV8] ID=0x50 LEN=4 DATA=... (batt V)
[TLV9] ID=0x51 LEN=4 DATA=... (batt A)
[TLV10] ID=0x65 LEN=4 DATA=... (temp ESP2)
[TLV11] ID=0x71 LEN=1 DATA=... (flight mode)
[TLV12] ID=0x41 LEN=4 DATA=... (altitude)
CRC8 (0xXX)

7. Exemplo de Sessão de Telemetria (GS)

Pacote recebido (formato JSON para debug):

```json
{
  "roll": 2.34,
  "pitch": -1.23,
  "yaw": 145.67,
  "heading": 145.67,
  "accel_x": 0.12,
  "accel_y": -0.05,
  "accel_z": 9.81,
  "batt_v": 12.34,
  "batt_a": 0.12,
  "temp": 25.3,
  "mode": 1,
  "alt": 50.2
}
´´´

8. Exemplo de Sessão de Telemetria (Failsafe)

Pacote reduzido (modo FAST):

´´´json
{
  "roll": -35.67,
  "pitch": -28.45,
  "alt": 45.2,
  "batt_v": 10.8,
  "failsafe": 1
}
´´´

9. Debug

Ativação

```cpp
telemetry.setDebug(true);
´´´

Mensagens Típicas

[Telemetry] begin() — Telemetria inicializada (modo: NORMAL)
[Telemetry] buildTelemetryPacket() — 12 TLVs, 156 bytes
[Telemetry] sendImmediate() — Pacote enviado (156 bytes, modo=NORMAL)
[Telemetry] setMode() — Modo alterado: FAST
[Telemetry] buildCriticalPacket() — 5 TLVs, 68 bytes
[Telemetry] sendImmediate() — Pacote enviado (68 bytes, modo=FAST)

10. Performance

Métrica             Modo NORMAL     Modo FAST       Modo DEBUG
Taxa de envio       10 Hz           20 Hz           20 Hz
Tamanho do pacote   ~150 bytes      ~70 bytes       ~200 bytes
Largura de banda    ~1.5 KB/s       ~1.4 KB/s       ~4 KB/s
CPU usage           ~5%             ~8%             ~10%

11. Integração com LoRa

O módulo Telemetry utiliza o módulo LoRa para envio, atribuindo a prioridade LORA_PRIORITY_TELEMETRY (2), que é média/alta na fila de prioridades.

```cpp
void Telemetry::_sendToLoRa(const uint8_t* data, size_t len) {
    _lora->send(data, len, LORA_PRIORITY_TELEMETRY);
}
´´´

Fim da documentação – Telemetry v1.0.0

# Shell – Pasta `Shell/` (ESP2)

> **Versão:** 2.0.0
> **Data:** 2026-04-28
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O módulo `Shell` (ESP2) é a **consola de comando remota** do sistema BeaconFly.

**DIFERENÇAS CRÍTICAS PARA O ESP1:**

| Aspeto                  | ESP1                                | ESP2                                     |
| ------------------------|-------------------------------------|------------------------------------------|
| **Receção de comandos** | Via LoRa 2.4GHz (diretamente da GS) | Via UART (do ESP1)                       |
| **Envio de respostas**  | NUNCA envia (não tem TX)            | Envia para GS via LoRa 868MHz            |
| **Dados disponíveis**   | Dados de voo (PIDs, atuadores)      | Dados SI + GPS + temperaturas + bateria  |

**Princípio fundamental:** O ESP2 é o **responsável por enviar TODAS as respostas de shell para a Ground Station**, sejam elas originadas no ESP1 ou no próprio ESP2.

---

## 2. Arquitetura do Shell ESP2

### 2.1 Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────┐
│ GROUND STATION                                                              │
│                                                                             │
│ ┌─────────────────────────────────────────────────────────────────────┐     │
│ │ Software Ground Station                                             │     │
│ │ (Interface Web/Python + Joystick)                                   │     │
│ └─────────────────────────────────────────────────────────────────────┘     │
│ │                                                                           │
│ │ (Comando shell via LoRa 2.4GHz) │ (Resposta via LoRa 868MHz)              |
│ ▼                                                                           │
│ ┌─────────────┐ ┌─────────────┐                                             │
│ │ LoRa 2.4GHz │ │ LoRa 868MHz │                                             │
│ │ (RX da GS)  │ │ (TX da GS)  │                                             │
│ └─────────────┘ └─────────────┘                                             │
└─────────────────────────────────────────────────────────────────────────────┘
│
▼
┌────────────────────────────────────────────────────────────────────────────┐
│ ESP1                                                                       │
│                                                                            │
│ ┌───────────────────────────────────────────────────────────────────────┐  │
│ │ SHELL ESP1                                                            │  │
│ │                                                                       │  │
│ │ • Se comando for para ESP1 → processa                                 │  │
│ │ • Se comando for para ESP2 → encaminha                                │  │
│ └───────────────────────────────────────────────────────────────────────┘  │
│ │                                                                          │
│ │ (UART — Comando ou Resposta)                                             │
│ ▼                                                                          │
└────────────────────────────────────────────────────────────────────────────┘         │
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP2                                                                            │
│                                                                                 │
│ ┌───────────────────────────────────────────────────────────────────────┐       │
│ │ SHELL ESP2                                                            │       │
│ │                                                                       │       │
│ │ • Processa comandos DESTINADOS A ESP2                                 │       │
│ │ • Reencaminha respostas do ESP1 para a GS                             │       │
│ │ • Envia respostas do ESP2 diretamente para a GS                       │       │
│ └───────────────────────────────────────────────────────────────────────┘       │
│ │                                                                               │
│ ▼                                                                               │
│ ┌───────────────────────────────────────────────────────────────────────┐       │
│ │ LoRa 868MHz (TX APENAS — NUNCA RECEBE)                                │       │
│ │ • Envia respostas shell para GS                                       │       │
│ └───────────────────────────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────────────────────────┘

text

### 2.2 Comandos Disponíveis (ESP2 tem MAIS dados)

| Categoria | Comandos | Quantidade |
|-----------|----------|:----------:|
| Sistema | help, status, logbook, clear | 4 |
| Atitude | get_roll, get_pitch, get_yaw, get_heading | 4 |
| Aceleração | get_accel_x, get_accel_y, get_accel_z | 3 |
| Giroscópio | get_gyro_x, get_gyro_y, get_gyro_z | 3 |
| Altitude/Pressão | get_alt, get_pressure | 2 |
| Bateria | get_batt, get_current, get_power, get_charge | 4 |
| Temperaturas | get_temp_imu, get_temp_baro, get_tc0-3 | 6 |
| GPS | get_gps_lat, get_gps_lon, get_gps_alt, get_gps_sats, get_gps_hdop, get_gps_speed | 6 |
| Estado | get_mode, get_security, get_failsafe | 3 |
| Diagnóstico | get_loop_time, get_heap, get_uptime | 3 |
| Controlo | set_roll, set_pitch, set_yaw, set_alt, set_throttle, set_mode | 6 |
| Sistema | reboot | 1 |
| Debug | debug_on, debug_off, echo_on, echo_off, test | 5 |
| **TOTAL** | | **48** |

---

## 3. Formato dos Comandos (com destinatário)

| Formato          | Significado      | Exemplo               |
| -----------------|------------------|-----------------------|
| `[ESP1] comando` | Executar no ESP1 | `[ESP1] get_roll`     |
| `[ESP2] comando` | Executar no ESP2 | `[ESP2] get_temp_imu` |
| `comando`        | Padrão: ESP1     | `status`              |

---

## 4. Segurança

### 4.1 Controlo de Acesso por Estado de Voo

| Tipo de Comando    | Em solo | Em voo normal | Em failsafe |
| -------------------|---------|---------------|-------------|
| Leitura (`get_*`)  | ✅      | ✅            | ✅          |
| Controlo (`set_*`) | ✅      | ✅            | ✅          |
| Sistema (`reboot`) | ✅      | ❌            | ❌          |

### 4.2 Limites de Segurança

| Parâmetro             | Limite | Descrição                        |
| ----------------------|--------|----------------------------------|
| `MAX_ROLL_DEGREES`    | 45°    | Ângulo máximo de roll            |
| `MAX_PITCH_DEGREES`   | 35°    | Ângulo máximo de pitch           |
| `MAX_ALTITUDE_METERS` | 400 m  | Altitude máxima legal (Europa)   |
| `MAX_THROTTLE_PERCENT`| 100%   | Potência máxima do motor         |

---

## 5. API Pública

### 5.1 Inicialização

```cpp
#include "Shell.h"
#include "SIConverter.h"
#include "GPS.h"
#include "Security.h"
#include "FlightControl.h"
#include "Failsafe.h"
#include "LoRa.h"

SIConverter si;
GPS gps;
Security sec;
FlightControl fc;
Failsafe failsafe;
LoRa lora;
Shell shell;

void setup() {
    si.begin();
    gps.begin();
    sec.begin();
    fc.begin();
    failsafe.begin();
    lora.begin(LORA_MODULE_SX1262, config);
    
    shell.begin(&si, &gps, &sec, &fc, &failsafe, &lora);
    shell.setDebug(true);
    shell.setEcho(true);
}
´´´

5.2 Processamento de Comandos (via UART do ESP1)

```cpp
void loop() {
    if (Serial2.available()) {
        char line[256];
        int len = Serial2.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';
        shell.processCommand(line);
    }
}
´´´

5.3 Processamento de Respostas do ESP1

```cpp
void loop() {
    if (Serial2.available()) {
        char response[1024];
        int len = Serial2.readBytesUntil('\n', response, sizeof(response) - 1);
        response[len] = '\0';
        shell.processESP1Response(response);
    }
}
´´´

5.4 Atualização de Estado

```cpp
shell.setFlightState(inFlight, failsafeActive);
´´´

6. Exemplos de Comandos e Respostas

Comandos de Leitura

> get_roll

Roll: 2.34°

> get_batt

Tensão: 12.34 V

> get_gps_lat

Latitude: 41.149612

> get_temp_imu

Temperatura IMU: 25.3 °C

Comandos de Controlo

> set_roll 15

Setpoint roll: 15.0° (enviado para ESP1)

> set_throttle 50

Throttle: 50.0% (enviado para ESP1)

> set_mode 1

Modo de voo: 1 (enviado para ESP1)

Comandos de Sistema

> status

=== ESTADO DO SISTEMA (ESP2) ===
Shell inicializado: SIM
Comandos registados: 48
Em voo: NÃO
Failsafe: INATIVO

Dados SI:
  Roll: 2.34° | Pitch: -1.23° | Yaw: 145.67°
  Bateria: 12.34V, 0.12A, 1.48W, 0.034Ah

GPS:
  Lat: 41.149612 | Lon: -8.610912 | Alt: 50.2m
  Sats: 12 | HDOP: 0.8

Modo de voo: 1
========================

> reboot

Reiniciando ESP2 em 1 segundo...
Comandos de Debug

> debug_on

Debug ativado.

> test

Teste de diagnóstico (ESP2):
  Shell inicializado: SIM
  Comandos registados: 48
  Em voo: NÃO
  Failsafe: INATIVO
  SIConverter: OK
  GPS: VÁLIDO
  LoRa: PRONTO

7. Logbook

O logbook é um buffer circular com 100 entradas. Cada entrada contém:

Campo       Descrição
timestamp   millis() no momento do log
level       ERROR (0), WARN (1), INFO (2), DEBUG (3)
message     Mensagem de texto

Comandos do Logbook

> logbook

=== LOGBOOK ===
[1000] [INFO] Shell ESP2 inicializado — 48 comandos registados
[2000] [INFO] Estado de voo: EM SOLO | Failsafe: INATIVO
[3000] [INFO] Comando get_roll processado
[4000] [INFO] Comando set_roll 15 processado
===============

> clear

Logbook limpo.

8. Níveis de Log
Nível               Valor   Uso
LOG_LEVEL_ERROR     0       Erros críticos
LOG_LEVEL_WARNING   1       Avisos
LOG_LEVEL_INFO      2       Informação normal
LOG_LEVEL_DEBUG     3       Debug (apenas desenvolvimento)

9. Debug

Ativação

```cpp
shell.setDebug(true);
´´´

Mensagens Típicas

[Shell ESP2] begin() concluído — 48 comandos
[Shell ESP2] setDebug() — Modo debug ATIVADO
[Shell ESP2] sendToLoRa() — Resposta enviada para GS
[Shell ESP2] processESP1Response() — Resposta do ESP1 reencaminhada para GS

10. Resumo dos Comandos

Categoria   Comandos
Leitura     get_roll, get_pitch, get_yaw, get_heading, get_accel_x, get_accel_y, get_accel_z, get_gyro_x, get_gyro_y, get_gyro_z, get_alt, get_pressure, get_batt, get_current, get_power, get_charge, get_temp_imu, get_temp_baro, get_tc0, get_tc1, get_tc2, get_tc3, get_gps_lat, get_gps_lon, get_gps_alt, get_gps_sats, get_gps_hdop, get_gps_speed, get_mode, get_security, get_failsafe, get_loop_time, get_heap, get_uptime
Controlo    set_roll, set_pitch, set_yaw, set_alt, set_throttle, set_mode
Sistema     reboot
Debug       debug_on, debug_off, echo_on, echo_off, test
Ajuda       help, status, logbook, clear

Fim da documentação – Shell ESP2 v2.0.0

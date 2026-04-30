# Guia Rápido – Shell ESP2

> **Versão:** 2.0.0
> **Data:** 2026-04-28
> **Autor:** BeaconFly UAS Team

---

## 1. Comandos Essenciais

| Comando        | O que faz            | Exemplo           |
| ---------------|----------------------|-------------------|
| `help`         | Mostra ajuda         | `help`            |
| `status`       | Estado do sistema    | `status`          |
| `get_roll`     | Ângulo de roll       | `get_roll`        |
| `get_pitch`    | Ângulo de pitch      | `get_pitch`       |
| `get_alt`      | Altitude             | `get_alt`         |
| `get_batt`     | Tensão da bateria    | `get_batt`        |
| `get_current`  | Corrente             | `get_current`     |
| `get_power`    | Potência             | `get_power`       |
| `get_charge`   | Carga consumida (Ah) | `get_charge`      |
| `get_temp_imu` | Temperatura IMU      | `get_temp_imu`    |
| `get_temp_baro`| Temperatura barómetro| `get_temp_baro`   |
| `get_tc0`      | Termopar 0 (motor 1) | `get_tc0`         |
| `get_gps_lat`  | Latitude GPS         | `get_gps_lat`     |
| `get_gps_lon`  | Longitude GPS        | `get_gps_lon`     |
| `get_gps_sats` | Número de satélites  | `get_gps_sats`    |
| `get_mode`     | Modo de voo          | `get_mode`        |
| `get_failsafe` | Estado do failsafe   | `get_failsafe`    |
| `get_heap`     | Memória livre        | `get_heap`        |
| `get_uptime`   | Tempo de atividade   | `get_uptime`      |
| `set_roll`     | Define roll          | `set_roll 15`     |
| `set_throttle` | Define potência      | `set_throttle 50` |
| `set_mode`     | Define modo de voo   | `set_mode 1`      |
| `reboot`       | Reinicia ESP2        | `reboot`          |
| `debug_on`     | Ativa debug          | `debug_on`        |
| `logbook`      | Mostra histórico     | `logbook`         |

---

## 2. Prefixos de Destinatário

| Prefixo  | Significado               | Exemplo               |
| ---------|---------------------------|-----------------------|
| `[ESP1]` | Executar no ESP1 (padrão) | `[ESP1] get_roll`     |
| `[ESP2]` | Executar no ESP2          | `[ESP2] get_temp_imu` |

---

## 3. Modos de Voo (set_mode)

| Valor | Modo      |
| ------|-----------|
| 0     | MANUAL    |
| 1     | STABILIZE |
| 2     | ALT_HOLD  |
| 3     | POSHOLD   |
| 4     | AUTO      |
| 5     | RTL       |

---

## 4. Respostas Típicas

### Comando bem-sucedido

get_batt
Tensão: 12.34 V

### Estado do sistema

status
=== ESTADO DO SISTEMA (ESP2) ===
Shell inicializado: SIM
Comandos registados: 48
Em voo: NÃO
Failsafe: INATIVO
...

---

## 5. Debug

```cpp
shell.setDebug(true);
´´´

Mensagens:

[Shell ESP2] sendToLoRa() — Resposta enviada para GS

6. Exemplos Rápidos

Verificar estado antes do voo

> status
> get_batt
> get_gps_sats
> get_mode

Monitorizar durante o voo

> get_roll
> get_pitch
> get_alt
> get_batt

Emergência

> set_mode 5    # RTL
> set_throttle 0

Diagnóstico pós-voo

> logbook
> get_charge
> get_temp_imu

Fim do Guia Rápido – Shell ESP2 v2.0.0

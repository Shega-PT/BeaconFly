# Guia Rápido – Shell BeaconFly

> **Versão:** 2.0.0  
> **Uso:** Referência rápida para operação e debug

---

## 1. Regras de Ouro

| Regra | Descrição                                |
|-------|------------------------------------------|
| 1     | ESP1 NUNCA transmite via LoRa            |
| 2     | ESP2 NUNCA recebe via LoRa               |
| 3     | TODAS as respostas à GS passam pelo ESP2 |
| 4     | Comunicação ESP1↔ESP2 via UART           |

---

## 2. Comandos Essenciais

| Comando        | O que faz         | Exemplo           |
| ---------------|-------------------|-------------------|
| `help`         | Mostra ajuda      | `help`            |
| `status`       | Estado do sistema | `status`          |
| `get_roll`     | Ângulo de roll    | `get_roll`        |
| `get_alt`      | Altitude          | `get_alt`         |
| `get_batt`     | Bateria           | `get_batt`        |
| `set_roll`     | Define roll       | `set_roll 15`     |
| `set_throttle` | Potência motor    | `set_throttle 50` |
| `set_mode`     | Modo de voo       | `set_mode 1`      |
| `arm`          | Armar motores     | `arm`             |
| `disarm`       | Desarmar          | `disarm`          |
| `reboot`       | Reiniciar         | `reboot`          |

---

## 3. Prefixos de Destinatário

| Prefixo  | Significado      | Exemplo           |
| ---------|------------------|-------------------|
| `[ESP1]` | Executar no ESP1 | `[ESP1] get_roll` |
| `[ESP2]` | Executar no ESP2 | `[ESP2] get_temp` |
| (nenhum) | Padrão = ESP1    | `status`          |

---

## 4. Modos de Voo (set_mode)

| Valor | Modo      |
| ------|-----------|
| 0     | MANUAL    |
| 1     | STABILIZE |
| 2     | ALT_HOLD  |
| 3     | POSHOLD   |
| 4     | AUTO      |
| 5     | RTL       |

---

## 5. Eixos PID (set_pid / get_pid)

| Axis | Eixo     |
| -----|----------|
| 0    | Roll     |
| 1    | Pitch    |
| 2    | Yaw      |
| 3    | Altitude |

---

## 6. Trims (set_trim)

set_trim <wingR> <wingL> <rudder> <elevonR> <elevonL>

---

## 7. Níveis de Log

| Nível | Valor |
|-------|-------|
| ERROR | 0     |
| WARN  | 1     |
| INFO  | 2     |
| DEBUG | 3     |

---

## 8. Exemplos Rápidos

### Verificar estado

status
get_batt
get_alt

### Armar e descolar

arm
set_throttle 30
set_roll 0
set_pitch 5

### Mudar para modo ALT_HOLD

set_mode 2
set_alt 50

### Retorno de emergência

set_mode 5

### Ajustar PIDs

get_pid 0
set_pid 0 1.35 0.07 0.32

### Comando no ESP2

[ESP2] get_temp
[ESP2] status

### Debug

debug_on
test
logbook

---

## 9. Respostas Típicas

### Comando bem-sucedido

set_roll 15
Setpoint roll: 15.00°

### Comando bloqueado em voo

reboot
ERRO: Comando 'reboot' bloqueado em voo!

### Argumentos insuficientes

set_pid
ERRO: set_pid requer pelo menos 4 argumento(s)
Uso: set_pid <axis> <Kp> <Ki> <Kd>

### Comando sem destinatário (no ESP2)

status
ERRO: Comando sem destinatário [ESP1] ou [ESP2]
Uso: [ESP1] comando ou [ESP2] comando

---

## 10. Debug — Ativação

```cpp
shell.setDebug(true);
´´´

Mensagens de debug:

[Shell] begin() concluído — 38 comandos (ESP2=NÃO)
[Shell] forwardToESP2: comando 'get_temp' encaminhado (origem=1)

Fim do Guia Rápido – Shell BeaconFly v2.0.0

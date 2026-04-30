# Shell – Pasta `Shell/`

> **Versão:** 2.0.0  
> **Data:** 2026-04-19  
> **Autor:** BeaconFly UAS Team

Esta documentação detalha o **módulo de consola de comandos** do sistema BeaconFly. Abrange os 2 arquivos da pasta: `Shell.h` e `Shell.cpp`, explicando a arquitetura de comunicação entre ESPs, comandos disponíveis, segurança e integração.

---

## 1. Objetivo do Módulo

O módulo `Shell` é a **consola de comando remota** do BeaconFly. Permite:

| Funcionalidade    | Descrição                                    |
| ------------------|----------------------------------------------|
| **Diagnóstico**   | Ler estado do sistema, sensores, PIDs, trims |
| **Configuração**  | Alterar PIDs, trims, limites, modos de voo   |
| **Controlo**      | Armar/desarmar motores, definir setpoints    |
| **Logging**       | Consultar logbook, adicionar marcadores      |
| **Debug**         | Ativar/desativar debug, eco de comandos      |

**Canais de comunicação suportados:**

- **USB (Serial)** – Debug local (PC ligado ao ESP1)
- **LoRa 2.4GHz** – Receção de comandos da Ground Station (ESP1 apenas RX)
- **LoRa 868MHz** – Transmissão de respostas para GS (ESP2 apenas TX)
- **UART** – Comunicação interna entre ESP1 e ESP2

---

## 2. Arquitetura de Comunicação (VERSÃO CORRETA)

### 2.1 Diagrama de Fluxo

┌─────────────────────────────────────────────────────────────────────────────┐
│ GROUND STATION                                                              │
│                                                                             │
│ ┌─────────────────────────────────────────────────────────────────────┐     │
│ │ Software Ground Station                                             │     │
│ │ (Interface Web/Python + Joystick)                                   │     │
│ └─────────────────────────────────────────────────────────────────────┘     │
│ │                                                                           │
│ │ (Comandos)      (Respostas)                                               │
│ ▼      |               |                                                    │
│ ┌────────────┐  ┌─────────────┐                                             │
│ │ LoRa 2.4GHz│  │ LoRa 868MHz │                                             │
│ │ (TX APENAS)│  │ (RX APENAS) │                                             │
│ └────────────┘  └─────────────┘                                             │
└─────────────────────────────────────────────────────────────────────────────┘
│
│
▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ ESP1                                                                        │
│                                                                             │
│ ┌───────────────────────────────────────────────────────────────────────┐   │
│ │ LoRa 2.4GHz (RX APENAS — NUNCA TRANSMITE)                             │   │
│ │ • Recebe comandos da GS                                               │   │
│ │ • NUNCA envia nada via LoRa                                           │   │
│ └───────────────────────────────────────────────────────────────────────┘   │
│ │                                                                           │
│ ▼                                                                           │
│ ┌───────────────────────────────────────────────────────────────────────┐   │
│ │ SHELL (ESP1)                                                          │   │
│ │                                                                       │   │
│ │ • Processa comandos DESTINADOS A ESP1                                 │   │
│ │ • Encaminha comandos para ESP2 via UART                               │   │
│ │ • NUNCA envia respostas diretamente (não tem TX)                      │   │
│ │ • Respostas vão para ESP2 via UART para serem transmitidas            │   │
│ └───────────────────────────────────────────────────────────────────────┘   │
│ │                                                                           │
│ │ (UART — Comandos + Respostas)                                             │
│ ▼                                                                           │
└─────────────────────────────────────────────────────────────────────────────┘
│
▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ ESP2                                                                        │
│                                                                             │
│ ┌───────────────────────────────────────────────────────────────────────┐   │
│ │ SHELL (ESP2)                                                          │   │
│ │                                                                       │   │
│ │ • Processa comandos DESTINADOS A ESP2                                 │   │
│ │ • Gera respostas para TODOS os comandos (ESP1 ou ESP2)                │   │
│ │ • Único que transmite via LoRa                                        │   │
│ └───────────────────────────────────────────────────────────────────────┘   │
│ │                                                                           │
│ ▼                                                                           │
│ ┌───────────────────────────────────────────────────────────────────────┐   │
│ │ LoRa 868MHz (TX APENAS — NUNCA RECEBE)                                │   │
│ │ • Envia respostas para GS                                             │   │
│ │ • NUNCA recebe nada via LoRa                                          │   │
│ └───────────────────────────────────────────────────────────────────────┘   │
│ │                                                                           │
│ ▼ (Respostas via 868MHz)                                                    │
└─────────────────────────────────────────────────────────────────────────────┘

### 2.2 Fluxos Completos

#### 📡 Comando para ESP1 (ex: `get_roll`)

GS → (2.4GHz TX) → ESP1 (RX) → Shell ESP1 → processa comando
│
▼
Resposta via UART → ESP2
│
▼
ESP2 → (868MHz TX) → GS (RX)

#### 📡 Comando para ESP2 (ex: `[ESP2] get_temp`)

GS → (2.4GHz TX) → ESP1 (RX) → Shell ESP1 → identifica [ESP2]
│
▼
Encaminha via UART → ESP2
│
▼
Shell ESP2 processa comando
│
▼
Resposta via (868MHz TX) → GS (RX)

#### 📡 Comando via USB (debug local)

PC → USB → ESP1 → Shell ESP1 → processa ou encaminha
│
▼
Resposta via USB → PC (direto, sem LoRa)

### 2.3 Regras de Ouro (NUNCA ESQUECER)

| Regra | Descrição                                                           |
| ------|---------------------------------------------------------------------|
| **1** | ESP1 NUNCA transmite via LoRa (não tem hardware de TX 868MHz)       |
| **2** | ESP2 NUNCA recebe via LoRa (não tem hardware de RX 2.4GHz)          |
| **3** | TODAS as respostas para a GS passam pelo ESP2 (único com TX 868MHz) |
| **4** | ESP1 e ESP2 comunicam entre si via UART (bidirecional)              |
| **5** | USB é apenas para debug local (PC ligado diretamente ao ESP1)       |

---

## 3. Formato dos Comandos

### 3.1 Com Destinatário Explícito

| Formato          | Significado          | Exemplo           |
| -----------------|----------------------|-------------------|
| `[ESP1] comando` | Executar no ESP1     | `[ESP1] get_roll` |
| `[ESP2] comando` | Encaminhar para ESP2 | `[ESP2] get_temp` |
| `comando`        | Padrão: ESP1         | `status`          |

### 3.2 Formato UART (Interno)

| Tipo     | Formato                         | Exemplo                   |
| ---------|---------------------------------|---------------------------|
| Comando  | `[CMD]origem:tamanho:comando`   | `[CMD]1:8:get_roll`       |
| Resposta | `[RESP]origem:tamanho:resposta` | `[RESP]1:20:Roll: 15.23°` |

---

## 4. Lista Completa de Comandos

### 4.1 Sistema e Ajuda (6 comandos)

| Comando        | Sintaxe         | Descrição                               | Bloqueado |
| ---------------|-----------------|-----------------------------------------|-----------|
| `help`         | `help [comando]`| Mostra ajuda de um comando específico   | ❌        |
| `status`       | `status`        | Mostra estado completo do sistema       | ❌        |
| `logbook`      | `logbook`       | Mostra o logbook (últimos 100 eventos)  | ❌        |
| `clear`        | `clear`         | Limpa o logbook                         | ❌        |
| `save`         | `save`          | Salva configuração atual na NVS         | ✅        |
| `reset_config` | `reset_config`  | Reseta configuração para valores padrão | ✅        |

### 4.2 Leitura de Sensores (12 comandos)

| Comando        | Sintaxe         | Descrição                               | Bloqueado |
| ---------------|-----------------|-----------------------------------------|-----------|
| `get_roll`     | `get_roll`      | Lê ângulo de roll atual (graus)         | ❌        |
| `get_pitch`    | `get_pitch`     | Lê ângulo de pitch atual (graus)        | ❌        |
| `get_yaw`      | `get_yaw`       | Lê ângulo de yaw atual (graus)          | ❌        |
| `get_heading`  | `get_heading`   | Lê rumo magnético (graus)               | ❌        |
| `get_alt`      | `get_alt`       | Lê altitude atual (metros)              | ❌        |
| `get_batt`     | `get_batt`      | Lê tensão (V) e corrente (A) da bateria | ❌        |
| `get_outputs`  | `get_outputs`   | Lê sinais PWM atuais (µs)               | ❌        |
| `get_mode`     | `get_mode`      | Lê modo de voo atual (0-5)              | ❌        |
| `get_security` | `get_security`  | Lê nível de segurança e estado lockdown | ❌        |
| `get_pid`      | `get_pid <axis>`| Lê ganhos PID de um eixo                | ❌        |
| `get_trim`     | `get_trim`      | Lê trims atuais de todos os canais      | ❌        |
| `get_temp`     | `get_temp`      | Lê temperaturas (ESP1, ESP2, motores)   | ❌        |

### 4.3 Controlo de Voo (8 comandos — PERMITIDOS em voo para emergência)

| Comando | Sintaxe | Descrição | Bloqueado |
| --------|---------|-----------|-----------|
| `set_roll` | `set_roll <graus>` | Define setpoint de roll (-45 a +45) | ❌ |
| `set_pitch` | `set_pitch <graus>` | Define setpoint de pitch (-35 a +35) | ❌ |
| `set_yaw` | `set_yaw <graus>` | Define setpoint de yaw | ❌ |
| `set_alt` | `set_alt <metros>` | Define altitude alvo (0-400) | ❌ |
| `set_throttle` | `set_throttle <0-100>` | Define potência do motor (%) | ❌ |
| `set_mode` | `set_mode <0-5>` | Define modo de voo | ❌ |
| `set_pid` | `set_pid <axis> <Kp> <Ki> <Kd>` | Define ganhos PID | ❌ |
| `set_trim` | `set_trim <WR> <WL> <Rud> <ER> <EL>` | Define trims (valores em µs) | ❌ |

### 4.4 Comandos de Sistema (4 comandos — BLOQUEADOS em voo)

| Comando       | Sintaxe                        | Descrição                     |Bloqueado|
| --------------|--------------------------------|-------------------------------|---------|
| `reboot`      | `reboot`                       | Reinicia o ESP1 ou ESP2       | ✅      |
| `arm`         | `arm`                          | Arma os motores               | ✅      |
| `disarm`      | `disarm`                       | Desarma os motores            | ✅      |
| `calibrate`   | `calibrate <sensor\|actuator>` | Calibra sensores ou atuadores | ✅      |

### 4.5 Debug e Diagnóstico (5 comandos)

| Comando     | Sintaxe     | Descrição                         | Bloqueado |
| ------------|-------------|-----------------------------------|-----------|
| `debug_on`  | `debug_on`  | Ativa mensagens de debug do shell | ❌        |
| `debug_off` | `debug_off` | Desativa mensagens de debug       | ❌        |
| `echo_on`   | `echo_on`   | Ativa eco de comandos             | ❌        |
| `echo_off`  | `echo_off`  | Desativa eco de comandos          | ❌        |
| `test`      | `test`      | Executa teste de diagnóstico      | ❌        |

### 4.6 Telemetria e Logging (3 comandos)

| Comando     | Sintaxe         | Descrição                           | Bloqueado |
| ------------|-----------------|-------------------------------------|-----------|
| `start_log` | `start_log`     | Inicia gravação de telemetria no SD | ❌        |
| `stop_log`  | `stop_log`      | Para gravação de telemetria         | ❌        |
| `marker`    | `marker <texto>`| Adiciona marcador ao log            | ❌        |

---

## 5. Modos de Voo (set_mode)

| Valor | Modo      | Descrição                 |
| ------|-----------|---------------------------|
| 0     | MANUAL    | Passthrough direto        |
| 1     | STABILIZE | Estabilização angular     |
| 2     | ALT_HOLD  | Manutenção de altitude    |
| 3     | POSHOLD   | Manutenção de posição GPS |
| 4     | AUTO      | Navegação por waypoints   |
| 5     | RTL       | Return To Launch          |

---

## 6. Eixos PID (set_pid / get_pid)

| Axis | Eixo     | Descrição  |
| -----|----------|------------|
| 0    | Roll     | Rolamento  |
| 1    | Pitch    | Inclinação |
| 2    | Yaw      | Guinada    |
| 3    | Altitude | Altitude   |

---

## 7. Trims (set_trim)

A ordem dos 5 argumentos é:

set_trim <wingR> <wingL> <rudder> <elevonR> <elevonL>

text

Exemplo: `set_trim 5 -3 0 2 -2`

---

## 8. Níveis de Log

| Nível | Valor | Uso               |
| ------|-------|-------------------|
| ERROR | 0     | Erro crítico      |
| WARN  | 1     | Aviso             |
| INFO  | 2     | Informação normal |
| DEBUG | 3     | Debug             |

---

## 9. Exemplos de Sessão

### Sessão via USB (debug local)

=== BeaconFly UAS Shell v3.0 (ESP1) ===
NOTA: Respostas via LoRa 868MHz (ESP2)
Type 'help' for commands

#### help

Comandos disponíveis:

COMANDO     DESCRIÇÃO
help        Mostra ajuda
status      Mostra estado do sistema
get_roll    Lê ângulo de roll
get_pitch   Lê ângulo de pitch
set_roll    Define setpoint de roll
set_pitch   Define setpoint de pitch
arm         Arma os motores [!]
disarm      Desarma os motores [!]
...
[!] = bloqueado em voo (segurança)

#### status

=== ESTADO DO SISTEMA ===

- ESP: ESP1
- Shell inicializado: SIM
- Comandos registados: 38
- Logbook entries: 2/100
- Em voo: NÃO
- Failsafe: INATIVO
- Roll: 2.34°
- Pitch: -1.23°
- Heading: 145.67°
- Altitude: 0.00m
- Bateria: 12.34V
- GPS: VÁLIDO

========================

set_roll 15
Setpoint roll: 15.00°

[ESP2] get_temp
Temperatura: ESP1=45.2°C | ESP2=52.1°C

---

## 10. API Pública

### 10.1 Inicialização (ESP1)

```cpp
#include "Shell.h"
#include "FlightControl.h"
#include "Security.h"
#include "Telemetry.h"

FlightControl fc;
Security sec;
Telemetry tel;
Shell shell;

void setup() {
    fc.begin();
    sec.begin();
    tel.begin();
    
    // ESP1: isESP2 = false
    shell.begin(&fc, &sec, &tel, false);
    shell.setDebug(true);
    shell.setEcho(true);
}
´´´

10.2 Inicialização (ESP2)

```cpp
// ESP2: isESP2 = true
shell.begin(nullptr, &sec, &tel, true);

// Configurar callback para LoRa 868MHz (TX)
shell.setLoRaCallback([](const uint8_t* data, size_t len) {
    lora_868.send(data, len);
});
´´´

10.3 Loop Principal (ESP1)

```cpp
void loop() {
    // Verificar UART (comunicação com ESP2)
    shell.updateUART();
    
    // Verificar comandos via USB
    if (Serial.available()) {
        char line[256];
        int len = Serial.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';
        shell.processCommand(line, SHELL_ORIGIN_USB);
    }
    
    // Verificar comandos via LoRa 2.4GHz
    if (lora_24.available()) {
        uint8_t buffer[256];
        size_t len = lora_24.readPacket(buffer, sizeof(buffer));
        // Construir TLVMessage e chamar:
        // shell.processShellCommand(msg, SHELL_ORIGIN_LORA);
    }
}
´´´

10.4 Loop Principal (ESP2)

```cpp
void loop() {
    // Verificar UART (comunicação com ESP1)
    shell.updateUART();
    
    // ESP2 não recebe comandos diretamente via LoRa
    // Apenas processa comandos vindos da UART
}
´´´

11. Resumo dos Códigos de Origem (ShellOrigin)

Código                  Valor   Descrição
SHELL_ORIGIN_USB        0       Comando veio da USB (PC local)
SHELL_ORIGIN_LORA       1       Comando veio da LoRa 2.4GHz (GS)
SHELL_ORIGIN_UART       2       Comando veio da UART (do outro ESP)
SHELL_ORIGIN_INTERNAL   3       Comando interno (testes)

Fim da documentação – Shell BeaconFly v2.0.0

# 🚀 BeaconFly UAS — ESP2 (Communication & Video Processor)

> **Versão:** 2.0.0
> **Data:** 2026-04-30
> **Autor:** BeaconFly UAS Team

---

## 📖 Visão Geral

O **ESP2** é o nó de comunicação e interface externa do sistema BeaconFly. Responsável por:

| Funcionalidade             | Descrição                                                       |
| ---------------------------|-----------------------------------------------------------------|
| **Conversão RAW → SI**     | Converte dados crus dos sensores do ESP1 em unidades SI         |
| **Processamento GPS**      | Lê e processa dados de posicionamento global                    |
| **Failsafe**               | Vigia constante, analisa condições e aciona modos de emergência |
| **Controlo de emergência** | Assume controlo do UAS quando failsafe ativo                    |
| **Telemetria**             | Envia dados de voo para a Ground Station via LoRa 868MHz        |
| **Vídeo**                  | Transmite vídeo (prova de conceito)                             |
| **Shell remoto**           | Processa comandos shell e envia respostas via LoRa              |
| **Beacon legal**           | Transmite identificação via Wi-Fi (EASA/DGAC/ANAC)              |

**Princípio fundamental:** O ESP2 NUNCA recebe comandos diretamente da GS. Todas as comunicações incoming vêm do ESP1 via UART.

---

## 🏗️ Arquitetura do Sistema

### Organização das Pastas

| Pasta            | Função     | Responsabilidade                                      |
| -----------------|------------|-------------------------------------------------------|
| `Protocol/`      | Língua     | Definição de mensagens TLV (partilhado com ESP1)      |
| `Parser/`        | Tradutor   | Reconstrução de mensagens TLV byte-a-byte             |
| `SIConverter/`   | Conversor  | RAW → unidades SI                                     |
| `GPS/`           | Posição    | Processamento de dados GPS                            |
| `Failsafe/`      | Vigia      | Análise e ativação de modos de emergência             |
| `FlightControl/` | Controlo   | PIDs e controlo de emergência (quando failsafe ativo) |
| `Actuators/`     | Músculos   | Controlo de ailerons, leme e motor                    |
| `Telemetry/`     | Memória    | Envio de telemetria para GS via LoRa                  |
| `Video/`         | Visão      | Processamento e transmissão de vídeo (POC)            |
| `Shell/`         | Voz        | Consola de comandos remota                            |
| `LoRa/`          | Rádio      | Driver LoRa 868MHz (TX only)                          |
| `Beacon/`        | Identidade | Sinal Wi-Fi de identificação legal                    |
| `Security/`      | Escudo     | Autenticação HMAC, anti-replay, lockdown              |

### Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP1                                                                            │
│                                                                                 │
│ Sensors (RAW) → UART TX →                                                       │
└─────────────────────────────────────────────────────────────────────────────────┘
│
▼ (UART RX)
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP2                                                                            │
│                                                                                 │
│ ┌─────────────────────────────────────────────────────────────────────┐         │
│ │ MAIN LOOP (100Hz)                                                   │         │
│ │                                                                     │         │
│ │ 1. SIConverter: RAW → SI                                            │         │
│ │ 2. GPS: atualiza posição                                            │         │
│ │ 3. Failsafe: analisa condições                                      │         │
│ │ 4. FlightControl: executa (se failsafe ativo)                       │         │
│ │ 5. Telemetry: envia para GS (LoRa 868MHz)                           │         │
│ │ 6. Shell: processa comandos                                         │         │
│ │ 7. Beacon: transmite Wi-Fi                                          │         │
│ │ 8. Video: processa e transmite                                      │         │
│ └─────────────────────────────────────────────────────────────────────┘         │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
│
┌───────────────────┼───────────────────┐
│                   │                   │
▼                   ▼                   ▼
┌────────────┐ ┌───────────┐ ┌────────────┐
│ LoRa 868MHz│ │ Wi-Fi     │ │ UART       │
│ (para GS)  │ │ (Beacon)  │ │ (para ESP1)│
└────────────┘ └───────────┘ └────────────┘

---

## 🔌 Pinagem (PCB BeaconFly v1.0)

### UART (ESP1 ↔ ESP2)

| Sinal            | Pino GPIO | Descrição             |
| -----------------|-----------|-----------------------|
| TX (ESP2 → ESP1) | 17        | Transmissão para ESP1 |
| RX (ESP2 ← ESP1) | 16        | Receção do ESP1       |

### LoRa 868MHz (SX1262) — TX only

| Pino | GPIO | Descrição   |
| -----|------|-------------|
| CS   | 5    | Chip Select |
| DIO1 | 4    | Interrupção |
| RST  | 2    | Reset       |
| BUSY | 15   | Busy        |

### GPS (opcional)

| Sinal | Pino GPIO | Descrição                  |
| ------|-----------|----------------------------|
| RX    | 16        | Receção de dados GPS       |
| TX    | 17        | Transmissão (configuração) |

### Atuadores (ESP2)

| Atuador          | Pino GPIO | Função              |
| -----------------|-----------|---------------------|
| Aileron Direito  | 13        | Roll (asa direita)  |
| Aileron Esquerdo | 14        | Roll (asa esquerda) |
| Leme             | 27        | Yaw (guinada)       |
| Motor ESC        | 33        | Throttle (potência) |

### Alarmes (Failsafe)

| Sinal  | Pino GPIO | Descrição        |
| -------|-----------|------------------|
| LED    | 32        | LED de alarme    |
| Buzzer | 33        | Buzzer de alarme |

---

## 🚀 Como Começar

### Pré-requisitos

- **Hardware:** ESP32-S3 ou ESP32 DevKit
- **Framework:** Arduino IDE ou PlatformIO
- **Bibliotecas:**
  - RadioLib (LoRa)
  - Wire (I2C)
  - SPI
  - SD (opcional)

### Instalação

1. Clonar o repositório
2. Abrir o ficheiro `main.ino` na Arduino IDE ou PlatformIO
3. Certificar que todas as subpastas estão no mesmo diretório
4. Configurar a placa como `ESP32 Dev Module`
5. Compilar e fazer upload

### Configuração da Placa (PlatformIO)

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.partitions = huge_app.csv
lib_deps = 
    jgromes/RadioLib
´´´

🛡️ Modos de Failsafe

Nível   Nome            Quando ativar                           O que faz               Recuperável
0       NORMAL          Nenhuma anomalia                        Nada                    N/A
1       ALERTA          Bateria baixa, GPS lost                 Apenas alerta           Sim
2       RTL             Perda de link, bateria baixa, GPS ok    Regressa à origem       Sim
3       ATERRAGEM       Bateria crítica, GPS inválido           Desce verticalmente     Sim
4       PLANAGEM        Falha motor, ângulos extremos           Apenas estabilidade     Parcial
5       CORTE TOTAL     Falha IMU, bateria morta                Corta tudo, cai         Não

📡 Comunicação

ESP2 → GS (LoRa 868MHz)

Parâmetro           Valor           Descrição
Frequência          868 MHz         Banda ISM europeia
Potência            20 dBm          Máximo permitido
Spreading Factor    9               Equilíbrio alcance/velocidade
Largura de banda    125 kHz         Padrão Long Range
Prioridade          Configurável    Failsafe > Comando > Telemetria > Vídeo

ESP1 ↔ ESP2 (UART)

Parâmetro   Valor                                   Descrição
Baud rate   921600                                  Alta velocidade
Formato     8N1                                     8 bits, sem paridade, 1 stop bit
Dados       SensorsPayload (estrutura compacta)     RAW + comandos

🎮 Comandos Shell (ESP2)

Comando         Descrição                   Exemplo
help            Mostra ajuda                help
status          Estado do sistema           status
get_roll        Ângulo de roll              get_roll
get_pitch       Ângulo de pitch             get_pitch
get_alt         Altitude                    get_alt
get_batt        Tensão da bateria           get_batt
get_temp_imu    Temperatura IMU             get_temp_imu
get_gps_lat     Latitude GPS                get_gps_lat
set_roll        Define roll (emergência)    set_roll 15
set_mode        Define modo de voo          set_mode 1
reboot          Reinicia ESP2               reboot
debug_on        Ativa debug                 debug_on

🔧 Loop Principal (100Hz Determinístico)

```cpp
void loop() {
    // Timing: garantir 100Hz
    uint32_t nowUs = micros();
    if (nowUs - _lastLoopUs < 10000) {
        delayMicroseconds(10000 - (nowUs - _lastLoopUs));
    }
    
    // 1. Receber dados RAW do ESP1
    processUARTData();
    
    // 2. Atualizar GPS
    gps.update();
    
    // 3. Atualizar Failsafe
    updateFailsafeAndFlightControl();
    
    // 4. Executar FlightControl (se ativo)
    flightControl.update(dt);
    
    // 5. Enviar dados SI de volta para ESP1
    sendBackToESP1();
    
    // 6. Enviar telemetria para GS
    updateTelemetry();
    
    // 7. Processar comandos shell
    shell.updateUART();
    
    // 8. Transmitir beacon
    updateBeacon();
    
    // 9. Processar vídeo
    video.sendNext();
    
    // 10. Processar fila LoRa
    lora.transmit();
}
´´´

📊 Performance

Métrica                 Valor
Loop principal          100 Hz (10ms)
Tempo médio por ciclo   ~2-3 ms
Pico de CPU             ~30%
RAM utilizada           ~150 KB
Flash utilizada         ~1.5 MB
UART ESP1↔ESP2          921600 baud
LoRa 868MHz             ~1.8 kbps (SF9)

🐛 Debug

Ativar debug nos módulos:

```cpp
#define MAIN_DEBUG
#define SICONVERTER_DEBUG
#define GPS_DEBUG
#define FAILSAFE_DEBUG
#define FLIGHTCONTROL_DEBUG
#define ACTUATORS_DEBUG
#define TELEMETRY_DEBUG
#define VIDEO_DEBUG
#define SHELL_DEBUG
#define LORA_DEBUG
#define BEACON_DEBUG
#define SECURITY_DEBUG
´´´

Monitor série:

```bash
screen /dev/ttyUSB0 115200
´´´

⚠️ Troubleshooting

Problema            Possível causa              Solução
ESP2 não inicia     Alimentação insuficiente    Verificar bateria/ligações
LoRa não transmite  Antena mal conectada        Verificar antena e configuração
Failsafe não ativa  Limiares muito altos        Ajustar limiares no código
GPS sem fix         Antena mal posicionada      Aguardar ou reposicionar
Beacon não visível  Wi-Fi desativado            Verificar configuração do Beacon

📚 Documentação por Módulo

Módulo          Documentação                Guia Rápido
Protocol        PROTOCOL_DOC.md             PROTOCOL_QUICK.md
SIConverter     SICONVERTER_DOC.md          SICONVERTER_QUICK.md
GPS             GPS_DOC.md                  GPS_QUICK.md
Failsafe        FAILSAFE_DOC.md             FAILSAFE_QUICK.md
FlightControl   FLIGHTCONTROL_DOC.md        FLIGHTCONTROL_QUICK.md
Actuators       ACTUATORS_DOC.md            ACTUATORS_QUICK.md
Telemetry       TELEMETRY_DOC.md            TELEMETRY_QUICK.md
Video           VIDEO_DOC.md                VIDEO_QUICK.md
Shell           SHELL_DOC.md                SHELL_QUICK.md
LoRa            LORA_DOC.md                 LORA_QUICK.md
Beacon          BEACON_DOC.md               BEACON_QUICK.md
Security        SECURITY_DOC.md             SECURITY_QUICK.md

📝 Licença
Este projeto é distribuído sob a licença MIT. Consulte o ficheiro LICENSE para mais detalhes.

👥 Créditos
Desenvolvido por BeaconFly UAS Team — 2026

BeaconFly UAS — Comunicação Robusta e Segura. 🚀

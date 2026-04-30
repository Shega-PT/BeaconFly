# 🚀 BeaconFly UAS — ESP1 (Core Flight Unit)

> **Versão:** 2.0.0  
> **Data:** 2026-04-19  
> **Autor:** ShegaPT

---

## 📖 Visão Geral

O **ESP1** é o processador de tempo real do sistema BeaconFly, responsável pela **estabilidade, segurança e execução de comandos** do drone. Opera num loop determinístico de **100Hz** com filosofia **Fail-Secure** (segurança em primeiro lugar).

### Características Principais

| Característica          | Descrição                                                      |
| ------------------------|----------------------------------------------------------------|
| **Loop determinístico** | 100Hz (10ms) com watchdog integrado                            |
| **Segurança**           | HMAC-SHA256, anti-replay, lockdown, envelope de voo            |
| **Controlo de voo**     | PIDs para Roll, Pitch, Yaw e Altitude                          |
| **Modos de voo**        | MANUAL, STABILIZE, ALT_HOLD, POSHOLD, AUTO, RTL, DEBUG_AUTO_MANUAL |
| **Atuadores**           | 5 servos + 1 motor (PWM 1000-2000µs)                           |
| **Sensores**            | IMU (MPU6050), Barómetro (BMP280), Bateria (ADC), Termopares (MAX31855) |
| **Comunicação**         | LoRa 2.4GHz (RX apenas), UART (ESP2), USB (debug)              |
| **Caixa negra**         | Logging em SD/SPIFFS (CSV, 10Hz, 24 campos)                    |
| **Shell remoto**        | Consola de comandos via USB ou LoRa                            |

---

## 🏗️ Arquitetura do Sistema

### Organização das Pastas

| Pasta           | Função   | Responsabilidade                                |
|-----------------|----------|-------------------------------------------------|
| `FlightControl/`| Cérebro  | PID, modos de voo, setpoints, navegação         |
| `Actuators/`    | Músculos | Geração de sinais PWM para ESCs e Servos        |
| `Sensors/`      | Sentidos | Leitura RAW de IMU, Baro, Bateria, Termopares   |
| `Security/`     | Escudo   | HMAC, anti-replay, geofencing, lockdown         |
| `Protocol/`     | Língua   | Definição de mensagens TLV (Type-Length-Value)  |
| `Parser/`       | Tradutor | Reconstrução de mensagens TLV byte-a-byte       |
| `Shell/`        | Voz      | Consola de diagnóstico e comandos remotos       |
| `Telemetry/`    | Memória  | Logger de "Caixa Negra" em cartão SD/SPIFFS     |

### Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ GROUND STATION                                                                  │
│ (Interface Web/Python + Joystick)                                               │
└─────────────────────────────────────────────────────────────────────────────────┘
│ (Comandos via LoRa 2.4GHz)                        ▲ (Respostas via LoRa 868MHz)
▼                                                   │
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP1                                                                            │
│                                                                                 │
│ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐                 │
│ │ LoRa        │→│ Parser      │→│ Security    │→│ Shell       │                 │
│ │ (2.4GHz)    │ │ (TLV/CRC)   │ │ (HMAC/SEQ)  │ │ (Comandos)  │                 │
│ └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘                 │
│ │  │                                                                            |
│ │  ▼                                                                            │
│ │ ┌─────────────┐ ┌─────────────┐                                               │
│ │ │FlightControl│ │ Telemetry   │                                               │
│ │ │ (PID)       │ │(Caixa Negra)│                                               │
│ │ └─────────────┘ └─────────────┘                                               │
│ │                                                                               │
│ ▼                                                                               │
│ ┌─────────────┐ ┌─────────────┐                                                 │
│ │ Sensors     │ │ Actuators   │                                                 │
│ │ (IMU/Baro/  │ │ (Servos/    │                                                 │
│ │ Bateria)    │ │ Motor)      │                                                 │
│ └──────┬──────┘ └─────────────┘                                                 │
│        │                                                                        │
│        ▼ (Dados RAW via UART)                                                   │
│ ┌─────────────┐                                                                 │
│ │ ESP2        │ → (Converte RAW → SI) → FlightControl                           │
│ └─────────────┘                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘

---

## 🔌 Pinagem (PCB BeaconFly v1.0)

### LoRa 2.4GHz (SX1280) — Apenas RX

| Pino | Função      | GPIO |
| -----|-------------|------|
| CS   | Chip Select | 5    |
| DIO1 | Interrupção | 4    |
| RST  | Reset       | 2    |
| BUSY | Busy        | 15   |

### I2C (IMU + Barómetro)

| Pino | Função    | GPIO |
| -----|-----------|------|
| SDA  | I2C Data  | 21   |
| SCL  | I2C Clock | 22   |

### SPI (Termopares MAX31855)

| Pino  | Função     | GPIO |
| ------|------------|------|
| SCK   | SPI Clock  | 23   |
| MISO  | SPI Data   | 19   |
| CS1   | Termopar 1 | 15   |
| CS2   | Termopar 2 | 16   |
| CS3   | Termopar 3 | 17   |
| CS4   | Termopar 4 | 18   |

### Atuadores (PWM)

| Atuador        | Função          | GPIO |
| ---------------|-----------------|------|
| Servo Asa R    | Asa direita     | 13   |
| Servo Asa L    | Asa esquerda    | 14   |
| Servo Leme     | Guinada (yaw)   | 27   |
| Servo Elevon R | Elevon direito  | 26   |
| Servo Elevon L | Elevon esquerdo | 25   |
| Motor ESC      | Motor principal | 33   |

### Bateria (ADC)

| Sinal    | Função         | GPIO |
| ---------|----------------|------|
| Tensão   | ADC da bateria | 34   |
| Corrente | ADC do shunt   | 35   |

### UART (ESP1 ↔ ESP2)

| Sinal | Função      | GPIO |
| ------|-------------|------|
| TX    | Transmissão | 17   |
| RX    | Receção     | 16   |

---

## 🚀 Como Começar

### Pré-requisitos

- **Hardware:** ESP32-S3 ou ESP32 DevKit
- **Framework:** Arduino IDE ou PlatformIO
- **Bibliotecas:**
  - RadioLib (LoRa)
  - Wire (I2C)
  - SPI
  - SD (para caixa negra)
  - SPIFFS (fallback)

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
    adafruit/Adafruit BMP280 Library
    adafruit/Adafruit MAX31855 library
´´´

🛡️ Segurança

O BeaconFly utiliza um sistema de HMAC-SHA256 para autenticar todos os comandos recebidos via LoRa.

Mecanismo           Descrição
HMAC-SHA256         Autenticação de integridade e origem
Anti-replay         Janela deslizante de 32 bits
Rate limiting       Proteção contra flood de comandos
Envelope de voo     Limites estruturais da aeronave
Lockdown            Bloqueio total após 10 violações

Modo Lockdown: Em caso de 10 violações consecutivas, o ESP1 corta os motores e exige um reset físico.

🎮 Modos de Voo

Modo                    Valor   Descrição
MODE_MANUAL             0       Passthrough direto (sem PID)
MODE_STABILIZE          1       Estabilização angular
MODE_ALT_HOLD           2       Estabilização + manutenção de altitude
MODE_POSHOLD            3       Manutenção de posição GPS
MODE_AUTO               4       Navegação autónoma por waypoints
MODE_RTL                5       Return To Launch
MODE_DEBUG_AUTO_MANUAL  6       Híbrido para testes (autónomo + override manual)

🛠️ Diagnóstico via Shell

Podes interagir com o drone via Monitor Serial (115200 baud) ou via LoRa:

Comandos básicos

Comando         Descrição
help            Mostra todos os comandos disponíveis
status          Resumo de saúde dos sistemas
get_roll        Lê ângulo de roll atual
get_pitch       Lê ângulo de pitch atual
get_alt         Lê altitude atual
get_batt        Lê tensão e corrente da bateria
set_roll 15     Define setpoint de roll para 15 graus
set_mode 1      Muda para modo STABILIZE
arm             Arma os motores
logbook         Visualiza os últimos eventos gravados

Comandos com destinatário

Prefixo         Significado
[ESP1] comando  Executa no ESP1 (padrão)
[ESP2] comando  Encaminha para ESP2

Exemplo: [ESP2] get_temp → Lê temperatura do ESP2

📡 Comunicação ESP1 ↔ ESP2

O ESP1 envia dados RAW ao ESP2 via UART (921600 baud). O ESP2 processa GPS e telemetria de longo alcance (868MHz) e devolve correções de navegação em unidades SI.

Formato dos dados RAW

```cpp
#pragma pack(push, 1)
struct SensorsPayload {
    IMURaw      imu;       // Acel, giro, temp
    BaroRaw     baro;      // Pressão, temp
    BatteryRaw  battery;   // ADC tensão, corrente
    int32_t     thermocouples[4];  // Temperaturas
};
#pragma pack(pop)
´´´

Formato dos dados SI (ESP2 → ESP1)

Mensagens TLV do tipo MSG_SI_DATA com campos:

- FLD_ROLL, FLD_PITCH, FLD_YAW (ângulos em graus)
- FLD_ROLL_RATE, FLD_PITCH_RATE, FLD_YAW_RATE (velocidades angulares em °/s)
- FLD_ALT_GPS, FLD_ALT_BARO (altitude em metros)
- FLD_VX, FLD_VY, FLD_VZ (velocidades lineares em m/s)
- FLD_BATT_V, FLD_BATT_A, FLD_BATT_SOC (bateria)

📊 Caixa Negra (Telemetry)

O ESP1 regista automaticamente todos os dados de voo em CSV:

Ficheiro            Conteúdo
/flight_XXX.csv     Dados periódicos (10Hz, 24 campos)
/events_XXX.csv     Eventos críticos (ARM, FAILSAFE, etc.)
/session.bin        Número da última sessão

Backends (por ordem de preferência):

- SD Card (maior capacidade)
- SPIFFS (flash interna)
- RAM (apenas buffer, dados perdem-se no reset)

🔧 Loop Principal (100Hz Determinístico)

```cpp
void loop() {
    // Timing: garantir 100Hz
    uint32_t nowUs = micros();
    if (nowUs - _lastLoopUs < 10000) {
        delayMicroseconds(10000 - (nowUs - _lastLoopUs));
    }
    
    // 1. Sensores
    sensors.update();
    
    // 2. Enviar RAW para ESP2
    sendRawDataToESP2();
    
    // 3. Comandos LoRa
    processLoRaCommands();
    
    // 4. Comandos USB
    processUSBCommands();
    
    // 5. Dados SI do ESP2
    updateFlightStateFromESP2();
    
    // 6. Controlo de voo
    flightControl.update();
    
    // 7. Atuadores
    actuators.update(flightControl.getOutputs());
    
    // 8. Caixa negra
    telemetry.update(flightControl, actuators);
}
´´´

⚙️ Configuração de Pinos (Ajustável)

Todos os pinos podem ser alterados nos ficheiros de cabeçalho de cada módulo:

Módulo      Ficheiro        Constantes
LoRa        LoRa.h          LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY
I2C         Sensors.h       I2C_SDA, I2C_SCL
SPI         Sensors.h       TC_SCK_PIN, TC_MISO_PIN, TC_CS1_PIN...
Atuadores   Actuators.h     PIN_SERVO_*, PIN_MOTOR_ECU
Bateria     Sensors.h       BATT_PIN_VOLTAGE, BATT_PIN_CURRENT
UART        main.ino        UART_BAUD_RATE

🐛 Debug e Diagnóstico

Ativar debug

```cpp
#define MAIN_DEBUG      // No main.ino
#define PROTOCOL_DEBUG  // No Protocol.h
#define PARSER_DEBUG    // No Parser.h
#define FC_DEBUG        // No FlightControl.h
#define ACTUATORS_DEBUG // No Actuators.h
#define SENSORS_DEBUG   // No Sensors.h
#define SECURITY_DEBUG  // No Security.h
#define SHELL_DEBUG     // No Shell.h
#define TELEMETRY_DEBUG // No Telemetry.h
´´´

Monitor série

```bash
screen /dev/ttyUSB0 115200
´´´

Comandos úteis no shell:

> status
> get_batt
> get_alt
> set_mode 1
> arm
> set_throttle 30
> logbook

📈 Performance

Métrica                 Valor
Loop principal          100 Hz (10ms)
Tempo médio por ciclo   ~2-3 ms
Pico de CPU             ~30%
RAM utilizada           ~120 KB
Flash utilizada         ~1.2 MB
UART ESP1↔ESP2          921600 baud
Caixa negra             10Hz, ~2 KB/s

⚠️ Troubleshooting

Problema                Possível causa              Solução
ESP1 não inicia         Alimentação insuficiente    Verificar bateria/ligações
LoRa não recebe         Antena mal conectada        Verificar antena e configuração
Atuadores não respondem Sistema desarmado           Executar arm no shell
Dados SI stale          ESP2 não está a enviar      Verificar UART e alimentação do ESP2
Caixa negra sem dados   SD ausente ou corrompido    Verificar cartão SD

📚 Documentação por Módulo

Módulo          Documentação            Guia Rápido
Protocol        PROTOCOL_DOC.md         PROTOCOL_QUICK.md
Parser          (incluído no Protocol)  (incluído no Protocol)
FlightControl   FLIGHTCONTROL_DOC.md    FLIGHTCONTROL_QUICK.md
Actuators       ACTUATORS_DOC.md        ACTUATORS_QUICK.md
Sensors         SENSORS_DOC.md          SENSORS_QUICK.md
Security        SECURITY_DOC.md         SECURITY_QUICK.md
Shell           SHELL_DOC.md            SHELL_QUICK.md
Telemetry       TELEMETRY_DOC.md        TELEMETRY_QUICK.md

📝 Licença

Este projeto é distribuído sob a licença MIT. Consulte o ficheiro LICENSE para mais detalhes.

👥 Créditos

Desenvolvido por BeaconFly UAS Team — 2026

BeaconFly UAS — Controlo de Voo Robusto e Seguro. 🚀

🚀 BeaconFly UAS — Sistema Completo de Aeronave Não Tripulada
Versão: 2.0.0
Data: 2026-04-30
Autor: BeaconFly UAS Team
Licença: MIT

📖 Visão Geral
BeaconFly é um sistema completo de controlo para aeronaves não tripuladas (UAS), projetado para ser modular, seguro, determinístico e em conformidade com os regulamentos europeus (EASA, DGAC, ANAC).

O sistema é composto por três componentes principais que funcionam em simbiose:

Componente	  Hardware	    Função Principal
ESP1	        ESP32	        Flight Controller — controlo de voo em tempo real (100Hz)
ESP2	        ESP32	        Communication & Video Processor — gateway com a Ground Station
GS	          Python/Web	  Ground Station — interface de controlo e monitorização

┌─────────────────────────────────────────────────────────────────────────────────┐
│                         BEACONFLY UAS — ARQUITETURA COMPLETA                    │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│   ┌─────────────────────────────────────────────────────────────────────────┐   │
│   │                         GROUND STATION (GS)                             │   │
│   │                                                                         │   │
│   │   • Interface Web (React/Vue)                                           │   │
│   │   • Visualização de telemetria em tempo real                            │   │
│   │   • Controlo por joystick                                               │   │
│   │   • Planeamento de rotas e waypoints                                    │   │
│   │   • Shell remoto para diagnóstico                                       │   │
│   │   • Gravação de logs e análise pós-voo                                  │   │
│   └─────────────────────────────────────────────────────────────────────────┘   │
│          │ (Comandos via LoRa 2.4GHz)             ▲ (Telemetria via LoRa 868MHz)│
│          ▼                                        │                             │
│   ┌─────────────────────────────────────────────────────────────────────────┐   │
│   │                              ESP1                                       │   │
│   │                                                                         │   │
│   │   FLIGHT CONTROLLER (100Hz determinístico)                              │   │
│   │                                                                         │   │
│   │   • Recebe comandos da GS via LoRa 2.4GHz                               │   │
│   │   • Controlo de voo com PIDs (Roll, Pitch, Yaw, Altitude)               │   │
│   │   • 6 modos de voo (MANUAL, STABILIZE, ALT_HOLD, AUTO, RTL, DEBUG)      │   │
│   │   • Aquisição de sensores (IMU, Barómetro, Bateria, Termopares)         │   │
│   │   • Controlo de atuadores (5 servos + 1 motor)                          │   │
│   │   • Caixa negra (logging em SD/SPIFFS)                                  │   │
│   │   • Shell de diagnóstico via USB/LoRa                                   │   │
│   │   • Segurança HMAC-SHA256 + anti-replay + lockdown                      │   │
│   └─────────────────────────────────────────────────────────────────────────┘   │
│          │ (Dados RAW via UART)                 ▲ (Dados SI via UART)           │
│          ▼                                      │                               │
│   ┌─────────────────────────────────────────────────────────────────────────┐   │
│   │                              ESP2                                       │   │
│   │                                                                         │   │
│   │   COMMUNICATION & VIDEO PROCESSOR                                       │   │
│   │                                                                         │   │
│   │   • Converte RAW → SI (unidades do Sistema Internacional)               │   │
│   │   • Processa GPS (NMEA/UBX/SiRF/MTK)                                    │   │
│   │   • Failsafe autónomo (5 níveis de emergência)                          │   │
│   │   • Assumir controlo em caso de emergência                              │   │
│   │   • Envia telemetria para GS via LoRa 868MHz                            │   │
│   │   • Processa comandos shell remoto                                      │   │
│   │   • Transmite vídeo (prova de conceito)                                 │   │
│   │   • Beacon Wi-Fi de identificação legal (EASA/DGAC/ANAC)                │   │
│   └─────────────────────────────────────────────────────────────────────────┘   │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘

✨ Características Principais

Característica	          Descrição
Arquitetura simbiótica	  ESP1, ESP2 e GS dependem uns dos outros — sistema distribuído inteligente
Determinismo	            Loop principal a 100Hz (10ms) com watchdog integrado
Fail-Secure	              Segurança em primeiro lugar — qualquer anomalia bloqueia o sistema
Múltiplos modos de voo	  MANUAL, STABILIZE, ALT_HOLD, POSHOLD, AUTO, RTL, DEBUG_AUTO_MANUAL
Navegação autónoma	      Waypoints, RTL (Return To Launch), planeamento de rotas
Caixa negra	              Logging contínuo em CSV (10Hz, 24 campos)
Shell remoto	            Consola de comandos via USB ou LoRa (diagnóstico remoto)
Segurança robusta	        HMAC-SHA256, anti-replay, rate limiting, lockdown progressivo
Failsafe                  5 níveis	Alerta → RTL → Aterragem → Planagem → Corte total
Beacon legal	            Wi-Fi aberto com identificação do piloto (EASA/DGAC/ANAC)
Portabilidade	            Código compatível com ESP32, Arduinos, Raspberrys, Jetsons

🏗️ Arquitetura do Sistema

Fluxo de Dados Completo

┌─────────────────────────────────────────────────────────────────────────────────┐
│                         GROUND STATION (GS)                                     │
│                                                                                 │
│   ┌─────────────────────────────────────────────────────────────────────────┐   │
│   │  Interface Web (Python/Flask + React)                                   │   │
│   │  • Telemetria em tempo real                                             │   │
│   │  • Controlo por joystick                                                │   │
│   │  • Configuração de parâmetros (PIDs, trims, limites)                    │   │
│   │  • Planeamento de waypoints                                             │   │
│   │  • Shell remoto                                                         │   │
│   │  • Visualização de logs e caixa negra                                   │   │
│   └─────────────────────────────────────────────────────────────────────────┘   │
│         │                                         ▲                             │
│         │ (Comandos via LoRa 2.4GHz)              │ (Telemetria via LoRa 868MHz)│
│         ▼                                         │                             │
│   ┌─────────────────────────────────────────────────────────────────────────┐   │
│   │                              ESP1                                       │   │
│   │                                                                         │   │
│   │  ┌─────────────────────────────────────────────────────────────────┐    │   │
│   │  │  LoRa 2.4GHz (RX) → Parser → Security → Shell/FlightControl     │    │   │
│   │  └─────────────────────────────────────────────────────────────────┘    │   │
│   │         │                                                               │   │
│   │         ▼                                                               │   │
│   │  ┌─────────────────────────────────────────────────────────────────┐    │   │
│   │  │  FlightControl (PIDs: Roll, Pitch, Yaw, Altitude)               │    │   │
│   │  │  • 6 modos de voo                                               │    │   │
│   │  │  • Mixer de elevons (asa delta)                                 │    │   │
│   │  │  • Slew rate para suavização                                    │    │   │
│   │  └─────────────────────────────────────────────────────────────────┘    │   │
│   │         │                                                               │   │
│   │         ▼                                                               │   │
│   │  ┌─────────────────────────────────────────────────────────────────┐    │   │
│   │  │  Actuators (5 servos + 1 motor)                                 │    │   │
│   │  │  • PWM 1000-2000µs, LEDC hardware                               │    │   │
│   │  │  • ARM/DISARM, failsafe block                                   │    │   │
│   │  └─────────────────────────────────────────────────────────────────┘    │   │
│   │         │                                                               │   │
│   │         ▼ (Dados RAW via UART)                                          │   │
│   └─────────────────────────────────────────────────────────────────────────┘   │
│                                      │                                          │
│                                      ▼                                          │
│   ┌─────────────────────────────────────────────────────────────────────────┐   │
│   │                              ESP2                                       │   │
│   │                                                                         │   │
│   │  ┌─────────────────────────────────────────────────────────────────┐    │   │
│   │  │  SIConverter: RAW → SI                                          │    │   │
│   │  │  • Aceleração (counts → m/s²)                                   │    │   │
│   │  │  • Giro (counts → °/s)                                          │    │   │
│   │  │  • Temperatura (counts → °C)                                    │    │   │
│   │  │  • Pressão (counts → Pa)                                        │    │   │
│   │  │  • Bateria (ADC → V, A)                                         │    │   │
│   │  └─────────────────────────────────────────────────────────────────┘    │   │
│   │         │                                                               │   │
│   │         ▼                                                               │   │
│   │  ┌─────────────────────────────────────────────────────────────────┐    │   │
│   │  │  Failsafe (Vigia constante)                                     │    │   │
│   │  │  • 5 níveis de emergência                                       │    │   │
│   │  │  • Alarmes sonoros e luminosos                                  │    │   │
│   │  │  • Assume controlo em caso de falha                             │    │   │
│   │  └─────────────────────────────────────────────────────────────────┘    │   │
│   │         │                                                               │   │
│   │         ▼                                                               │   │
│   │  ┌────────────────────────────────────────────────────────────────────┐ │   │
│   │  │  FlightControl (emergência) + Actuators (2 ailerons + leme + motor)│ │   │
│   │  └────────────────────────────────────────────────────────────────────┘ │   │
│   │         │                                                               │   │
│   │         ▼ (Dados SI via UART)  ▲ (Telemetria via LoRa 868MHz)           │   │
│   └─────────────────────────────────────────────────────────────────────────┘   │
│                                      │                                          │
│                                      ▼                                          │
│                              (Telemetria para GS)                               │
└─────────────────────────────────────────────────────────────────────────────────┘

Matriz de Responsabilidades

Função	                              ESP1	  ESP2	              GS	            Observação
Controlo de voo (PIDs)	              ✅	      ❌ (só emergência)	  ❌	              ESP1 autoridade principal
Navegação autónoma	                  ✅	      ❌	                  ❌	              Waypoints, RTL
Atuadores (5 servos + motor)	        ✅	      ❌	                  ❌	              Asa delta
Atuadores (2 ailerons + leme + motor)	❌	      ✅ (emergência)	    ❌	              Avião convencional
Aquisição de sensores (RAW)	          ✅	      ❌	                  ❌	              IMU, Baro, Bateria, TC
Conversão RAW → SI	                  ❌	      ✅	                  ❌	              SIConverter
Processamento GPS	                    ❌	      ✅	                  ❌	              NMEA/UBX/SiRF/MTK
Failsafe	                            ❌	      ✅	                  ❌	              5 níveis, alarmes
Caixa negra	                          ✅	      ❌	                  ❌	              SD/SPIFFS
Telemetria para GS	                  ❌	      ✅ (TX)              ✅ (RX)	        LoRa 868MHz
Comandos da GS	                      ✅ (RX)	❌	                  ✅ (TX)	        LoRa 2.4GHz
Shell remoto	                        ✅	      ✅	                  ❌	              Diagnóstico
Beacon legal	                        ❌	      ✅	                  ❌	              Wi-Fi aberto
Vídeo	                                ❌	      ✅ (POC)	            ❌	              Prova de conceito
Interface gráfica	                    ❌	      ❌	                  ✅	              Web/Python
Planeamento de rotas	                ❌	      ❌	                  ✅	              Waypoints
Análise pós-voo	                      ❌	      ❌	                  ✅	              Logs CSV

🔒 Segurança

Autenticação e Integridade

Mecanismo	            Descrição
HMAC-SHA256	          Autenticação de todos os comandos da GS
Anti-replay	          Janela deslizante de 32 bits
Rate limiting	        Proteção contra flood (3 categorias)
Lockdown progressivo	5 níveis de segurança (NORMAL → LOCKDOWN)
Envelope de voo	      Limites estruturais da aeronave
Sanidade de comandos	Validação de limites físicos

Níveis de Segurança

Nível	    Violações	Roll max	Pitch max	  Comandos permitidos
NORMAL	  0	        45°	      35°	        Todos
CAUTION	  1-2	      30°	      25°	        Todos (reduzidos)
WARNING	  3-4	      15°	      15°	        Todos (muito reduzidos)
CRITICAL	5-7	      10°	      10°	        Apenas emergência
LOCKDOWN	8+	      —	        —	          Nenhum (requer reset)

🛡️ Failsafe (5 Níveis de Emergência)

Nível	Nome	        Quando ativar	                              O que faz	Alarmes
0	    NORMAL	      Nenhuma anomalia	                          Operação normal	OFF
1	    ALERTA	      Bateria baixa, GPS lost	                    Apenas alerta	ON
2	    RTL	          Perda de link, bateria baixa, GPS ok	      Regressa à origem	Luz azul 1Hz
3	    ATERRAGAGEM	  Bateria crítica, GPS inválido	              Desce verticalmente	Luz amarela 2Hz + 1 beep/s
4	    PLANAGEM	    Falha motor, ângulos extremos	              Apenas estabilidade, motor=0	Luz laranja 3Hz + 2 beeps/s
5	    CORTE TOTAL	  Falha IMU, bateria morta	                  Corta tudo, queda livre	Luz vermelha 5Hz + alarme

🎮 Modos de Voo (ESP1)

Modo	                  Valor	  Descrição	                              Aplicação
MODE_MANUAL	            0	      Passthrough direto (sem PID)	          Testes manuais, pilotos experientes
MODE_STABILIZE	        1	      Estabilização angular	                  Voo normal assistido
MODE_ALT_HOLD	          2	      Estabilização + manutenção de altitude	Voo com altitude constante
MODE_POSHOLD	          3	      Manutenção de posição GPS	              Pairar no lugar (requer GPS)
MODE_AUTO	              4	      Navegação autónoma por waypoints	      Missões pré-programadas
MODE_RTL	              5	      Return To Launch	                      Emergência / fim de missão
MODE_DEBUG_AUTO_MANUAL	6	      Híbrido para testes	                    Autónomo + override manual

📡 Protocolo de Comunicação (TLV)

Formato da Mensagem

┌─────────┬─────────┬───────────┬─────────────────┬─────────┬──────────────┬─────────┐
│ START   │ MSG ID  │ TLV COUNT │ TLV FIELDS      │ CRC8    │ HMAC (32B)   │ SEQ (4B)│
│ (1 byte)│ (1 byte)│ (1 byte)  │ (variável)      │ (1 byte)│ (opcional)   │ (opç.)  │
├─────────┼─────────┼───────────┼─────────────────┼─────────┼──────────────┼─────────┤
│ 0xAA    │ 0x10-18 │ 0-32      │ ID(1)+LEN(1)+N  │ CRC-8   │ Segurança    │ Anti-   │
│         │         │           │                 │ SMBUS   │ (módulo      │ replay  │
│         │         │           │                 │ 0x07    │ Security)    │         │
└─────────┴─────────┴───────────┴─────────────────┴─────────┴──────────────┴─────────┘

Tipos de Mensagem

ID	  Constante	      Direção	        Descrição
0x10	MSG_HEARTBEAT	  ESP1 → GS	      Coração do sistema
0x11	MSG_TELEMETRY	  ESP2 → GS	      Telemetria normal
0x12	MSG_COMMAND	    GS → ESP1	      Comandos de voo
0x13	MSG_ACK	        ESP1 → GS	      Confirmação de receção
0x14	MSG_FAILSAFE	  ESP2 → GS	      Notificação de failsafe
0x15	MSG_DEBUG	      ESP2 → GS	      Debug (prioridade dinâmica)
0x16	MSG_VIDEO	      ESP2 → GS	      Vídeo (POC)
0x17	MSG_SHELL_CMD	  GS ↔ ESP1/ESP2	Comandos shell
0x18	MSG_SI_DATA	    ESP2 ↔ ESP1	    Dados SI

🛠️ Estrutura do Projeto

ESP1 (Flight Controller)

ESP1/
├── main.ino                     # Loop principal (100Hz determinístico)
├── README.md                    # Documentação do ESP1
├── Protocol/                    # Comunicação TLV (partilhado)
│   ├── PROTOCOL_DOC.md
│   ├── PROTOCOL_QUICK.md
│   ├── Protocol.h
│   ├── Protocol.cpp
│   ├── Parser.h
│   └── Parser.cpp
├── FlightControl/               # Controlador de voo (PIDs, modos)
│   ├── FLIGHTCONTROL_DOC.md
│   ├── FLIGHTCONTROL_QUICK.md
│   ├── FlightControl.h
│   └── FlightControl.cpp
├── Actuators/                   # Controlo de atuadores (5 servos + motor)
│   ├── ACTUATORS_DOC.md
│   ├── ACTUATORS_QUICK.md
│   ├── Actuators.h
│   └── Actuators.cpp
├── Sensors/                     # Aquisição de sensores (RAW)
│   ├── SENSORS_DOC.md
│   ├── SENSORS_QUICK.md
│   ├── Sensors.h
│   └── Sensors.cpp
├── Security/                    # Segurança (HMAC, anti-replay, lockdown)
│   ├── SECURITY_DOC.md
│   ├── SECURITY_QUICK.md
│   ├── Security.h
│   └── Security.cpp
├── Telemetry/                   # Caixa negra (logging SD/SPIFFS)
│   ├── TELEMETRY_DOC.md
│   ├── TELEMETRY_QUICK.md
│   ├── Telemetry.h
│   └── Telemetry.cpp
├── Shell/                       # Consola de comandos
│   ├── SHELL_DOC.md
│   ├── SHELL_QUICK.md
│   ├── Shell.h
│   └── Shell.cpp
└── LoRa/                        # Driver LoRa 2.4GHz (RX only)
    ├── LORA_DOC.md
    ├── LORA_QUICK.md
    ├── LoRa.h
    └── LoRa.cpp
    
ESP2 (Communication & Video Processor)

ESP2/
├── main.ino                     # Loop principal (100Hz determinístico)
├── README.md                    # Documentação do ESP2
├── Protocol/                    # Comunicação TLV (partilhado com ESP1)
│   ├── PROTOCOL_DOC.md
│   ├── PROTOCOL_QUICK.md
│   ├── Protocol.h
│   ├── Protocol.cpp
│   ├── Parser.h
│   └── Parser.cpp
├── SIConverter/                 # Conversão RAW → SI
│   ├── SICONVERTER_DOC.md
│   ├── SICONVERTER_QUICK.md
│   ├── SIConverter.h
│   └── SIConverter.cpp
├── GPS/                         # Processamento de GPS
│   ├── GPS_DOC.md
│   ├── GPS_QUICK.md
│   ├── GPS.h
│   └── GPS.cpp
├── Failsafe/                    # Vigia e ativação de emergência (5 níveis)
│   ├── FAILSAFE_DOC.md
│   ├── FAILSAFE_QUICK.md
│   ├── Failsafe.h
│   └── Failsafe.cpp
├── FlightControl/               # Controlo de emergência (quando failsafe ativo)
│   ├── FLIGHTCONTROL_DOC.md
│   ├── FLIGHTCONTROL_QUICK.md
│   ├── FlightControl.h
│   └── FlightControl.cpp
├── Actuators/                   # Controlo de atuadores (2 ailerons + leme + motor)
│   ├── ACTUATORS_DOC.md
│   ├── ACTUATORS_QUICK.md
│   ├── Actuators.h
│   └── Actuators.cpp
├── Telemetry/                   # Envio de telemetria para GS
│   ├── TELEMETRY_DOC.md
│   ├── TELEMETRY_QUICK.md
│   ├── Telemetry.h
│   └── Telemetry.cpp
├── Video/                       # Vídeo (prova de conceito)
│   ├── VIDEO_DOC.md
│   ├── VIDEO_QUICK.md
│   ├── Video.h
│   └── Video.cpp
├── Shell/                       # Consola de comandos (respostas via LoRa)
│   ├── SHELL_DOC.md
│   ├── SHELL_QUICK.md
│   ├── Shell.h
│   └── Shell.cpp
├── LoRa/                        # Driver LoRa 868MHz (TX only)
│   ├── LORA_DOC.md
│   ├── LORA_QUICK.md
│   ├── LoRa.h
│   └── LoRa.cpp
├── Beacon/                      # Sinal Wi-Fi de identificação legal
│   ├── BEACON_DOC.md
│   ├── BEACON_QUICK.md
│   ├── Beacon.h
│   └── Beacon.cpp
└── Security/                    # Segurança (HMAC, anti-replay)
    ├── SECURITY_DOC.md
    ├── SECURITY_QUICK.md
    ├── Security.h
    └── Security.cpp
    
Ground Station (GS) — Planeado

GS/
├── backend/                     # Servidor Python (Flask/FastAPI)
│   ├── app.py                   # API principal
│   ├── telemetry.py             # Processamento de telemetria
│   ├── commands.py              # Envio de comandos
│   ├── database.py              # Armazenamento de logs
│   └── requirements.txt         # Dependências Python
├── frontend/                    # Interface Web (React/Vue)
│   ├── src/
│   │   ├── components/          # Componentes UI
│   │   ├── pages/               # Páginas (Dashboard, Mapas, Config)
│   │   ├── services/            # Serviços (WebSocket, API)
│   │   └── App.js
│   └── package.json
├── lora/                        # Driver LoRa para GS
│   ├── lora_24.py               # LoRa 2.4GHz (TX)
│   └── lora_868.py              # LoRa 868MHz (RX)
├── joystick/                    # Controlo por joystick
│   └── joystick.py
└── README.md                    # Documentação da GS

📊 Especificações Técnicas

Performance

Métrica	                  ESP1	        ESP2	            GS
Loop principal	          100 Hz	      100 Hz	          N/A
Tempo médio por ciclo	    ~2-3 ms	      ~2-3 ms	          N/A
Pico de CPU	              ~30%	        ~30%	            N/A
RAM utilizada	            ~120 KB	      ~150 KB	          N/A
Flash utilizada	          ~1.2 MB	      ~1.5 MB	          N/A
UART ESP1↔ESP2	          921600 baud	  921600 baud	      N/A
LoRa 2.4GHz (comandos)	  50 kbps	      N/A	              50 kbps
LoRa 868MHz (telemetria)	N/A	          ~1.8 kbps (SF9)	  ~1.8 kbps

Sensores Suportados

Sensor	      Modelo	            Interface	  Dados
IMU	          MPU6050, ICM42688	  I2C	        Aceleração, Giro, Temperatura
Barómetro	    BMP280, MS5611	    I2C	        Pressão, Temperatura
GPS	          NMEA/UBX/SiRF/MTK	  UART	      Posição, Velocidade, Tempo
Termopares	  MAX31855	          SPI	        Temperatura (4 canais)
Bateria	      ADC interno	        ADC	        Tensão, Corrente

Atuadores Controlados

Atuador	          ESP1	ESP2	  Sinal
Aileron Direito	  ❌	    ✅	PWM   1000-2000µs
Aileron Esquerdo	❌	    ✅	PWM   1000-2000µs
Elevon Direito	  ✅	    ❌	PWM   1000-2000µs
Elevon Esquerdo	  ✅	    ❌	PWM   1000-2000µs
Asa Direita	      ✅	    ❌	PWM   1000-2000µs
Asa Esquerda	    ✅	    ❌	PWM   1000-2000µs
Leme	            ✅	    ✅	PWM   1000-2000µs
Motor	            ✅	    ✅	PWM   1000-1900µs

🚀 Como Começar

Pré-requisitos

Hardware:

2 × ESP32 (ESP1 e ESP2)
Módulos LoRa: SX1280 (2.4GHz) e SX1262/SX1276 (868MHz)
IMU: MPU6050
Barómetro: BMP280
GPS: NMEA (ex: NEO-6M, NEO-8M, ZED-F9P)
Termopares: MAX31855 (opcional)
Atuadores: 5 servos + 1 ESC (ESP1)
Atuadores: 2 servos + 1 leme + 1 ESC (ESP2)
Cartão SD (opcional, para caixa negra)

Software:

Arduino IDE ou PlatformIO (VSCode)
Bibliotecas: RadioLib, Wire, SPI, SD, SPIFFS, mbedtls

Instalação

´´´bash
# Clonar o repositório
git clone https://github.com/BeaconFly/BeaconFly-UAS.git
cd BeaconFly-UAS

# Configurar PlatformIO (opção 1)
cd ESP1
pio run --target upload
pio device monitor

cd ../ESP2
pio run --target upload
pio device monitor

# Ou usar Arduino IDE (opção 2)
# Abrir ESP1/main.ino e ESP2/main.ino separadamente
# Selecionar placa: ESP32 Dev Module
# Compilar e carregar
´´´

Configuração dos Pinos

Todos os pinos podem ser ajustados nos ficheiros de cabeçalho de cada módulo:

Módulo	            Ficheiro	    Constantes
LoRa 2.4GHz (ESP1)	LoRa.h	      LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY
LoRa 868MHz (ESP2)	LoRa.h	      LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RESET_PIN, LORA_BUSY_PIN
I2C	                Sensors.h	    I2C_SDA, I2C_SCL
SPI (Termopares)	  Sensors.h	    TC_SCK_PIN, TC_MISO_PIN, TC_CS1_PIN...
Atuadores ESP1	    Actuators.h	  PIN_SERVO_*, PIN_MOTOR_ECU
Atuadores ESP2	    Actuators.h	  PIN_SERVO_WING_R, PIN_SERVO_WING_L, PIN_SERVO_RUDDER, PIN_MOTOR_ECU
Bateria	            Sensors.h	    BATT_PIN_VOLTAGE, BATT_PIN_CURRENT
GPS	                GPS.h	        rxPin, txPin (na configuração)
Beacon	            Beacon.cpp	  PILOT_ID, UAS_SERIAL, UAS_MODEL, SESSION_ID

🛠️ Shell de Comandos

Comandos Disponíveis (ESP1 e ESP2)

Categoria	  Comandos	                                                              Descrição
Sistema	    help, status, logbook, clear	                                          Ajuda e diagnóstico
Leitura	    get_roll, get_pitch, get_yaw, get_heading	                              Atitude
            get_accel_x, get_accel_y, get_accel_z	                                  Aceleração
            get_gyro_x, get_gyro_y, get_gyro_z	                                    Velocidade angular
            get_alt, get_pressure	                                                  Altitude e pressão
            get_batt, get_current, get_power, get_charge	                          Bateria
            get_temp_imu, get_temp_baro, get_tc0-3	                                Temperaturas
            get_gps_lat, get_gps_lon, get_gps_alt, get_gps_sats, get_gps_hdop	      GPS
            get_mode, get_security, get_failsafe	                                  Estado
            get_loop_time, get_heap, get_uptime	                                    Diagnóstico
Controlo	  set_roll, set_pitch, set_yaw, set_alt, set_throttle, set_mode	          Comandos de voo
Sistema	    reboot, arm, disarm, calibrate	                                        Gestão do sistema
Debug	      debug_on, debug_off, echo_on, echo_off, test	                          Diagnóstico

Prefixos de Destinatário

Prefixo	  Destino	            Exemplo
[ESP1]	  Executar no ESP1	  [ESP1] get_roll
[ESP2]	  Executar no ESP2	  [ESP2] get_temp_imu
(nenhum)	Padrão: ESP1	      status

🐛 Debug

Ativar debug nos módulos (definir as macros nos respetivos ficheiros):

```cpp
#define MAIN_DEBUG           // main.ino
#define PROTOCOL_DEBUG       // Protocol.h
#define PARSER_DEBUG         // Parser.h
#define FC_DEBUG             // FlightControl.h
#define ACTUATORS_DEBUG      // Actuators.h
#define SENSORS_DEBUG        // Sensors.h
#define SECURITY_DEBUG       // Security.h
#define SHELL_DEBUG          // Shell.h
#define TELEMETRY_DEBUG      // Telemetry.h
#define LORA_DEBUG           // LoRa.h
#define GPS_DEBUG            // GPS.h
#define SICONVERTER_DEBUG    // SIConverter.h
#define FAILSAFE_DEBUG       // Failsafe.h
#define VIDEO_DEBUG          // Video.h
#define BEACON_DEBUG         // Beacon.h
´´´

Monitor série:

```bash
screen /dev/ttyUSB0 115200
´´´

📈 Roadmap

Fase  Objetivo	                                                  Estado
1.0	  Firmware ESP1 (Flight Controller) completo	                ✅ Concluído
2.0	  Firmware ESP2 (Communication & Video Processor) completo	  ✅ Concluído
3.0	  Ground Station (Python + Web)	                              🚧 Em desenvolvimento
3.1	  Integração completa ESP1 + ESP2 + GS	                      🔜 Planeado
3.2	  Vídeo em tempo real (MJPEG sobre LoRa)	                    🔜 Planeado
3.3	  RTK GPS (ZED-F9P) para precisão centimétrica	              🔜 Planeado
3.4	  Inteligência artificial para deteção de obstáculos	        🔜 Planeado

🤝 Contribuição
- Contribuições são bem-vindas! Por favor, siga os seguintes passos:
- Fork do projeto
- Criar uma branch para a sua feature (git checkout -b feature/nova-feature)
- Commit das alterações (git commit -m 'Adiciona nova feature')
- Push para a branch (git push origin feature/nova-feature)
- Abrir um Pull Request

📝 Licença
Este projeto é distribuído sob a licença MIT. Consulte o ficheiro LICENSE para mais detalhes.

👥 Créditos
Desenvolvido por BeaconFly UAS Team — 2026

🙏 Agradecimentos
RadioLib — Biblioteca para rádios LoRa
mbedtls — Biblioteca criptográfica
Arduino ESP32 — Core ESP32 para Arduino

BeaconFly UAS — Controlo de Voo Robusto e Seguro. 🚀

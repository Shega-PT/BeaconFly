/**
 * =================================================================================
 * MAIN.INO — BEACONFLY UAS — ESP1 (FLIGHT CONTROLLER)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-19
 * VERSÃO:     2.0.0
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * Este é o programa principal do ESP1 (Flight Controller) do sistema BeaconFly.
 * O ESP1 é responsável por:
 * 
 *   • Receber comandos da Ground Station via LoRa 2.4GHz
 *   • Processar e validar comandos (com segurança HMAC/SEQ)
 *   • Controlar os atuadores (servos e motor) via PWM
 *   • Executar o loop de controlo de voo a 100Hz (determinístico)
 *   • Enviar dados RAW dos sensores para o ESP2 via UART
 *   • Fornecer consola de debug via USB (Shell)
 * 
 * =================================================================================
 * ARQUITETURA DO SISTEMA
 * =================================================================================
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                         GROUND STATION                                      │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 *          │ (Comandos via LoRa 2.4GHz)              ▲ (Respostas via LoRa 868MHz)
 *          ▼                                         │
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP1                                           │
 *   │                                                                             │
 *   │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *   │  │    LoRa     │→│   Parser    │→│  Security   │→│   Shell     │        │
 *   │  │  (2.4GHz)   │  │  (TLV/CRC)  │  │ (HMAC/SEQ)  │  │ (Comandos)  │        │
 *   │  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘        │
 *   │         │                                   │              │               │
 *   │         │                                   ▼              ▼               │
 *   │         │                            ┌─────────────┐  ┌─────────────┐      │
 *   │         │                            │FlightControl│  │  Telemetry  │      │
 *   │         │                            │   (PID)     │  │ (Caixa Negra)│      │
 *   │         │                            └──────┬──────┘  └─────────────┘      │
 *   │         │                                   │                               │
 *   │         ▼                                   ▼                               │
 *   │  ┌─────────────┐                    ┌─────────────┐                        │
 *   │  │   Sensors   │                    │  Actuators  │                        │
 *   │  │ (IMU/Baro/  │                    │ (Servos/    │                        │
 *   │  │  Bateria)   │                    │   Motor)    │                        │
 *   │  └──────┬──────┘                    └─────────────┘                        │
 *   │         │                                                                   │
 *   │         ▼ (Dados RAW via UART)                                              │
 *   │  ┌─────────────┐                                                            │
 *   │  │   ESP2      │ → (Converte RAW → SI) → FlightControl                     │
 *   │  └─────────────┘                                                            │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * LOOP PRINCIPAL (DETERMINÍSTICO A 100Hz)
 * =================================================================================
 * 
 *   Cada ciclo de 10ms:
 *     1. Atualizar sensores (IMU, barómetro, bateria, termopares)
 *     2. Enviar dados RAW para ESP2 via UART
 *     3. Verificar receção de comandos via LoRa (parser)
 *     4. Processar comandos autenticados (FlightControl ou Shell)
 *     5. Atualizar controlo de voo (PIDs)
 *     6. Atualizar atuadores (PWM)
 *     7. Logging para caixa negra (Telemetry)
 *     8. Processar comandos USB (Shell debug)
 * 
 * =================================================================================
 * PINAGEM HARDWARE (PCB BeaconFly v1.0)
 * =================================================================================
 * 
 *   LoRa 2.4GHz (SX1280):
 *     CS:     5
 *     DIO1:   4
 *     RST:    2
 *     BUSY:   15
 * 
 *   I2C (IMU MPU6050, Barómetro BMP280):
 *     SDA:    21
 *     SCL:    22
 * 
 *   SPI (Termopares MAX31855):
 *     SCK:    23
 *     MISO:   19
 *     CS1:    15  (Termopar 1)
 *     CS2:    16  (Termopar 2)
 *     CS3:    17  (Termopar 3)
 *     CS4:    18  (Termopar 4)
 * 
 *   Atuadores (PWM):
 *     Servo Asa R:    13
 *     Servo Asa L:    14
 *     Servo Leme:     27
 *     Servo Elevon R: 26
 *     Servo Elevon L: 25
 *     Motor ESC:      33
 * 
 *   Bateria (ADC):
 *     Tensão:         34
 *     Corrente:       35
 * 
 *   UART (ESP1 ↔ ESP2):
 *     TX:             17
 *     RX:             16
 * 
 * =================================================================================
 */

#include <Arduino.h>
#include "Protocol/Protocol.h"
#include "Parser.h"
#include "Security.h"
#include "FlightControl.h"
#include "Actuators.h"
#include "Sensors.h"
#include "Telemetry.h"
#include "Shell.h"

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define MAIN_DEBUG

#ifdef MAIN_DEBUG
    #define DEBUG_PRINT(fmt, ...) Serial.printf("[MAIN] " fmt, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * CONSTANTES DE CONFIGURAÇÃO DO SISTEMA
 * =================================================================================
 */

/**
 * LOOP_DELAY_US — Tempo de ciclo do loop principal (microssegundos)
 * 
 * 10000 µs = 10ms = 100Hz (determinístico)
 * Este valor garante que o loop de controlo é executado exatamente a 100Hz,
 * permitindo que os PIDs sejam calculados com dt consistente.
 */
#define LOOP_DELAY_US       10000

/**
 * UART_BAUD_RATE — Velocidade de comunicação entre ESP1 e ESP2
 * 
 * 921600 baud é suficientemente rápido para transmitir dados RAW a 100Hz
 * sem perdas ou bloqueios.
 */
#define UART_BAUD_RATE      921600

/**
 * WATCHDOG_TIMEOUT_MS — Timeout do watchdog (milissegundos)
 * 
 * Se o loop principal bloquear por mais de 2 segundos, o watchdog
 * reinicia o ESP1 automaticamente.
 */
#define WATCHDOG_TIMEOUT_MS 2000

/* =================================================================================
 * INSTÂNCIAS DOS MÓDULOS
 * =================================================================================
 */

/* Protocolo e comunicação */
Parser      parser;         /* Parser TLV byte-a-byte */
Security    security;       /* Segurança (HMAC, anti-replay, lockdown) */

/* Controlo de voo e atuadores */
FlightControl flightControl; /* Controlador de voo (PIDs, modos, navegação) */
Actuators   actuators;      /* Controlo de servos e motor (PWM) */

/* Sensores e telemetria */
Sensors     sensors;        /* Aquisição de dados RAW */
Telemetry   telemetry;      /* Caixa negra (logging para SD/SPIFFS) */

/* Shell de comandos */
Shell       shell;          /* Consola de debug (USB e LoRa) */

/* =================================================================================
 * VARIÁVEIS GLOBAIS
 * =================================================================================
 */

/**
 * _lastLoopUs — Timestamp do último ciclo (microssegundos)
 * 
 * Utilizado para manter o loop determinístico a 100Hz.
 */
static uint32_t _lastLoopUs = 0;

/**
 * _loopCount — Contador de ciclos (para diagnóstico)
 */
static uint32_t _loopCount = 0;

/**
 * _failsafeTriggered — Flag de failsafe ativado
 */
static bool _failsafeTriggered = false;

/* =================================================================================
 * PROTÓTIPOS DE FUNÇÕES
 * =================================================================================
 */

void setup();
void loop();
void processLoRaCommands();
void processUSBCommands();
void sendRawDataToESP2();
void updateFlightStateFromESP2();
void checkFailsafeConditions();
void printSystemStatus();

/* =================================================================================
 * SETUP — CONFIGURAÇÃO INICIAL DO SISTEMA
 * =================================================================================
 */

void setup() {
    /* =========================================================================
     * INICIALIZAÇÃO DA COMUNICAÇÃO SERIAL
     * ========================================================================= */
    
    /* USB Serial (debug e shell) */
    Serial.begin(115200);
    delay(100);
    DEBUG_PRINT("\n\n=== BeaconFly UAS ESP1 — Flight Controller ===\n");
    DEBUG_PRINT("Versão: 2.0.0\n");
    DEBUG_PRINT("Data:   2026-04-19\n\n");
    
    /* UART para comunicação com ESP2 */
    Serial2.begin(UART_BAUD_RATE);
    DEBUG_PRINT("UART com ESP2 inicializada a %d baud\n", UART_BAUD_RATE);
    
    /* =========================================================================
     * INICIALIZAÇÃO DOS MÓDULOS
     * ========================================================================= */
    
    /* Inicializar sensores (IMU, barómetro, bateria, termopares) */
    sensors.begin();
    DEBUG_PRINT("Sensors inicializado\n");
    
    /* Inicializar atuadores (servos e motor) */
    actuators.begin();
    DEBUG_PRINT("Actuators inicializado\n");
    
    /* Inicializar controlador de voo (PIDs, modos) */
    flightControl.begin();
    DEBUG_PRINT("FlightControl inicializado\n");
    
    /* Inicializar segurança (HMAC, chaves, lockdown) */
    security.begin();
    DEBUG_PRINT("Security inicializado\n");
    
    /* Inicializar telemetria (caixa negra) */
    telemetry.begin();
    DEBUG_PRINT("Telemetry inicializado\n");
    
    /* Inicializar shell (consola de comandos) */
    shell.begin(&flightControl, &security, &telemetry, false);  /* false = ESP1 */
    shell.setDebug(true);
    shell.setEcho(true);
    DEBUG_PRINT("Shell inicializado\n");
    
    /* =========================================================================
     * CONFIGURAÇÃO DO LOOP DETERMINÍSTICO
     * ========================================================================= */
    
    _lastLoopUs = micros();
    
    /* =========================================================================
     * CONFIGURAÇÃO DO WATCHDOG
     * ========================================================================= */
    
    esp_task_wdt_init(WATCHDOG_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);
    DEBUG_PRINT("Watchdog configurado para %d ms\n", WATCHDOG_TIMEOUT_MS);
    
    /* =========================================================================
     * REGISTAR EVENTO DE BOOT NA CAIXA NEGRA
     * ========================================================================= */
    
    telemetry.logEvent(EVT_BOOT, "ESP1 inicializado com sucesso");
    
    DEBUG_PRINT("\nSistema pronto. Aguardando comandos...\n");
    DEBUG_PRINT("Loop principal a 100Hz (10ms)\n");
    DEBUG_PRINT("========================================\n\n");
}

/* =================================================================================
 * LOOP — CICLO PRINCIPAL (DETERMINÍSTICO A 100Hz)
 * =================================================================================
 */

void loop() {
    /* =========================================================================
     * TIMING — GARANTIR LOOP A 100Hz
     * ========================================================================= */
    
    uint32_t nowUs = micros();
    uint32_t elapsedUs = nowUs - _lastLoopUs;
    
    if (elapsedUs < LOOP_DELAY_US) {
        /* Ainda não é hora de executar o ciclo */
        delayMicroseconds(LOOP_DELAY_US - elapsedUs);
        nowUs = micros();
        elapsedUs = nowUs - _lastLoopUs;
    }
    
    /* Calcular dt para os PIDs (em segundos) */
    float dt = elapsedUs / 1000000.0f;
    _lastLoopUs = nowUs;
    _loopCount++;
    
    /* Reset watchdog (indicar que o sistema está vivo) */
    esp_task_wdt_reset();
    
    /* =========================================================================
     * 1. ATUALIZAR SENSORES
     * ========================================================================= */
    
    sensors.update();
    
    /* =========================================================================
     * 2. ENVIAR DADOS RAW PARA ESP2 VIA UART
     * ========================================================================= */
    
    sendRawDataToESP2();
    
    /* =========================================================================
     * 3. PROCESSAR COMANDOS VIA LoRa (GROUND STATION)
     * ========================================================================= */
    
    processLoRaCommands();
    
    /* =========================================================================
     * 4. PROCESSAR COMANDOS VIA USB (SHELL DEBUG)
     * ========================================================================= */
    
    processUSBCommands();
    
    /* =========================================================================
     * 5. ATUALIZAR ESTADO DE VOO A PARTIR DO ESP2 (DADOS SI)
     * ========================================================================= */
    
    updateFlightStateFromESP2();
    
    /* =========================================================================
     * 6. ATUALIZAR ESTADO DE VOO NA SEGURANÇA
     * ========================================================================= */
    
    bool inFlight = (flightControl.getMode() == MODE_FLYING || 
                     flightControl.getMode() == MODE_ALT_HOLD ||
                     flightControl.getMode() == MODE_AUTO ||
                     flightControl.getMode() == MODE_RTL);
    
    security.setFlightState(inFlight ? STATE_FLYING : STATE_GROUND_DISARMED);
    shell.setFlightState(inFlight, _failsafeTriggered);
    
    /* =========================================================================
     * 7. VERIFICAR CONDIÇÕES DE FAILSAFE
     * ========================================================================= */
    
    checkFailsafeConditions();
    
    /* =========================================================================
     * 8. EXECUTAR CONTROLADOR DE VOO (PIDs)
     * ========================================================================= */
    
    flightControl.update();
    
    /* =========================================================================
     * 9. ATUALIZAR ATUADORES (SINAIS PWM)
     * ========================================================================= */
    
    actuators.update(flightControl.getOutputs());
    
    /* =========================================================================
     * 10. LOGGING PARA CAIXA NEGRA (TELEMETRY)
     * ========================================================================= */
    
    telemetry.update(flightControl, actuators);
    
    /* =========================================================================
     * 11. COMUNICAÇÃO UART (ENTRE ESP1 E ESP2)
     * ========================================================================= */
    
    shell.updateUART();
    
    /* =========================================================================
     * 12. DIAGNÓSTICO PERIÓDICO (A CADA 1000 CICLOS ≈ 10 SEGUNDOS)
     * ========================================================================= */
    
    if (_loopCount % 1000 == 0) {
        printSystemStatus();
    }
}

/* =================================================================================
 * PROCESSAR COMANDOS VIA LoRa (GROUND STATION)
 * =================================================================================
 * 
 * Esta função verifica se há pacotes recebidos via LoRa 2.4GHz e processa
 * os comandos da Ground Station.
 * 
 * NOTA: O ESP1 NUNCA transmite via LoRa — apenas recebe.
 *       As respostas são enviadas pelo ESP2 via LoRa 868MHz.
 */

void processLoRaCommands() {
    /* TODO: Integrar com o módulo LoRa quando implementado */
    /* 
    if (lora.available()) {
        uint8_t buffer[MAX_MESSAGE_SIZE];
        size_t len = lora.readPacket(buffer, sizeof(buffer));
        
        // Alimentar parser byte a byte
        for (size_t i = 0; i < len; i++) {
            if (parser.feed(buffer[i])) {
                TLVMessage* msg = parser.getMessage();
                
                // Extrair seqNum e HMAC (do cabeçalho da mensagem)
                uint32_t seqNum = 0;  // TODO: extrair do buffer
                uint8_t hmac[32];     // TODO: extrair do buffer
                
                // Verificar segurança
                SecurityResult result = security.verifyPacket(*msg, seqNum, hmac);
                
                if (result == SEC_OK) {
                    // Comando autenticado — processar
                    if (msg->msgID == MSG_COMMAND) {
                        flightControl.processCommand(*msg);
                    } else if (msg->msgID == MSG_SHELL_CMD) {
                        shell.processShellCommand(*msg, SHELL_ORIGIN_LORA);
                    }
                } else if (result == SEC_LOCKDOWN) {
                    // Lockdown ativo — apenas MSG_FAILSAFE é aceite
                    if (msg->msgID == MSG_FAILSAFE) {
                        _failsafeTriggered = true;
                        actuators.failsafeBlock();
                    }
                }
                
                parser.acknowledge();
            }
        }
    }
    */
}

/* =================================================================================
 * PROCESSAR COMANDOS VIA USB (SHELL DEBUG)
 * =================================================================================
 * 
 * Esta função processa comandos digitados no monitor série (USB) para debug
 * local durante o desenvolvimento e manutenção.
 */

void processUSBCommands() {
    if (Serial.available()) {
        char line[SHELL_MAX_LINE_LENGTH];
        int len = Serial.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';
        
        /* Remover caracteres de回车 se existirem */
        for (int i = 0; i < len; i++) {
            if (line[i] == '\r') line[i] = '\0';
        }
        
        shell.processCommand(line, SHELL_ORIGIN_USB);
    }
}

/* =================================================================================
 * ENVIAR DADOS RAW PARA ESP2 VIA UART
 * =================================================================================
 * 
 * O ESP1 adquire dados RAW dos sensores e envia para o ESP2 via UART.
 * O ESP2 converte RAW → SI e devolve para o FlightControl.
 * 
 * Formato: SensorsPayload (estrutura compacta com #pragma pack)
 */

void sendRawDataToESP2() {
    /* Obter payload compacto dos sensores */
    SensorsPayload payload = sensors.getPayload();
    
    /* Enviar via UART para o ESP2 */
    Serial2.write((uint8_t*)&payload, sizeof(payload));
    
    if (_loopCount % 100 == 0) {
        DEBUG_PRINT("Dados RAW enviados para ESP2 (%d bytes)\n", sizeof(payload));
    }
}

/* =================================================================================
 * ATUALIZAR ESTADO DE VOO A PARTIR DO ESP2 (DADOS SI)
 * =================================================================================
 * 
 * O ESP2 processa os dados RAW e envia de volta os valores em unidades SI
 * através de mensagens TLV (MSG_SI_DATA).
 */

void updateFlightStateFromESP2() {
    /* Verificar se há dados disponíveis na UART do ESP2 */
    while (Serial2.available()) {
        uint8_t byte = Serial2.read();
        
        /* Alimentar parser com os dados recebidos */
        if (parser.feed(byte)) {
            TLVMessage* msg = parser.getMessage();
            
            /* Verificar se é uma mensagem de dados SI */
            if (msg->msgID == MSG_SI_DATA) {
                /* Atualizar estado do FlightControl com os dados SI */
                flightControl.updateState(*msg);
            }
            
            parser.acknowledge();
        }
    }
}

/* =================================================================================
 * VERIFICAR CONDIÇÕES DE FAILSAFE
 * =================================================================================
 * 
 * O failsafe pode ser ativado por:
 *   1. Comando da Ground Station (MSG_FAILSAFE)
 *   2. Perda de comunicação com o ESP2 (>500ms sem dados)
 *   3. Bateria crítica (<10.5V)
 *   4. Violações de segurança (lockdown)
 */

void checkFailsafeConditions() {
    static uint32_t lastESP2DataMs = 0;
    static bool wasFailsafe = false;
    
    /* Verificar se há dados do ESP2 */
    if (flightControl.getState().timestamp != lastESP2DataMs) {
        lastESP2DataMs = flightControl.getState().timestamp;
    }
    
    /* Perda de comunicação com ESP2 (>500ms) */
    uint32_t now = millis();
    bool esp2Timeout = (now - lastESP2DataMs) > 500;
    
    /* Bateria crítica */
    float battV = flightControl.getState().battV;
    bool battCritical = (battV < 10.5f && battV > 0.1f);
    
    /* Lockdown de segurança */
    bool securityLockdown = security.isInLockdown();
    
    /* Ativar failsafe se necessário */
    bool shouldFailsafe = (esp2Timeout || battCritical || securityLockdown);
    
    if (shouldFailsafe && !_failsafeTriggered) {
        _failsafeTriggered = true;
        
        /* Ativar failsafe nos atuadores */
        actuators.failsafeBlock();
        
        /* Mudar para modo RTL no FlightControl */
        flightControl.setMode(MODE_RTL);
        
        /* Registrar evento na caixa negra */
        if (esp2Timeout) {
            telemetry.logEvent(EVT_FAILSAFE_ON, "Perda de comunicação com ESP2");
            DEBUG_PRINT("⚠️ FAILSAFE: Perda de comunicação com ESP2\n");
        } else if (battCritical) {
            telemetry.logEvent(EVT_FAILSAFE_ON, "Bateria crítica");
            DEBUG_PRINT("⚠️ FAILSAFE: Bateria crítica (%.1fV)\n", battV);
        } else if (securityLockdown) {
            telemetry.logEvent(EVT_FAILSAFE_ON, "Lockdown de segurança");
            DEBUG_PRINT("⚠️ FAILSAFE: Lockdown de segurança\n");
        }
    }
    
    /* Desativar failsafe se as condições normalizarem */
    if (!shouldFailsafe && _failsafeTriggered && !securityLockdown) {
        _failsafeTriggered = false;
        
        /* Limpar failsafe nos atuadores */
        actuators.failsafeClear();
        
        /* Registrar evento na caixa negra */
        telemetry.logEvent(EVT_FAILSAFE_OFF, "Condições normalizadas");
        DEBUG_PRINT("✅ FAILSAFE: Desativado (condições normalizadas)\n");
    }
}

/* =================================================================================
 * IMPRIMIR ESTADO DO SISTEMA (DIAGNÓSTICO)
 * =================================================================================
 * 
 * Esta função é chamada periodicamente (a cada ~10 segundos) para fornecer
 * informações de diagnóstico sobre o estado do sistema.
 */

void printSystemStatus() {
    DEBUG_PRINT("\n=== STATUS DO SISTEMA ===\n");
    DEBUG_PRINT("Loop count: %lu (%.1f Hz)\n", _loopCount, _loopCount / (millis() / 1000.0f));
    DEBUG_PRINT("Modo de voo: %d\n", flightControl.getMode());
    DEBUG_PRINT("Roll: %.2f° | Pitch: %.2f° | Yaw: %.2f°\n", 
                flightControl.getState().roll,
                flightControl.getState().pitch,
                flightControl.getState().yaw);
    DEBUG_PRINT("Altitude: %.2fm | Bateria: %.1fV\n", 
                flightControl.getState().altFused,
                flightControl.getState().battV);
    DEBUG_PRINT("GPS: %s | Atitude: %s\n",
                flightControl.getState().gpsValid ? "VÁLIDO" : "INVÁLIDO",
                flightControl.getState().attitudeValid ? "VÁLIDA" : "INVÁLIDA");
    DEBUG_PRINT("Failsafe: %s | Lockdown: %s\n",
                _failsafeTriggered ? "ATIVO" : "INATIVO",
                security.isInLockdown() ? "SIM" : "NÃO");
    DEBUG_PRINT("Sensores críticos: %s\n",
                sensors.areCriticalValid() ? "OK" : "FALHA");
    DEBUG_PRINT("Backend telemetria: %d\n", telemetry.getBackend());
    DEBUG_PRINT("========================\n");
}
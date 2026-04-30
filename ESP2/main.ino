/**
 * =================================================================================
 * MAIN.INO — BEACONFLY UAS — ESP2 (COMMUNICATION & VIDEO PROCESSOR)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-30
 * VERSÃO:     2.0.0
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * Este é o programa principal do ESP2 (Communication & Video Processor) do sistema
 * BeaconFly. O ESP2 é responsável por:
 * 
 *   • Converter dados RAW do ESP1 para unidades SI
 *   • Processar dados GPS
 *   • Executar o Failsafe (vigia constante, aciona modos de emergência)
 *   • Controlar atuadores em caso de failsafe (via FlightControl)
 *   • Enviar telemetria para a Ground Station via LoRa 868MHz
 *   • Transmitir vídeo (prova de conceito)
 *   • Processar comandos shell (respostas enviadas via LoRa)
 *   • Gerir beacon de identificação legal (Wi-Fi)
 * 
 * =================================================================================
 * ARQUITETURA DO SISTEMA
 * =================================================================================
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP1                                           │
 *   │                                                                             │
 *   │   Sensors (RAW) → UART TX →                                                │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 *                                        │
 *                                        ▼ (UART RX)
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP2                                           │
 *   │                                                                             │
 *   │   ┌─────────────────────────────────────────────────────────────────────┐  │
 *   │   │                         MAIN LOOP (100Hz)                           │  │
 *   │   │                                                                      │  │
 *   │   │   1. Receber dados RAW do ESP1                                      │  │
 *   │   │   2. Converter RAW → SI (SIConverter)                               │  │
 *   │   │   3. Atualizar GPS                                                  │  │
 *   │   │   4. Atualizar Failsafe (analisa condições)                         │  │
 *   │   │   5. Se failsafe ativo → comandar FlightControl                     │  │
 *   │   │   6. Enviar dados SI de volta para ESP1 (UART)                      │  │
 *   │   │   7. Enviar telemetria para GS (LoRa 868MHz)                        │  │
 *   │   │   8. Processar comandos shell (via UART do ESP1)                    │  │
 *   │   │   9. Transmitir beacon Wi-Fi                                        │  │
 *   │   │  10. Processar vídeo (se disponível)                               │  │
 *   │   └─────────────────────────────────────────────────────────────────────┘  │
 *   │                                                                             │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 *                                        │
 *                    ┌───────────────────┼───────────────────┐
 *                    │                   │                   │
 *                    ▼                   ▼                   ▼
 *              ┌───────────┐       ┌───────────┐       ┌───────────┐
 *              │ LoRa 868MHz│       │  Wi-Fi    │       │   UART    │
 *              │  (para GS) │       │ (Beacon)  │       │ (para ESP1)│
 *              └───────────┘       └───────────┘       └───────────┘
 * 
 * =================================================================================
 * LOOP PRINCIPAL (DETERMINÍSTICO A 100Hz)
 * =================================================================================
 * 
 *   Cada ciclo de 10ms:
 *     1. Receber dados RAW do ESP1 (SensorsPayload)
 *     2. Converter RAW → SI
 *     3. Atualizar GPS
 *     4. Atualizar Failsafe (análise de condições)
 *     5. Se failsafe ativo → comandar FlightControl
 *     6. Executar FlightControl (se ativo)
 *     7. Enviar dados SI de volta para ESP1
 *     8. Enviar telemetria para GS (a cada 100ms)
 *     9. Processar comandos shell (UART)
 *    10. Transmitir beacon Wi-Fi (a cada 1 segundo)
 *    11. Processar vídeo (se disponível)
 * 
 * =================================================================================
 * PINAGEM HARDWARE (PCB BeaconFly v1.0)
 * =================================================================================
 * 
 *   UART (ESP1 ↔ ESP2):
 *     TX (ESP2 → ESP1): 17
 *     RX (ESP2 ← ESP1): 16
 * 
 *   LoRa 868MHz (SX1262):
 *     CS:     5
 *     DIO1:   4
 *     RST:    2
 *     BUSY:   15
 * 
 *   GPS:
 *     RX:     16 (partilhado com UART? Ajustar)
 *     TX:     17
 * 
 *   Beacon Wi-Fi:
 *     Antena interna do ESP32
 * 
 *   Atuadores (ESP2):
 *     Aileron D:  13
 *     Aileron E:  14
 *     Leme:       27
 *     Motor:      33
 * 
 *   LED e Buzzer (Failsafe):
 *     LED:        32
 *     Buzzer:     33
 * 
 * =================================================================================
 */

#include <Arduino.h>
#include "Protocol.h"
#include "Parser.h"
#include "SIConverter.h"
#include "GPS.h"
#include "Failsafe.h"
#include "FlightControl.h"
#include "Actuators.h"
#include "Telemetry.h"
#include "Video.h"
#include "Shell.h"
#include "LoRa.h"
#include "Beacon.h"

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
 * CONSTANTES DE CONFIGURAÇÃO
 * =================================================================================
 */

#define LOOP_DELAY_US           10000   /* 10ms = 100Hz */
#define UART_BAUD_RATE          921600  /* Velocidade UART entre ESP1 e ESP2 */
#define WATCHDOG_TIMEOUT_MS     2000    /* Timeout do watchdog (ms) */

/* =================================================================================
 * INSTÂNCIAS DOS MÓDULOS
 * =================================================================================
 */

/* Comunicação e protocolo */
Parser      parser;         /* Parser TLV byte-a-byte */
LoRa        lora;           /* LoRa 868MHz (TX only) */

/* Conversão e sensores */
SIConverter siConverter;    /* Conversão RAW → SI */
GPS         gps;            /* GPS */

/* Segurança e failsafe */
Security    security;       /* Segurança (HMAC, anti-replay) */
Failsafe    failsafe;       /* Failsafe (análise e ativação) */

/* Controlo de voo e atuadores */
FlightControl flightControl; /* Controlador de voo (emergência) */
Actuators   actuators;      /* Atuadores (ailerons, leme, motor) */

/* Telemetria e vídeo */
Telemetry   telemetry;      /* Telemetria para GS */
Video       video;          /* Vídeo (prova de conceito) */
Beacon      beacon;         /* Beacon Wi-Fi (identificação legal) */

/* Shell de comandos */
Shell       shell;          /* Consola de comandos */

/* =================================================================================
 * VARIÁVEIS GLOBAIS
 * =================================================================================
 */

static uint32_t _lastLoopUs = 0;
static uint32_t _loopCount = 0;
static uint32_t _lastBeaconMs = 0;
static uint32_t _lastTelemetryMs = 0;

/* =================================================================================
 * PROTÓTIPOS DE FUNÇÕES
 * =================================================================================
 */

void setup();
void loop();
void processUARTData();
void updateFailsafeAndFlightControl();
void sendBackToESP1();
void updateTelemetry();
void updateBeacon();
void printSystemStatus();
void onLoRaTransmit(const uint8_t* data, size_t len);
void onUARTTransmit(const uint8_t* data, size_t len);

/* =================================================================================
 * SETUP — CONFIGURAÇÃO INICIAL DO SISTEMA
 * =================================================================================
 */

void setup() {
    /* =========================================================================
     * INICIALIZAÇÃO DA COMUNICAÇÃO SERIAL
     * ========================================================================= */
    
    Serial.begin(115200);
    delay(1000);
    
    DEBUG_PRINT("\n\n=== BeaconFly UAS ESP2 — Communication & Video Processor ===\n");
    DEBUG_PRINT("Versão: 2.0.0\n");
    DEBUG_PRINT("Data:   2026-04-30\n\n");
    
    /* UART para comunicação com ESP1 */
    Serial2.begin(UART_BAUD_RATE);
    DEBUG_PRINT("UART com ESP1 inicializada a %d baud\n", UART_BAUD_RATE);
    
    /* =========================================================================
     * INICIALIZAÇÃO DOS MÓDULOS
     * ========================================================================= */
    
    /* LoRa 868MHz (TX only) */
    LoRaConfig loraConfig;
    loraConfig.frequency = 868000000;
    loraConfig.txPower = 20;
    loraConfig.spreadingFactor = 9;
    loraConfig.bandwidth = 125000;
    loraConfig.codingRate = 8;
    loraConfig.crcEnabled = true;
    
    if (lora.begin(LORA_MODULE_SX1262, loraConfig)) {
        DEBUG_PRINT("LoRa 868MHz inicializado (TX only)\n");
    } else {
        DEBUG_PRINT("ERRO: LoRa 868MHz não inicializado!\n");
    }
    lora.setDebug(true);
    
    /* SIConverter (RAW → SI) */
    siConverter.begin();
    siConverter.setDebug(true);
    DEBUG_PRINT("SIConverter inicializado\n");
    
    /* GPS */
    GPSConfig gpsConfig = GPSConfig::defaultConfig();
    gpsConfig.rxPin = 16;
    gpsConfig.txPin = 17;
    gpsConfig.enableGLONASS = true;
    if (gps.begin(gpsConfig)) {
        DEBUG_PRINT("GPS inicializado\n");
    } else {
        DEBUG_PRINT("AVISO: GPS não detetado (opcional)\n");
    }
    gps.setDebug(true);
    
    /* Security */
    security.begin();
    security.setDebug(true);
    DEBUG_PRINT("Security inicializado\n");
    
    /* Actuators */
    actuators.begin();
    actuators.setDebug(true);
    DEBUG_PRINT("Actuators inicializado\n");
    
    /* FlightControl */
    flightControl.begin(&actuators, &siConverter, &gps);
    flightControl.setDebug(true);
    DEBUG_PRINT("FlightControl inicializado\n");
    
    /* Failsafe */
    failsafe.begin();
    failsafe.setDebug(true);
    DEBUG_PRINT("Failsafe inicializado\n");
    
    /* Telemetry */
    telemetry.begin(&siConverter, &gps, &flightControl, &failsafe, &lora);
    telemetry.setDebug(true);
    DEBUG_PRINT("Telemetry inicializado\n");
    
    /* Video */
    video.begin(&lora);
    video.setDebug(true);
    DEBUG_PRINT("Video inicializado (POC)\n");
    
    /* Shell */
    shell.begin(&siConverter, &gps, &security, &flightControl, &failsafe, &lora);
    shell.setDebug(true);
    shell.setEcho(true);
    DEBUG_PRINT("Shell inicializado\n");
    
    /* Beacon (Wi-Fi identificação legal) */
    if (beacon.begin()) {
        DEBUG_PRINT("Beacon Wi-Fi inicializado\n");
    } else {
        DEBUG_PRINT("AVISO: Beacon Wi-Fi não inicializado\n");
    }
    
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
     * CONFIGURAÇÃO DE CALLBACKS
     * ========================================================================= */
    
    telemetry.setUARTCallback(onUARTTransmit);
    shell.setUARTCallback(onUARTTransmit);
    
    DEBUG_PRINT("\nSistema pronto. Loop principal a 100Hz (10ms)\n");
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
        delayMicroseconds(LOOP_DELAY_US - elapsedUs);
        nowUs = micros();
        elapsedUs = nowUs - _lastLoopUs;
    }
    
    float dt = elapsedUs / 1000000.0f;
    _lastLoopUs = nowUs;
    _loopCount++;
    
    /* Reset watchdog */
    esp_task_wdt_reset();
    
    /* =========================================================================
     * 1. RECEBER DADOS RAW DO ESP1 VIA UART
     * ========================================================================= */
    
    processUARTData();
    
    /* =========================================================================
     * 2. ATUALIZAR GPS
     * ========================================================================= */
    
    gps.update();
    
    /* =========================================================================
     * 3. CONVERTER RAW → SI (SE HOUVER NOVOS DADOS)
     * ========================================================================= */
    
    /* A conversão é feita em processUARTData() quando novos dados chegam */
    
    /* =========================================================================
     * 4. ATUALIZAR FAILSAFE E FLIGHT CONTROL
     * ========================================================================= */
    
    updateFailsafeAndFlightControl();
    
    /* =========================================================================
     * 5. EXECUTAR FLIGHTCONTROL (SE ATIVO)
     * ========================================================================= */
    
    flightControl.update(dt);
    
    /* =========================================================================
     * 6. ENVIAR DADOS SI DE VOLTA PARA ESP1
     * ========================================================================= */
    
    sendBackToESP1();
    
    /* =========================================================================
     * 7. ENVIAR TELEMETRIA PARA GS (VIA LoRa)
     * ========================================================================= */
    
    updateTelemetry();
    
    /* =========================================================================
     * 8. PROCESSAR COMANDOS SHELL (UART)
     * ========================================================================= */
    
    shell.updateUART();
    
    /* =========================================================================
     * 9. TRANSMITIR BEACON Wi-Fi (IDENTIFICAÇÃO LEGAL)
     * ========================================================================= */
    
    updateBeacon();
    
    /* =========================================================================
     * 10. PROCESSAR VÍDEO (SE DISPONÍVEL)
     * ========================================================================= */
    
    video.sendNext();
    
    /* =========================================================================
     * 11. PROCESSAR FILA LORA
     * ========================================================================= */
    
    lora.transmit();
    
    /* =========================================================================
     * 12. DIAGNÓSTICO PERIÓDICO (A CADA 1000 CICLOS ≈ 10 SEGUNDOS)
     * ========================================================================= */
    
    if (_loopCount % 1000 == 0) {
        printSystemStatus();
    }
}

/* =================================================================================
 * PROCESS UART DATA — RECEBE E PROCESSA DADOS DO ESP1
 * =================================================================================
 */

void processUARTData() {
    static SensorsPayload rawData;
    static uint8_t buffer[sizeof(SensorsPayload)];
    static size_t bufferIndex = 0;
    
    while (Serial2.available()) {
        uint8_t c = Serial2.read();
        
        /* Acumular no buffer */
        if (bufferIndex < sizeof(buffer)) {
            buffer[bufferIndex++] = c;
        }
        
        /* Verificar se temos um pacote completo */
        if (bufferIndex == sizeof(SensorsPayload)) {
            memcpy(&rawData, buffer, sizeof(SensorsPayload));
            bufferIndex = 0;
            
            /* Converter RAW → SI */
            siConverter.update(rawData);
            
            /* Atualizar dados do Failsafe */
            const SIData& si = siConverter.getData();
            failsafe.updateInputs(
                0, 0, 0,  /* heartbeatESP1, lastCommandGS, flightMode (não usados) */
                si.gyroX_dps, si.gyroY_dps, si.gyroZ_dps,
                si.accelX_ms2, si.accelY_ms2, si.accelZ_ms2,
                si.batteryVoltage_v, si.batteryCurrent_a,
                si.thermocoupleTemp_c[0]
            );
            failsafe.setFlightState(si.altitude_m > 0.5f, false);
            
            DEBUG_PRINT("Dados RAW recebidos: Accel(%.1f,%.1f,%.1f) Gyro(%.1f,%.1f,%.1f)\n",
                        si.accelX_ms2, si.accelY_ms2, si.accelZ_ms2,
                        si.gyroX_dps, si.gyroY_dps, si.gyroZ_dps);
        }
    }
}

/* =================================================================================
 * UPDATE FAILSAFE AND FLIGHT CONTROL — ANALISA E ATIVA FAILSAFE
 * =================================================================================
 */

void updateFailsafeAndFlightControl() {
    /* Processar failsafe (analisar condições) */
    FailsafeLevel level = failsafe.process();
    
    /* Se failsafe ativo, comandar FlightControl */
    if (failsafe.isActive()) {
        switch (level) {
            case FS_LEVEL_RTL:
                flightControl.setCommand(MODE_RTL);
                DEBUG_PRINT("FAILSAFE: Ativando RTL\n");
                break;
            case FS_LEVEL_LAND:
                flightControl.setCommand(MODE_LAND);
                DEBUG_PRINT("FAILSAFE: Ativando LAND\n");
                break;
            case FS_LEVEL_GLIDE:
                flightControl.setCommand(MODE_GLIDE);
                DEBUG_PRINT("FAILSAFE: Ativando GLIDE\n");
                break;
            case FS_LEVEL_FREE_FALL:
                flightControl.setCommand(MODE_FREE_FALL);
                DEBUG_PRINT("FAILSAFE: Ativando FREE_FALL\n");
                break;
            default:
                break;
        }
    } else {
        /* Se não ativo, garantir que FlightControl está em IDLE */
        if (flightControl.getMode() != MODE_IDLE) {
            flightControl.setCommand(MODE_IDLE);
            flightControl.reset();
        }
    }
}

/* =================================================================================
 * SEND BACK TO ESP1 — ENVIA DADOS SI DE VOLTA PARA ESP1
 * =================================================================================
 */

void sendBackToESP1() {
    static uint32_t lastSend = 0;
    uint32_t now = millis();
    
    /* Enviar a cada 20ms (50Hz) para não sobrecarregar a UART */
    if (now - lastSend >= 20) {
        const SIData& si = siConverter.getData();
        telemetry.sendToESP1(si);
        lastSend = now;
    }
}

/* =================================================================================
 * UPDATE TELEMETRY — ENVIA TELEMETRIA PARA GS
 * =================================================================================
 */

void updateTelemetry() {
    telemetry.send();
}

/* =================================================================================
 * UPDATE BEACON — TRANSMITE BEACON Wi-Fi
 * =================================================================================
 */

void updateBeacon() {
    uint32_t now = millis();
    
    /* Transmitir a cada 1 segundo */
    if (now - _lastBeaconMs >= 1000) {
        const SIData& si = siConverter.getData();
        const GPSData& gpsData = gps.getData();
        
        beacon.updatePosition(gpsData.latitude, gpsData.longitude,
                              si.altitude_m, si.speedMs, 0.0f, gpsData.heading);
        beacon.loop();
        _lastBeaconMs = now;
    }
}

/* =================================================================================
 * ON LORA TRANSMIT — CALLBACK PARA ENVIO VIA LoRa
 * =================================================================================
 */

void onLoRaTransmit(const uint8_t* data, size_t len) {
    lora.sendImmediate(data, len);
}

/* =================================================================================
 * ON UART TRANSMIT — CALLBACK PARA ENVIO VIA UART PARA ESP1
 * =================================================================================
 */

void onUARTTransmit(const uint8_t* data, size_t len) {
    Serial2.write(data, len);
}

/* =================================================================================
 * PRINT SYSTEM STATUS — IMPRIME ESTADO DO SISTEMA
 * =================================================================================
 */

void printSystemStatus() {
    DEBUG_PRINT("\n=== STATUS DO SISTEMA (ESP2) ===\n");
    DEBUG_PRINT("Loop count: %lu (%.1f Hz)\n", _loopCount, _loopCount / (millis() / 1000.0f));
    
    /* Estado do GPS */
    if (gps.hasFix()) {
        const GPSData& gpsData = gps.getData();
        DEBUG_PRINT("GPS: Lat=%.6f Lon=%.6f Alt=%.1fm Sats=%d\n",
                    gpsData.latitude, gpsData.longitude,
                    gpsData.altitude, gpsData.satellitesUsed);
    } else {
        DEBUG_PRINT("GPS: SEM FIX\n");
    }
    
    /* Estado do Failsafe */
    DEBUG_PRINT("Failsafe: %s | Nível: %d | Razão: %d\n",
                failsafe.isActive() ? "ATIVO" : "INATIVO",
                failsafe.getLevel(), failsafe.getReason());
    
    /* Estado do FlightControl */
    DEBUG_PRINT("FlightControl: modo=%d | ativo=%s\n",
                flightControl.getMode(), flightControl.isActive() ? "SIM" : "NÃO");
    
    /* Estado do LoRa */
    const LoRaStats& loraStats = lora.getStats();
    DEBUG_PRINT("LoRa: enviados=%lu | falhas=%lu | fila=%d\n",
                loraStats.packetsSent, loraStats.packetsFailed, loraStats.queueSize);
    
    /* Estado da Telemetria */
    DEBUG_PRINT("Telemetria: enviados=%lu | modo=%d\n",
                telemetry.getPacketsSent(), telemetry.getMode());
    
    /* Estado do SIConverter */
    DEBUG_PRINT("SIConverter: críticos=%s\n",
                siConverter.areCriticalValid() ? "VÁLIDOS" : "INVÁLIDOS");
    
    /* Estado do Beacon */
    DEBUG_PRINT("Beacon: %s\n", beacon.isActive() ? "ATIVO" : "INATIVO");
    
    /* Memória livre */
    DEBUG_PRINT("Memória livre: %u bytes\n", esp_get_free_heap_size());
    DEBUG_PRINT("================================\n");
}
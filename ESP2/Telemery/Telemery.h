/**
 * =================================================================================
 * TELEMETRY.H — GESTÃO DE TELEMETRIA PARA GROUND STATION (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-29
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * Este módulo é responsável por:
 *   1. Coletar dados de todos os subsistemas (SI, GPS, estado, etc.)
 *   2. Construir mensagens TLV (Type-Length-Value) com os dados
 *   3. Enviar telemetria para a Ground Station via LoRa 868MHz
 *   4. Enviar dados SI de volta para o ESP1 via UART
 * 
 * =================================================================================
 * FLUXO DE DADOS
 * =================================================================================
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP2                                           │
 *   │                                                                             │
 *   │   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐    │
 *   │   │  SIConverter│   │     GPS     │   │  Failsafe   │   │ FlightControl│    │
 *   │   │   (SI)      │   │  (posição)  │   │  (estado)   │   │   (modo)    │    │
 *   │   └──────┬──────┘   └──────┬──────┘   └──────┬──────┘   └──────┬──────┘    │
 *   │          │                 │                 │                 │            │
 *   │          └─────────────────┼─────────────────┼─────────────────┘            │
 *   │                            │                 │                              │
 *   │                            ▼                 ▼                              │
 *   │                    ┌─────────────────────────────────┐                      │
 *   │                    │        Telemetry::build()       │                      │
 *   │                    │  • Constroi mensagem TLV        │                      │
 *   │                    │  • Adiciona todos os campos     │                      │
 *   │                    │  • Retorna buffer serializado   │                      │
 *   │                    └─────────────────────────────────┘                      │
 *   │                                        │                                     │
 *   │                    ┌───────────────────┴───────────────────┐                │
 *   │                    │                                       │                │
 *   │                    ▼                                       ▼                │
 *   │            ┌─────────────┐                         ┌─────────────┐         │
 *   │            │  LoRa 868MHz│                         │  UART (ESP1)│         │
 *   │            │  (para GS)  │                         │  (SI data)  │         │
 *   │            └─────────────┘                         └─────────────┘         │
 *   │                                                                             │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * PRIORIDADES DE ENVIO
 * =================================================================================
 * 
 *   O módulo Telemetry NÃO gerencia prioridades diretamente. Apenas constrói
 *   os pacotes e os entrega ao módulo LoRa, que possui fila de prioridades.
 * 
 *   Prioridades atribuídas pelo LoRa:
 *     • FAILSAFE (0)  → Mensagens de emergência
 *     • COMMAND (1)   → Respostas a comandos
 *     • TELEMETRY (2) → Telemetria normal (este módulo)
 *     • SHELL (3)     → Respostas shell
 *     • VIDEO (4)     → Vídeo
 *     • DEBUG (5)     → Debug
 * 
 * =================================================================================
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "Protocol.h"
#include "Parser.h"
#include "SIConverter.h"
#include "GPS.h"
#include "FlightControl.h"
#include "Failsafe.h"
#include "LoRa.h"

/* =================================================================================
 * CONSTANTES DE CONFIGURAÇÃO
 * =================================================================================
 */

#define TELEMETRY_MAX_TLVS          32      /* Máximo de campos TLV por mensagem */
#define TELEMETRY_BUFFER_SIZE       512     /* Tamanho do buffer de telemetria */
#define TELEMETRY_SEND_INTERVAL_MS  100     /* Intervalo de envio (ms) — 10Hz */
#define TELEMETRY_FAST_INTERVAL_MS  50      /* Intervalo rápido (ms) — 20Hz (failsafe) */

/* =================================================================================
 * ENUMS
 * =================================================================================
 */

/**
 * TelemetryMode — Modo de operação da telemetria
 */
enum TelemetryMode : uint8_t {
    TELEMETRY_MODE_NORMAL   = 0,   /* Envio normal a 10Hz */
    TELEMETRY_MODE_FAST     = 1,   /* Envio rápido a 20Hz (failsafe ativo) */
    TELEMETRY_MODE_DEBUG    = 2    /* Envio de debug (todos os dados) */
};

/* =================================================================================
 * CLASSE TELEMETRY
 * =================================================================================
 */

class Telemetry {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    Telemetry();
    
    /**
     * Inicializa o módulo de telemetria
     * 
     * @param si       Ponteiro para o SIConverter (dados SI)
     * @param gps      Ponteiro para o GPS
     * @param fc       Ponteiro para o FlightControl
     * @param failsafe Ponteiro para o Failsafe
     * @param lora     Ponteiro para o LoRa (para envio)
     * @return true se inicializado com sucesso
     */
    bool begin(SIConverter* si, GPS* gps, FlightControl* fc, 
               Failsafe* failsafe, LoRa* lora);
    
    /**
     * Define o modo de telemetria
     * 
     * @param mode Modo (NORMAL, FAST, DEBUG)
     */
    void setMode(TelemetryMode mode);
    
    /**
     * Define o modo de telemetria com base no estado do failsafe
     * 
     * @param failsafeActive true se failsafe ativo
     */
    void setFailsafeMode(bool failsafeActive);
    
    /* =========================================================================
     * CONSTRUÇÃO DE PACOTES
     * ========================================================================= */
    
    /**
     * Constrói um pacote de telemetria completa
     * 
     * @param buffer Buffer de saída
     * @param size   Tamanho do buffer
     * @return Tamanho do pacote construído (0 se erro)
     */
    size_t buildTelemetryPacket(uint8_t* buffer, size_t size);
    
    /**
     * Constrói um pacote de telemetria reduzida (apenas dados críticos)
     * 
     * @param buffer Buffer de saída
     * @param size   Tamanho do buffer
     * @return Tamanho do pacote construído (0 se erro)
     */
    size_t buildCriticalPacket(uint8_t* buffer, size_t size);
    
    /**
     * Constrói um pacote de telemetria de debug (todos os dados)
     * 
     * @param buffer Buffer de saída
     * @param size   Tamanho do buffer
     * @return Tamanho do pacote construído (0 se erro)
     */
    size_t buildDebugPacket(uint8_t* buffer, size_t size);
    
    /* =========================================================================
     * ENVIO (VIA LORA)
     * ========================================================================= */
    
    /**
     * Envia telemetria para a Ground Station
     * 
     * Deve ser chamada periodicamente (no loop principal).
     * Utiliza o intervalo configurado para controlar a taxa de envio.
     */
    void send();
    
    /**
     * Envia telemetria imediatamente (ignora intervalo)
     */
    void sendImmediate();
    
    /* =========================================================================
     * ENVIO PARA ESP1 (VIA UART)
     * ========================================================================= */
    
    /**
     * Envia dados SI de volta para o ESP1 via UART
     * 
     * @param data Dados SI a enviar
     */
    void sendToESP1(const SIData& data);
    
    /**
     * Envia um comando para o ESP1 via UART
     * 
     * @param cmdID  ID do comando
     * @param value  Valor do comando (opcional)
     */
    void sendCommandToESP1(uint8_t cmdID, float value = 0);
    
    /* =========================================================================
     * CONFIGURAÇÃO
     * ========================================================================= */
    
    /**
     * Ativa/desativa o envio de telemetria
     */
    void setEnabled(bool enable);
    
    /**
     * Verifica se a telemetria está ativa
     */
    bool isEnabled() const;
    
    /**
     * Define o callback para envio via UART para ESP1
     */
    void setUARTCallback(void (*callback)(const uint8_t*, size_t));
    
    /* =========================================================================
     * ESTATÍSTICAS
     * ========================================================================= */
    
    /**
     * Retorna o número de pacotes enviados
     */
    uint32_t getPacketsSent() const;
    
    /**
     * Reseta as estatísticas
     */
    void resetStats();
    
    /* =========================================================================
     * DIAGNÓSTICO
     * ========================================================================= */
    
    /**
     * Retorna uma string com o estado atual do módulo
     */
    void getStatusString(char* buffer, size_t bufferSize) const;
    
    /* =========================================================================
     * DEBUG
     * ========================================================================= */
    
    void setDebug(bool enable);

private:
    /* =========================================================================
     * PONTEIROS PARA SUBSISTEMAS
     * ========================================================================= */
    
    SIConverter*   _si;
    GPS*           _gps;
    FlightControl* _flightControl;
    Failsafe*      _failsafe;
    LoRa*          _lora;
    
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    TelemetryMode  _mode;
    bool           _enabled;
    bool           _debugEnabled;
    uint32_t       _lastSendMs;
    uint32_t       _packetsSent;
    uint32_t       _packetsFailed;
    
    /* =========================================================================
     * BUFFERS
     * ========================================================================= */
    
    uint8_t        _buffer[TELEMETRY_BUFFER_SIZE];
    
    /* =========================================================================
     * CALLBACKS
     * ========================================================================= */
    
    void           (*_uartCallback)(const uint8_t*, size_t);
    
    /* =========================================================================
     * INSTÂNCIA GLOBAL
     * ========================================================================= */
    
    static Telemetry* _instance;
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — CONSTRUÇÃO DE CAMPOS
     * ========================================================================= */
    
    void _addAttitudeFields(TLVMessage& msg);
    void _addAccelFields(TLVMessage& msg);
    void _addGyroFields(TLVMessage& msg);
    void _addAltitudeFields(TLVMessage& msg);
    void _addBatteryFields(TLVMessage& msg);
    void _addTemperatureFields(TLVMessage& msg);
    void _addGPSFields(TLVMessage& msg);
    void _addSystemFields(TLVMessage& msg);
    void _addFailsafeFields(TLVMessage& msg);
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — UTILITÁRIOS
     * ========================================================================= */
    
    uint32_t _getSendInterval() const;
    void _sendToLoRa(const uint8_t* data, size_t len);
    void _sendToUART(const uint8_t* data, size_t len);
    void _addField(TLVMessage& msg, uint8_t id, float value);
    void _addField(TLVMessage& msg, uint8_t id, int32_t value);
    void _addField(TLVMessage& msg, uint8_t id, uint32_t value);
    void _addField(TLVMessage& msg, uint8_t id, uint16_t value);
    void _addField(TLVMessage& msg, uint8_t id, uint8_t value);
};

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* telemetryModeToString(TelemetryMode mode);

#endif /* TELEMETRY_H */
/**
 * =================================================================================
 * LORA.H — DRIVER DE COMUNICAÇÃO LoRa 868MHz (ESP2 → GS)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-27
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * Este módulo é responsável por toda a comunicação do ESP2 com a Ground Station
 * via rádio LoRa na frequência de 868MHz.
 * 
 * DIFERENÇAS CRÍTICAS PARA O ESP1:
 *   • ESP1: Apenas RECEBE comandos da GS via 2.4GHz
 *   • ESP2: Apenas TRANSMITE para a GS via 868MHz
 * 
 * O ESP2 NUNCA recebe nada diretamente da GS. Toda a comunicação incoming
 * (comandos) chega via ESP1 através da UART.
 * 
 * =================================================================================
 * FUNCIONALIDADES
 * =================================================================================
 * 
 *   1. Transmissão de pacotes via LoRa 868MHz
 *   2. Gestão de fila de transmissão (prioridades)
 *   3. Estatísticas de transmissão
 *   4. Configuração dinâmica de parâmetros RF
 *   5. Suporte a múltiplos módulos (SX1262, SX1276, etc.)
 * 
 * =================================================================================
 * ARQUITETURA
 * =================================================================================
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP2                                           │
 *   │                                                                             │
 *   │   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐    │
 *   │   │  Telemetry  │   │   Video     │   │   Shell     │   │   Failsafe  │    │
 *   │   └──────┬──────┘   └──────┬──────┘   └──────┬──────┘   └──────┬──────┘    │
 *   │          │                 │                 │                 │            │
 *   │          └─────────────────┼─────────────────┼─────────────────┘            │
 *   │                            │                 │                              │
 *   │                            ▼                 ▼                              │
 *   │                    ┌─────────────────────────────────┐                      │
 *   │                    │           Scheduler             │                      │
 *   │                    │  (Prioridade: Failsafe > Telemetria)                    │
 *   │                    └─────────────────────────────────┘                      │
 *   │                                        │                                     │
 *   │                                        ▼                                     │
 *   │                    ┌─────────────────────────────────┐                      │
 *   │                    │         LoRa (este módulo)      │                      │
 *   │                    │  • Fila de transmissão          │                      │
 *   │                    │  • Gestão de prioridades        │                      │
 *   │                    │  • Driver de hardware           │                      │
 *   │                    └─────────────────────────────────┘                      │
 *   │                                        │                                     │
 *   │                                        ▼ (TX apenas)                         │
 *   │                    ┌─────────────────────────────────┐                      │
 *   │                    │      Módulo LoRa 868MHz         │                      │
 *   │                    │      (SX1262 / SX1276)          │                      │
 *   │                    └─────────────────────────────────┘                      │
 *   │                                        │                                     │
 *   │                                        ▼                                     │
 *   │                              (Telemetria para GS)                            │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * PRIORIDADES DE TRANSMISSÃO
 * =================================================================================
 * 
 *   O Scheduler (módulo separado) decide o que enviar. Este módulo apenas
 *   transmite o que lhe é entregue, com uma fila interna para evitar perdas.
 * 
 *   Prioridade (da mais alta para a mais baixa):
 *     1. FAILSAFE      → Mensagens críticas de emergência
 *     2. COMMAND       → Respostas a comandos da GS (ACKs)
 *     3. TELEMETRY     → Dados de voo em tempo real
 *     4. SHELL         → Respostas de comandos shell
 *     5. VIDEO         → Stream de vídeo (prova de conceito)
 *     6. DEBUG         → Mensagens de debug (apenas desenvolvimento)
 * 
 * =================================================================================
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

/* =================================================================================
 * CONSTANTES DE CONFIGURAÇÃO (Pinos - Ajustar conforme PCB)
 * =================================================================================
 */

/* Pinos para módulos LoRa (ex: SX1262, SX1276) */
#define LORA_NSS_PIN             5      /* Chip Select (CS) */
#define LORA_DIO1_PIN            4      /* DIO1 - interrupção */
#define LORA_RESET_PIN           2      /* Reset */
#define LORA_BUSY_PIN            15     /* Busy (para SX1262) */

/* Pinos alternativos para SX1276 */
#define LORA_DIO0_PIN            4      /* DIO0 - interrupção (SX1276) */

/* =================================================================================
 * CONSTANTES DE CONFIGURAÇÃO RF
 * =================================================================================
 */

#define LORA_DEFAULT_FREQUENCY   868000000UL   /* 868 MHz (Europa) */
#define LORA_DEFAULT_TX_POWER    20            /* dBm (máximo permitido) */
#define LORA_DEFAULT_SPREADING_FACTOR 9       /* SF9 (equilíbrio alcance/velocidade) */
#define LORA_DEFAULT_BANDWIDTH   125000        /* 125 kHz */
#define LORA_DEFAULT_CODING_RATE 8             /* 4/8 (mais robusto) */

/* =================================================================================
 * CONSTANTES DE OPERAÇÃO
 * =================================================================================
 */

#define LORA_TX_TIMEOUT_MS       10000         /* Timeout de transmissão (ms) */
#define LORA_MAX_PACKET_SIZE     256           /* Tamanho máximo do pacote */
#define LORA_TX_QUEUE_SIZE       8             /* Tamanho da fila de transmissão */

/* =================================================================================
 * ENUMS
 * =================================================================================
 */

/**
 * LoRaModuleType — Tipo de módulo LoRa utilizado
 */
enum LoRaModuleType : uint8_t {
    LORA_MODULE_SX1262 = 0,      /* Semtech SX1262 (recomendado) */
    LORA_MODULE_SX1276 = 1,      /* Semtech SX1276 (fallback) */
    LORA_MODULE_RAK811 = 2,      /* RAKWireless RAK811 */
    LORA_MODULE_CUSTOM = 3       /* Módulo personalizado */
};

/**
 * LoRaStatus — Estado atual do módulo LoRa
 */
enum LoRaStatus : uint8_t {
    LORA_STATUS_IDLE       = 0,  /* Pronto para transmitir */
    LORA_STATUS_BUSY       = 1,  /* A transmitir */
    LORA_STATUS_ERROR      = 2,  /* Erro de inicialização/hardware */
    LORA_STATUS_NO_MODULE  = 3   /* Módulo não encontrado */
};

/**
 * LoRaPriority — Prioridade de transmissão
 */
enum LoRaPriority : uint8_t {
    LORA_PRIORITY_FAILSAFE  = 0,   /* Emergência */
    LORA_PRIORITY_COMMAND   = 1,   /* Respostas a comandos */
    LORA_PRIORITY_TELEMETRY = 2,   /* Telemetria normal */
    LORA_PRIORITY_SHELL     = 3,   /* Shell remoto */
    LORA_PRIORITY_VIDEO     = 4,   /* Vídeo (prova de conceito) */
    LORA_PRIORITY_DEBUG     = 5    /* Debug (mais baixa) */
};

/* =================================================================================
 * ESTRUTURAS
 * =================================================================================
 */

/**
 * LoRaConfig — Configuração do módulo LoRa
 */
struct LoRaConfig {
    uint32_t frequency;          /* Frequência central (Hz) */
    int8_t   txPower;            /* Potência de transmissão (dBm) */
    uint8_t  spreadingFactor;    /* Spreading Factor (5-12) */
    uint32_t bandwidth;          /* Largura de banda (Hz) */
    uint8_t  codingRate;         /* Coding Rate (5=4/5, 6=4/6, 7=4/7, 8=4/8) */
    bool     crcEnabled;         /* CRC ativo? */
};

/**
 * LoRaStats — Estatísticas de transmissão
 */
struct LoRaStats {
    uint32_t packetsSent;        /* Total de pacotes enviados */
    uint32_t packetsFailed;      /* Pacotes com falha de envio */
    uint32_t queueOverflows;     /* Pacotes perdidos (fila cheia) */
    int16_t  lastRSSI;           /* RSSI do último pacote (se aplicável) */
    int16_t  lastSNR;            /* SNR do último pacote (se aplicável) */
    uint32_t lastTransmitTime;   /* Timestamp da última transmissão (ms) */
    uint8_t  queueSize;          /* Tamanho atual da fila */
};

/**
 * LoRaPacket — Estrutura de pacote para a fila
 */
struct LoRaPacket {
    uint8_t      data[LORA_MAX_PACKET_SIZE];
    size_t       length;
    LoRaPriority priority;
    uint32_t     timestamp;
};

/* =================================================================================
 * CLASSE LORA
 * =================================================================================
 */

class LoRa {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    /**
     * Construtor — inicializa variáveis internas
     */
    LoRa();
    
    /**
     * Inicializa o módulo LoRa
     * 
     * @param moduleType Tipo de módulo (SX1262, SX1276, etc.)
     * @param config     Configuração RF (frequência, potência, etc.)
     * @return true se inicializado com sucesso
     */
    bool begin(LoRaModuleType moduleType, const LoRaConfig& config);
    
    /**
     * Encerra o módulo LoRa
     */
    void end();
    
    /* =========================================================================
     * TRANSMISSÃO
     * ========================================================================= */
    
    /**
     * Envia um pacote via LoRa
     * 
     * Adiciona o pacote à fila de transmissão. O envio real ocorre
     * na função transmit() ou sendFromQueue().
     * 
     * @param data     Ponteiro para os dados a enviar
     * @param length   Tamanho dos dados (bytes)
     * @param priority Prioridade do pacote (0=mais alta)
     * @return true se adicionado à fila com sucesso
     */
    bool send(const uint8_t* data, size_t length, LoRaPriority priority);
    
    /**
     * Processa a fila de transmissão
     * 
     * Deve ser chamada frequentemente no loop().
     * Envia o próximo pacote da fila (maior prioridade).
     */
    void transmit();
    
    /**
     * Envia um pacote imediatamente (ignora a fila)
     * 
     * @param data   Ponteiro para os dados
     * @param length Tamanho dos dados
     * @return true se enviado com sucesso
     */
    bool sendImmediate(const uint8_t* data, size_t length);
    
    /* =========================================================================
     * CONFIGURAÇÃO DINÂMICA
     * ========================================================================= */
    
    /**
     * Altera a frequência de operação em tempo real
     * 
     * @param freqHz Nova frequência em Hz
     * @return true se sucesso
     */
    bool setFrequency(uint32_t freqHz);
    
    /**
     * Altera a potência de transmissão em tempo real
     * 
     * @param txPower Nova potência em dBm
     * @return true se sucesso
     */
    bool setTxPower(int8_t txPower);
    
    /* =========================================================================
     * CONSULTA DE ESTADO
     * ========================================================================= */
    
    /**
     * Verifica se o módulo está pronto para transmitir
     */
    bool isReady() const;
    
    /**
     * Retorna o status atual do módulo
     */
    LoRaStatus getStatus() const;
    
    /**
     * Retorna as estatísticas de transmissão
     */
    const LoRaStats& getStats() const;
    
    /**
     * Limpa as estatísticas
     */
    void resetStats();
    
    /**
     * Limpa a fila de transmissão
     */
    void clearQueue();
    
    /* =========================================================================
     * UTILITÁRIOS
     * ========================================================================= */
    
    /**
     * Converte uma prioridade para string (debug)
     */
    static const char* priorityToString(LoRaPriority priority);
    
    /* =========================================================================
     * DEBUG
     * ========================================================================= */
    
    void setDebug(bool enable);

private:
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    LoRaModuleType _moduleType;
    LoRaConfig     _config;
    LoRaStatus     _status;
    LoRaStats      _stats;
    bool           _debugEnabled;
    bool           _initialized;
    
    /* =========================================================================
     * FILA DE TRANSMISSÃO
     * ========================================================================= */
    
    LoRaPacket     _queue[LORA_TX_QUEUE_SIZE];
    uint8_t        _queueHead;
    uint8_t        _queueTail;
    uint8_t        _queueCount;
    
    /* =========================================================================
     * TEMPORIZADORES
     * ========================================================================= */
    
    uint32_t       _lastTransmitMs;
    uint32_t       _lastSendAttemptMs;
    
    /* =========================================================================
     * PONTEIRO PARA O OBJETO RÁDIO (OPACO)
     * ========================================================================= */
    
    void*          _radio;          /* Ponteiro para SX1262, SX1276, etc. */
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — HARDWARE
     * ========================================================================= */
    
    bool _initSX1262();
    bool _initSX1276();
    bool _sendPacket(const uint8_t* data, size_t length);
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — FILA
     * ========================================================================= */
    
    bool _enqueue(const uint8_t* data, size_t length, LoRaPriority priority);
    bool _dequeue(LoRaPacket* packet);
    int  _getNextQueueIndex();
};

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* loraStatusToString(LoRaStatus status);
const char* loraModuleTypeToString(LoRaModuleType type);
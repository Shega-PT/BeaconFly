#pragma once

/**
 * =================================================================================
 * TELEMETRY.H — DATA LOGGER INTERNO (CAIXA NEGRA) — ESP1
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-19
 * VERSÃO:     2.0.0 (Fusão VA + VB com melhorias)
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * Este módulo é responsável por registar localmente TODOS os dados relevantes
 * de voo para análise posterior, debug e investigação de incidentes.
 * 
 * É um DATA LOGGER PURO — não transmite nada via UART, LoRa ou qualquer
 * outro canal. Apenas guarda informação em storage local (caixa negra).
 * 
 * =================================================================================
 * PRINCÍPIOS DE DESIGN
 * =================================================================================
 * 
 *   1. TEMPO REAL: Cada linha de dados é acompanhada do timestamp EXATO
 *      em microssegundos (us) desde o boot. Isto permite reconstruir
 *      a sequência temporal exata de eventos.
 * 
 *   2. NÃO-BLOQUEANTE: Buffer circular em RAM evita que o logging
 *      atrase o loop de controlo de voo (100Hz).
 * 
 *   3. REDUNDÂNCIA: Múltiplos backends (SD → SPIFFS → RAM) garantem
 *      que os dados são preservados sempre que possível.
 * 
 *   4. SESSÕES: Cada boot gera um novo par de ficheiros, facilitando
 *      a análise pós-voo.
 * 
 *   5. FICHEIRO ABERTO: Durante a sessão, os ficheiros permanecem
 *      abertos para evitar overhead de abrir/fechar a cada linha.
 * 
 * =================================================================================
 * FORMATO DOS DADOS
 * =================================================================================
 * 
 * Cada linha de dados contém:
 *   • timestamp_us: microssegundos desde o boot (precisão máxima)
 *   • dados de voo: roll, pitch, yaw, altitude, etc.
 *   • setpoints: valores desejados
 *   • erros PID: diferença entre setpoint e estado
 *   • PWM: sinais enviados aos atuadores
 *   • sistema: modo de voo, tensão da bateria, corrente
 * 
 * Exemplo de linha:
 *   12345678,2.34,-1.23,45.67,12.34,0.00,5.00,90.00,50.00,...
 * 
 * =================================================================================
 * EVENTOS CRÍTICOS
 * =================================================================================
 * 
 * Eventos são escritos IMEDIATAMENTE (sem buffer) para garantir que
 * não se perdem em caso de falha catastrófica:
 * 
 *   • BOOT           — Arranque do sistema
 *   • ARM            — Motores armados
 *   • DISARM         — Motores desarmados
 *   • MODE_CHANGE    — Mudança de modo de voo
 *   • FAILSAFE_ON    — Ativação do modo de segurança
 *   • FAILSAFE_OFF   — Desativação do failsafe
 *   • BATT_LOW       — Bateria baixa (alerta)
 *   • BATT_CRITICAL  — Bateria crítica (emergência)
 *   • SENSOR_FAIL    — Falha de sensor
 *   • SECURITY       — Violação de segurança
 *   • CUSTOM         — Evento definido pelo utilizador
 * 
 * =================================================================================
 */

#include <Arduino.h>
#include <stdint.h>
#include "FlightControl.h"
#include "Actuators.h"

/* =================================================================================
 * CONFIGURAÇÃO DO LOGGER
 * =================================================================================
 */

/**
 * TELEMETRY_SD_CS_PIN — Pino Chip Select do cartão SD
 * 
 * Ajustar conforme a PCB. Valor padrão: 15
 */
#define TELEMETRY_SD_CS_PIN          15

/**
 * TELEMETRY_BUFFER_SIZE — Número de entradas no buffer circular RAM
 * 
 * 200 entradas × ~80 bytes = ~16 KB de RAM
 * Suficiente para 20 segundos de voo a 10Hz
 */
#define TELEMETRY_BUFFER_SIZE        200

/**
 * TELEMETRY_SAMPLE_INTERVAL_MS — Taxa de amostragem (milissegundos)
 * 
 * 100ms = 10Hz. Taxa ideal para análise de voo
 */
#define TELEMETRY_SAMPLE_INTERVAL_MS 100

/**
 * TELEMETRY_FLUSH_INTERVAL_MS — Intervalo entre flushes (milissegundos)
 * 
 * 2 segundos = escreve buffer no storage a cada 2s
 * Equilíbrio entre segurança (pouca perda de dados) e performance
 */
#define TELEMETRY_FLUSH_INTERVAL_MS  2000

/**
 * TELEMETRY_LINE_MAX — Tamanho máximo de uma linha CSV
 * 
 * ~200 bytes é suficiente para todos os campos
 */
#define TELEMETRY_LINE_MAX           256

/**
 * TELEMETRY_FILENAME_MAX — Tamanho máximo do nome do ficheiro
 */
#define TELEMETRY_FILENAME_MAX       32

/* =================================================================================
 * NOMES BASE DOS FICHEIROS
 * =================================================================================
 */

#define TELEMETRY_FLIGHT_FILE_BASE   "/flight"
#define TELEMETRY_EVENT_FILE_BASE    "/events"
#define TELEMETRY_SESSION_FILE       "/session.bin"

/* =================================================================================
 * BACKENDS DE ARMAZENAMENTO
 * =================================================================================
 */

enum StorageBackend : uint8_t {
    STORAGE_NONE    = 0,   /* Apenas buffer RAM (dados perdem-se no reset) */
    STORAGE_SPIFFS  = 1,   /* Flash interna (SPIFFS/LittleFS) */
    STORAGE_SD      = 2    /* Cartão SD (preferido) */
};

/* =================================================================================
 * TIPOS DE EVENTOS
 * =================================================================================
 */

enum TelemetryEvent : uint8_t {
    EVT_BOOT            = 0,
    EVT_ARM             = 1,
    EVT_DISARM          = 2,
    EVT_MODE_CHANGE     = 3,
    EVT_FAILSAFE_ON     = 4,
    EVT_FAILSAFE_OFF    = 5,
    EVT_BATT_LOW        = 6,
    EVT_BATT_CRITICAL   = 7,
    EVT_SENSOR_FAIL     = 8,
    EVT_SECURITY        = 9,
    EVT_CUSTOM          = 10
};

/* =================================================================================
 * ESTRUTURA DE ENTRADA PERIÓDICA (DADOS DE VOO)
 * =================================================================================
 * 
 * Cada entrada é armazenada no buffer circular em RAM antes de ser
 * escrita no storage. O timestamp é em microssegundos para máxima precisão.
 */

struct TelemetryEntry {
    /* Timestamp — PRECISÃO MÁXIMA */
    uint64_t timestampUs;           /* microssegundos desde o boot (precisão) */
    
    /* FlightControl — ESTADO ATUAL */
    float roll;                     /* Ângulo de rolamento (graus) */
    float pitch;                    /* Ângulo de inclinação (graus) */
    float yaw;                      /* Ângulo de guinada (graus) */
    float altFused;                 /* Altitude fundida (metros) */
    
    /* SETPOINTS (valores desejados) */
    float targetRoll;               /* Roll desejado (graus) */
    float targetPitch;              /* Pitch desejado (graus) */
    float targetYaw;                /* Yaw desejado (graus) */
    float targetAlt;                /* Altitude desejada (metros) */
    
    /* ERROS PID (setpoint - estado) */
    float errorRoll;                /* Erro de roll (graus) */
    float errorPitch;               /* Erro de pitch (graus) */
    float errorYaw;                 /* Erro de yaw (graus) */
    float errorAlt;                 /* Erro de altitude (metros) */
    
    /* ATUADORES — SINAIS PWM ENVIADOS (microssegundos) */
    uint16_t pwmWingR;              /* Asa direita (µs) */
    uint16_t pwmWingL;              /* Asa esquerda (µs) */
    uint16_t pwmRudder;             /* Leme (µs) */
    uint16_t pwmElevonR;            /* Elevon direito (µs) */
    uint16_t pwmElevonL;            /* Elevon esquerdo (µs) */
    uint16_t pwmMotor;              /* Motor (µs) */
    
    /* SISTEMA */
    uint8_t  flightMode;            /* Modo de voo (FlightMode) */
    float    battV;                 /* Tensão da bateria (volts) */
    float    battA;                 /* Corrente da bateria (amperes) */
    
    /* DIAGNÓSTICO — TEMPO DE LOOP */
    uint16_t loopTimeUs;            /* Tempo do último ciclo de controlo (µs) */
};

/* =================================================================================
 * ESTRUTURA DE EVENTO CRÍTICO
 * =================================================================================
 * 
 * Eventos são escritos IMEDIATAMENTE (sem buffer) para garantir que não se perdem.
 * O timestamp é em microssegundos para precisão absoluta.
 */

struct TelemetryEventEntry {
    uint64_t       timestampUs;    /* microssegundos desde o boot */
    TelemetryEvent type;           /* Tipo de evento (EVT_*) */
    char           message[64];    /* Mensagem descritiva (opcional) */
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
     * Inicializa o logger, deteta storage disponível e abre ficheiros
     * 
     * Deve ser chamada uma vez no setup().
     * Tenta inicializar SD Card → SPIFFS → RAM (fallback)
     */
    void begin();
    
    /**
     * Fecha os ficheiros de forma segura
     * 
     * Deve ser chamada antes de desligar ou reiniciar o sistema.
     * Garante que todo o buffer é escrito e os ficheiros são fechados.
     */
    void close();

    /* =========================================================================
     * CICLO PRINCIPAL (CHAMAR NO LOOP)
     * ========================================================================= */
    
    /**
     * Atualiza o logger — deve ser chamado no loop principal
     * 
     * Gerencia:
     *   • Amostragem periódica (10Hz)
     *   • Flush automático do buffer (a cada 2s)
     * 
     * @param fc  Referência para o FlightControl (dados de voo)
     * @param act Referência para os Actuators (sinais PWM)
     */
    void update(const FlightControl& fc, const Actuators& act);

    /* =========================================================================
     * LOGGING PERIÓDICO (DADOS DE VOO)
     * ========================================================================= */
    
    /**
     * Regista uma amostra de dados de voo no buffer circular
     * 
     * @param fc  Referência para o FlightControl
     * @param act Referência para os Actuators
     */
    void log(const FlightControl& fc, const Actuators& act);
    
    /**
     * Força a escrita de todo o buffer para o storage
     */
    void flush();

    /* =========================================================================
     * EVENTOS CRÍTICOS (ESCRITA IMEDIATA)
     * ========================================================================= */
    
    /**
     * Regista um evento crítico com escrita imediata
     * 
     * @param type    Tipo de evento (EVT_*)
     * @param message Mensagem descritiva (opcional, pode ser nullptr)
     */
    void logEvent(TelemetryEvent type, const char* message = nullptr);

    /* =========================================================================
     * CONTROLO
     * ========================================================================= */
    
    /**
     * Ativa/desativa o logging (útil para desativar em voo se necessário)
     * 
     * @param enable true = ativar, false = desativar
     */
    void setEnabled(bool enable);
    
    /**
     * Verifica se o logging está ativo
     */
    bool isEnabled() const;

    /* =========================================================================
     * GETTERS (DIAGNÓSTICO)
     * ========================================================================= */
    
    /**
     * Retorna o backend de armazenamento ativo
     */
    StorageBackend getBackend() const;
    
    /**
     * Retorna o número da sessão atual
     */
    uint16_t getSessionNumber() const;
    
    /**
     * Retorna o número total de entradas escritas no storage
     */
    uint32_t getTotalLogged() const;
    
    /**
     * Retorna o número de entradas descartadas (buffer cheio)
     */
    uint32_t getTotalDropped() const;

private:
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    StorageBackend _backend;            /* Backend ativo (SD/SPIFFS/NONE) */
    uint16_t       _sessionNumber;      /* Número da sessão (incrementa a cada boot) */
    bool           _enabled;            /* Logging ativo? */
    
    /* =========================================================================
     * FICHEIROS (MANTIDOS ABERTOS DURANTE A SESSÃO)
     * ========================================================================= */
    
    File           _flightFile;         /* Ficheiro de dados de voo (aberto) */
    File           _eventFile;          /* Ficheiro de eventos (aberto) */
    char           _flightFilename[TELEMETRY_FILENAME_MAX];
    char           _eventFilename[TELEMETRY_FILENAME_MAX];
    bool           _filesOpen;          /* Ficheiros estão abertos? */
    
    /* =========================================================================
     * BUFFER CIRCULAR EM RAM
     * ========================================================================= */
    
    TelemetryEntry _buffer[TELEMETRY_BUFFER_SIZE];
    uint16_t       _head;               /* Índice de escrita */
    uint16_t       _count;              /* Número de entradas no buffer */
    
    /* =========================================================================
     * ESTATÍSTICAS
     * ========================================================================= */
    
    uint32_t       _totalLogged;        /* Total escrito no storage */
    uint32_t       _totalDropped;       /* Total descartado (buffer cheio) */
    
    /* =========================================================================
     * TIMERS PARA AMOSTRAGEM E FLUSH
     * ========================================================================= */
    
    uint32_t       _lastSampleMs;       /* Última amostragem (ms) */
    uint32_t       _lastFlushMs;        /* Último flush (ms) */
    uint32_t       _lastLoopTimeUs;     /* Tempo do último ciclo (para diagnóstico) */
    
    /* =========================================================================
     * FUNÇÕES PRIVADAS — INICIALIZAÇÃO DE BACKENDS
     * ========================================================================= */
    
    /**
     * Inicializa o cartão SD
     * 
     * @return true se o SD foi inicializado com sucesso
     */
    bool _initSD();
    
    /**
     * Inicializa o SPIFFS (flash interna)
     * 
     * @return true se o SPIFFS foi inicializado com sucesso
     */
    bool _initSPIFFS();
    
    /* =========================================================================
     * FUNÇÕES PRIVADAS — GESTÃO DE SESSÃO
     * ========================================================================= */
    
    /**
     * Lê o número da última sessão do storage
     * 
     * @return Número da última sessão (0 se não existir)
     */
    uint16_t _readSessionNumber();
    
    /**
     * Escreve o número da sessão atual no storage
     * 
     * @param n Número da sessão a guardar
     */
    void _writeSessionNumber(uint16_t n);
    
    /* =========================================================================
     * FUNÇÕES PRIVADAS — GESTÃO DE FICHEIROS
     * ========================================================================= */
    
    /**
     * Abre os ficheiros de logging (mantém abertos durante a sessão)
     * 
     * @return true se ambos os ficheiros foram abertos com sucesso
     */
    bool _openFiles();
    
    /**
     * Fecha os ficheiros de logging
     */
    void _closeFiles();
    
    /**
     * Garante que os ficheiros estão abertos (reabre se necessário)
     * 
     * @return true se os ficheiros estão abertos
     */
    bool _ensureFilesOpen();
    
    /* =========================================================================
     * FUNÇÕES PRIVADAS — ESCRITA DE DADOS
     * ========================================================================= */
    
    /**
     * Escreve o cabeçalho CSV nos ficheiros de voo e eventos
     */
    void _writeCSVHeader();
    
    /**
     * Converte uma entrada de telemetria para linha CSV
     * 
     * @param e   Entrada a converter
     * @param out Buffer de saída (mínimo TELEMETRY_LINE_MAX bytes)
     */
    void _entryToCSV(const TelemetryEntry& e, char* out);
    
    /**
     * Escreve uma linha no ficheiro de voo (bufferizado)
     * 
     * @param e Entrada a escrever
     */
    void _writeFlightLine(const TelemetryEntry& e);
    
    /**
     * Escreve uma linha no ficheiro de eventos (imediato)
     * 
     * @param type    Tipo de evento
     * @param message Mensagem (opcional)
     */
    void _writeEventLine(TelemetryEvent type, const char* message);
    
    /**
     * Escreve uma linha genérica num ficheiro
     * 
     * @param file Ficheiro já aberto
     * @param line Linha a escrever
     */
    void _writeLine(File& file, const char* line);
};

/* =========================================================================
 * FUNÇÕES AUXILIARES
 * ========================================================================= */

const char* storageBackendToString(StorageBackend backend);
const char* telemetryEventToString(TelemetryEvent event);
#pragma once

/**
 * =================================================================================
 * SHELL.H — CONSOLA DE DIAGNÓSTICO E COMANDO (ESP1 e ESP2)
 * =================================================================================
 * 
 * AUTOR:      ShegaPT
 * DATA:       2026-04-19
 * VERSÃO:     2.0.0 (ARQUITETURA CORRETA — ESP1 RX only, ESP2 TX only)
 * 
 * =================================================================================
 *              ARQUITETURA DE COMUNICAÇÃO — VERSÃO FINAL CORRETA
 * =================================================================================
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                         GROUND STATION                                      │
 *   │                                                                             │
 *   │  ┌─────────────────────────────────────────────────────────────────────┐    │
 *   │  │                    Software Ground Station                          │    │
 *   │  │              (Interface Web/Python + Joystick)                      │    │
 *   │  └─────────────────────────────────────────────────────────────────────┘    │
 *   │         │                                         ▲                         │
 *   │         │ (Comandos)                              │ (Respostas)             │
 *   │         ▼                                         │                         │
 *   │  ┌─────────────┐                           ┌─────────────┐                  │
 *   │  │  LoRa 2.4GHz│                           │  LoRa 868MHz│                  │
 *   │  │  (TX APENAS)│                           │  (RX APENAS)│                  │
 *   │  └──────┬──────┘                           └──────▲──────┘                  │
 *   └─────────┼─────────────────────────────────────────┼─────────────────────────┘
 *             │                                         │
 *             │ (Comandos)                              │ (Respostas)
 *             ▼                                         │
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP1                                           │
 *   │                                                                             │
 *   │  ┌───────────────────────────────────────────────────────────────────────┐  │
 *   │  │              LoRa 2.4GHz (RX APENAS — NUNCA TRANSMITE)                │  │
 *   │  │              • Recebe comandos da GS                                  │  │
 *   │  │              • NUNCA envia nada via LoRa                              │  │
 *   │  └───────────────────────────────────────────────────────────────────────┘  │
 *   │                                    │                                        │
 *   │                                    ▼                                        │
 *   │  ┌───────────────────────────────────────────────────────────────────────┐  │
 *   │  │                         SHELL (ESP1)                                  │  │
 *   │  │                                                                       │  │
 *   │  │  • Processa comandos DESTINADOS A ESP1                                │  │
 *   │  │  • Encaminha comandos para ESP2 via UART                              │  │
 *   │  │  • NUNCA envia respostas diretamente (não tem TX)                     │  │
 *   │  │  • Respostas vão para ESP2 via UART para serem transmitidas           │  │
 *   │  └───────────────────────────────────────────────────────────────────────┘  │
 *   │                                    │                                        │
 *   │                                    │ (UART — Comandos + Respostas)          │
 *   │                                    ▼                                        │
 *   └────────────────────────────────────┼────────────────────────────────────────┘
 *                                        │
 *                                        ▼
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP2                                           │
 *   │                                                                             │
 *   │  ┌───────────────────────────────────────────────────────────────────────┐  │
 *   │  │                         SHELL (ESP2)                                  │  │
 *   │  │                                                                       │  │
 *   │  │  • Processa comandos DESTINADOS A ESP2                                │  │
 *   │  │  • Gera respostas para TODOS os comandos (ESP1 ou ESP2)               │  │
 *   │  │  • Único que transmite via LoRa                                       │  │
 *   │  └───────────────────────────────────────────────────────────────────────┘  │
 *   │                                    │                                        │
 *   │                                    ▼                                        │
 *   │  ┌───────────────────────────────────────────────────────────────────────┐  │
 *   │  │              LoRa 868MHz (TX APENAS — NUNCA RECEBE)                   │  │
 *   │  │              • Envia respostas para GS                                │  │
 *   │  │              • NUNCA recebe nada via LoRa                             │  │
 *   │  └───────────────────────────────────────────────────────────────────────┘  │
 *   │                                    │                                        │
 *   │                                    ▼ (Respostas via 868MHz)                 │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 *                          FLUXOS COMPLETOS (CORRETOS)
 * =================================================================================
 * 
 * 📡 COMANDO PARA ESP1 (ex: get_roll):
 * 
 *   GS → (2.4GHz TX) → ESP1 (RX) → Shell ESP1 → processa comando
 *                                              │
 *                                              ▼
 *                              Resposta via UART → ESP2
 *                                              │
 *                                              ▼
 *                              ESP2 → (868MHz TX) → GS (RX)
 * 
 * 
 * 📡 COMANDO PARA ESP2 (ex: [ESP2] get_temp):
 * 
 *   GS → (2.4GHz TX) → ESP1 (RX) → Shell ESP1 → identifica [ESP2]
 *                                              │
 *                                              ▼
 *                              Encaminha via UART → ESP2
 *                                              │
 *                                              ▼
 *                              Shell ESP2 processa comando
 *                                              │
 *                                              ▼
 *                              Resposta via (868MHz TX) → GS (RX)
 * 
 * 
 * 📡 COMANDO VIA USB (DEBUG LOCAL):
 * 
 *   PC → USB → ESP1 → Shell ESP1 → processa ou encaminha
 *                              │
 *                              ▼
 *              Resposta via USB → PC (direto, sem LoRa)
 * 
 * =================================================================================
 *                      REGRAS DE OURO (NUNCA ESQUECER)
 * =================================================================================
 * 
 *   1. ESP1 NUNCA transmite via LoRa (não tem hardware de TX 868MHz)
 *   2. ESP2 NUNCA recebe via LoRa (não tem hardware de RX 2.4GHz)
 *   3. TODAS as respostas para a GS passam pelo ESP2 (único com TX 868MHz)
 *   4. ESP1 e ESP2 comunicam entre si via UART (bidirecional)
 *   5. USB é apenas para debug local (PC ligado diretamente ao ESP1)
 * 
 * =================================================================================
 *                    FORMATO DO COMANDO (COM DESTINATÁRIO)
 * =================================================================================
 * 
 *   [ESP1] comando argumentos    → Executado no ESP1 (resposta via ESP2)
 *   [ESP2] comando argumentos    → Encaminhado para ESP2 via UART
 *   comando argumentos           → Padrão: ESP1 (assumido)
 * 
 * Exemplos:
 *   [ESP1] get_roll               → Lê roll do ESP1 (resposta via ESP2 → GS)
 *   [ESP2] get_temp               → Lê temperatura do ESP2 (encaminhado)
 *   status                        → Executado no ESP1 (padrão)
 * 
 * =================================================================================
 */

#include <Arduino.h>
#include <stdint.h>
#include <stdarg.h>
#include "Protocol.h"
#include "FlightControl.h"
#include "Security.h"
#include "Telemetry.h"

/* =================================================================================
 * CONSTANTES DE CONFIGURAÇÃO
 * =================================================================================
 */

#define SHELL_MAX_COMMANDS          48
#define SHELL_MAX_ARGS              8
#define SHELL_MAX_LINE_LENGTH       256
#define SHELL_MAX_RESPONSE_LENGTH   1024
#define SHELL_LOGBOOK_SIZE          100
#define SHELL_MAX_MESSAGE_LENGTH    256
#define SHELL_UART_BUFFER_SIZE      2048

/* =================================================================================
 * IDENTIFICADORES DE DESTINATÁRIO
 * =================================================================================
 */

enum ShellTarget : uint8_t {
    SHELL_TARGET_ESP1 = 0,      /* Comando para o ESP1 (local) */
    SHELL_TARGET_ESP2 = 1,      /* Comando para o ESP2 (via UART) */
    SHELL_TARGET_AUTO = 2       /* Auto-detetar pelo prefixo */
};

/* =================================================================================
 * NÍVEIS DE LOG
 * =================================================================================
 */

#define LOG_LEVEL_ERROR     0
#define LOG_LEVEL_WARNING   1
#define LOG_LEVEL_INFO      2
#define LOG_LEVEL_DEBUG     3

/* =================================================================================
 * CANAIS DE ORIGEM (PARA ROTEAMENTO DE RESPOSTA)
 * =================================================================================
 */

enum ShellOrigin : uint8_t {
    SHELL_ORIGIN_USB = 0,       /* Comando veio da USB (PC local) */
    SHELL_ORIGIN_LORA = 1,      /* Comando veio da LoRa 2.4GHz (GS) */
    SHELL_ORIGIN_UART = 2,      /* Comando veio da UART (do outro ESP) */
    SHELL_ORIGIN_INTERNAL = 3   /* Comando interno (testes) */
};

/* =================================================================================
 * IDs DE COMANDOS SHELL (PARA COMUNICAÇÃO VIA TLV)
 * =================================================================================
 */

enum ShellCommandID : uint8_t {
    /* Sistema e ajuda */
    CMD_SHELL_HELP          = 0x01,
    CMD_SHELL_STATUS        = 0x02,
    CMD_SHELL_LOGBOOK       = 0x03,
    CMD_SHELL_CLEAR         = 0x04,
    CMD_SHELL_SAVE          = 0x05,
    CMD_SHELL_RESET_CONFIG  = 0x06,

    /* Leitura de sensores e estado */
    CMD_SHELL_GET_ROLL      = 0x10,
    CMD_SHELL_GET_PITCH     = 0x11,
    CMD_SHELL_GET_YAW       = 0x12,
    CMD_SHELL_GET_HEADING   = 0x13,
    CMD_SHELL_GET_ALTITUDE  = 0x14,
    CMD_SHELL_GET_BATTERY   = 0x15,
    CMD_SHELL_GET_OUTPUTS   = 0x16,
    CMD_SHELL_GET_MODE      = 0x17,
    CMD_SHELL_GET_SECURITY  = 0x18,
    CMD_SHELL_GET_PID       = 0x19,
    CMD_SHELL_GET_TRIM      = 0x1A,
    CMD_SHELL_GET_TEMPERATURE = 0x1B,

    /* Controlo de voo (sempre permitidos em voo para emergência) */
    CMD_SHELL_SET_ROLL      = 0x20,
    CMD_SHELL_SET_PITCH     = 0x21,
    CMD_SHELL_SET_YAW       = 0x22,
    CMD_SHELL_SET_ALTITUDE  = 0x23,
    CMD_SHELL_SET_THROTTLE  = 0x24,
    CMD_SHELL_SET_MODE      = 0x25,
    CMD_SHELL_SET_PID       = 0x26,
    CMD_SHELL_SET_TRIM      = 0x27,

    /* Comandos de sistema (bloqueados em voo por segurança) */
    CMD_SHELL_REBOOT        = 0x30,
    CMD_SHELL_ARM           = 0x31,
    CMD_SHELL_DISARM        = 0x32,
    CMD_SHELL_CALIBRATE     = 0x33,

    /* Debug e diagnóstico */
    CMD_SHELL_DEBUG_ON      = 0x40,
    CMD_SHELL_DEBUG_OFF     = 0x41,
    CMD_SHELL_ECHO_ON       = 0x42,
    CMD_SHELL_ECHO_OFF      = 0x43,
    CMD_SHELL_TEST          = 0x44,

    /* Telemetria e logging */
    CMD_SHELL_START_LOG     = 0x50,
    CMD_SHELL_STOP_LOG      = 0x51,
    CMD_SHELL_MARKER        = 0x52
};

/* =================================================================================
 * ESTRUTURA DE COMANDO
 * =================================================================================
 */

struct ShellCommand {
    ShellCommandID id;
    const char*    name;
    const char*    description;
    const char*    usage;
    uint8_t        minArgs;
    uint8_t        maxArgs;
    bool           blockedInFlight;     /* Apenas para comandos perigosos (reboot, calibrate) */
    void           (*handler)(int argc, char** argv);
};

/* =================================================================================
 * ESTRUTURA DE LOGBOOK
 * =================================================================================
 */

struct LogbookEntry {
    uint32_t timestamp;
    uint8_t  level;
    char     message[SHELL_MAX_MESSAGE_LENGTH];
};

/* =================================================================================
 * CLASSE SHELL
 * =================================================================================
 */

class Shell {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    Shell();
    
    /**
     * Inicializa o shell no ESP atual
     * 
     * @param fc      Ponteiro para FlightControl (nullptr se não aplicável)
     * @param sec     Ponteiro para Security (nullptr se não aplicável)
     * @param tel     Ponteiro para Telemetry (nullptr se não aplicável)
     * @param isESP2  true se este shell está a correr no ESP2
     * @return true se inicializado com sucesso
     */
    bool begin(FlightControl* fc, Security* sec, Telemetry* tel, bool isESP2 = false);
    
    /**
     * Define o estado atual de voo (afeta o travao de segurança)
     */
    void setFlightState(bool inFlight, bool failsafeActive = false);
    
    /**
     * Define o callback para envio via LoRa 868MHz (APENAS NO ESP2)
     */
    void setLoRaCallback(void (*callback)(const uint8_t*, size_t));

    /* =========================================================================
     * PROCESSAMENTO DE COMANDOS
     * ========================================================================= */
    
    /**
     * Processa uma string de comando (origem USB ou LoRa)
     * 
     * @param line   Linha de comando terminada em '\0'
     * @param origin Origem do comando (USB, LoRa, UART)
     * @return true se o comando foi processado (local ou encaminhado)
     */
    bool processCommand(const char* line, ShellOrigin origin);
    
    /**
     * Processa um comando recebido via UART (do outro ESP)
     * 
     * @param data Buffer com o comando
     * @param len  Tamanho do buffer
     */
    void processUARTCommand(const uint8_t* data, size_t len);
    
    /**
     * Processa um comando recebido via TLV (LoRa 2.4GHz)
     */
    void processShellCommand(const TLVMessage& msg, ShellOrigin origin);

    /* =========================================================================
     * LOGGING
     * ========================================================================= */
    
    void log(const char* format, ...);
    void logToBook(uint8_t level, const char* format, ...);
    void getLogbook(char* buffer, size_t size) const;
    void clearLogbook();

    /* =========================================================================
     * ENVIO DE RESPOSTAS
     * ========================================================================= */
    
    /**
     * Envia uma resposta para o canal apropriado
     * 
     * REGRAS:
     *   - Se ESP2: resposta vai para LoRa 868MHz (se origem for LoRa) ou USB
     *   - Se ESP1: resposta vai para ESP2 via UART (que depois transmite)
     */
    void sendResponse(const char* format, ...);
    
    /**
     * Envia uma resposta apenas para USB (debug local)
     */
    void sendDebug(const char* format, ...);

    /* =========================================================================
     * DIAGNÓSTICO
     * ========================================================================= */
    
    void getStatusString(char* buffer, size_t bufferSize) const;
    bool isInitialized() const;
    bool isESP2() const { return _isESP2; }
    
    void setDebug(bool enable);
    void setEcho(bool enable);

    /* =========================================================================
     * PROCESSAMENTO DE UART (PARA SER CHAMADO NO LOOP)
     * ========================================================================= */
    
    /**
     * Verifica se há dados na UART vindos do outro ESP
     * Deve ser chamado no loop() de ambos os ESPs
     */
    void updateUART();

private:
    /* =========================================================================
     * REGISTO DE COMANDOS
     * ========================================================================= */
    
    void registerCommand(const ShellCommand& cmd);
    bool isCommandAllowed(const ShellCommand& cmd) const;
    int  parseLine(char* line, char* argv[]);
    
    /* =========================================================================
     * IDENTIFICAÇÃO DE DESTINATÁRIO
     * ========================================================================= */
    
    /**
     * Extrai o destinatário da linha de comando
     * 
     * @param line Linha de comando (pode ser modificada)
     * @return SHELL_TARGET_ESP1, SHELL_TARGET_ESP2
     */
    ShellTarget extractTarget(char* line);
    
    /* =========================================================================
     * ENCAMINHAMENTO VIA UART
     * ========================================================================= */
    
    /**
     * Encaminha um comando para o ESP2 via UART
     * 
     * @param command Comando sem o prefixo [ESP2]
     * @param origin  Origem original (USB ou LoRa)
     */
    void forwardToESP2(const char* command, ShellOrigin origin);
    
    /**
     * Encaminha uma resposta do ESP1 para o ESP2 (que vai transmitir)
     * 
     * @param response Resposta a ser transmitida
     * @param origin   Origem original (USB ou LoRa)
     */
    void forwardResponseToESP2(const char* response, ShellOrigin origin);
    
    /* =========================================================================
     * ENVIO DE RESPOSTAS (INTERNO)
     * ========================================================================= */
    
    void sendToUSB(const char* msg);
    void sendToLoRa(const char* msg);      /* APENAS NO ESP2 */
    void sendToUART(const char* msg);
    void setCurrentOrigin(ShellOrigin origin) { _currentOrigin = origin; }

    /* =========================================================================
     * HANDLERS DE COMANDOS (DECLARAÇÕES)
     * ========================================================================= */
    
    /* Sistema */
    static void cmdHelp(int argc, char** argv);
    static void cmdStatus(int argc, char** argv);
    static void cmdLogbook(int argc, char** argv);
    static void cmdClear(int argc, char** argv);
    static void cmdSave(int argc, char** argv);
    static void cmdResetConfig(int argc, char** argv);
    
    /* Leitura */
    static void cmdGetRoll(int argc, char** argv);
    static void cmdGetPitch(int argc, char** argv);
    static void cmdGetYaw(int argc, char** argv);
    static void cmdGetHeading(int argc, char** argv);
    static void cmdGetAltitude(int argc, char** argv);
    static void cmdGetBattery(int argc, char** argv);
    static void cmdGetOutputs(int argc, char** argv);
    static void cmdGetMode(int argc, char** argv);
    static void cmdGetSecurity(int argc, char** argv);
    static void cmdGetPID(int argc, char** argv);
    static void cmdGetTrim(int argc, char** argv);
    static void cmdGetTemperature(int argc, char** argv);
    
    /* Controlo (sempre permitidos em voo para emergência) */
    static void cmdSetRoll(int argc, char** argv);
    static void cmdSetPitch(int argc, char** argv);
    static void cmdSetYaw(int argc, char** argv);
    static void cmdSetAltitude(int argc, char** argv);
    static void cmdSetThrottle(int argc, char** argv);
    static void cmdSetMode(int argc, char** argv);
    static void cmdSetPID(int argc, char** argv);
    static void cmdSetTrim(int argc, char** argv);
    
    /* Sistema (bloqueados em voo) */
    static void cmdReboot(int argc, char** argv);
    static void cmdArm(int argc, char** argv);
    static void cmdDisarm(int argc, char** argv);
    static void cmdCalibrate(int argc, char** argv);
    
    /* Debug */
    static void cmdDebugOn(int argc, char** argv);
    static void cmdDebugOff(int argc, char** argv);
    static void cmdEchoOn(int argc, char** argv);
    static void cmdEchoOff(int argc, char** argv);
    static void cmdTest(int argc, char** argv);
    
    /* Telemetria */
    static void cmdStartLog(int argc, char** argv);
    static void cmdStopLog(int argc, char** argv);
    static void cmdMarker(int argc, char** argv);

    /* =========================================================================
     * PONTEIROS PARA SUBSISTEMAS
     * ========================================================================= */
    
    FlightControl* _flightControl;
    Security*      _security;
    Telemetry*     _telemetry;
    
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    ShellCommand   _commands[SHELL_MAX_COMMANDS];
    uint8_t        _commandCount;
    
    LogbookEntry   _logbook[SHELL_LOGBOOK_SIZE];
    uint8_t        _logbookHead;
    uint8_t        _logbookCount;
    
    bool           _initialized;
    bool           _isESP2;              /* true se este shell está no ESP2 */
    bool           _inFlight;
    bool           _failsafeActive;
    bool           _echoEnabled;
    bool           _debugEnabled;
    
    ShellOrigin    _currentOrigin;       /* Origem do comando atual (para resposta) */
    
    char           _responseBuffer[SHELL_MAX_RESPONSE_LENGTH];
    char           _uartBuffer[SHELL_UART_BUFFER_SIZE];
    size_t         _uartBufferLen;
    
    /* Callbacks */
    void           (*_loRaCallback)(const uint8_t*, size_t);  /* APENAS no ESP2 */
    
    /* Instância global para handlers estáticos */
    static Shell*  _instance;
};

/* =========================================================================
 * INSTÂNCIA GLOBAL
 * ========================================================================= */

extern Shell shell;

/* =========================================================================
 * FUNÇÕES AUXILIARES
 * ========================================================================= */

const char* logLevelToString(uint8_t level);
const char* shellCommandIDToString(ShellCommandID id);
const char* shellOriginToString(ShellOrigin origin);
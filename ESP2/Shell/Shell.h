/**
 * =================================================================================
 * SHELL.H — CONSOLA DE DIAGNÓSTICO E COMANDO (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-28
 * VERSÃO:     2.0.0
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * O módulo Shell (ESP2) é a consola de comando remota do sistema BeaconFly.
 * 
 * DIFERENÇAS CRÍTICAS PARA O ESP1:
 *   • ESP2 NUNCA recebe comandos diretamente da GS (tudo vem via ESP1)
 *   • ESP2 é o RESPONSÁVEL por enviar as respostas de shell para a GS
 *   • Respostas do ESP1 chegam via UART e são reenviadas para a GS
 *   • Respostas do ESP2 são enviadas diretamente para a GS
 *   • ESP2 tem acesso a MAIS dados (SI, GPS, etc.) que o ESP1
 * 
 * =================================================================================
 * FLUXO DE COMANDOS E RESPOSTAS
 * =================================================================================
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                         GROUND STATION                                      │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 *          │ (Comando shell via LoRa 2.4GHz)      ▲ (Resposta via LoRa 868MHz)
 *          ▼                                      │
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP1                                           │
 *   │                                                                             │
 *   │   Shell ESP1:                                                              │
 *   │   • Se comando for para ESP1 → processa e envia resposta para ESP2 via UART│
 *   │   • Se comando for para ESP2 → encaminha para ESP2 via UART                │
 *   │                                                                             │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 *          │                                      ▲
 *          │ (Comando via UART)                   │ (Resposta via UART)
 *          ▼                                      │
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP2                                           │
 *   │                                                                             │
 *   │   Shell ESP2:                                                              │
 *   │   • Se comando veio da GS via ESP1 → identifica destinatário               │
 *   │   • Se for para ESP2 → processa localmente                                 │
 *   │   • TODAS as respostas são enviadas para a GS via LoRa 868MHz              │
 *   │   • Respostas do ESP1 são reencaminhadas para a GS                         │
 *   │                                                                             │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * COMANDOS DISPONÍVEIS (ESP2 tem MAIS dados que ESP1)
 * =================================================================================
 * 
 *   SISTEMA:
 *     help, status, logbook, clear
 * 
 *   LEITURA DE ATITUDE:
 *     get_roll, get_pitch, get_yaw, get_heading
 * 
 *   LEITURA DE ACELERAÇÃO (m/s²):
 *     get_accel_x, get_accel_y, get_accel_z
 * 
 *   LEITURA DE GIROSCÓPIO (°/s):
 *     get_gyro_x, get_gyro_y, get_gyro_z
 * 
 *   LEITURA DE ALTITUDE E PRESSÃO:
 *     get_alt, get_pressure
 * 
 *   LEITURA DE BATERIA:
 *     get_batt (V), get_current (A), get_power (W), get_charge (Ah)
 * 
 *   LEITURA DE TEMPERATURAS:
 *     get_temp_imu, get_temp_baro, get_tc0, get_tc1, get_tc2, get_tc3
 * 
 *   LEITURA DE GPS:
 *     get_gps_lat, get_gps_lon, get_gps_alt, get_gps_sats, get_gps_hdop, get_gps_speed
 * 
 *   LEITURA DE ESTADO:
 *     get_mode, get_security, get_failsafe
 * 
 *   DIAGNÓSTICO:
 *     get_loop_time, get_heap, get_uptime
 * 
 *   CONTROLO (permitidos em voo para emergência):
 *     set_roll, set_pitch, set_yaw, set_alt, set_throttle, set_mode
 * 
 *   SISTEMA (bloqueados em voo):
 *     reboot
 * 
 *   DEBUG:
 *     debug_on, debug_off, echo_on, echo_off, test
 * 
 * =================================================================================
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdarg.h>
#include "Protocol.h"
#include "FlightControl.h"
#include "Security.h"
#include "SIConverter.h"
#include "GPS.h"
#include "Failsafe.h"
#include "LoRa.h"

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

/* =================================================================================
 * LIMITES DE SEGURANÇA PARA COMANDOS
 * =================================================================================
 */

#define MAX_ROLL_DEGREES            45.0f
#define MAX_PITCH_DEGREES           35.0f
#define MAX_ALTITUDE_METERS         400.0f
#define MAX_THROTTLE_PERCENT        100.0f

/* =================================================================================
 * NÍVEIS DE LOG
 * =================================================================================
 */

#define LOG_LEVEL_ERROR     0
#define LOG_LEVEL_WARNING   1
#define LOG_LEVEL_INFO      2
#define LOG_LEVEL_DEBUG     3

/* =================================================================================
 * IDs DE COMANDOS SHELL (PARA COMUNICAÇÃO VIA TLV)
 * =================================================================================
 */

enum ShellCommandID : uint8_t {
    /* Sistema e ajuda (0x01-0x0F) */
    CMD_SHELL_HELP          = 0x01,
    CMD_SHELL_STATUS        = 0x02,
    CMD_SHELL_LOGBOOK       = 0x03,
    CMD_SHELL_CLEAR         = 0x04,
    
    /* Leitura de atitude (0x10-0x1F) */
    CMD_SHELL_GET_ROLL      = 0x10,
    CMD_SHELL_GET_PITCH     = 0x11,
    CMD_SHELL_GET_YAW       = 0x12,
    CMD_SHELL_GET_HEADING   = 0x13,
    
    /* Leitura de aceleração (0x20-0x2F) */
    CMD_SHELL_GET_ACCEL_X   = 0x20,
    CMD_SHELL_GET_ACCEL_Y   = 0x21,
    CMD_SHELL_GET_ACCEL_Z   = 0x22,
    
    /* Leitura de giroscópio (0x30-0x3F) */
    CMD_SHELL_GET_GYRO_X    = 0x30,
    CMD_SHELL_GET_GYRO_Y    = 0x31,
    CMD_SHELL_GET_GYRO_Z    = 0x32,
    
    /* Leitura de altitude e pressão (0x40-0x4F) */
    CMD_SHELL_GET_ALTITUDE  = 0x40,
    CMD_SHELL_GET_PRESSURE  = 0x41,
    
    /* Leitura de bateria (0x50-0x5F) */
    CMD_SHELL_GET_BATTERY   = 0x50,
    CMD_SHELL_GET_CURRENT   = 0x51,
    CMD_SHELL_GET_POWER     = 0x52,
    CMD_SHELL_GET_CHARGE    = 0x53,
    
    /* Leitura de temperaturas (0x60-0x6F) */
    CMD_SHELL_GET_TEMP_IMU  = 0x60,
    CMD_SHELL_GET_TEMP_BARO = 0x61,
    CMD_SHELL_GET_TC0       = 0x62,
    CMD_SHELL_GET_TC1       = 0x63,
    CMD_SHELL_GET_TC2       = 0x64,
    CMD_SHELL_GET_TC3       = 0x65,
    
    /* Leitura de GPS (0x70-0x7F) */
    CMD_SHELL_GET_GPS_LAT   = 0x70,
    CMD_SHELL_GET_GPS_LON   = 0x71,
    CMD_SHELL_GET_GPS_ALT   = 0x72,
    CMD_SHELL_GET_GPS_SATS  = 0x73,
    CMD_SHELL_GET_GPS_HDOP  = 0x74,
    CMD_SHELL_GET_GPS_SPEED = 0x75,
    
    /* Leitura de estado (0x80-0x8F) */
    CMD_SHELL_GET_MODE      = 0x80,
    CMD_SHELL_GET_SECURITY  = 0x81,
    CMD_SHELL_GET_FAILSAFE  = 0x82,
    
    /* Diagnóstico (0x90-0x9F) */
    CMD_SHELL_GET_LOOP_TIME = 0x90,
    CMD_SHELL_GET_HEAP      = 0x91,
    CMD_SHELL_GET_UPTIME    = 0x92,
    
    /* Controlo de voo (0xA0-0xAF) — permitidos em voo para emergência */
    CMD_SHELL_SET_ROLL      = 0xA0,
    CMD_SHELL_SET_PITCH     = 0xA1,
    CMD_SHELL_SET_YAW       = 0xA2,
    CMD_SHELL_SET_ALTITUDE  = 0xA3,
    CMD_SHELL_SET_THROTTLE  = 0xA4,
    CMD_SHELL_SET_MODE      = 0xA5,
    
    /* Comandos de sistema (0xB0-0xBF) — bloqueados em voo */
    CMD_SHELL_REBOOT        = 0xB0,
    
    /* Debug (0xC0-0xCF) */
    CMD_SHELL_DEBUG_ON      = 0xC0,
    CMD_SHELL_DEBUG_OFF     = 0xC1,
    CMD_SHELL_ECHO_ON       = 0xC2,
    CMD_SHELL_ECHO_OFF      = 0xC3,
    CMD_SHELL_TEST          = 0xC4
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
    bool           blockedInFlight;
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
     * Inicializa o shell no ESP2
     * 
     * @param si      Ponteiro para o SIConverter (dados SI)
     * @param gps     Ponteiro para o GPS
     * @param sec     Ponteiro para Security
     * @param fc      Ponteiro para FlightControl (opcional)
     * @param failsafe Ponteiro para Failsafe
     * @param lora    Ponteiro para LoRa (para enviar respostas)
     * @return true se inicializado com sucesso
     */
    bool begin(SIConverter* si, GPS* gps, Security* sec, 
               FlightControl* fc, Failsafe* failsafe, LoRa* lora);
    
    /**
     * Define o estado atual de voo (afeta bloqueio de comandos)
     * 
     * @param inFlight       true se a aeronave está em voo
     * @param failsafeActive true se failsafe está ativo
     */
    void setFlightState(bool inFlight, bool failsafeActive = false);
    
    /**
     * Define o callback para envio via UART para ESP1
     * 
     * @param callback Função que será chamada para enviar dados para ESP1
     */
    void setUARTCallback(void (*callback)(const uint8_t*, size_t));

    /* =========================================================================
     * PROCESSAMENTO DE COMANDOS
     * ========================================================================= */
    
    /**
     * Processa uma string de comando (origem: UART do ESP1)
     * 
     * @param line   Linha de comando terminada em '\0'
     */
    void processCommand(const char* line);
    
    /**
     * Processa um comando recebido via TLV (do ESP1)
     * 
     * @param msg Mensagem TLV do tipo MSG_SHELL_CMD
     */
    void processShellCommand(const TLVMessage& msg);
    
    /**
     * Processa uma resposta do ESP1 (para reencaminhar para GS)
     * 
     * @param response Resposta do ESP1
     */
    void processESP1Response(const char* response);

    /* =========================================================================
     * LOGGING
     * ========================================================================= */
    
    /**
     * Regista uma mensagem no logbook (nível INFO)
     * 
     * @param format Formato printf
     * @param ...    Argumentos
     */
    void log(const char* format, ...);
    
    /**
     * Regista uma mensagem no logbook com nível específico
     * 
     * @param level  LOG_LEVEL_ERROR/WARNING/INFO/DEBUG
     * @param format Formato printf
     * @param ...    Argumentos
     */
    void logToBook(uint8_t level, const char* format, ...);
    
    /**
     * Obtém todo o logbook como string
     * 
     * @param buffer Buffer de destino
     * @param size   Tamanho do buffer
     */
    void getLogbook(char* buffer, size_t size) const;
    
    /**
     * Limpa o logbook
     */
    void clearLogbook();

    /* =========================================================================
     * RESPOSTAS (ENVIO PARA GS VIA LoRa)
     * ========================================================================= */
    
    /**
     * Envia uma resposta para a Ground Station via LoRa 868MHz
     * 
     * @param format Formato printf
     * @param ...    Argumentos
     */
    void sendResponse(const char* format, ...);

    /* =========================================================================
     * DIAGNÓSTICO
     * ========================================================================= */
    
    /**
     * Obtém uma string com o estado atual do sistema
     * 
     * @param buffer     Buffer de destino
     * @param bufferSize Tamanho do buffer
     */
    void getStatusString(char* buffer, size_t bufferSize) const;
    
    /**
     * Verifica se o shell foi inicializado
     */
    bool isInitialized() const;
    
    /**
     * Ativa/desativa modo debug
     */
    void setDebug(bool enable);
    
    /**
     * Ativa/desativa eco de comandos
     */
    void setEcho(bool enable);

private:
    /* =========================================================================
     * REGISTO DE COMANDOS
     * ========================================================================= */
    
    void registerCommand(const ShellCommand& cmd);
    bool isCommandAllowed(const ShellCommand& cmd) const;
    int  parseLine(char* line, char* argv[]);
    
    /* =========================================================================
     * ENVIO DE RESPOSTAS (INTERNO)
     * ========================================================================= */
    
    void sendToUART(const char* msg);      /* Para ESP1 */
    void sendToLoRa(const char* msg);      /* Para GS via LoRa 868MHz */

    /* =========================================================================
     * HANDLERS DE COMANDOS (DECLARAÇÕES)
     * ========================================================================= */
    
    /* Sistema */
    static void cmdHelp(int argc, char** argv);
    static void cmdStatus(int argc, char** argv);
    static void cmdLogbook(int argc, char** argv);
    static void cmdClear(int argc, char** argv);
    
    /* Leitura de atitude */
    static void cmdGetRoll(int argc, char** argv);
    static void cmdGetPitch(int argc, char** argv);
    static void cmdGetYaw(int argc, char** argv);
    static void cmdGetHeading(int argc, char** argv);
    
    /* Leitura de aceleração */
    static void cmdGetAccelX(int argc, char** argv);
    static void cmdGetAccelY(int argc, char** argv);
    static void cmdGetAccelZ(int argc, char** argv);
    
    /* Leitura de giroscópio */
    static void cmdGetGyroX(int argc, char** argv);
    static void cmdGetGyroY(int argc, char** argv);
    static void cmdGetGyroZ(int argc, char** argv);
    
    /* Leitura de altitude e pressão */
    static void cmdGetAltitude(int argc, char** argv);
    static void cmdGetPressure(int argc, char** argv);
    
    /* Leitura de bateria */
    static void cmdGetBattery(int argc, char** argv);
    static void cmdGetCurrent(int argc, char** argv);
    static void cmdGetPower(int argc, char** argv);
    static void cmdGetCharge(int argc, char** argv);
    
    /* Leitura de temperaturas */
    static void cmdGetTempIMU(int argc, char** argv);
    static void cmdGetTempBaro(int argc, char** argv);
    static void cmdGetTC0(int argc, char** argv);
    static void cmdGetTC1(int argc, char** argv);
    static void cmdGetTC2(int argc, char** argv);
    static void cmdGetTC3(int argc, char** argv);
    
    /* Leitura de GPS */
    static void cmdGetGPSLat(int argc, char** argv);
    static void cmdGetGPSLon(int argc, char** argv);
    static void cmdGetGPSAlt(int argc, char** argv);
    static void cmdGetGPSSats(int argc, char** argv);
    static void cmdGetGPSHdop(int argc, char** argv);
    static void cmdGetGPSSpeed(int argc, char** argv);
    
    /* Leitura de estado */
    static void cmdGetMode(int argc, char** argv);
    static void cmdGetSecurity(int argc, char** argv);
    static void cmdGetFailsafe(int argc, char** argv);
    
    /* Diagnóstico */
    static void cmdGetLoopTime(int argc, char** argv);
    static void cmdGetHeap(int argc, char** argv);
    static void cmdGetUptime(int argc, char** argv);
    
    /* Controlo de voo (permitidos em voo para emergência) */
    static void cmdSetRoll(int argc, char** argv);
    static void cmdSetPitch(int argc, char** argv);
    static void cmdSetYaw(int argc, char** argv);
    static void cmdSetAltitude(int argc, char** argv);
    static void cmdSetThrottle(int argc, char** argv);
    static void cmdSetMode(int argc, char** argv);
    
    /* Sistema (bloqueados em voo) */
    static void cmdReboot(int argc, char** argv);
    
    /* Debug */
    static void cmdDebugOn(int argc, char** argv);
    static void cmdDebugOff(int argc, char** argv);
    static void cmdEchoOn(int argc, char** argv);
    static void cmdEchoOff(int argc, char** argv);
    static void cmdTest(int argc, char** argv);

    /* =========================================================================
     * PONTEIROS PARA SUBSISTEMAS
     * ========================================================================= */
    
    SIConverter*   _si;
    GPS*           _gps;
    Security*      _security;
    FlightControl* _flightControl;
    Failsafe*      _failsafe;
    LoRa*          _lora;
    
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    ShellCommand   _commands[SHELL_MAX_COMMANDS];
    uint8_t        _commandCount;
    
    LogbookEntry   _logbook[SHELL_LOGBOOK_SIZE];
    uint8_t        _logbookHead;
    uint8_t        _logbookCount;
    
    bool           _initialized;
    bool           _inFlight;
    bool           _failsafeActive;
    bool           _echoEnabled;
    bool           _debugEnabled;
    
    char           _responseBuffer[SHELL_MAX_RESPONSE_LENGTH];
    
    /* Callbacks */
    void           (*_uartCallback)(const uint8_t*, size_t);
    
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

/**
 * Converte nível de log para string
 * 
 * @param level Nível de log (0-3)
 * @return String descritiva ("ERRO", "WARN", "INFO", "DEBUG")
 */
const char* logLevelToString(uint8_t level);

/**
 * Converte ID de comando shell para string
 * 
 * @param id ID do comando (ShellCommandID)
 * @return String descritiva (ex: "HELP", "STATUS", etc.)
 */
const char* shellCommandIDToString(ShellCommandID id);

/**
 * Converte um valor de throttle de percentagem para float (0-1)
 * 
 * @param percent Valor em percentagem (0-100)
 * @return Valor normalizado (0.0-1.0)
 */
float throttlePercentToFloat(float percent);

/**
 * Converte um valor de throttle de float para percentagem (0-100)
 * 
 * @param value Valor normalizado (0.0-1.0)
 * @return Valor em percentagem (0-100)
 */
float throttleFloatToPercent(float value);

#endif /* SHELL_H */
/**
 * =================================================================================
 * SHELL.CPP — IMPLEMENTAÇÃO DA CONSOLA DE COMANDOS (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-28
 * VERSÃO:     2.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. ESP2 é o RESPONSÁVEL por enviar TODAS as respostas para a GS via LoRa 868MHz
 * 2. Comandos do ESP2 são processados localmente
 * 3. Respostas do ESP1 são reencaminhadas para a GS
 * 4. Logbook mantém histórico de comandos e eventos
 * 
 * =================================================================================
 */

#include "Shell.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <esp_system.h>

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define SHELL_DEBUG

#ifdef SHELL_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[Shell ESP2] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * INSTÂNCIA GLOBAL
 * =================================================================================
 */

Shell shell;
Shell* Shell::_instance = nullptr;

/* =================================================================================
 * FUNÇÕES AUXILIARES (IMPLEMENTAÇÃO)
 * =================================================================================
 */

const char* logLevelToString(uint8_t level) {
    switch (level) {
        case LOG_LEVEL_ERROR:   return "ERRO";
        case LOG_LEVEL_WARNING: return "WARN";
        case LOG_LEVEL_INFO:    return "INFO";
        case LOG_LEVEL_DEBUG:   return "DEBUG";
        default:                return "UNKN";
    }
}

const char* shellCommandIDToString(ShellCommandID id) {
    switch (id) {
        case CMD_SHELL_HELP:          return "HELP";
        case CMD_SHELL_STATUS:        return "STATUS";
        case CMD_SHELL_LOGBOOK:       return "LOGBOOK";
        case CMD_SHELL_CLEAR:         return "CLEAR";
        case CMD_SHELL_GET_ROLL:      return "GET_ROLL";
        case CMD_SHELL_GET_PITCH:     return "GET_PITCH";
        case CMD_SHELL_GET_YAW:       return "GET_YAW";
        case CMD_SHELL_GET_HEADING:   return "GET_HEADING";
        case CMD_SHELL_GET_ACCEL_X:   return "GET_ACCEL_X";
        case CMD_SHELL_GET_ACCEL_Y:   return "GET_ACCEL_Y";
        case CMD_SHELL_GET_ACCEL_Z:   return "GET_ACCEL_Z";
        case CMD_SHELL_GET_GYRO_X:    return "GET_GYRO_X";
        case CMD_SHELL_GET_GYRO_Y:    return "GET_GYRO_Y";
        case CMD_SHELL_GET_GYRO_Z:    return "GET_GYRO_Z";
        case CMD_SHELL_GET_ALTITUDE:  return "GET_ALTITUDE";
        case CMD_SHELL_GET_PRESSURE:  return "GET_PRESSURE";
        case CMD_SHELL_GET_BATTERY:   return "GET_BATTERY";
        case CMD_SHELL_GET_CURRENT:   return "GET_CURRENT";
        case CMD_SHELL_GET_POWER:     return "GET_POWER";
        case CMD_SHELL_GET_CHARGE:    return "GET_CHARGE";
        case CMD_SHELL_GET_TEMP_IMU:  return "GET_TEMP_IMU";
        case CMD_SHELL_GET_TEMP_BARO: return "GET_TEMP_BARO";
        case CMD_SHELL_GET_TC0:       return "GET_TC0";
        case CMD_SHELL_GET_TC1:       return "GET_TC1";
        case CMD_SHELL_GET_TC2:       return "GET_TC2";
        case CMD_SHELL_GET_TC3:       return "GET_TC3";
        case CMD_SHELL_GET_GPS_LAT:   return "GET_GPS_LAT";
        case CMD_SHELL_GET_GPS_LON:   return "GET_GPS_LON";
        case CMD_SHELL_GET_GPS_ALT:   return "GET_GPS_ALT";
        case CMD_SHELL_GET_GPS_SATS:  return "GET_GPS_SATS";
        case CMD_SHELL_GET_GPS_HDOP:  return "GET_GPS_HDOP";
        case CMD_SHELL_GET_GPS_SPEED: return "GET_GPS_SPEED";
        case CMD_SHELL_GET_MODE:      return "GET_MODE";
        case CMD_SHELL_GET_SECURITY:  return "GET_SECURITY";
        case CMD_SHELL_GET_FAILSAFE:  return "GET_FAILSAFE";
        case CMD_SHELL_GET_LOOP_TIME: return "GET_LOOP_TIME";
        case CMD_SHELL_GET_HEAP:      return "GET_HEAP";
        case CMD_SHELL_GET_UPTIME:    return "GET_UPTIME";
        case CMD_SHELL_SET_ROLL:      return "SET_ROLL";
        case CMD_SHELL_SET_PITCH:     return "SET_PITCH";
        case CMD_SHELL_SET_YAW:       return "SET_YAW";
        case CMD_SHELL_SET_ALTITUDE:  return "SET_ALTITUDE";
        case CMD_SHELL_SET_THROTTLE:  return "SET_THROTTLE";
        case CMD_SHELL_SET_MODE:      return "SET_MODE";
        case CMD_SHELL_REBOOT:        return "REBOOT";
        case CMD_SHELL_DEBUG_ON:      return "DEBUG_ON";
        case CMD_SHELL_DEBUG_OFF:     return "DEBUG_OFF";
        case CMD_SHELL_ECHO_ON:       return "ECHO_ON";
        case CMD_SHELL_ECHO_OFF:      return "ECHO_OFF";
        case CMD_SHELL_TEST:          return "TEST";
        default:                      return "UNKNOWN";
    }
}

float throttlePercentToFloat(float percent) {
    return constrain(percent / 100.0f, 0.0f, 1.0f);
}

float throttleFloatToPercent(float value) {
    return constrain(value, 0.0f, 1.0f) * 100.0f;
}

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

Shell::Shell()
    : _si(nullptr)
    , _gps(nullptr)
    , _security(nullptr)
    , _flightControl(nullptr)
    , _failsafe(nullptr)
    , _lora(nullptr)
    , _commandCount(0)
    , _logbookHead(0)
    , _logbookCount(0)
    , _initialized(false)
    , _inFlight(false)
    , _failsafeActive(false)
    , _echoEnabled(true)
    , _debugEnabled(false)
    , _uartCallback(nullptr)
{
    memset(_commands, 0, sizeof(_commands));
    memset(_logbook, 0, sizeof(_logbook));
    memset(_responseBuffer, 0, sizeof(_responseBuffer));
    
    _instance = this;
    
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO DO SHELL
 * =================================================================================
 */

bool Shell::begin(SIConverter* si, GPS* gps, Security* sec, 
                  FlightControl* fc, Failsafe* failsafe, LoRa* lora) {
    if (!si || !gps || !sec || !lora) {
        DEBUG_PRINT("begin() falhou: ponteiros nulos\n");
        return false;
    }
    
    _si = si;
    _gps = gps;
    _security = sec;
    _flightControl = fc;
    _failsafe = failsafe;
    _lora = lora;
    
    /* =========================================================================
     * REGISTO DE TODOS OS COMANDOS
     * ========================================================================= */
    
    /* Sistema e ajuda */
    registerCommand({CMD_SHELL_HELP,     "help",     "Mostra ajuda",                 "help [comando]",    0, 1, false, &Shell::cmdHelp});
    registerCommand({CMD_SHELL_STATUS,   "status",   "Mostra estado do sistema",     "status",            0, 0, false, &Shell::cmdStatus});
    registerCommand({CMD_SHELL_LOGBOOK,  "logbook",  "Mostra o logbook",             "logbook",           0, 0, false, &Shell::cmdLogbook});
    registerCommand({CMD_SHELL_CLEAR,    "clear",    "Limpa o logbook",              "clear",             0, 0, false, &Shell::cmdClear});
    
    /* Leitura de atitude */
    registerCommand({CMD_SHELL_GET_ROLL,     "get_roll",     "Lê ângulo de roll",                    "get_roll",     0, 0, false, &Shell::cmdGetRoll});
    registerCommand({CMD_SHELL_GET_PITCH,    "get_pitch",    "Lê ângulo de pitch",                   "get_pitch",    0, 0, false, &Shell::cmdGetPitch});
    registerCommand({CMD_SHELL_GET_YAW,      "get_yaw",      "Lê ângulo de yaw",                     "get_yaw",      0, 0, false, &Shell::cmdGetYaw});
    registerCommand({CMD_SHELL_GET_HEADING,  "get_heading",  "Lê rumo magnético",                    "get_heading",  0, 0, false, &Shell::cmdGetHeading});
    
    /* Leitura de aceleração */
    registerCommand({CMD_SHELL_GET_ACCEL_X, "get_accel_x", "Lê aceleração X", "get_accel_x", 0, 0, false, &Shell::cmdGetAccelX});
    registerCommand({CMD_SHELL_GET_ACCEL_Y, "get_accel_y", "Lê aceleração Y", "get_accel_y", 0, 0, false, &Shell::cmdGetAccelY});
    registerCommand({CMD_SHELL_GET_ACCEL_Z, "get_accel_z", "Lê aceleração Z", "get_accel_z", 0, 0, false, &Shell::cmdGetAccelZ});
    
    /* Leitura de giroscópio */
    registerCommand({CMD_SHELL_GET_GYRO_X, "get_gyro_x", "Lê velocidade angular X", "get_gyro_x", 0, 0, false, &Shell::cmdGetGyroX});
    registerCommand({CMD_SHELL_GET_GYRO_Y, "get_gyro_y", "Lê velocidade angular Y", "get_gyro_y", 0, 0, false, &Shell::cmdGetGyroY});
    registerCommand({CMD_SHELL_GET_GYRO_Z, "get_gyro_z", "Lê velocidade angular Z", "get_gyro_z", 0, 0, false, &Shell::cmdGetGyroZ});
    
    /* Leitura de altitude e pressão */
    registerCommand({CMD_SHELL_GET_ALTITUDE, "get_alt", "Lê altitude", "get_alt", 0, 0, false, &Shell::cmdGetAltitude});
    registerCommand({CMD_SHELL_GET_PRESSURE, "get_pressure", "Lê pressão barométrica", "get_pressure", 0, 0, false, &Shell::cmdGetPressure});
    
    /* Leitura de bateria */
    registerCommand({CMD_SHELL_GET_BATTERY, "get_batt", "Lê tensão da bateria", "get_batt", 0, 0, false, &Shell::cmdGetBattery});
    registerCommand({CMD_SHELL_GET_CURRENT, "get_current", "Lê corrente", "get_current", 0, 0, false, &Shell::cmdGetCurrent});
    registerCommand({CMD_SHELL_GET_POWER,   "get_power",   "Lê potência",   "get_power",   0, 0, false, &Shell::cmdGetPower});
    registerCommand({CMD_SHELL_GET_CHARGE,  "get_charge",  "Lê carga consumida", "get_charge", 0, 0, false, &Shell::cmdGetCharge});
    
    /* Leitura de temperaturas */
    registerCommand({CMD_SHELL_GET_TEMP_IMU,  "get_temp_imu",  "Lê temperatura IMU",  "get_temp_imu", 0, 0, false, &Shell::cmdGetTempIMU});
    registerCommand({CMD_SHELL_GET_TEMP_BARO, "get_temp_baro", "Lê temperatura Baro", "get_temp_baro", 0, 0, false, &Shell::cmdGetTempBaro});
    registerCommand({CMD_SHELL_GET_TC0, "get_tc0", "Lê temperatura termopar 0", "get_tc0", 0, 0, false, &Shell::cmdGetTC0});
    registerCommand({CMD_SHELL_GET_TC1, "get_tc1", "Lê temperatura termopar 1", "get_tc1", 0, 0, false, &Shell::cmdGetTC1});
    registerCommand({CMD_SHELL_GET_TC2, "get_tc2", "Lê temperatura termopar 2", "get_tc2", 0, 0, false, &Shell::cmdGetTC2});
    registerCommand({CMD_SHELL_GET_TC3, "get_tc3", "Lê temperatura termopar 3", "get_tc3", 0, 0, false, &Shell::cmdGetTC3});
    
    /* Leitura de GPS */
    registerCommand({CMD_SHELL_GET_GPS_LAT,   "get_gps_lat",   "Lê latitude GPS",   "get_gps_lat",   0, 0, false, &Shell::cmdGetGPSLat});
    registerCommand({CMD_SHELL_GET_GPS_LON,   "get_gps_lon",   "Lê longitude GPS",  "get_gps_lon",   0, 0, false, &Shell::cmdGetGPSLon});
    registerCommand({CMD_SHELL_GET_GPS_ALT,   "get_gps_alt",   "Lê altitude GPS",   "get_gps_alt",   0, 0, false, &Shell::cmdGetGPSAlt});
    registerCommand({CMD_SHELL_GET_GPS_SATS,  "get_gps_sats",  "Lê número de satélites", "get_gps_sats", 0, 0, false, &Shell::cmdGetGPSSats});
    registerCommand({CMD_SHELL_GET_GPS_HDOP,  "get_gps_hdop",  "Lê HDOP",           "get_gps_hdop",  0, 0, false, &Shell::cmdGetGPSHdop});
    registerCommand({CMD_SHELL_GET_GPS_SPEED, "get_gps_speed", "Lê velocidade GPS", "get_gps_speed", 0, 0, false, &Shell::cmdGetGPSSpeed});
    
    /* Leitura de estado */
    registerCommand({CMD_SHELL_GET_MODE,      "get_mode",      "Lê modo de voo",       "get_mode",      0, 0, false, &Shell::cmdGetMode});
    registerCommand({CMD_SHELL_GET_SECURITY,  "get_security",  "Lê nível de segurança","get_security",  0, 0, false, &Shell::cmdGetSecurity});
    registerCommand({CMD_SHELL_GET_FAILSAFE,  "get_failsafe",  "Lê estado do failsafe","get_failsafe",  0, 0, false, &Shell::cmdGetFailsafe});
    
    /* Diagnóstico */
    registerCommand({CMD_SHELL_GET_LOOP_TIME, "get_loop_time", "Lê tempo do loop", "get_loop_time", 0, 0, false, &Shell::cmdGetLoopTime});
    registerCommand({CMD_SHELL_GET_HEAP,      "get_heap",      "Lê memória livre",  "get_heap",      0, 0, false, &Shell::cmdGetHeap});
    registerCommand({CMD_SHELL_GET_UPTIME,    "get_uptime",    "Lê tempo de atividade", "get_uptime", 0, 0, false, &Shell::cmdGetUptime});
    
    /* Controlo de voo (PERMITIDOS em voo para emergência) — blockedInFlight = false */
    registerCommand({CMD_SHELL_SET_ROLL,     "set_roll",     "Define setpoint de roll",     "set_roll <graus>",     1, 1, false, &Shell::cmdSetRoll});
    registerCommand({CMD_SHELL_SET_PITCH,    "set_pitch",    "Define setpoint de pitch",    "set_pitch <graus>",    1, 1, false, &Shell::cmdSetPitch});
    registerCommand({CMD_SHELL_SET_YAW,      "set_yaw",      "Define setpoint de yaw",      "set_yaw <graus>",      1, 1, false, &Shell::cmdSetYaw});
    registerCommand({CMD_SHELL_SET_ALTITUDE, "set_alt",      "Define altitude alvo",        "set_alt <metros>",     1, 1, false, &Shell::cmdSetAltitude});
    registerCommand({CMD_SHELL_SET_THROTTLE, "set_throttle", "Define throttle",             "set_throttle <0-100>", 1, 1, false, &Shell::cmdSetThrottle});
    registerCommand({CMD_SHELL_SET_MODE,     "set_mode",     "Define modo de voo",          "set_mode <0-5>",       1, 1, false, &Shell::cmdSetMode});
    
    /* Comandos de sistema (BLOQUEADOS em voo por segurança) */
    registerCommand({CMD_SHELL_REBOOT, "reboot", "Reinicia o ESP2", "reboot", 0, 0, true, &Shell::cmdReboot});
    
    /* Debug */
    registerCommand({CMD_SHELL_DEBUG_ON,  "debug_on",  "Ativa debug",  "debug_on",  0, 0, false, &Shell::cmdDebugOn});
    registerCommand({CMD_SHELL_DEBUG_OFF, "debug_off", "Desativa debug","debug_off", 0, 0, false, &Shell::cmdDebugOff});
    registerCommand({CMD_SHELL_ECHO_ON,   "echo_on",   "Ativa eco",     "echo_on",   0, 0, false, &Shell::cmdEchoOn});
    registerCommand({CMD_SHELL_ECHO_OFF,  "echo_off",  "Desativa eco",  "echo_off",  0, 0, false, &Shell::cmdEchoOff});
    registerCommand({CMD_SHELL_TEST,      "test",      "Teste diagnóstico", "test",  0, 0, false, &Shell::cmdTest});
    
    _initialized = true;
    
    logToBook(LOG_LEVEL_INFO, "Shell ESP2 inicializado — %d comandos registados", _commandCount);
    DEBUG_PRINT("begin() concluído — %d comandos\n", _commandCount);
    
    return true;
}

/* =================================================================================
 * REGISTER COMMAND — Regista um comando na tabela
 * =================================================================================
 */

void Shell::registerCommand(const ShellCommand& cmd) {
    if (_commandCount >= SHELL_MAX_COMMANDS) {
        DEBUG_PRINT("registerCommand falhou: máximo de %d comandos atingido\n", SHELL_MAX_COMMANDS);
        return;
    }
    _commands[_commandCount++] = cmd;
}

/* =================================================================================
 * IS COMMAND ALLOWED — Verifica se o comando é permitido no estado atual
 * =================================================================================
 */

bool Shell::isCommandAllowed(const ShellCommand& cmd) const {
    if (!cmd.blockedInFlight) return true;
    if (!_inFlight) return true;
    if (_failsafeActive) return true;
    return false;
}

/* =================================================================================
 * SET FLIGHT STATE — Atualiza estado de voo
 * =================================================================================
 */

void Shell::setFlightState(bool inFlight, bool failsafeActive) {
    _inFlight = inFlight;
    _failsafeActive = failsafeActive;
    logToBook(LOG_LEVEL_INFO, "Estado de voo: %s | Failsafe: %s",
              inFlight ? "EM VOO" : "EM SOLO",
              failsafeActive ? "ATIVO" : "INATIVO");
}

/* =================================================================================
 * SET UART CALLBACK — Define callback para enviar dados para ESP1
 * =================================================================================
 */

void Shell::setUARTCallback(void (*callback)(const uint8_t*, size_t)) {
    _uartCallback = callback;
    DEBUG_PRINT("setUARTCallback() — Callback registado\n");
}

/* =================================================================================
 * PARSE LINE — Divide linha em argumentos
 * =================================================================================
 */

int Shell::parseLine(char* line, char* argv[]) {
    int argc = 0;
    char* token = strtok(line, " ");
    
    while (token != NULL && argc < SHELL_MAX_ARGS) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }
    
    return argc;
}

/* =================================================================================
 * PROCESS COMMAND — Processa uma string de comando
 * =================================================================================
 */

void Shell::processCommand(const char* line) {
    if (!_initialized || !line || !line[0]) return;
    
    /* Eco do comando (se ativo) */
    if (_echoEnabled) {
        sendResponse("\r\n> %s\r\n", line);
    }
    
    /* Copiar linha para buffer mutável */
    char lineCopy[SHELL_MAX_LINE_LENGTH];
    strncpy(lineCopy, line, sizeof(lineCopy) - 1);
    lineCopy[sizeof(lineCopy) - 1] = '\0';
    
    /* Parsear argumentos */
    char* argv[SHELL_MAX_ARGS];
    int argc = parseLine(lineCopy, argv);
    
    if (argc == 0) return;
    
    /* Procurar comando na tabela */
    for (uint8_t i = 0; i < _commandCount; i++) {
        if (strcmp(_commands[i].name, argv[0]) == 0) {
            /* Verificar número de argumentos */
            if (argc - 1 < _commands[i].minArgs) {
                sendResponse("ERRO: %s requer pelo menos %d argumento(s)\r\nUso: %s\r\n",
                            _commands[i].name, _commands[i].minArgs, _commands[i].usage);
                return;
            }
            
            /* Verificar se comando é permitido no estado atual */
            if (!isCommandAllowed(_commands[i])) {
                sendResponse("ERRO: Comando '%s' bloqueado em voo!\r\n", _commands[i].name);
                logToBook(LOG_LEVEL_WARNING, "Tentativa de comando bloqueado: %s", _commands[i].name);
                return;
            }
            
            /* Executar handler */
            _commands[i].handler(argc, argv);
            return;
        }
    }
    
    sendResponse("ERRO: Comando desconhecido '%s'. Digite 'help'\r\n", argv[0]);
}

/* =================================================================================
 * PROCESS SHELL COMMAND — Processa comando via TLV (do ESP1)
 * =================================================================================
 */

void Shell::processShellCommand(const TLVMessage& msg) {
    if (msg.msgID != MSG_SHELL_CMD || msg.tlvCount == 0) return;
    
    const TLVField& field = msg.tlvs[0];
    if (field.len == 0 || field.len > SHELL_MAX_LINE_LENGTH - 1) return;
    
    char command[SHELL_MAX_LINE_LENGTH];
    memcpy(command, field.data, field.len);
    command[field.len] = '\0';
    
    processCommand(command);
}

/* =================================================================================
 * PROCESS ESP1 RESPONSE — Processa resposta do ESP1 para reencaminhar para GS
 * =================================================================================
 */

void Shell::processESP1Response(const char* response) {
    if (!response || !response[0]) return;
    
    /* Reencaminhar para a GS via LoRa */
    sendToLoRa(response);
    
    DEBUG_PRINT("processESP1Response() — Resposta do ESP1 reencaminhada para GS\n");
}

/* =================================================================================
 * LOGGING
 * =================================================================================
 */

void Shell::log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[SHELL_MAX_MESSAGE_LENGTH];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logToBook(LOG_LEVEL_INFO, "%s", buffer);
}

void Shell::logToBook(uint8_t level, const char* format, ...) {
    if (_logbookCount >= SHELL_LOGBOOK_SIZE) {
        _logbookHead = (_logbookHead + 1) % SHELL_LOGBOOK_SIZE;
        _logbookCount = SHELL_LOGBOOK_SIZE;
    }
    
    va_list args;
    va_start(args, format);
    vsnprintf(_logbook[_logbookHead].message, sizeof(_logbook[_logbookHead].message), format, args);
    va_end(args);
    
    _logbook[_logbookHead].timestamp = millis();
    _logbook[_logbookHead].level = level;
    
    _logbookHead = (_logbookHead + 1) % SHELL_LOGBOOK_SIZE;
    if (_logbookCount < SHELL_LOGBOOK_SIZE) _logbookCount++;
    
    if (_debugEnabled) {
        DEBUG_PRINT("[%s] %s\n", logLevelToString(level), _logbook[(_logbookHead - 1 + SHELL_LOGBOOK_SIZE) % SHELL_LOGBOOK_SIZE].message);
    }
}

void Shell::getLogbook(char* buffer, size_t size) const {
    if (!buffer || size == 0) return;
    
    size_t offset = 0;
    uint8_t idx = (_logbookHead - _logbookCount + SHELL_LOGBOOK_SIZE) % SHELL_LOGBOOK_SIZE;
    
    for (uint8_t i = 0; i < _logbookCount; i++) {
        const LogbookEntry& entry = _logbook[idx];
        int written = snprintf(buffer + offset, size - offset,
                              "[%lu] [%s] %s\n",
                              entry.timestamp,
                              logLevelToString(entry.level),
                              entry.message);
        if (written <= 0) break;
        offset += written;
        idx = (idx + 1) % SHELL_LOGBOOK_SIZE;
    }
}

void Shell::clearLogbook() {
    _logbookHead = 0;
    _logbookCount = 0;
    memset(_logbook, 0, sizeof(_logbook));
    logToBook(LOG_LEVEL_INFO, "Logbook limpo");
}

/* =================================================================================
 * ENVIO DE RESPOSTAS
 * =================================================================================
 */

void Shell::sendResponse(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(_responseBuffer, sizeof(_responseBuffer), format, args);
    va_end(args);
    
    /* TODAS as respostas vão para a GS via LoRa 868MHz */
    sendToLoRa(_responseBuffer);
}

void Shell::sendToUART(const char* msg) {
    if (_uartCallback) {
        _uartCallback((const uint8_t*)msg, strlen(msg));
    }
}

void Shell::sendToLoRa(const char* msg) {
    if (_lora) {
        _lora->send((const uint8_t*)msg, strlen(msg), LORA_PRIORITY_SHELL);
        DEBUG_PRINT("sendToLoRa() — Resposta enviada para GS\n");
    } else {
        /* Fallback para Serial se LoRa não disponível */
        Serial.print(msg);
    }
}

/* =================================================================================
 * HANDLERS — SISTEMA E AJUDA
 * =================================================================================
 */

void Shell::cmdHelp(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    
    if (argc == 1) {
        self->sendResponse("\r\nComandos disponíveis:\r\n");
        self->sendResponse("  %-15s %s\r\n", "COMANDO", "DESCRIÇÃO");
        self->sendResponse("  %-15s %s\r\n", "-------", "---------");
        for (uint8_t i = 0; i < self->_commandCount; i++) {
            const char* block = self->_commands[i].blockedInFlight ? "[!]" : "   ";
            self->sendResponse("  %-15s %s %s\r\n", 
                              self->_commands[i].name,
                              block,
                              self->_commands[i].description);
        }
        self->sendResponse("\r\n[!] = bloqueado em voo (exceto em failsafe)\r\n");
    } else {
        for (uint8_t i = 0; i < self->_commandCount; i++) {
            if (strcmp(self->_commands[i].name, argv[1]) == 0) {
                self->sendResponse("\r\n%s: %s\r\n", self->_commands[i].name, self->_commands[i].description);
                self->sendResponse("Uso: %s\r\n", self->_commands[i].usage);
                self->sendResponse("Bloqueado em voo: %s\r\n", self->_commands[i].blockedInFlight ? "SIM" : "NÃO");
                return;
            }
        }
        self->sendResponse("Comando '%s' não encontrado\r\n", argv[1]);
    }
}

void Shell::cmdStatus(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    
    self->sendResponse("\r\n=== ESTADO DO SISTEMA (ESP2) ===\r\n");
    self->sendResponse("Shell inicializado: %s\r\n", self->_initialized ? "SIM" : "NÃO");
    self->sendResponse("Comandos registados: %d\r\n", self->_commandCount);
    self->sendResponse("Logbook entries: %d/%d\r\n", self->_logbookCount, SHELL_LOGBOOK_SIZE);
    self->sendResponse("Em voo: %s\r\n", self->_inFlight ? "SIM" : "NÃO");
    self->sendResponse("Failsafe: %s\r\n", self->_failsafeActive ? "ATIVO" : "INATIVO");
    self->sendResponse("Debug: %s\r\n", self->_debugEnabled ? "ATIVO" : "INATIVO");
    self->sendResponse("Echo: %s\r\n", self->_echoEnabled ? "ATIVO" : "INATIVO");
    
    if (self->_si && self->_si->areCriticalValid()) {
        const SIData& data = self->_si->getData();
        self->sendResponse("\nDados SI:\r\n");
        self->sendResponse("  Roll: %.2f° | Pitch: %.2f° | Yaw: %.2f°\r\n", 
                          data.gyroX_dps, data.gyroY_dps, data.gyroZ_dps);
        self->sendResponse("  Acel: %.2f, %.2f, %.2f m/s²\r\n", 
                          data.accelX_ms2, data.accelY_ms2, data.accelZ_ms2);
        self->sendResponse("  Bateria: %.2fV, %.2fA, %.2fW, %.3fAh\r\n",
                          data.batteryVoltage_v, data.batteryCurrent_a,
                          data.batteryPower_w, data.batteryCharge_ah);
    }
    
    if (self->_gps && self->_gps->isValid()) {
        self->sendResponse("\nGPS:\r\n");
        self->sendResponse("  Lat: %.6f | Lon: %.6f | Alt: %.1fm\r\n",
                          self->_gps->getLatitude(), self->_gps->getLongitude(),
                          self->_gps->getAltitude());
        self->sendResponse("  Sats: %d | HDOP: %.1f\r\n",
                          self->_gps->getSatellites(), self->_gps->getHdop());
    }
    
    if (self->_failsafe) {
        self->sendResponse("\nFailsafe: %s | Razão: %d\r\n",
                          self->_failsafe->isActive() ? "ATIVO" : "INATIVO",
                          self->_failsafe->getReason());
    }
    
    if (self->_flightControl) {
        self->sendResponse("\nModo de voo: %d\r\n", self->_flightControl->getMode());
    }
    
    self->sendResponse("========================\r\n");
}

void Shell::cmdLogbook(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    
    char buffer[4096];
    self->getLogbook(buffer, sizeof(buffer));
    self->sendResponse("\r\n=== LOGBOOK ===\r\n%s\r\n", buffer);
    self->sendResponse("===============\r\n");
}

void Shell::cmdClear(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->clearLogbook();
    self->sendResponse("Logbook limpo.\r\n");
}

/* =================================================================================
 * HANDLERS — LEITURA DE ATITUDE
 * =================================================================================
 */

void Shell::cmdGetRoll(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Roll: %.2f°\r\n", data.gyroX_dps);
}

void Shell::cmdGetPitch(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Pitch: %.2f°\r\n", data.gyroY_dps);
}

void Shell::cmdGetYaw(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Yaw: %.2f°\r\n", data.gyroZ_dps);
}

void Shell::cmdGetHeading(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_gps) {
        self->sendResponse("ERRO: GPS não disponível\r\n");
        return;
    }
    self->sendResponse("Heading: %.2f°\r\n", self->_gps->getHeading());
}

/* =================================================================================
 * HANDLERS — LEITURA DE ACELERAÇÃO
 * =================================================================================
 */

void Shell::cmdGetAccelX(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Aceleração X: %.2f m/s²\r\n", data.accelX_ms2);
}

void Shell::cmdGetAccelY(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Aceleração Y: %.2f m/s²\r\n", data.accelY_ms2);
}

void Shell::cmdGetAccelZ(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Aceleração Z: %.2f m/s²\r\n", data.accelZ_ms2);
}

/* =================================================================================
 * HANDLERS — LEITURA DE GIROSCÓPIO
 * =================================================================================
 */

void Shell::cmdGetGyroX(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Velocidade angular X: %.2f °/s\r\n", data.gyroX_dps);
}

void Shell::cmdGetGyroY(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Velocidade angular Y: %.2f °/s\r\n", data.gyroY_dps);
}

void Shell::cmdGetGyroZ(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Velocidade angular Z: %.2f °/s\r\n", data.gyroZ_dps);
}

/* =================================================================================
 * HANDLERS — LEITURA DE ALTITUDE E PRESSÃO
 * =================================================================================
 */

void Shell::cmdGetAltitude(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Altitude: %.2f m\r\n", data.altitude_m);
}

void Shell::cmdGetPressure(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Pressão: %.2f Pa\r\n", data.pressure_pa);
}

/* =================================================================================
 * HANDLERS — LEITURA DE BATERIA
 * =================================================================================
 */

void Shell::cmdGetBattery(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Tensão: %.2f V\r\n", data.batteryVoltage_v);
}

void Shell::cmdGetCurrent(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Corrente: %.2f A\r\n", data.batteryCurrent_a);
}

void Shell::cmdGetPower(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Potência: %.2f W\r\n", data.batteryPower_w);
}

void Shell::cmdGetCharge(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Carga consumida: %.3f Ah\r\n", data.batteryCharge_ah);
}

/* =================================================================================
 * HANDLERS — LEITURA DE TEMPERATURAS
 * =================================================================================
 */

void Shell::cmdGetTempIMU(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Temperatura IMU: %.1f °C\r\n", data.imuTemp_c);
}

void Shell::cmdGetTempBaro(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Temperatura Barómetro: %.1f °C\r\n", data.baroTemp_c);
}

void Shell::cmdGetTC0(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Termopar 0: %.1f °C\r\n", data.thermocoupleTemp_c[0]);
}

void Shell::cmdGetTC1(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Termopar 1: %.1f °C\r\n", data.thermocoupleTemp_c[1]);
}

void Shell::cmdGetTC2(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Termopar 2: %.1f °C\r\n", data.thermocoupleTemp_c[2]);
}

void Shell::cmdGetTC3(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_si) {
        self->sendResponse("ERRO: Dados SI não disponíveis\r\n");
        return;
    }
    const SIData& data = self->_si->getData();
    self->sendResponse("Termopar 3: %.1f °C\r\n", data.thermocoupleTemp_c[3]);
}

/* =================================================================================
 * HANDLERS — LEITURA DE GPS
 * =================================================================================
 */

void Shell::cmdGetGPSLat(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_gps) {
        self->sendResponse("ERRO: GPS não disponível\r\n");
        return;
    }
    self->sendResponse("Latitude: %.6f\r\n", self->_gps->getLatitude());
}

void Shell::cmdGetGPSLon(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_gps) {
        self->sendResponse("ERRO: GPS não disponível\r\n");
        return;
    }
    self->sendResponse("Longitude: %.6f\r\n", self->_gps->getLongitude());
}

void Shell::cmdGetGPSAlt(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_gps) {
        self->sendResponse("ERRO: GPS não disponível\r\n");
        return;
    }
    self->sendResponse("Altitude GPS: %.1f m\r\n", self->_gps->getAltitude());
}

void Shell::cmdGetGPSSats(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_gps) {
        self->sendResponse("ERRO: GPS não disponível\r\n");
        return;
    }
    self->sendResponse("Satélites: %d\r\n", self->_gps->getSatellites());
}

void Shell::cmdGetGPSHdop(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_gps) {
        self->sendResponse("ERRO: GPS não disponível\r\n");
        return;
    }
    self->sendResponse("HDOP: %.1f\r\n", self->_gps->getHdop());
}

void Shell::cmdGetGPSSpeed(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_gps) {
        self->sendResponse("ERRO: GPS não disponível\r\n");
        return;
    }
    self->sendResponse("Velocidade GPS: %.1f m/s\r\n", self->_gps->getSpeed());
}

/* =================================================================================
 * HANDLERS — LEITURA DE ESTADO
 * =================================================================================
 */

void Shell::cmdGetMode(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    self->sendResponse("Modo de voo: %d\r\n", self->_flightControl->getMode());
}

void Shell::cmdGetSecurity(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_security) {
        self->sendResponse("ERRO: Security não disponível\r\n");
        return;
    }
    self->sendResponse("Nível de segurança: %d\r\n", self->_security->getLevel());
}

void Shell::cmdGetFailsafe(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_failsafe) {
        self->sendResponse("ERRO: Failsafe não disponível\r\n");
        return;
    }
    self->sendResponse("Failsafe: %s | Nível: %d | Razão: %d\r\n",
                      self->_failsafe->isActive() ? "ATIVO" : "INATIVO",
                      self->_failsafe->getLevel(),
                      self->_failsafe->getReason());
}

/* =================================================================================
 * HANDLERS — DIAGNÓSTICO
 * =================================================================================
 */

void Shell::cmdGetLoopTime(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    /* TODO: Implementar medição de tempo de loop */
    self->sendResponse("Tempo de loop: N/A µs\r\n");
}

void Shell::cmdGetHeap(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->sendResponse("Memória livre: %u bytes\r\n", esp_get_free_heap_size());
}

void Shell::cmdGetUptime(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    uint32_t uptimeSec = millis() / 1000;
    uint32_t hours = uptimeSec / 3600;
    uint32_t minutes = (uptimeSec % 3600) / 60;
    uint32_t seconds = uptimeSec % 60;
    self->sendResponse("Tempo de atividade: %02d:%02d:%02d\r\n", hours, minutes, seconds);
}

/* =================================================================================
 * HANDLERS — CONTROLO DE VOO
 * =================================================================================
 */

void Shell::cmdSetRoll(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    float roll = atof(argv[1]);
    roll = constrain(roll, -MAX_ROLL_DEGREES, MAX_ROLL_DEGREES);
    
    /* Criar mensagem TLV com comando para ESP1 (via UART) */
    /* O ESP1 receberá e processará o comando de voo */
    self->sendResponse("Setpoint roll: %.1f° (enviado para ESP1)\r\n", roll);
}

void Shell::cmdSetPitch(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    float pitch = atof(argv[1]);
    pitch = constrain(pitch, -MAX_PITCH_DEGREES, MAX_PITCH_DEGREES);
    
    self->sendResponse("Setpoint pitch: %.1f° (enviado para ESP1)\r\n", pitch);
}

void Shell::cmdSetYaw(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    float yaw = atof(argv[1]);
    
    self->sendResponse("Setpoint yaw: %.1f° (enviado para ESP1)\r\n", yaw);
}

void Shell::cmdSetAltitude(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    float alt = atof(argv[1]);
    alt = constrain(alt, 0.0f, MAX_ALTITUDE_METERS);
    
    self->sendResponse("Altitude alvo: %.1f m (enviado para ESP1)\r\n", alt);
}

void Shell::cmdSetThrottle(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    float thr = atof(argv[1]);
    thr = constrain(thr, 0.0f, MAX_THROTTLE_PERCENT);
    
    self->sendResponse("Throttle: %.1f%% (enviado para ESP1)\r\n", thr);
}

void Shell::cmdSetMode(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    int mode = atoi(argv[1]);
    mode = constrain(mode, 0, 5);
    
    self->sendResponse("Modo de voo: %d (enviado para ESP1)\r\n", mode);
}

/* =================================================================================
 * HANDLERS — SISTEMA
 * =================================================================================
 */

void Shell::cmdReboot(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->sendResponse("Reiniciando ESP2 em 1 segundo...\r\n");
    delay(1000);
    esp_restart();
}

/* =================================================================================
 * HANDLERS — DEBUG
 * =================================================================================
 */

void Shell::cmdDebugOn(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->setDebug(true);
    self->sendResponse("Debug ativado.\r\n");
}

void Shell::cmdDebugOff(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->setDebug(false);
    self->sendResponse("Debug desativado.\r\n");
}

void Shell::cmdEchoOn(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->setEcho(true);
    self->sendResponse("Eco ativado.\r\n");
}

void Shell::cmdEchoOff(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->setEcho(false);
    self->sendResponse("Eco desativado.\r\n");
}

void Shell::cmdTest(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->sendResponse("Teste de diagnóstico (ESP2):\r\n");
    self->sendResponse("  Shell inicializado: %s\r\n", self->_initialized ? "SIM" : "NÃO");
    self->sendResponse("  Comandos registados: %d\r\n", self->_commandCount);
    self->sendResponse("  Logbook entries: %d\r\n", self->_logbookCount);
    self->sendResponse("  Em voo: %s\r\n", self->_inFlight ? "SIM" : "NÃO");
    self->sendResponse("  Failsafe: %s\r\n", self->_failsafeActive ? "ATIVO" : "INATIVO");
    self->sendResponse("  Debug: %s\r\n", self->_debugEnabled ? "ATIVO" : "INATIVO");
    self->sendResponse("  Echo: %s\r\n", self->_echoEnabled ? "ATIVO" : "INATIVO");
    
    if (self->_si) {
        self->sendResponse("  SIConverter: %s\r\n", self->_si->areCriticalValid() ? "OK" : "FALHA");
    }
    if (self->_gps) {
        self->sendResponse("  GPS: %s\r\n", self->_gps->isValid() ? "VÁLIDO" : "INVÁLIDO");
    }
    if (self->_lora) {
        self->sendResponse("  LoRa: %s\r\n", self->_lora->isReady() ? "PRONTO" : "OCUPADO");
    }
}

/* =================================================================================
 * CONFIGURAÇÃO
 * =================================================================================
 */

void Shell::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}

void Shell::setEcho(bool enable) {
    _echoEnabled = enable;
}

bool Shell::isInitialized() const {
    return _initialized;
}

void Shell::getStatusString(char* buffer, size_t bufferSize) const {
    if (!buffer || bufferSize == 0) return;
    snprintf(buffer, bufferSize,
             "Shell ESP2: init=%s flight=%s failsafe=%s cmds=%d log=%d/%d",
             _initialized ? "Y" : "N",
             _inFlight ? "Y" : "N",
             _failsafeActive ? "Y" : "N",
             _commandCount,
             _logbookCount, SHELL_LOGBOOK_SIZE);
}

#endif /* SHELL_CPP */
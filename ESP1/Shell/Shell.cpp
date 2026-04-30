/**
 * =================================================================================
 * SHELL.CPP — IMPLEMENTAÇÃO DA CONSOLA DE COMANDOS
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-19
 * VERSÃO:     2.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. ESP1: Apenas RX via LoRa 2.4GHz, NUNCA transmite
 * 2. ESP2: Apenas TX via LoRa 868MHz, NUNCA recebe
 * 3. Toda resposta à GS passa pelo ESP2
 * 4. Comunicação ESP1 ↔ ESP2 via UART bidirecional
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
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[Shell] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * CONSTANTES DE UART PARA COMUNICAÇÃO ENTRE ESPs
 * =================================================================================
 */

#ifndef SHELL_UART_BAUD
#define SHELL_UART_BAUD 921600
#endif

/* =================================================================================
 * INSTÂNCIA GLOBAL
 * =================================================================================
 */

Shell shell;
Shell* Shell::_instance = nullptr;

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

Shell::Shell()
    : _flightControl(nullptr)
    , _security(nullptr)
    , _telemetry(nullptr)
    , _commandCount(0)
    , _logbookHead(0)
    , _logbookCount(0)
    , _initialized(false)
    , _isESP2(false)
    , _inFlight(false)
    , _failsafeActive(false)
    , _echoEnabled(true)
    , _debugEnabled(false)
    , _currentOrigin(SHELL_ORIGIN_USB)
    , _uartBufferLen(0)
    , _loRaCallback(nullptr)
{
    memset(_commands, 0, sizeof(_commands));
    memset(_logbook, 0, sizeof(_logbook));
    memset(_responseBuffer, 0, sizeof(_responseBuffer));
    memset(_uartBuffer, 0, sizeof(_uartBuffer));
    
    _instance = this;
    
    DEBUG_PRINT("Construtor chamado (ESP2=%s)\n", _isESP2 ? "SIM" : "NÃO");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO DO SHELL
 * =================================================================================
 */

bool Shell::begin(FlightControl* fc, Security* sec, Telemetry* tel, bool isESP2) {
    _flightControl = fc;
    _security = sec;
    _telemetry = tel;
    _isESP2 = isESP2;
    
    /* Inicializar UART para comunicação entre ESPs */
    Serial2.begin(SHELL_UART_BAUD);
    
    /* =========================================================================
     * REGISTO DE TODOS OS COMANDOS
     * ========================================================================= */
    
    /* Sistema e ajuda (NUNCA bloqueados em voo) */
    registerCommand({CMD_SHELL_HELP,        "help",         "Mostra ajuda",                    "help [comando]",       0, 1, false, &Shell::cmdHelp});
    registerCommand({CMD_SHELL_STATUS,      "status",       "Mostra estado do sistema",        "status",               0, 0, false, &Shell::cmdStatus});
    registerCommand({CMD_SHELL_LOGBOOK,     "logbook",      "Mostra o logbook",                "logbook",              0, 0, false, &Shell::cmdLogbook});
    registerCommand({CMD_SHELL_CLEAR,       "clear",        "Limpa o logbook",                 "clear",                0, 0, false, &Shell::cmdClear});
    registerCommand({CMD_SHELL_SAVE,        "save",         "Salva config na NVS",             "save",                 0, 0, true,  &Shell::cmdSave});
    registerCommand({CMD_SHELL_RESET_CONFIG,"reset_config", "Reseta config para padrão",       "reset_config",         0, 0, true,  &Shell::cmdResetConfig});
    
    /* Leitura de sensores e estado (NUNCA bloqueados em voo) */
    registerCommand({CMD_SHELL_GET_ROLL,     "get_roll",     "Lê ângulo de roll",               "get_roll",             0, 0, false, &Shell::cmdGetRoll});
    registerCommand({CMD_SHELL_GET_PITCH,    "get_pitch",    "Lê ângulo de pitch",              "get_pitch",            0, 0, false, &Shell::cmdGetPitch});
    registerCommand({CMD_SHELL_GET_YAW,      "get_yaw",      "Lê ângulo de yaw",                "get_yaw",              0, 0, false, &Shell::cmdGetYaw});
    registerCommand({CMD_SHELL_GET_HEADING,  "get_heading",  "Lê rumo magnético",               "get_heading",          0, 0, false, &Shell::cmdGetHeading});
    registerCommand({CMD_SHELL_GET_ALTITUDE, "get_alt",      "Lê altitude",                     "get_alt",              0, 0, false, &Shell::cmdGetAltitude});
    registerCommand({CMD_SHELL_GET_BATTERY,  "get_batt",     "Lê tensão e corrente da bateria","get_batt",             0, 0, false, &Shell::cmdGetBattery});
    registerCommand({CMD_SHELL_GET_OUTPUTS,  "get_outputs",  "Lê sinais PWM atuais",            "get_outputs",          0, 0, false, &Shell::cmdGetOutputs});
    registerCommand({CMD_SHELL_GET_MODE,     "get_mode",     "Lê modo de voo atual",            "get_mode",             0, 0, false, &Shell::cmdGetMode});
    registerCommand({CMD_SHELL_GET_SECURITY, "get_security", "Lê nível de segurança",           "get_security",         0, 0, false, &Shell::cmdGetSecurity});
    registerCommand({CMD_SHELL_GET_PID,      "get_pid",      "Lê ganhos PID de um eixo",        "get_pid <axis>",       1, 1, false, &Shell::cmdGetPID});
    registerCommand({CMD_SHELL_GET_TRIM,     "get_trim",     "Lê trims atuais",                 "get_trim",             0, 0, false, &Shell::cmdGetTrim});
    registerCommand({CMD_SHELL_GET_TEMPERATURE,"get_temp",   "Lê temperaturas",                 "get_temp",             0, 0, false, &Shell::cmdGetTemperature});
    
    /* Controlo de voo (PERMITIDOS em voo para emergência) */
    registerCommand({CMD_SHELL_SET_ROLL,     "set_roll",     "Define setpoint de roll",         "set_roll <graus>",     1, 1, false, &Shell::cmdSetRoll});
    registerCommand({CMD_SHELL_SET_PITCH,    "set_pitch",    "Define setpoint de pitch",        "set_pitch <graus>",    1, 1, false, &Shell::cmdSetPitch});
    registerCommand({CMD_SHELL_SET_YAW,      "set_yaw",      "Define setpoint de yaw",          "set_yaw <graus>",      1, 1, false, &Shell::cmdSetYaw});
    registerCommand({CMD_SHELL_SET_ALTITUDE, "set_alt",      "Define altitude alvo",            "set_alt <metros>",     1, 1, false, &Shell::cmdSetAltitude});
    registerCommand({CMD_SHELL_SET_THROTTLE, "set_throttle", "Define throttle",                 "set_throttle <0-100>", 1, 1, false, &Shell::cmdSetThrottle});
    registerCommand({CMD_SHELL_SET_MODE,     "set_mode",     "Define modo de voo",              "set_mode <0-5>",       1, 1, false, &Shell::cmdSetMode});
    registerCommand({CMD_SHELL_SET_PID,      "set_pid",      "Define ganhos PID",               "set_pid <axis> <Kp> <Ki> <Kd>", 4, 4, false, &Shell::cmdSetPID});
    registerCommand({CMD_SHELL_SET_TRIM,     "set_trim",     "Define trims",                    "set_trim <WR> <WL> <Rud> <ER> <EL>", 5, 5, false, &Shell::cmdSetTrim});
    
    /* Comandos de sistema (BLOQUEADOS em voo por segurança) */
    registerCommand({CMD_SHELL_REBOOT,       "reboot",       "Reinicia o ESP1/ESP2",            "reboot",               0, 0, true, &Shell::cmdReboot});
    registerCommand({CMD_SHELL_ARM,          "arm",          "Arma os motores",                 "arm",                  0, 0, true, &Shell::cmdArm});
    registerCommand({CMD_SHELL_DISARM,       "disarm",       "Desarma os motores",              "disarm",               0, 0, true, &Shell::cmdDisarm});
    registerCommand({CMD_SHELL_CALIBRATE,    "calibrate",    "Calibra sensores/atuadores",      "calibrate <sensor|actuator>", 1, 1, true, &Shell::cmdCalibrate});
    
    /* Debug e diagnóstico (NUNCA bloqueados) */
    registerCommand({CMD_SHELL_DEBUG_ON,     "debug_on",     "Ativa debug do shell",            "debug_on",             0, 0, false, &Shell::cmdDebugOn});
    registerCommand({CMD_SHELL_DEBUG_OFF,    "debug_off",    "Desativa debug do shell",         "debug_off",            0, 0, false, &Shell::cmdDebugOff});
    registerCommand({CMD_SHELL_ECHO_ON,      "echo_on",      "Ativa eco de comandos",           "echo_on",              0, 0, false, &Shell::cmdEchoOn});
    registerCommand({CMD_SHELL_ECHO_OFF,     "echo_off",     "Desativa eco de comandos",        "echo_off",             0, 0, false, &Shell::cmdEchoOff});
    registerCommand({CMD_SHELL_TEST,         "test",         "Executa teste de diagnóstico",    "test",                 0, 0, false, &Shell::cmdTest});
    
    /* Telemetria e logging (NUNCA bloqueados) */
    registerCommand({CMD_SHELL_START_LOG,    "start_log",    "Inicia gravação de telemetria",   "start_log",            0, 0, false, &Shell::cmdStartLog});
    registerCommand({CMD_SHELL_STOP_LOG,     "stop_log",     "Para gravação de telemetria",     "stop_log",             0, 0, false, &Shell::cmdStopLog});
    registerCommand({CMD_SHELL_MARKER,       "marker",       "Adiciona marcador ao log",        "marker <texto>",       1, 1, false, &Shell::cmdMarker});
    
    _initialized = true;
    
    logToBook(LOG_LEVEL_INFO, "Shell inicializado — %d comandos registados (ESP2=%s)", 
              _commandCount, _isESP2 ? "SIM" : "NÃO");
    
    if (_isESP2) {
        sendResponse("\r\n=== BeaconFly UAS Shell v3.0 (ESP2) ===\r\n");
    } else {
        sendResponse("\r\n=== BeaconFly UAS Shell v3.0 (ESP1) ===\r\n");
        sendResponse("NOTA: Respostas via LoRa 868MHz (ESP2)\r\n");
    }
    sendResponse("Type 'help' for available commands\r\n");
    
    DEBUG_PRINT("begin() concluído — %d comandos (ESP2=%s)\n", _commandCount, _isESP2 ? "SIM" : "NÃO");
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
    /* Comandos sem bloqueio → sempre permitidos */
    if (!cmd.blockedInFlight) return true;
    
    /* Comandos com bloqueio: só permitidos em solo ou em failsafe */
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
 * SET LORA CALLBACK — APENAS NO ESP2
 * =================================================================================
 */

void Shell::setLoRaCallback(void (*callback)(const uint8_t*, size_t)) {
    if (!_isESP2) {
        DEBUG_PRINT("setLoRaCallback() ignorado — ESP1 não tem TX LoRa\n");
        return;
    }
    _loRaCallback = callback;
    DEBUG_PRINT("setLoRaCallback() registado (ESP2)\n");
}

/* =================================================================================
 * EXTRACT TARGET — Extrai destinatário da linha de comando
 * =================================================================================
 */

ShellTarget Shell::extractTarget(char* line) {
    /* Verificar se começa com [ESP1] */
    if (strncmp(line, "[ESP1]", 6) == 0) {
        /* Remover o prefixo */
        memmove(line, line + 6, strlen(line + 6) + 1);
        return SHELL_TARGET_ESP1;
    }
    
    /* Verificar se começa com [ESP2] */
    if (strncmp(line, "[ESP2]", 6) == 0) {
        memmove(line, line + 6, strlen(line + 6) + 1);
        return SHELL_TARGET_ESP2;
    }
    
    return SHELL_TARGET_AUTO;
}

/* =================================================================================
 * FORWARD TO ESP2 — Encaminha comando para ESP2 via UART
 * =================================================================================
 */

void Shell::forwardToESP2(const char* command, ShellOrigin origin) {
    /* Formato: [CMD]origem:tamanho:comando */
    char buffer[SHELL_UART_BUFFER_SIZE];
    int len = snprintf(buffer, sizeof(buffer), "[CMD]%d:%d:%s", 
                       (int)origin, (int)strlen(command), command);
    
    Serial2.write((uint8_t*)buffer, len);
    DEBUG_PRINT("forwardToESP2: comando '%s' encaminhado (origem=%d)\n", command, origin);
}

/* =================================================================================
 * FORWARD RESPONSE TO ESP2 — Encaminha resposta do ESP1 para ESP2
 * =================================================================================
 */

void Shell::forwardResponseToESP2(const char* response, ShellOrigin origin) {
    /* Formato: [RESP]origem:tamanho:resposta */
    char buffer[SHELL_UART_BUFFER_SIZE];
    int len = snprintf(buffer, sizeof(buffer), "[RESP]%d:%d:%s", 
                       (int)origin, (int)strlen(response), response);
    
    Serial2.write((uint8_t*)buffer, len);
    DEBUG_PRINT("forwardResponseToESP2: resposta encaminhada (origem=%d)\n", origin);
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

bool Shell::processCommand(const char* line, ShellOrigin origin) {
    if (!_initialized || !line || !line[0]) return false;
    
    setCurrentOrigin(origin);
    
    /* Eco do comando (se ativo) */
    if (_echoEnabled && origin != SHELL_ORIGIN_UART) {
        if (_isESP2) {
            sendResponse("[%lu] ESP2 > %s\r\n", millis(), line);
        } else {
            sendResponse("[%lu] ESP1 > %s\r\n", millis(), line);
        }
    }
    
    /* Copiar linha para buffer mutável */
    char lineCopy[SHELL_MAX_LINE_LENGTH];
    strncpy(lineCopy, line, sizeof(lineCopy) - 1);
    lineCopy[sizeof(lineCopy) - 1] = '\0';
    
    /* Extrair destinatário */
    ShellTarget target = extractTarget(lineCopy);
    
    /* Se o destino for ESP2 e este é ESP1 → encaminhar */
    if (target == SHELL_TARGET_ESP2 && !_isESP2) {
        forwardToESP2(lineCopy, origin);
        return true;
    }
    
    /* Se o destino for ESP1 e este é ESP2 → erro (comando não deveria chegar aqui) */
    if (target == SHELL_TARGET_ESP1 && _isESP2) {
        sendResponse("ERRO: Comando [ESP1] recebido no ESP2 (encaminhamento incorreto)\r\n");
        return false;
    }
    
    /* Se o destino for AUTO, assumir o ESP atual se for ESP1, ou rejeitar se for ESP2 */
    if (target == SHELL_TARGET_AUTO && _isESP2) {
        /* Comando sem prefixo chega ao ESP2? Só se veio via UART */
        if (origin != SHELL_ORIGIN_UART) {
            sendResponse("ERRO: Comando sem destinatário [ESP1] ou [ESP2]\r\n");
            sendResponse("Uso: [ESP1] comando ou [ESP2] comando\r\n");
            return false;
        }
    }
    
    /* Parsear argumentos */
    char* argv[SHELL_MAX_ARGS];
    int argc = parseLine(lineCopy, argv);
    
    if (argc == 0) return false;
    
    /* Procurar comando na tabela */
    for (uint8_t i = 0; i < _commandCount; i++) {
        if (strcmp(_commands[i].name, argv[0]) == 0) {
            /* Verificar número de argumentos */
            if (argc - 1 < _commands[i].minArgs) {
                sendResponse("ERRO: %s requer pelo menos %d argumento(s)\r\nUso: %s\r\n",
                            _commands[i].name, _commands[i].minArgs, _commands[i].usage);
                return false;
            }
            
            /* Verificar se comando é permitido no estado atual */
            if (!isCommandAllowed(_commands[i])) {
                sendResponse("ERRO: Comando '%s' bloqueado em voo!\r\n", _commands[i].name);
                logToBook(LOG_LEVEL_WARNING, "Tentativa de comando bloqueado: %s", _commands[i].name);
                return false;
            }
            
            /* Executar handler */
            _commands[i].handler(argc, argv);
            return true;
        }
    }
    
    sendResponse("ERRO: Comando desconhecido '%s'. Digite 'help'\r\n", argv[0]);
    return false;
}

/* =================================================================================
 * PROCESS UART COMMAND — Processa comando recebido via UART
 * =================================================================================
 */

void Shell::processUARTCommand(const uint8_t* data, size_t len) {
    char buffer[SHELL_UART_BUFFER_SIZE];
    if (len >= sizeof(buffer)) len = sizeof(buffer) - 1;
    memcpy(buffer, data, len);
    buffer[len] = '\0';
    
    DEBUG_PRINT("processUARTCommand: %s\n", buffer);
    
    /* Verificar tipo de mensagem */
    if (strncmp(buffer, "[CMD]", 5) == 0) {
        /* Comando vindo do ESP1 para o ESP2 */
        /* Formato: [CMD]origem:tamanho:comando */
        int originInt, cmdLen;
        char cmdBuffer[SHELL_MAX_LINE_LENGTH];
        sscanf(buffer + 5, "%d:%d:", &originInt, &cmdLen);
        
        /* Extrair comando */
        char* cmdStart = strchr(buffer + 5, ':');
        if (cmdStart) {
            cmdStart = strchr(cmdStart + 1, ':');
            if (cmdStart) {
                cmdStart++;
                strncpy(cmdBuffer, cmdStart, sizeof(cmdBuffer) - 1);
                processCommand(cmdBuffer, (ShellOrigin)originInt);
            }
        }
    } 
    else if (strncmp(buffer, "[RESP]", 6) == 0) {
        /* Resposta vinda do ESP2 para o ESP1 (para encaminhar para USB/LoRa) */
        /* Formato: [RESP]origem:tamanho:resposta */
        int originInt, respLen;
        char respBuffer[SHELL_MAX_RESPONSE_LENGTH];
        sscanf(buffer + 6, "%d:%d:", &originInt, &respLen);
        
        /* Extrair resposta */
        char* respStart = strchr(buffer + 6, ':');
        if (respStart) {
            respStart = strchr(respStart + 1, ':');
            if (respStart) {
                respStart++;
                strncpy(respBuffer, respStart, sizeof(respBuffer) - 1);
                /* Enviar resposta para o canal apropriado */
                ShellOrigin orig = (ShellOrigin)originInt;
                if (orig == SHELL_ORIGIN_USB) {
                    sendToUSB(respBuffer);
                } else if (orig == SHELL_ORIGIN_LORA && _loRaCallback) {
                    _loRaCallback((const uint8_t*)respBuffer, strlen(respBuffer));
                }
            }
        }
    }
}

/* =================================================================================
 * PROCESS SHELL COMMAND — Processa comando via TLV (LoRa 2.4GHz)
 * =================================================================================
 */

void Shell::processShellCommand(const TLVMessage& msg, ShellOrigin origin) {
    if (msg.msgID != MSG_SHELL_CMD || msg.tlvCount == 0) return;
    
    const TLVField& field = msg.tlvs[0];
    if (field.len == 0 || field.len > SHELL_MAX_LINE_LENGTH - 1) return;
    
    char command[SHELL_MAX_LINE_LENGTH];
    memcpy(command, field.data, field.len);
    command[field.len] = '\0';
    
    processCommand(command, origin);
}

/* =================================================================================
 * UPDATE UART — Verifica dados na UART (chamar no loop)
 * =================================================================================
 */

void Shell::updateUART() {
    while (Serial2.available()) {
        uint8_t c = Serial2.read();
        
        if (c == '\n') {
            if (_uartBufferLen > 0) {
                _uartBuffer[_uartBufferLen] = '\0';
                processUARTCommand((const uint8_t*)_uartBuffer, _uartBufferLen);
                _uartBufferLen = 0;
            }
        } else if (c != '\r' && _uartBufferLen < SHELL_UART_BUFFER_SIZE - 1) {
            _uartBuffer[_uartBufferLen++] = c;
        }
    }
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
    
    if (_isESP2) {
        /* ESP2: envia diretamente para o canal apropriado */
        if (_currentOrigin == SHELL_ORIGIN_LORA && _loRaCallback) {
            _loRaCallback((const uint8_t*)_responseBuffer, strlen(_responseBuffer));
        } else if (_currentOrigin == SHELL_ORIGIN_USB) {
            sendToUSB(_responseBuffer);
        } else if (_currentOrigin == SHELL_ORIGIN_UART) {
            sendToUART(_responseBuffer);
        } else {
            sendToUSB(_responseBuffer);
        }
    } else {
        /* ESP1: resposta tem que ir para ESP2 via UART (que depois transmite) */
        if (_currentOrigin == SHELL_ORIGIN_LORA || _currentOrigin == SHELL_ORIGIN_USB) {
            forwardResponseToESP2(_responseBuffer, _currentOrigin);
        } else {
            sendToUSB(_responseBuffer);
        }
    }
}

void Shell::sendDebug(const char* format, ...) {
    if (!_debugEnabled) return;
    
    va_list args;
    va_start(args, format);
    vsnprintf(_responseBuffer, sizeof(_responseBuffer), format, args);
    va_end(args);
    
    sendToUSB(_responseBuffer);
}

void Shell::sendToUSB(const char* msg) {
    Serial.print(msg);
}

void Shell::sendToLoRa(const char* msg) {
    if (_isESP2 && _loRaCallback) {
        _loRaCallback((const uint8_t*)msg, strlen(msg));
    }
}

void Shell::sendToUART(const char* msg) {
    Serial2.print(msg);
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
        self->sendResponse("\r\n[!] = bloqueado em voo (segurança)\r\n");
        self->sendResponse("\r\nPrefixos:\r\n");
        self->sendResponse("  [ESP1] comando → executar no ESP1\r\n");
        self->sendResponse("  [ESP2] comando → executar no ESP2\r\n");
        self->sendResponse("  comando        → padrão: ESP1\r\n");
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
    
    self->sendResponse("\r\n=== ESTADO DO SISTEMA ===\r\n");
    self->sendResponse("ESP: %s\r\n", self->_isESP2 ? "ESP2" : "ESP1");
    self->sendResponse("Shell inicializado: %s\r\n", self->_initialized ? "SIM" : "NÃO");
    self->sendResponse("Comandos registados: %d\r\n", self->_commandCount);
    self->sendResponse("Logbook entries: %d/%d\r\n", self->_logbookCount, SHELL_LOGBOOK_SIZE);
    self->sendResponse("Em voo: %s\r\n", self->_inFlight ? "SIM" : "NÃO");
    self->sendResponse("Failsafe: %s\r\n", self->_failsafeActive ? "ATIVO" : "INATIVO");
    self->sendResponse("Debug: %s\r\n", self->_debugEnabled ? "ATIVO" : "INATIVO");
    self->sendResponse("Echo: %s\r\n", self->_echoEnabled ? "ATIVO" : "INATIVO");
    
    if (self->_flightControl) {
        const FlightState& state = self->_flightControl->getState();
        self->sendResponse("Roll: %.2f°\r\n", state.roll);
        self->sendResponse("Pitch: %.2f°\r\n", state.pitch);
        self->sendResponse("Heading: %.2f°\r\n", state.heading);
        self->sendResponse("Altitude: %.2fm\r\n", state.altFused);
        self->sendResponse("Bateria: %.2fV\r\n", state.battV);
        self->sendResponse("GPS: %s\r\n", state.gpsValid ? "VÁLIDO" : "INVÁLIDO");
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

void Shell::cmdSave(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->sendResponse("Salvando configuração na NVS...\r\n");
    /* TODO: Implementar salvamento em NVS */
    self->sendResponse("Configuração salva (reinício mantém valores).\r\n");
}

void Shell::cmdResetConfig(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->sendResponse("Resetando configuração para valores padrão...\r\n");
    /* TODO: Resetar PIDs, trims, etc. para valores padrão */
    self->sendResponse("Configuração resetada.\r\n");
}

/* =================================================================================
 * HANDLERS — LEITURA DE SENSORES
 * =================================================================================
 */

void Shell::cmdGetRoll(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    const FlightState& state = self->_flightControl->getState();
    self->sendResponse("Roll: %.2f°\r\n", state.roll);
}

void Shell::cmdGetPitch(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    const FlightState& state = self->_flightControl->getState();
    self->sendResponse("Pitch: %.2f°\r\n", state.pitch);
}

void Shell::cmdGetYaw(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    const FlightState& state = self->_flightControl->getState();
    self->sendResponse("Yaw: %.2f°\r\n", state.yaw);
}

void Shell::cmdGetHeading(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    const FlightState& state = self->_flightControl->getState();
    self->sendResponse("Heading: %.2f°\r\n", state.heading);
}

void Shell::cmdGetAltitude(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    const FlightState& state = self->_flightControl->getState();
    self->sendResponse("Altitude: %.2fm\r\n", state.altFused);
}

void Shell::cmdGetBattery(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    const FlightState& state = self->_flightControl->getState();
    self->sendResponse("Tensão: %.2fV | Corrente: %.2fA\r\n", state.battV, state.battA);
}

void Shell::cmdGetOutputs(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    const ActuatorSignals& out = self->_flightControl->getOutputs();
    self->sendResponse("PWM: WR=%d WL=%d Rud=%d ER=%d EL=%d Mot=%d\r\n",
                      out.wingR, out.wingL, out.rudder, out.elevonR, out.elevonL, out.motor);
}

void Shell::cmdGetMode(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    FlightMode mode = self->_flightControl->getMode();
    self->sendResponse("Modo de voo: %d\r\n", mode);
}

void Shell::cmdGetSecurity(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_security) {
        self->sendResponse("ERRO: Security não disponível\r\n");
        return;
    }
    SecurityLevel level = self->_security->getSecurityLevel();
    bool lockdown = self->_security->isInLockdown();
    self->sendResponse("Nível de segurança: %d | Lockdown: %s\r\n", level, lockdown ? "SIM" : "NÃO");
}

void Shell::cmdGetPID(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    int axis = atoi(argv[1]);
    /* TODO: Obter ganhos PID do FlightControl */
    self->sendResponse("PID eixo %d: Kp=0.00 Ki=0.00 Kd=0.00\r\n", axis);
}

void Shell::cmdGetTrim(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    const TrimValues& trim = self->_flightControl->getTrim();
    self->sendResponse("Trims: WR=%d WL=%d Rud=%d ER=%d EL=%d\r\n",
                      trim.wingR, trim.wingL, trim.rudder, trim.elevonR, trim.elevonL);
}

void Shell::cmdGetTemperature(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->sendResponse("Temperatura: ESP1=??.?°C | ESP2=??.?°C\r\n");
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
    FlightSetpoints sp = self->_flightControl->getSetpoints();
    sp.roll = constrain(roll, -MAX_ROLL_ANGLE, MAX_ROLL_ANGLE);
    self->_flightControl->setSetpoints(sp);
    self->sendResponse("Setpoint roll: %.2f°\r\n", sp.roll);
}

void Shell::cmdSetPitch(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    float pitch = atof(argv[1]);
    FlightSetpoints sp = self->_flightControl->getSetpoints();
    sp.pitch = constrain(pitch, -MAX_PITCH_ANGLE, MAX_PITCH_ANGLE);
    self->_flightControl->setSetpoints(sp);
    self->sendResponse("Setpoint pitch: %.2f°\r\n", sp.pitch);
}

void Shell::cmdSetYaw(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    float yaw = atof(argv[1]);
    FlightSetpoints sp = self->_flightControl->getSetpoints();
    sp.yaw = yaw;
    self->_flightControl->setSetpoints(sp);
    self->sendResponse("Setpoint yaw: %.2f°\r\n", sp.yaw);
}

void Shell::cmdSetAltitude(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    float alt = atof(argv[1]);
    FlightSetpoints sp = self->_flightControl->getSetpoints();
    sp.altitude = constrain(alt, MIN_ALTITUDE, MAX_ALTITUDE);
    self->_flightControl->setSetpoints(sp);
    self->sendResponse("Altitude alvo: %.2fm\r\n", sp.altitude);
}

void Shell::cmdSetThrottle(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    float thr = atof(argv[1]) / 100.0f;
    FlightSetpoints sp = self->_flightControl->getSetpoints();
    sp.throttle = constrain(thr, 0.0f, 1.0f);
    self->_flightControl->setSetpoints(sp);
    self->sendResponse("Throttle: %.1f%%\r\n", sp.throttle * 100);
}

void Shell::cmdSetMode(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    FlightMode mode = (FlightMode)atoi(argv[1]);
    self->_flightControl->setMode(mode);
    self->sendResponse("Modo alterado para: %d\r\n", mode);
}

void Shell::cmdSetPID(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    int axis = atoi(argv[1]);
    float Kp = atof(argv[2]);
    float Ki = atof(argv[3]);
    float Kd = atof(argv[4]);
    self->_flightControl->setPIDGains(axis, Kp, Ki, Kd);
    self->sendResponse("PID eixo %d: Kp=%.3f Ki=%.3f Kd=%.3f\r\n", axis, Kp, Ki, Kd);
}

void Shell::cmdSetTrim(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_flightControl) {
        self->sendResponse("ERRO: FlightControl não disponível\r\n");
        return;
    }
    TrimValues trim;
    trim.wingR = atoi(argv[1]);
    trim.wingL = atoi(argv[2]);
    trim.rudder = atoi(argv[3]);
    trim.elevonR = atoi(argv[4]);
    trim.elevonL = atoi(argv[5]);
    self->_flightControl->setTrim(trim);
    self->sendResponse("Trims atualizados: WR=%d WL=%d Rud=%d ER=%d EL=%d\r\n",
                      trim.wingR, trim.wingL, trim.rudder, trim.elevonR, trim.elevonL);
}

/* =================================================================================
 * HANDLERS — SISTEMA
 * =================================================================================
 */

void Shell::cmdReboot(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->sendResponse("Reiniciando ESP em 1 segundo...\r\n");
    delay(1000);
    esp_restart();
}

void Shell::cmdArm(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->sendResponse("Armando motores...\r\n");
    /* TODO: Chamar actuators.arm() via FlightControl */
}

void Shell::cmdDisarm(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->sendResponse("Desarmando motores...\r\n");
    /* TODO: Chamar actuators.disarm() via FlightControl */
}

void Shell::cmdCalibrate(int argc, char** argv) {
    Shell* self = _instance;
    if (!self) return;
    self->sendResponse("Calibrando %s...\r\n", argv[1]);
    /* TODO: Implementar calibração de sensores/atuadores */
    self->sendResponse("Calibração concluída.\r\n");
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
    self->sendResponse("Teste de diagnóstico:\r\n");
    self->sendResponse("  Shell inicializado: %s\r\n", self->_initialized ? "SIM" : "NÃO");
    self->sendResponse("  ESP2: %s\r\n", self->_isESP2 ? "SIM" : "NÃO");
    self->sendResponse("  Comandos registados: %d\r\n", self->_commandCount);
    self->sendResponse("  Logbook entries: %d\r\n", self->_logbookCount);
    self->sendResponse("  Em voo: %s\r\n", self->_inFlight ? "SIM" : "NÃO");
    self->sendResponse("  Failsafe: %s\r\n", self->_failsafeActive ? "ATIVO" : "INATIVO");
    self->sendResponse("  Debug: %s\r\n", self->_debugEnabled ? "ATIVO" : "INATIVO");
    self->sendResponse("  Echo: %s\r\n", self->_echoEnabled ? "ATIVO" : "INATIVO");
}

/* =================================================================================
 * HANDLERS — TELEMETRIA
 * =================================================================================
 */

void Shell::cmdStartLog(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_telemetry) {
        self->sendResponse("ERRO: Telemetry não disponível\r\n");
        return;
    }
    self->_telemetry->startLogging();
    self->sendResponse("Gravação de telemetria iniciada.\r\n");
}

void Shell::cmdStopLog(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_telemetry) {
        self->sendResponse("ERRO: Telemetry não disponível\r\n");
        return;
    }
    self->_telemetry->stopLogging();
    self->sendResponse("Gravação de telemetria parada.\r\n");
}

void Shell::cmdMarker(int argc, char** argv) {
    Shell* self = _instance;
    if (!self || !self->_telemetry) {
        self->sendResponse("ERRO: Telemetry não disponível\r\n");
        return;
    }
    self->_telemetry->addMarker(argv[1]);
    self->sendResponse("Marcador adicionado: %s\r\n", argv[1]);
}

/* =================================================================================
 * CONFIGURAÇÃO
 * =================================================================================
 */

void Shell::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("Debug %s\n", enable ? "ATIVADO" : "desativado");
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
             "Shell: init=%s esp2=%s flight=%s failsafe=%s cmds=%d log=%d/%d",
             _initialized ? "Y" : "N",
             _isESP2 ? "Y" : "N",
             _inFlight ? "Y" : "N",
             _failsafeActive ? "Y" : "N",
             _commandCount,
             _logbookCount, SHELL_LOGBOOK_SIZE);
}

/* =================================================================================
 * FUNÇÕES AUXILIARES
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
        case CMD_SHELL_SAVE:          return "SAVE";
        case CMD_SHELL_RESET_CONFIG:  return "RESET_CONFIG";
        case CMD_SHELL_GET_ROLL:      return "GET_ROLL";
        case CMD_SHELL_GET_PITCH:     return "GET_PITCH";
        case CMD_SHELL_GET_YAW:       return "GET_YAW";
        case CMD_SHELL_GET_HEADING:   return "GET_HEADING";
        case CMD_SHELL_GET_ALTITUDE:  return "GET_ALTITUDE";
        case CMD_SHELL_GET_BATTERY:   return "GET_BATTERY";
        case CMD_SHELL_GET_OUTPUTS:   return "GET_OUTPUTS";
        case CMD_SHELL_GET_MODE:      return "GET_MODE";
        case CMD_SHELL_GET_SECURITY:  return "GET_SECURITY";
        case CMD_SHELL_GET_PID:       return "GET_PID";
        case CMD_SHELL_GET_TRIM:      return "GET_TRIM";
        case CMD_SHELL_GET_TEMPERATURE: return "GET_TEMPERATURE";
        case CMD_SHELL_SET_ROLL:      return "SET_ROLL";
        case CMD_SHELL_SET_PITCH:     return "SET_PITCH";
        case CMD_SHELL_SET_YAW:       return "SET_YAW";
        case CMD_SHELL_SET_ALTITUDE:  return "SET_ALTITUDE";
        case CMD_SHELL_SET_THROTTLE:  return "SET_THROTTLE";
        case CMD_SHELL_SET_MODE:      return "SET_MODE";
        case CMD_SHELL_SET_PID:       return "SET_PID";
        case CMD_SHELL_SET_TRIM:      return "SET_TRIM";
        case CMD_SHELL_REBOOT:        return "REBOOT";
        case CMD_SHELL_ARM:           return "ARM";
        case CMD_SHELL_DISARM:        return "DISARM";
        case CMD_SHELL_CALIBRATE:     return "CALIBRATE";
        case CMD_SHELL_DEBUG_ON:      return "DEBUG_ON";
        case CMD_SHELL_DEBUG_OFF:     return "DEBUG_OFF";
        case CMD_SHELL_ECHO_ON:       return "ECHO_ON";
        case CMD_SHELL_ECHO_OFF:      return "ECHO_OFF";
        case CMD_SHELL_TEST:          return "TEST";
        case CMD_SHELL_START_LOG:     return "START_LOG";
        case CMD_SHELL_STOP_LOG:      return "STOP_LOG";
        case CMD_SHELL_MARKER:        return "MARKER";
        default:                      return "UNKNOWN";
    }
}

const char* shellOriginToString(ShellOrigin origin) {
    switch (origin) {
        case SHELL_ORIGIN_USB:      return "USB";
        case SHELL_ORIGIN_LORA:     return "LORA";
        case SHELL_ORIGIN_UART:     return "UART";
        case SHELL_ORIGIN_INTERNAL: return "INTERNAL";
        default:                    return "UNKNOWN";
    }
}
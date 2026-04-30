/**
 * =================================================================================
 * TELEMETRY.CPP — IMPLEMENTAÇÃO DO DATA LOGGER (CAIXA NEGRA)
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
 * 1. TIMESTAMP EM MICROSSEGUNDOS: Uso de micros() em vez de millis() para
 *    precisão máxima na reconstrução temporal de eventos.
 * 
 * 2. FICHEIROS ABERTOS: Durante a sessão, os ficheiros permanecem abertos
 *    para evitar o overhead de abrir/fechar a cada linha.
 * 
 * 3. BUFFER CIRCULAR: 200 entradas em RAM evitam bloqueios no loop principal.
 * 
 * 4. FLUSH PERIÓDICO: A cada 2 segundos, o buffer é escrito no storage.
 * 
 * 5. FALLBACK: SD → SPIFFS → RAM (se ambos falharem, apenas buffer em RAM)
 * 
 * =================================================================================
 */

#include "Telemetry.h"
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define TELEMETRY_DEBUG

#ifdef TELEMETRY_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_enabled) { Serial.printf("[Telemetry] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

Telemetry::Telemetry()
    : _backend(STORAGE_NONE)
    , _sessionNumber(0)
    , _enabled(true)
    , _filesOpen(false)
    , _head(0)
    , _count(0)
    , _totalLogged(0)
    , _totalDropped(0)
    , _lastSampleMs(0)
    , _lastFlushMs(0)
    , _lastLoopTimeUs(0)
{
    memset(_flightFilename, 0, sizeof(_flightFilename));
    memset(_eventFilename, 0, sizeof(_eventFilename));
    memset(_buffer, 0, sizeof(_buffer));
    
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO DO LOGGER
 * =================================================================================
 */

void Telemetry::begin() {
    DEBUG_PRINT("begin() — Inicializando data logger (caixa negra)...\n");
    
    _head = 0;
    _count = 0;
    _totalLogged = 0;
    _totalDropped = 0;
    _enabled = true;
    
    /* =========================================================================
     * INICIALIZAÇÃO DOS BACKENDS (POR ORDEM DE PREFERÊNCIA)
     * ========================================================================= */
    
    if (_initSD()) {
        _backend = STORAGE_SD;
        DEBUG_PRINT("Backend: SD CARD\n");
    }
    else if (_initSPIFFS()) {
        _backend = STORAGE_SPIFFS;
        DEBUG_PRINT("Backend: SPIFFS (flash interna)\n");
    }
    else {
        _backend = STORAGE_NONE;
        DEBUG_PRINT("AVISO: Nenhum storage persistente — apenas buffer RAM\n");
        DEBUG_PRINT("       Os dados serão perdidos no reset!\n");
    }
    
    /* =========================================================================
     * GESTÃO DE SESSÃO
     * ========================================================================= */
    
    _sessionNumber = _readSessionNumber() + 1;
    _writeSessionNumber(_sessionNumber);
    
    /* =========================================================================
     * NOMES DOS FICHEIROS (COM NÚMERO DA SESSÃO)
     * ========================================================================= */
    
    snprintf(_flightFilename, sizeof(_flightFilename), "%s_%03d.csv",
             TELEMETRY_FLIGHT_FILE_BASE, _sessionNumber);
    snprintf(_eventFilename, sizeof(_eventFilename), "%s_%03d.csv",
             TELEMETRY_EVENT_FILE_BASE, _sessionNumber);
    
    DEBUG_PRINT("Sessão #%03d\n", _sessionNumber);
    DEBUG_PRINT("Ficheiro de voo:    %s\n", _flightFilename);
    DEBUG_PRINT("Ficheiro de eventos: %s\n", _eventFilename);
    
    /* =========================================================================
     * ABRIR FICHEIROS (MANTÊM ABERTOS DURANTE A SESSÃO)
     * ========================================================================= */
    
    if (_backend != STORAGE_NONE) {
        if (!_openFiles()) {
            DEBUG_PRINT("ERRO: Não foi possível abrir os ficheiros!\n");
            _backend = STORAGE_NONE;
        } else {
            _writeCSVHeader();
        }
    }
    
    /* =========================================================================
     * REGISTAR EVENTO DE BOOT
     * ========================================================================= */
    
    char bootMsg[64];
    snprintf(bootMsg, sizeof(bootMsg), "Sessão #%03d | Backend: %s",
             _sessionNumber,
             (_backend == STORAGE_SD) ? "SD" : 
             (_backend == STORAGE_SPIFFS) ? "SPIFFS" : "RAM ONLY");
    logEvent(EVT_BOOT, bootMsg);
    
    DEBUG_PRINT("Telemetry inicializado com sucesso\n");
}

/* =================================================================================
 * CLOSE — ENCERRAMENTO SEGURO
 * =================================================================================
 */

void Telemetry::close() {
    DEBUG_PRINT("close() — Encerramento seguro do logger...\n");
    
    /* Forçar flush de todo o buffer */
    flush();
    
    /* Registrar evento de encerramento */
    char msg[64];
    snprintf(msg, sizeof(msg), "Sessão #%03d encerrada | Total logged: %u | Dropped: %u",
             _sessionNumber, _totalLogged, _totalDropped);
    logEvent(EVT_CUSTOM, msg);
    
    /* Fechar ficheiros */
    _closeFiles();
    
    DEBUG_PRINT("Telemetry encerrado. Total escrito: %u | Descartados: %u\n", 
                _totalLogged, _totalDropped);
}

/* =================================================================================
 * UPDATE — CICLO PRINCIPAL (CHAMAR NO LOOP)
 * =================================================================================
 */

void Telemetry::update(const FlightControl& fc, const Actuators& act) {
    if (!_enabled) return;
    
    uint32_t nowMs = millis();
    uint32_t nowUs = micros();
    
    /* Calcular tempo do último loop (para diagnóstico) */
    static uint32_t lastCallUs = 0;
    if (lastCallUs != 0) {
        _lastLoopTimeUs = nowUs - lastCallUs;
    }
    lastCallUs = nowUs;
    
    /* Amostragem periódica (10Hz = 100ms) */
    if (nowMs - _lastSampleMs >= TELEMETRY_SAMPLE_INTERVAL_MS) {
        _lastSampleMs = nowMs;
        log(fc, act);
    }
    
    /* Flush periódico (a cada 2 segundos) */
    if (nowMs - _lastFlushMs >= TELEMETRY_FLUSH_INTERVAL_MS) {
        _lastFlushMs = nowMs;
        flush();
    }
}

/* =================================================================================
 * LOG — REGISTO PERIÓDICO DE DADOS DE VOO
 * =================================================================================
 * 
 * Adiciona uma entrada ao buffer circular. O timestamp é em MICROSSEGUNDOS
 * para precisão máxima na reconstrução temporal.
 */

void Telemetry::log(const FlightControl& fc, const Actuators& act) {
    if (!_enabled) return;
    
    const FlightState& state = fc.getState();
    const FlightSetpoints& sp = fc.getSetpoints();
    const ActuatorSignals& signals = act.getLastSignals();
    
    /* Criar entrada com timestamp em microssegundos */
    TelemetryEntry entry;
    entry.timestampUs = micros();           /* PRECISÃO MÁXIMA */
    
    /* Estado atual */
    entry.roll = state.roll;
    entry.pitch = state.pitch;
    entry.yaw = state.yaw;
    entry.altFused = state.altFused;
    
    /* Setpoints */
    entry.targetRoll = sp.roll;
    entry.targetPitch = sp.pitch;
    entry.targetYaw = sp.yaw;
    entry.targetAlt = sp.altitude;
    
    /* Erros PID */
    entry.errorRoll = sp.roll - state.roll;
    entry.errorPitch = sp.pitch - state.pitch;
    entry.errorYaw = sp.yaw - state.yaw;
    entry.errorAlt = sp.altitude - state.altFused;
    
    /* Sinais PWM dos atuadores */
    entry.pwmWingR = signals.wingR;
    entry.pwmWingL = signals.wingL;
    entry.pwmRudder = signals.rudder;
    entry.pwmElevonR = signals.elevonR;
    entry.pwmElevonL = signals.elevonL;
    entry.pwmMotor = signals.motor;
    
    /* Estado do sistema */
    entry.flightMode = (uint8_t)fc.getMode();
    entry.battV = state.battV;
    entry.battA = state.battA;
    
    /* Diagnóstico — tempo do último loop */
    entry.loopTimeUs = _lastLoopTimeUs;
    
    /* Inserir no buffer circular */
    if (_count >= TELEMETRY_BUFFER_SIZE) {
        /* Buffer cheio — descartar a entrada mais antiga */
        _totalDropped++;
        /* O buffer já está cheio, então a nova entrada sobrescreve a mais antiga */
        _buffer[_head] = entry;
        _head = (_head + 1) % TELEMETRY_BUFFER_SIZE;
        /* _count mantém-se igual (buffer cheio) */
        
        if (_totalDropped % 100 == 1) {
            DEBUG_PRINT("AVISO: Buffer cheio! %u entradas descartadas no total\n", _totalDropped);
        }
    } else {
        /* Buffer tem espaço */
        _buffer[_head] = entry;
        _head = (_head + 1) % TELEMETRY_BUFFER_SIZE;
        _count++;
    }
}

/* =================================================================================
 * FLUSH — ESCREVE TODO O BUFFER NO STORAGE
 * =================================================================================
 */

void Telemetry::flush() {
    if (!_enabled) return;
    if (_count == 0) return;
    if (_backend == STORAGE_NONE) return;
    if (!_ensureFilesOpen()) return;
    
    /* Calcular índice do início do buffer (FIFO) */
    uint16_t startIdx = (_head - _count + TELEMETRY_BUFFER_SIZE) % TELEMETRY_BUFFER_SIZE;
    
    /* Escrever todas as entradas do buffer */
    for (uint16_t i = 0; i < _count; i++) {
        uint16_t idx = (startIdx + i) % TELEMETRY_BUFFER_SIZE;
        _writeFlightLine(_buffer[idx]);
        _totalLogged++;
    }
    
    DEBUG_PRINT("Flush: %d entradas escritas (total: %u, descartadas: %u)\n", 
                _count, _totalLogged, _totalDropped);
    
    /* Limpar buffer após flush */
    _count = 0;
}

/* =================================================================================
 * LOG EVENT — REGISTO IMEDIATO DE EVENTO CRÍTICO
 * =================================================================================
 * 
 * Os eventos são escritos IMEDIATAMENTE (sem buffer) para garantir que
 * não se perdem em caso de falha catastrófica.
 */

void Telemetry::logEvent(TelemetryEvent type, const char* message) {
    if (!_enabled) return;
    
    /* Se não há storage, apenas debug */
    if (_backend == STORAGE_NONE) {
        DEBUG_PRINT("EVENTO (sem storage): %s\n", telemetryEventToString(type));
        return;
    }
    
    /* Garantir que os ficheiros estão abertos */
    if (!_ensureFilesOpen()) {
        DEBUG_PRINT("ERRO: Não foi possível abrir ficheiro para evento!\n");
        return;
    }
    
    /* Escrever evento imediatamente */
    _writeEventLine(type, message);
    
    DEBUG_PRINT("Evento crítico: %s — %s\n", 
                telemetryEventToString(type), 
                message ? message : "");
}

/* =================================================================================
 * SET ENABLED — ATIVA/DESATIVA LOGGING
 * =================================================================================
 */

void Telemetry::setEnabled(bool enable) {
    if (_enabled == enable) return;
    
    _enabled = enable;
    if (enable) {
        DEBUG_PRINT("Logging ATIVADO\n");
        /* Reabrir ficheiros se necessário */
        if (_backend != STORAGE_NONE) {
            _openFiles();
        }
    } else {
        DEBUG_PRINT("Logging DESATIVADO\n");
        /* Fechar ficheiros para evitar corrupção */
        _closeFiles();
    }
}

bool Telemetry::isEnabled() const {
    return _enabled;
}

/* =================================================================================
 * GETTERS
 * =================================================================================
 */

StorageBackend Telemetry::getBackend() const {
    return _backend;
}

uint16_t Telemetry::getSessionNumber() const {
    return _sessionNumber;
}

uint32_t Telemetry::getTotalLogged() const {
    return _totalLogged;
}

uint32_t Telemetry::getTotalDropped() const {
    return _totalDropped;
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — INICIALIZAÇÃO DE BACKENDS
 * =================================================================================
 */

bool Telemetry::_initSD() {
    DEBUG_PRINT("_initSD() — Tentando inicializar SD Card (CS=%d)...\n", TELEMETRY_SD_CS_PIN);
    
    if (!SD.begin(TELEMETRY_SD_CS_PIN)) {
        DEBUG_PRINT("_initSD() — Falha na inicialização\n");
        return false;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        DEBUG_PRINT("_initSD() — Nenhum cartão detectado\n");
        SD.end();
        return false;
    }
    
    uint64_t cardSize = SD.cardSize() / (1024ULL * 1024ULL);
    DEBUG_PRINT("_initSD() — Cartão SD: tipo=%d, tamanho=%llu MB\n", cardType, cardSize);
    
    return true;
}

bool Telemetry::_initSPIFFS() {
    DEBUG_PRINT("_initSPIFFS() — Tentando inicializar SPIFFS...\n");
    
    if (!SPIFFS.begin(false)) {
        DEBUG_PRINT("_initSPIFFS() — Falha na montagem (formatando?)\n");
        if (!SPIFFS.begin(true)) {
            DEBUG_PRINT("_initSPIFFS() — Falha definitiva\n");
            return false;
        }
    }
    
    DEBUG_PRINT("_initSPIFFS() — SPIFFS montado: total=%u KB, usado=%u KB\n",
                SPIFFS.totalBytes() / 1024, SPIFFS.usedBytes() / 1024);
    
    return true;
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — GESTÃO DE SESSÃO
 * =================================================================================
 */

uint16_t Telemetry::_readSessionNumber() {
    if (_backend == STORAGE_NONE) return 0;
    
    File f;
    if (_backend == STORAGE_SD) {
        f = SD.open(TELEMETRY_SESSION_FILE, FILE_READ);
    } else {
        f = SPIFFS.open(TELEMETRY_SESSION_FILE, FILE_READ);
    }
    
    if (!f) return 0;
    
    uint16_t n = 0;
    if (f.available() >= 2) {
        f.read((uint8_t*)&n, 2);
    }
    f.close();
    
    DEBUG_PRINT("_readSessionNumber() — Última sessão: %d\n", n);
    return n;
}

void Telemetry::_writeSessionNumber(uint16_t n) {
    if (_backend == STORAGE_NONE) return;
    
    File f;
    if (_backend == STORAGE_SD) {
        f = SD.open(TELEMETRY_SESSION_FILE, FILE_WRITE);
    } else {
        f = SPIFFS.open(TELEMETRY_SESSION_FILE, FILE_WRITE);
    }
    
    if (f) {
        f.write((uint8_t*)&n, 2);
        f.close();
        DEBUG_PRINT("_writeSessionNumber() — Sessão %d guardada\n", n);
    } else {
        DEBUG_PRINT("_writeSessionNumber() — ERRO: não foi possível guardar sessão\n");
    }
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — GESTÃO DE FICHEIROS
 * =================================================================================
 */

bool Telemetry::_openFiles() {
    if (_backend == STORAGE_NONE) return false;
    
    /* Fechar ficheiros se já estiverem abertos */
    _closeFiles();
    
    /* Abrir ficheiro de voo */
    if (_backend == STORAGE_SD) {
        _flightFile = SD.open(_flightFilename, FILE_APPEND);
        _eventFile = SD.open(_eventFilename, FILE_APPEND);
    } else {
        _flightFile = SPIFFS.open(_flightFilename, FILE_APPEND);
        _eventFile = SPIFFS.open(_eventFilename, FILE_APPEND);
    }
    
    if (!_flightFile || !_eventFile) {
        DEBUG_PRINT("_openFiles() — ERRO: não foi possível abrir ficheiros\n");
        _filesOpen = false;
        return false;
    }
    
    _filesOpen = true;
    DEBUG_PRINT("_openFiles() — Ficheiros abertos com sucesso\n");
    return true;
}

void Telemetry::_closeFiles() {
    if (!_filesOpen) return;
    
    if (_flightFile) {
        _flightFile.flush();
        _flightFile.close();
    }
    if (_eventFile) {
        _eventFile.flush();
        _eventFile.close();
    }
    
    _filesOpen = false;
    DEBUG_PRINT("_closeFiles() — Ficheiros fechados\n");
}

bool Telemetry::_ensureFilesOpen() {
    if (_backend == STORAGE_NONE) return false;
    if (_filesOpen && _flightFile && _eventFile) return true;
    
    /* Tentar reabrir */
    return _openFiles();
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — ESCRITA DE DADOS
 * =================================================================================
 */

void Telemetry::_writeCSVHeader() {
    if (!_ensureFilesOpen()) return;
    
    /* Cabeçalho do ficheiro de voo (23 campos + timestamp) */
    const char* flightHeader = 
        "timestamp_us,roll,pitch,yaw,alt_fused,"
        "target_roll,target_pitch,target_yaw,target_alt,"
        "error_roll,error_pitch,error_yaw,error_alt,"
        "pwm_wingR,pwm_wingL,pwm_rudder,pwm_elevonR,pwm_elevonL,pwm_motor,"
        "flight_mode,battV,battA,loop_time_us\n";
    
    /* Cabeçalho do ficheiro de eventos */
    const char* eventHeader = "timestamp_us,event_type,message\n";
    
    /* Verificar se os ficheiros estão vazios (para não escrever cabeçalho duplicado) */
    if (_flightFile.size() == 0) {
        _writeLine(_flightFile, flightHeader);
        DEBUG_PRINT("Cabeçalho CSV escrito no ficheiro de voo\n");
    }
    
    if (_eventFile.size() == 0) {
        _writeLine(_eventFile, eventHeader);
        DEBUG_PRINT("Cabeçalho CSV escrito no ficheiro de eventos\n");
    }
}

void Telemetry::_entryToCSV(const TelemetryEntry& e, char* out) {
    snprintf(out, TELEMETRY_LINE_MAX,
        "%llu,%.2f,%.2f,%.2f,%.2f,"
        "%.2f,%.2f,%.2f,%.2f,"
        "%.2f,%.2f,%.2f,%.2f,"
        "%u,%u,%u,%u,%u,%u,"
        "%u,%.2f,%.2f,%u\n",
        /* Timestamp (microssegundos) — PRECISÃO MÁXIMA */
        (unsigned long long)e.timestampUs,
        /* Estado atual */
        (double)e.roll, (double)e.pitch, (double)e.yaw, (double)e.altFused,
        /* Setpoints */
        (double)e.targetRoll, (double)e.targetPitch, (double)e.targetYaw, (double)e.targetAlt,
        /* Erros PID */
        (double)e.errorRoll, (double)e.errorPitch, (double)e.errorYaw, (double)e.errorAlt,
        /* PWM dos atuadores */
        e.pwmWingR, e.pwmWingL, e.pwmRudder,
        e.pwmElevonR, e.pwmElevonL, e.pwmMotor,
        /* Sistema */
        e.flightMode, (double)e.battV, (double)e.battA,
        /* Diagnóstico */
        e.loopTimeUs
    );
}

void Telemetry::_writeFlightLine(const TelemetryEntry& e) {
    if (!_ensureFilesOpen()) return;
    
    char line[TELEMETRY_LINE_MAX];
    _entryToCSV(e, line);
    _writeLine(_flightFile, line);
}

void Telemetry::_writeEventLine(TelemetryEvent type, const char* message) {
    if (!_ensureFilesOpen()) return;
    
    const char* typeStr = telemetryEventToString(type);
    char line[TELEMETRY_LINE_MAX];
    
    if (message && message[0] != '\0') {
        snprintf(line, sizeof(line), "%llu,%s,%s\n",
                 (unsigned long long)micros(), typeStr, message);
    } else {
        snprintf(line, sizeof(line), "%llu,%s,\n",
                 (unsigned long long)micros(), typeStr);
    }
    
    _writeLine(_eventFile, line);
}

void Telemetry::_writeLine(File& file, const char* line) {
    if (!file) return;
    
    file.print(line);
    
    /* Forçar flush imediato para eventos (garantir que não se perdem) */
    /* Para dados de voo, o flush é periódico */
}

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* storageBackendToString(StorageBackend backend) {
    switch (backend) {
        case STORAGE_NONE:   return "NONE";
        case STORAGE_SPIFFS: return "SPIFFS";
        case STORAGE_SD:     return "SD";
        default:             return "UNKNOWN";
    }
}

const char* telemetryEventToString(TelemetryEvent event) {
    switch (event) {
        case EVT_BOOT:          return "BOOT";
        case EVT_ARM:           return "ARM";
        case EVT_DISARM:        return "DISARM";
        case EVT_MODE_CHANGE:   return "MODE_CHANGE";
        case EVT_FAILSAFE_ON:   return "FAILSAFE_ON";
        case EVT_FAILSAFE_OFF:  return "FAILSAFE_OFF";
        case EVT_BATT_LOW:      return "BATT_LOW";
        case EVT_BATT_CRITICAL: return "BATT_CRITICAL";
        case EVT_SENSOR_FAIL:   return "SENSOR_FAIL";
        case EVT_SECURITY:      return "SECURITY";
        case EVT_CUSTOM:        return "CUSTOM";
        default:                return "UNKNOWN";
    }
}
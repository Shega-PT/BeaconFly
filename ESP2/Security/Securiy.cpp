/**
 * =================================================================================
 * SECURITY.CPP — IMPLEMENTAÇÃO DO MÓDULO DE SEGURANÇA (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-30
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. ESP2 verifica comandos recebidos via UART do ESP1
 * 2. Chave HMAC deve ser partilhada entre ESP1 e ESP2
 * 3. Anti-replay com janela deslizante de 32 bits
 * 4. Rate limiting por categoria de comando
 * 
 * =================================================================================
 */

#include "Security.h"
#include <Arduino.h>
#include <mbedtls/md.h>

/* =================================================================================
 * DEBUG
 * =================================================================================
 */

#define SECURITY_DEBUG

#ifdef SECURITY_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[Security] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * CHAVE HMAC PADRÃO (APENAS PARA DESENVOLVIMENTO)
 * =================================================================================
 * ⚠️ NUNCA usar esta chave em produção!
 */

static const uint8_t DEFAULT_HMAC_KEY[SECURITY_HMAC_KEY_SIZE] = {
    0x4B, 0x65, 0x79, 0x42, 0x65, 0x61, 0x63, 0x6F,
    0x6E, 0x46, 0x6C, 0x79, 0x55, 0x41, 0x53, 0x31,
    0x39, 0x38, 0x35, 0x21, 0x53, 0x45, 0x43, 0x55,
    0x52, 0x45, 0x4B, 0x45, 0x59, 0x58, 0x59, 0x5A
};

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

Security::Security()
    : _keyLoaded(false)
    , _lastSeqNum(0)
    , _seqBitmap(0)
    , _lastCriticalMs(0)
    , _lastControlMs(0)
    , _lastMaintenanceMs(0)
    , _flightState(STATE_GROUND_DISARMED)
    , _currentLevel(SECURITY_LEVEL_NORMAL)
    , _lockdown(false)
    , _violationCount(0)
    , _debugEnabled(false)
{
    memset(_hmacKey, 0, SECURITY_HMAC_KEY_SIZE);
    memset(&_stats, 0, sizeof(SecurityStats));
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN
 * =================================================================================
 */

void Security::begin() {
    DEBUG_PRINT("begin() — Inicializando módulo de segurança (ESP2)\n");
    
    memcpy(_hmacKey, DEFAULT_HMAC_KEY, SECURITY_HMAC_KEY_SIZE);
    _keyLoaded = true;
    
    _lastSeqNum = 0;
    _seqBitmap = 0;
    _lockdown = false;
    _violationCount = 0;
    _flightState = STATE_GROUND_DISARMED;
    _currentLevel = SECURITY_LEVEL_NORMAL;
    
    memset(&_stats, 0, sizeof(SecurityStats));
    
    DEBUG_PRINT("begin() — Security inicializado\n");
}

/* =================================================================================
 * LOAD KEY
 * =================================================================================
 */

bool Security::loadKey(const uint8_t* key, size_t keyLen) {
    if (key == nullptr || keyLen != SECURITY_HMAC_KEY_SIZE) {
        DEBUG_PRINT("loadKey() — Chave inválida\n");
        return false;
    }
    
    memcpy(_hmacKey, key, SECURITY_HMAC_KEY_SIZE);
    _keyLoaded = true;
    DEBUG_PRINT("loadKey() — Chave HMAC carregada\n");
    return true;
}

/* =================================================================================
 * SERIALIZE FOR HMAC
 * =================================================================================
 */

size_t Security::_serializeForHMAC(const TLVMessage& msg, uint8_t* output, size_t maxLen) {
    size_t offset = 0;
    
    if (offset + 3 > maxLen) return 0;
    output[offset++] = msg.startByte;
    output[offset++] = msg.msgID;
    output[offset++] = msg.tlvCount;
    
    for (uint8_t i = 0; i < msg.tlvCount; i++) {
        if (offset + 2 + msg.tlvs[i].len > maxLen) return 0;
        output[offset++] = msg.tlvs[i].id;
        output[offset++] = msg.tlvs[i].len;
        memcpy(&output[offset], msg.tlvs[i].data, msg.tlvs[i].len);
        offset += msg.tlvs[i].len;
    }
    
    return offset;
}

/* =================================================================================
 * HMAC-SHA256
 * =================================================================================
 */

void Security::_hmacSHA256(const uint8_t* key, size_t keyLen,
                           const uint8_t* data, size_t dataLen,
                           uint8_t* output) {
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    
    if (md_info == nullptr) {
        memset(output, 0, SECURITY_HMAC_SIZE);
        return;
    }
    
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, md_info, 1);
    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, data, dataLen);
    mbedtls_md_hmac_finish(&ctx, output);
    mbedtls_md_free(&ctx);
}

/* =================================================================================
 * CHECK HMAC
 * =================================================================================
 */

bool Security::_checkHMAC(const TLVMessage& msg, const uint8_t* hmac) {
    if (!_keyLoaded) return false;
    
    uint8_t serialized[512];
    size_t len = _serializeForHMAC(msg, serialized, sizeof(serialized));
    if (len == 0) return false;
    
    uint8_t expected[SECURITY_HMAC_SIZE];
    _hmacSHA256(_hmacKey, SECURITY_HMAC_KEY_SIZE, serialized, len, expected);
    
    uint8_t diff = 0;
    for (uint8_t i = 0; i < SECURITY_HMAC_SIZE; i++) {
        diff |= (hmac[i] ^ expected[i]);
    }
    
    return (diff == 0);
}

/* =================================================================================
 * CHECK SEQUENCE
 * =================================================================================
 */

bool Security::_checkSequence(uint32_t seqNum) {
    if (seqNum == 0) return false;
    
    if (seqNum > _lastSeqNum) {
        uint32_t diff = seqNum - _lastSeqNum;
        if (diff > SECURITY_SEQ_WINDOW) {
            _seqBitmap = 0;
        } else {
            _seqBitmap <<= diff;
        }
        _seqBitmap |= 0x01;
        _lastSeqNum = seqNum;
        return true;
    }
    
    uint32_t offset = _lastSeqNum - seqNum;
    if (offset >= SECURITY_SEQ_WINDOW) return false;
    
    if ((_seqBitmap >> offset) & 0x01) return false;
    
    _seqBitmap |= (0x01 << offset);
    return true;
}

/* =================================================================================
 * CHECK RATE LIMIT
 * =================================================================================
 */

bool Security::_checkRateLimit(uint8_t cmdID) {
    uint32_t now = millis();
    uint32_t* lastTime = nullptr;
    uint32_t limitMs = 0;
    
    if (cmdID == CMD_ARM || cmdID == CMD_DISARM || cmdID == CMD_REBOOT) {
        lastTime = &_lastCriticalMs;
        limitMs = RATE_LIMIT_CRITICAL_MS;
    } else if (cmdID >= CMD_SET_ROLL && cmdID <= CMD_SET_ALT_TARGET) {
        lastTime = &_lastControlMs;
        limitMs = RATE_LIMIT_CONTROL_MS;
    } else {
        lastTime = &_lastMaintenanceMs;
        limitMs = RATE_LIMIT_MAINTENANCE_MS;
    }
    
    if (now - *lastTime < limitMs) return false;
    
    *lastTime = now;
    return true;
}

/* =================================================================================
 * CHECK COMMAND SANITY
 * =================================================================================
 */

bool Security::_checkCommandSanity(uint8_t cmdID, float value) {
    switch (cmdID) {
        case CMD_SET_ROLL:
            return (value >= -SANITY_ROLL_MAX && value <= SANITY_ROLL_MAX);
        case CMD_SET_PITCH:
            return (value >= -SANITY_PITCH_MAX && value <= SANITY_PITCH_MAX);
        case CMD_SET_YAW:
            return (value >= -SANITY_YAW_MAX && value <= SANITY_YAW_MAX);
        case CMD_SET_ALT_TARGET:
            return (value >= 0.0f && value <= SANITY_ALT_MAX);
        case CMD_SET_THROTTLE:
            return (value >= 0.0f && value <= SANITY_THROTTLE_MAX);
        default:
            return true;
    }
}

/* =================================================================================
 * CHECK COMMAND STATE
 * =================================================================================
 */

bool Security::_checkCommandState(uint8_t cmdID) {
    switch (cmdID) {
        case CMD_ARM:
            return (_flightState == STATE_GROUND_DISARMED);
        case CMD_DISARM:
            return (_flightState != STATE_GROUND_DISARMED);
        case CMD_SET_ROLL:
        case CMD_SET_PITCH:
        case CMD_SET_YAW:
        case CMD_SET_THROTTLE:
            return (_flightState == STATE_FLYING || _flightState == STATE_TAKEOFF ||
                    _flightState == STATE_LANDING || _flightState == STATE_GROUND_ARMED);
        case CMD_REBOOT:
            return (_flightState == STATE_GROUND_DISARMED || _flightState == STATE_GROUND_ARMED);
        default:
            return true;
    }
}

/* =================================================================================
 * CHECK ENVELOPE
 * =================================================================================
 */

bool Security::_checkEnvelope(uint8_t cmdID, float value) {
    float rollLimit = SANITY_ROLL_MAX;
    float pitchLimit = SANITY_PITCH_MAX;
    
    switch (_currentLevel) {
        case SECURITY_LEVEL_NORMAL:
            rollLimit = 45.0f;
            pitchLimit = 35.0f;
            break;
        case SECURITY_LEVEL_CAUTION:
            rollLimit = 30.0f;
            pitchLimit = 25.0f;
            break;
        case SECURITY_LEVEL_WARNING:
            rollLimit = 15.0f;
            pitchLimit = 15.0f;
            break;
        case SECURITY_LEVEL_CRITICAL:
        case SECURITY_LEVEL_LOCKDOWN:
            rollLimit = 10.0f;
            pitchLimit = 10.0f;
            break;
    }
    
    switch (cmdID) {
        case CMD_SET_ROLL:
            return (value >= -rollLimit && value <= rollLimit);
        case CMD_SET_PITCH:
            return (value >= -pitchLimit && value <= pitchLimit);
        default:
            return true;
    }
}

/* =================================================================================
 * CHECK SENSOR SANITY
 * =================================================================================
 */

bool Security::_checkSensorSanity(uint8_t fieldID, float value) {
    switch (fieldID) {
        case FLD_ROLL:
        case FLD_PITCH:
            return (value >= -180.0f && value <= 180.0f);
        case FLD_YAW:
        case FLD_HEADING:
            return (value >= 0.0f && value <= 360.0f);
        case FLD_BATT_V:
            return (value >= SANITY_BATT_V_MIN && value <= SANITY_BATT_V_MAX);
        default:
            return true;
    }
}

/* =================================================================================
 * RECORD VIOLATION
 * =================================================================================
 */

void Security::_recordViolation(SecurityResult reason) {
    _violationCount++;
    _stats.lastViolationTime = millis();
    _stats.lastViolationReason = reason;
    
    DEBUG_PRINT("_recordViolation() — Violação #%d/%d (tipo=%d)\n", 
                _violationCount, SECURITY_MAX_VIOLATIONS, reason);
    
    _evaluateFailSecure(reason);
    
    if (_violationCount >= SECURITY_MAX_VIOLATIONS) {
        triggerLockdown("Máximo de violações de segurança atingido");
    }
    
    _updateSecurityLevel();
}

/* =================================================================================
 * EVALUATE FAIL SECURE
 * =================================================================================
 */

void Security::_evaluateFailSecure(SecurityResult reason) {
    if (!_config.enableFailSecure) return;
    
    switch (reason) {
        case SEC_INVALID_HMAC:
        case SEC_REPLAY_DETECTED:
            DEBUG_PRINT("_evaluateFailSecure() — Ameaça ativa detectada\n");
            break;
        case SEC_SANITY_FAILED:
        case SEC_ENVELOPE_EXCEEDED:
            DEBUG_PRINT("_evaluateFailSecure() — Limite de segurança excedido\n");
            break;
        default:
            break;
    }
}

/* =================================================================================
 * UPDATE SECURITY LEVEL
 * =================================================================================
 */

void Security::_updateSecurityLevel() {
    SecurityLevel newLevel = SECURITY_LEVEL_NORMAL;
    
    if (_lockdown) {
        newLevel = SECURITY_LEVEL_LOCKDOWN;
    } else if (_violationCount >= 8) {
        newLevel = SECURITY_LEVEL_CRITICAL;
    } else if (_violationCount >= 5) {
        newLevel = SECURITY_LEVEL_WARNING;
    } else if (_violationCount >= 3) {
        newLevel = SECURITY_LEVEL_CAUTION;
    } else {
        newLevel = SECURITY_LEVEL_NORMAL;
    }
    
    if (_currentLevel != newLevel) {
        DEBUG_PRINT("_updateSecurityLevel() — Nível alterado: %d → %d\n", _currentLevel, newLevel);
        _currentLevel = newLevel;
    }
}

/* =================================================================================
 * VERIFY PACKET
 * =================================================================================
 */

SecurityResult Security::verifyPacket(const TLVMessage& msg, uint32_t seqNum, const uint8_t* hmac) {
    _stats.packetsVerified++;
    
    if (_lockdown) {
        return SEC_LOCKDOWN;
    }
    
    if (!_checkHMAC(msg, hmac)) {
        _stats.hmacFailures++;
        _recordViolation(SEC_INVALID_HMAC);
        return SEC_INVALID_HMAC;
    }
    
    if (!_checkSequence(seqNum)) {
        _stats.replayAttempts++;
        _recordViolation(SEC_REPLAY_DETECTED);
        return SEC_REPLAY_DETECTED;
    }
    
    if (msg.msgID == MSG_COMMAND && msg.tlvCount > 0) {
        uint8_t cmdID = msg.tlvs[0].id;
        if (!_checkRateLimit(cmdID)) {
            _stats.rateLimitHits++;
            return SEC_RATE_LIMITED;
        }
    }
    
    resetViolationCount();
    return SEC_OK;
}

/* =================================================================================
 * VERIFY COMMAND
 * =================================================================================
 */

SecurityResult Security::verifyCommand(uint8_t cmdID, float value) {
    if (!_checkCommandSanity(cmdID, value)) {
        _stats.sanityFailures++;
        _recordViolation(SEC_SANITY_FAILED);
        return SEC_SANITY_FAILED;
    }
    
    if (!_checkCommandState(cmdID)) {
        _stats.wrongStateBlocks++;
        _recordViolation(SEC_WRONG_STATE);
        return SEC_WRONG_STATE;
    }
    
    if (!_checkEnvelope(cmdID, value)) {
        _stats.envelopeViolations++;
        _recordViolation(SEC_ENVELOPE_EXCEEDED);
        return SEC_ENVELOPE_EXCEEDED;
    }
    
    return SEC_OK;
}

/* =================================================================================
 * VERIFY SENSOR DATA
 * =================================================================================
 */

SecurityResult Security::verifySensorData(uint8_t fieldID, float value) {
    if (!_checkSensorSanity(fieldID, value)) {
        _stats.sanityFailures++;
        return SEC_SANITY_FAILED;
    }
    return SEC_OK;
}

/* =================================================================================
 * SET FLIGHT STATE
 * =================================================================================
 */

void Security::setFlightState(FlightState state) {
    if (_flightState != state) {
        DEBUG_PRINT("setFlightState() — Estado alterado: %d → %d\n", _flightState, state);
        _flightState = state;
    }
}

/* =================================================================================
 * GET FLIGHT STATE
 * =================================================================================
 */

FlightState Security::getFlightState() const {
    return _flightState;
}

/* =================================================================================
 * GET SECURITY LEVEL
 * =================================================================================
 */

SecurityLevel Security::getSecurityLevel() const {
    return _currentLevel;
}

/* =================================================================================
 * COMPUTE HMAC
 * =================================================================================
 */

void Security::computeHMAC(const uint8_t* data, size_t len, uint8_t* output) {
    if (!_keyLoaded) {
        memset(output, 0, SECURITY_HMAC_SIZE);
        return;
    }
    
    _hmacSHA256(_hmacKey, SECURITY_HMAC_KEY_SIZE, data, len, output);
}

/* =================================================================================
 * TRIGGER LOCKDOWN
 * =================================================================================
 */

void Security::triggerLockdown(const char* reason) {
    if (!_lockdown) {
        _lockdown = true;
        _stats.lockdownEvents++;
        _currentLevel = SECURITY_LEVEL_LOCKDOWN;
        
        DEBUG_PRINT("⚠️⚠️⚠️ LOCKDOWN ATIVADO ⚠️⚠️⚠️\n");
        DEBUG_PRINT("triggerLockdown() — Razão: %s\n", reason ? reason : "não especificada");
    }
}

/* =================================================================================
 * IS IN LOCKDOWN
 * =================================================================================
 */

bool Security::isInLockdown() const {
    return _lockdown;
}

/* =================================================================================
 * RESET VIOLATION COUNT
 * =================================================================================
 */

void Security::resetViolationCount() {
    if (_violationCount > 0) {
        DEBUG_PRINT("resetViolationCount() — Resetando contador de violações (%d)\n", _violationCount);
        _violationCount = 0;
        _updateSecurityLevel();
    }
}

/* =================================================================================
 * GET STATS
 * =================================================================================
 */

const SecurityStats& Security::getStats() const {
    return _stats;
}

/* =================================================================================
 * RESET STATS
 * =================================================================================
 */

void Security::resetStats() {
    memset(&_stats, 0, sizeof(SecurityStats));
    DEBUG_PRINT("resetStats() — Estatísticas reiniciadas\n");
}

/* =================================================================================
 * CONFIGURE
 * =================================================================================
 */

void Security::configure(const SecurityConfig& config) {
    _config = config;
    DEBUG_PRINT("configure() — FailSecure=%d, Watchdog=%d\n",
                config.enableFailSecure, config.enableWatchdog);
}

/* =================================================================================
 * SET DEBUG
 * =================================================================================
 */

void Security::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* securityResultToString(SecurityResult result) {
    switch (result) {
        case SEC_OK:                return "OK";
        case SEC_INVALID_HMAC:      return "INVALID_HMAC";
        case SEC_REPLAY_DETECTED:   return "REPLAY_DETECTED";
        case SEC_RATE_LIMITED:      return "RATE_LIMITED";
        case SEC_SANITY_FAILED:     return "SANITY_FAILED";
        case SEC_LOCKDOWN:          return "LOCKDOWN";
        case SEC_WRONG_STATE:       return "WRONG_STATE";
        case SEC_ENVELOPE_EXCEEDED: return "ENVELOPE_EXCEEDED";
        case SEC_NO_COMMAND:        return "NO_COMMAND";
        default:                    return "UNKNOWN";
    }
}

const char* flightStateToString(FlightState state) {
    switch (state) {
        case STATE_GROUND_DISARMED: return "GROUND_DISARMED";
        case STATE_GROUND_ARMED:    return "GROUND_ARMED";
        case STATE_TAKEOFF:         return "TAKEOFF";
        case STATE_FLYING:          return "FLYING";
        case STATE_LANDING:         return "LANDING";
        case STATE_FAILSAFE:        return "FAILSAFE";
        default:                    return "UNKNOWN";
    }
}

const char* securityLevelToString(SecurityLevel level) {
    switch (level) {
        case SECURITY_LEVEL_NORMAL:    return "NORMAL";
        case SECURITY_LEVEL_CAUTION:   return "CAUTION";
        case SECURITY_LEVEL_WARNING:   return "WARNING";
        case SECURITY_LEVEL_CRITICAL:  return "CRITICAL";
        case SECURITY_LEVEL_LOCKDOWN:  return "LOCKDOWN";
        default:                       return "UNKNOWN";
    }
}
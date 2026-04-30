/**
 * =================================================================================
 * SECURITY.CPP — IMPLEMENTAÇÃO DO MÓDULO DE SEGURANÇA
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-17
 * VERSÃO:     2.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. HMAC-SHA256: Utiliza mbedtls (hardware-accelerated no ESP32)
 * 2. Anti-replay: Janela deslizante de 32 bits
 * 3. Rate limiting: Três categorias independentes
 * 4. FailSecure: Níveis de segurança progressivos
 * 
 * =================================================================================
 * BIBLIOTECAS NECESSÁRIAS
 * =================================================================================
 * 
 * Este módulo requer mbedtls para HMAC-SHA256:
 *   #include <mbedtls/md.h>
 * 
 * No PlatformIO, adicionar ao platformio.ini:
 *   lib_deps = mbedtls
 * 
 * =================================================================================
 */

#include "Security.h"
#include <esp_random.h>

/* =================================================================================
 * INCLUSÃO DO MBEDTLS PARA HMAC-SHA256
 * =================================================================================
 */

#include <mbedtls/md.h>

/* =================================================================================
 * DEBUG — Macro configurável
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
 * 
 * ⚠️⚠️⚠️ NUNCA usar esta chave em produção! ⚠️⚠️⚠️
 * 
 * Em produção, a chave deve ser:
 *   • Gerada aleatoriamente no primeiro boot
 *   • Armazenada em NVS (Non-Volatile Storage) ou eFuse
 *   • Partilhada de forma segura com a Ground Station
 *   • Única por aeronave (não reutilizada)
 * 
 * Esta chave de desenvolvimento é pública e conhecida — NÃO É SEGURA!
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

Security::Security() {
    memset(_hmacKey, 0, SECURITY_HMAC_KEY_SIZE);
    _keyLoaded = false;
    
    _lastSeqNum = 0;
    _seqBitmap = 0;
    
    _lastCriticalMs = 0;
    _lastControlMs = 0;
    _lastMaintenanceMs = 0;
    
    _flightState = STATE_GROUND_DISARMED;
    _currentLevel = SECURITY_LEVEL_NORMAL;
    
    _gsAuthenticated = false;
    _currentChallenge = 0;
    
    _lockdown = false;
    _violationCount = 0;
    _debugEnabled = false;
    
    memset(&_stats, 0, sizeof(SecurityStats));
    
    _config.enableFailSecure = true;
    _config.enableGeofence = false;
    _config.enableWatchdog = true;
    _config.geofenceRadius = 500.0f;
    _config.geofenceLat = 0.0f;
    _config.geofenceLon = 0.0f;
    
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO DO MÓDULO
 * =================================================================================
 */

void Security::begin() {
    DEBUG_PRINT("begin() — Inicializando módulo de segurança...\n");
    
    /* Carregar chave de desenvolvimento (substituir em produção) */
    memcpy(_hmacKey, DEFAULT_HMAC_KEY, SECURITY_HMAC_KEY_SIZE);
    _keyLoaded = true;
    
    /* Estado inicial seguro */
    _flightState = STATE_GROUND_DISARMED;
    _currentLevel = SECURITY_LEVEL_NORMAL;
    _lockdown = false;
    _violationCount = 0;
    _gsAuthenticated = false;
    
    /* Gerar challenge inicial */
    _currentChallenge = generateChallenge();
    
    DEBUG_PRINT("begin() — Security inicializado. Challenge: 0x%08X\n", _currentChallenge);
    DEBUG_PRINT("begin() — Princípio Fail Secure ativo\n");
}

/* =================================================================================
 * LOAD KEY — CARREGA CHAVE HMAC PERSONALIZADA
 * =================================================================================
 */

bool Security::loadKey(const uint8_t* key, size_t keyLen) {
    if (key == nullptr || keyLen != SECURITY_HMAC_KEY_SIZE) {
        DEBUG_PRINT("loadKey() — Chave inválida (tamanho=%zu, esperado=%d)\n", 
                    keyLen, SECURITY_HMAC_KEY_SIZE);
        return false;
    }
    
    memcpy(_hmacKey, key, SECURITY_HMAC_KEY_SIZE);
    _keyLoaded = true;
    
    DEBUG_PRINT("loadKey() — Chave HMAC carregada com sucesso\n");
    return true;
}

/* =================================================================================
 * VERIFY PACKET — VERIFICAÇÃO COMPLETA DE PACOTE
 * =================================================================================
 */

SecurityResult Security::verifyPacket(const TLVMessage& msg, uint32_t seqNum, const uint8_t* hmac) {
    _stats.packetsVerified++;
    
    /* =====================================================================
     * 1. LOCKDOWN — Última linha de defesa
     * =====================================================================
     * Em lockdown, apenas MSG_FAILSAFE é aceite.
     */
    if (_lockdown) {
        if (msg.msgID != MSG_FAILSAFE) {
            DEBUG_PRINT("verifyPacket() — LOCKDOWN ativo, pacote 0x%02X rejeitado\n", msg.msgID);
            return SEC_LOCKDOWN;
        }
        DEBUG_PRINT("verifyPacket() — LOCKDOWN ativo, mas MSG_FAILSAFE aceite\n");
    }
    
    /* =====================================================================
     * 2. AUTENTICAÇÃO DA GROUND STATION
     * =====================================================================
     * GS deve autenticar-se antes de enviar comandos (exceto MSG_FAILSAFE
     * e MSG_HEARTBEAT que são críticos para segurança).
     */
    if (!_gsAuthenticated && msg.msgID != MSG_FAILSAFE && msg.msgID != MSG_HEARTBEAT) {
        DEBUG_PRINT("verifyPacket() — GS não autenticada, pacote rejeitado\n");
        _recordViolation(SEC_AUTH_REQUIRED);
        return SEC_AUTH_REQUIRED;
    }
    
    /* =====================================================================
     * 3. HMAC — AUTENTICIDADE E INTEGRIDADE
     * =====================================================================
     * Verifica se a mensagem foi realmente assinada pela GS legítima.
     */
    if (!_checkHMAC(msg, hmac)) {
        _stats.hmacFailures++;
        DEBUG_PRINT("verifyPacket() — HMAC inválido para pacote 0x%02X\n", msg.msgID);
        
        if (msg.msgID != MSG_FAILSAFE) {
            _recordViolation(SEC_INVALID_HMAC);
        }
        return SEC_INVALID_HMAC;
    }
    
    /* =====================================================================
     * 4. ANTI-REPLAY — VERIFICAÇÃO DE SEQUÊNCIA
     * =====================================================================
     * Impede que um atacante grave e reenvie pacotes legítimos.
     */
    if (!_checkSequence(seqNum)) {
        _stats.replayAttempts++;
        DEBUG_PRINT("verifyPacket() — Replay detectado! seqNum=%u\n", seqNum);
        _recordViolation(SEC_REPLAY_DETECTED);
        return SEC_REPLAY_DETECTED;
    }
    
    /* =====================================================================
     * 5. RATE LIMITING — PROTEÇÃO CONTRA FLOOD
     * =====================================================================
     * Impede que um atacante sobrecarregue o sistema com muitos comandos.
     */
    if (msg.msgID == MSG_COMMAND && msg.tlvCount > 0) {
        uint8_t cmdID = msg.tlvs[0].id;
        if (!_checkRateLimit(cmdID)) {
            _stats.rateLimitHits++;
            DEBUG_PRINT("verifyPacket() — Rate limit excedido para cmd=0x%02X\n", cmdID);
            _recordViolation(SEC_RATE_LIMITED);
            return SEC_RATE_LIMITED;
        }
    }
    
    /* Pacote válido — resetar contador de violações */
    resetViolationCount();
    
    DEBUG_PRINT("verifyPacket() — Pacote 0x%02X válido\n", msg.msgID);
    return SEC_OK;
}

/* =================================================================================
 * VERIFY COMMAND — SANIDADE E CONTEXTO DE COMANDO
 * =================================================================================
 */

SecurityResult Security::verifyCommand(uint8_t cmdID, float value) {
    /* Verificar sanidade física (limites absolutos) */
    if (!_checkCommandSanity(cmdID, value)) {
        _stats.sanityFailures++;
        DEBUG_PRINT("verifyCommand() — Sanidade falhou: cmd=0x%02X, val=%.2f\n", cmdID, value);
        _recordViolation(SEC_SANITY_FAILED);
        return SEC_SANITY_FAILED;
    }
    
    /* Verificar contexto (estado de voo) */
    if (!_checkCommandState(cmdID)) {
        _stats.wrongStateBlocks++;
        DEBUG_PRINT("verifyCommand() — Estado errado: cmd=0x%02X, state=%d\n", 
                    cmdID, _flightState);
        _recordViolation(SEC_WRONG_STATE);
        return SEC_WRONG_STATE;
    }
    
    /* Verificar envelope de voo (limites estruturais) */
    if (!_checkEnvelope(cmdID, value)) {
        _stats.envelopeViolations++;
        DEBUG_PRINT("verifyCommand() — Envelope excedido: cmd=0x%02X, val=%.2f\n", cmdID, value);
        _recordViolation(SEC_ENVELOPE_EXCEEDED);
        return SEC_ENVELOPE_EXCEEDED;
    }
    
    return SEC_OK;
}

/* =================================================================================
 * VERIFY SENSOR DATA — SANIDADE DE SENSORES
 * =================================================================================
 */

SecurityResult Security::verifySensorData(uint8_t fieldID, float value) {
    if (!_checkSensorSanity(fieldID, value)) {
        _stats.sanityFailures++;
        DEBUG_PRINT("verifySensorData() — Sanidade falhou: field=0x%02X, val=%.2f\n", fieldID, value);
        return SEC_SANITY_FAILED;
    }
    return SEC_OK;
}

/* =================================================================================
 * SET FLIGHT STATE — ATUALIZA ESTADO DE VOO
 * =================================================================================
 */

void Security::setFlightState(FlightState state) {
    if (_flightState != state) {
        DEBUG_PRINT("setFlightState() — Estado alterado: %d → %d\n", _flightState, state);
        _flightState = state;
        
        /* Resetar rate limiting ao mudar de estado (evita falsos positivos) */
        _lastCriticalMs = 0;
        _lastControlMs = 0;
        _lastMaintenanceMs = 0;
    }
}

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
 * GENERATE CHALLENGE — GERA DESAFIO PARA AUTENTICAÇÃO
 * =================================================================================
 */

uint32_t Security::generateChallenge() {
    _currentChallenge = esp_random();   /* Hardware RNG do ESP32 */
    _gsAuthenticated = false;
    DEBUG_PRINT("generateChallenge() — Novo challenge: 0x%08X\n", _currentChallenge);
    return _currentChallenge;
}

/* =================================================================================
 * VERIFY CHALLENGE RESPONSE — VERIFICA RESPOSTA DA GS
 * =================================================================================
 */

bool Security::verifyChallengeResponse(const uint8_t* response, size_t len) {
    if (!_keyLoaded) {
        DEBUG_PRINT("verifyChallengeResponse() — Chave não carregada!\n");
        return false;
    }
    
    if (len != SECURITY_HMAC_SIZE) {
        DEBUG_PRINT("verifyChallengeResponse() — Tamanho inválido: %zu (esperado %d)\n", 
                    len, SECURITY_HMAC_SIZE);
        return false;
    }
    
    /* Calcular HMAC esperado do challenge */
    uint8_t expected[SECURITY_HMAC_SIZE];
    uint8_t challengeBytes[4];
    challengeBytes[0] = (_currentChallenge >> 24) & 0xFF;
    challengeBytes[1] = (_currentChallenge >> 16) & 0xFF;
    challengeBytes[2] = (_currentChallenge >>  8) & 0xFF;
    challengeBytes[3] = (_currentChallenge      ) & 0xFF;
    
    _hmacSHA256(_hmacKey, SECURITY_HMAC_KEY_SIZE, challengeBytes, 4, expected);
    
    /* Comparação em tempo constante (evita timing attacks) */
    uint8_t diff = 0;
    for (uint8_t i = 0; i < SECURITY_HMAC_SIZE; i++) {
        diff |= (response[i] ^ expected[i]);
    }
    
    if (diff == 0) {
        _gsAuthenticated = true;
        _violationCount = 0;  /* Reset violações após autenticação bem-sucedida */
        DEBUG_PRINT("verifyChallengeResponse() — GS autenticada com sucesso!\n");
        return true;
    }
    
    DEBUG_PRINT("verifyChallengeResponse() — Resposta inválida\n");
    _recordViolation(SEC_AUTH_REQUIRED);
    return false;
}

bool Security::isGSAuthenticated() const {
    return _gsAuthenticated;
}

/* =================================================================================
 * COMPUTE HMAC — CALCULA HMAC PARA PACOTES DE SAÍDA
 * =================================================================================
 */

void Security::computeHMAC(const uint8_t* data, size_t len, uint8_t* output) {
    if (!_keyLoaded) {
        DEBUG_PRINT("computeHMAC() — Chave não carregada!\n");
        memset(output, 0, SECURITY_HMAC_SIZE);
        return;
    }
    
    _hmacSHA256(_hmacKey, SECURITY_HMAC_KEY_SIZE, data, len, output);
}

/* =================================================================================
 * LOCKDOWN — ATIVA BLOQUEIO TOTAL
 * =================================================================================
 */

void Security::triggerLockdown(const char* reason) {
    if (!_lockdown) {
        _lockdown = true;
        _stats.lockdownEvents++;
        _currentLevel = SECURITY_LEVEL_LOCKDOWN;
        
        DEBUG_PRINT("⚠️⚠️⚠️ LOCKDOWN ATIVADO ⚠️⚠️⚠️\n");
        DEBUG_PRINT("triggerLockdown() — Razão: %s\n", reason ? reason : "não especificada");
        DEBUG_PRINT("triggerLockdown() — Apenas MSG_FAILSAFE será aceite\n");
    }
}

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
 * STATISTICS
 * =================================================================================
 */

const SecurityStats& Security::getStats() const {
    return _stats;
}

void Security::resetStats() {
    memset(&_stats, 0, sizeof(SecurityStats));
    DEBUG_PRINT("resetStats() — Estatísticas reiniciadas\n");
}

/* =================================================================================
 * CONFIGURE — CONFIGURAÇÃO AVANÇADA
 * =================================================================================
 */

void Security::configure(const SecurityConfig& config) {
    _config = config;
    DEBUG_PRINT("configure() — FailSecure=%d, Geofence=%d, Watchdog=%d, Raio=%.1fm\n",
                config.enableFailSecure, config.enableGeofence, 
                config.enableWatchdog, config.geofenceRadius);
}

void Security::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — CHECK HMAC
 * =================================================================================
 */

bool Security::_checkHMAC(const TLVMessage& msg, const uint8_t* hmac) {
    if (!_keyLoaded) {
        DEBUG_PRINT("_checkHMAC() — Chave não carregada!\n");
        return false;
    }
    
    /* Serializar mensagem para cálculo do HMAC */
    uint8_t serialized[1024];
    size_t serializedLen = _serializeForHMAC(msg, serialized, sizeof(serialized));
    
    if (serializedLen == 0) {
        DEBUG_PRINT("_checkHMAC() — Falha na serialização\n");
        return false;
    }
    
    /* Calcular HMAC esperado */
    uint8_t expected[SECURITY_HMAC_SIZE];
    _hmacSHA256(_hmacKey, SECURITY_HMAC_KEY_SIZE, serialized, serializedLen, expected);
    
    /* Comparação em tempo constante */
    uint8_t diff = 0;
    for (uint8_t i = 0; i < SECURITY_HMAC_SIZE; i++) {
        diff |= (hmac[i] ^ expected[i]);
    }
    
    return (diff == 0);
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — ANTI-REPLAY
 * =================================================================================
 * 
 * Janela deslizante de 32 bits:
 *   - Cada bit representa uma sequência recente
 *   - Bit 0 = sequência atual (_lastSeqNum)
 *   - Bit 1 = _lastSeqNum - 1, etc.
 * 
 * Quando uma nova sequência chega:
 *   - Se for > _lastSeqNum: desloca a janela e marca o novo bit
 *   - Se for <= _lastSeqNum: verifica se o bit já está marcado (replay)
 */

bool Security::_checkSequence(uint32_t seqNum) {
    if (seqNum == 0) {
        DEBUG_PRINT("_checkSequence() — Sequência zero inválida\n");
        return false;
    }
    
    if (seqNum > _lastSeqNum) {
        /* Nova sequência — desloca a janela */
        uint32_t diff = seqNum - _lastSeqNum;
        
        if (diff > SECURITY_SEQ_WINDOW) {
            /* Salto muito grande — possível ataque ou reinicialização */
            DEBUG_PRINT("_checkSequence() — Salto grande: %u → %u\n", _lastSeqNum, seqNum);
            /* Ainda assim aceitamos, mas resetamos a janela */
            _seqBitmap = 0;
        } else {
            /* Desloca a janela para a direita */
            _seqBitmap <<= diff;
        }
        
        /* Marca o bit correspondente à nova sequência */
        _seqBitmap |= 0x01;
        _lastSeqNum = seqNum;
        
        return true;
    }
    
    /* Sequência <= última — verificar se já foi vista */
    uint32_t offset = _lastSeqNum - seqNum;
    
    if (offset >= SECURITY_SEQ_WINDOW) {
        /* Fora da janela — muito antigo, rejeitar */
        DEBUG_PRINT("_checkSequence() — Sequência muito antiga: %u (última=%u)\n", 
                    seqNum, _lastSeqNum);
        return false;
    }
    
    /* Verificar se o bit correspondente já está marcado */
    if ((_seqBitmap >> offset) & 0x01) {
        DEBUG_PRINT("_checkSequence() — REPLAY! seqNum=%u já visto\n", seqNum);
        return false;
    }
    
    /* Marcar o bit e aceitar (pacote fora de ordem, mas não replay) */
    _seqBitmap |= (0x01 << offset);
    return true;
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — RATE LIMITING
 * =================================================================================
 */

bool Security::_checkRateLimit(uint8_t cmdID) {
    uint32_t now = millis();
    uint32_t* lastTime = nullptr;
    uint32_t limitMs = 0;
    
    /* Comandos críticos (segurança) */
    if (cmdID == CMD_ARM || cmdID == CMD_DISARM || 
        cmdID == CMD_REBOOT || cmdID == CMD_SHUTDOWN) {
        lastTime = &_lastCriticalMs;
        limitMs = RATE_LIMIT_CRITICAL_MS;
    }
    /* Comandos de controlo de voo (alta frequência) */
    else if (cmdID >= CMD_SET_ROLL && cmdID <= CMD_SET_YAW) {
        lastTime = &_lastControlMs;
        limitMs = RATE_LIMIT_CONTROL_MS;
    }
    /* Comandos de manutenção/calibração */
    else if (cmdID == CMD_SET_PARAM || cmdID == CMD_SENSOR_CALIB || 
             cmdID == CMD_ACTUATOR_CALIB) {
        lastTime = &_lastMaintenanceMs;
        limitMs = RATE_LIMIT_MAINTENANCE_MS;
    }
    else {
        /* Comandos desconhecidos — rate limit mais restritivo */
        lastTime = &_lastMaintenanceMs;
        limitMs = RATE_LIMIT_MAINTENANCE_MS;
    }
    
    if (now - *lastTime < limitMs) {
        DEBUG_PRINT("_checkRateLimit() — cmd=0x%02X, limite=%lu ms, passaram=%lu ms\n",
                    cmdID, limitMs, now - *lastTime);
        return false;
    }
    
    *lastTime = now;
    return true;
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — SANIDADE DE COMANDOS
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
            return (value >= MIN_ALTITUDE && value <= SANITY_ALT_MAX);
            
        case CMD_SET_THROTTLE:
            return (value >= 0.0f && value <= SANITY_THROTTLE_MAX);
            
        /* Comandos sem parâmetros são sempre sãos */
        case CMD_ARM:
        case CMD_DISARM:
        case CMD_REBOOT:
        case CMD_SHUTDOWN:
        case CMD_RTL:
            return true;
            
        default:
            /* Comandos desconhecidos — rejeitar por segurança */
            return false;
    }
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — CONTEXTO DE ESTADO
 * =================================================================================
 */

bool Security::_checkCommandState(uint8_t cmdID) {
    switch (cmdID) {
        case CMD_ARM:
            /* Só pode armar em solo, desarmado */
            return (_flightState == STATE_GROUND_DISARMED);
            
        case CMD_DISARM:
            /* Pode desarmar em qualquer estado exceto já desarmado */
            return (_flightState != STATE_GROUND_DISARMED);
            
        case CMD_SET_ROLL:
        case CMD_SET_PITCH:
        case CMD_SET_YAW:
        case CMD_SET_THROTTLE:
        case CMD_SET_ALT_TARGET:
            /* Comandos de controlo apenas em voo ou armado em solo */
            return (_flightState == STATE_FLYING || 
                    _flightState == STATE_TAKEOFF ||
                    _flightState == STATE_LANDING ||
                    _flightState == STATE_GROUND_ARMED);
            
        case CMD_REBOOT:
        case CMD_SHUTDOWN:
            /* Reboot e shutdown apenas em solo (segurança!) */
            return (_flightState == STATE_GROUND_DISARMED || 
                    _flightState == STATE_GROUND_ARMED);
            
        default:
            return true;
    }
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — ENVELOPE DE VOO
 * =================================================================================
 */

bool Security::_checkEnvelope(uint8_t cmdID, float value) {
    /* O envelope de voo depende do nível de segurança */
    float rollLimit = SANITY_ROLL_MAX;
    float pitchLimit = SANITY_PITCH_MAX;
    
    switch (_currentLevel) {
        case SECURITY_LEVEL_NORMAL:
            rollLimit = 45.0f;   /* 45° máximo em voo normal */
            pitchLimit = 35.0f;
            break;
        case SECURITY_LEVEL_CAUTION:
            rollLimit = 30.0f;   /* Reduzido */
            pitchLimit = 25.0f;
            break;
        case SECURITY_LEVEL_WARNING:
            rollLimit = 15.0f;   /* Muito reduzido */
            pitchLimit = 15.0f;
            break;
        case SECURITY_LEVEL_CRITICAL:
        case SECURITY_LEVEL_LOCKDOWN:
            rollLimit = 10.0f;   /* Apenas movimentos suaves */
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
 * FUNÇÕES PRIVADAS — SANIDADE DE SENSORES
 * =================================================================================
 */

bool Security::_checkSensorSanity(uint8_t fieldID, float value) {
    switch (fieldID) {
        case FLD_ROLL:
        case FLD_PITCH:
            return (value >= -180.0f && value <= 180.0f);
            
        case FLD_YAW:
            return (value >= 0.0f && value <= 360.0f);
            
        case FLD_HEADING:
            return (value >= 0.0f && value <= 360.0f);
            
        case FLD_BATT_V:
            return (value >= SANITY_BATT_V_MIN && value <= SANITY_BATT_V_MAX);
            
        case FLD_TEMP1:
        case FLD_TEMP2:
        case FLD_TEMP3:
        case FLD_TEMP4:
        case FLD_ESP1_TEMP:
        case FLD_ESP2_TEMP:
            return (value >= SANITY_TEMP_MIN && value <= SANITY_TEMP_MAX);
            
        case FLD_ALT_GPS:
        case FLD_ALT_BARO:
            return (value >= -100.0f && value <= SANITY_ALT_MAX);
            
        default:
            /* Campos desconhecidos são aceites (podem ser novos) */
            return true;
    }
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — GEOFENCING (PLACEHOLDER)
 * =================================================================================
 */

bool Security::_checkGeofence(float lat, float lon) const {
    if (!_config.enableGeofence) {
        return true;  /* Geofencing desativado */
    }
    
    /* TODO: Implementar cálculo de distância ao centro do geofence */
    /* Fórmula de Haversine para distância entre dois pontos GPS */
    
    (void)lat;
    (void)lon;
    
    return true;  /* Placeholder */
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — VIOLAÇÕES E FAIL SECURE
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

void Security::_evaluateFailSecure(SecurityResult reason) {
    if (!_config.enableFailSecure) {
        return;
    }
    
    /* Diferentes níveis de gravidade */
    switch (reason) {
        case SEC_INVALID_HMAC:
        case SEC_REPLAY_DETECTED:
            /* Ameaças ativas — nível elevado */
            DEBUG_PRINT("_evaluateFailSecure() — Ameaça ativa detectada\n");
            break;
            
        case SEC_SANITY_FAILED:
        case SEC_ENVELOPE_EXCEEDED:
            /* Possível erro de sensor ou comando perigoso */
            DEBUG_PRINT("_evaluateFailSecure() — Limite de segurança excedido\n");
            break;
            
        default:
            break;
    }
}

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
        DEBUG_PRINT("_updateSecurityLevel() — Nível alterado: %d → %d (violações=%d)\n",
                    _currentLevel, newLevel, _violationCount);
        _currentLevel = newLevel;
    }
}

/* =================================================================================
 * FUNÇÕES PRIVADAS — SERIALIZAÇÃO PARA HMAC
 * =================================================================================
 * 
 * Serializa a mensagem TLV para cálculo do HMAC.
 * O formato é idêntico ao utilizado no canal, mas sem o HMAC e SEQ.
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
 * FUNÇÕES PRIVADAS — HMAC-SHA256 COM MBEDTLS
 * =================================================================================
 * 
 * Implementação real de HMAC-SHA256 utilizando mbedtls.
 * No ESP32, isto utiliza hardware acceleration para melhor performance.
 */

void Security::_hmacSHA256(const uint8_t* key, size_t keyLen,
                           const uint8_t* data, size_t dataLen,
                           uint8_t* output) {
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    
    if (md_info == nullptr) {
        /* Fallback seguro: preencher com zeros (nunca deve acontecer) */
        memset(output, 0, SECURITY_HMAC_SIZE);
        return;
    }
    
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, md_info, 1);  /* 1 = use HMAC */
    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, data, dataLen);
    mbedtls_md_hmac_finish(&ctx, output);
    mbedtls_md_free(&ctx);
}
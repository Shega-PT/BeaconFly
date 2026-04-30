/**
 * =================================================================================
 * SECURITY.H — MÓDULO DE SEGURANÇA (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-30
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * Este módulo é responsável pela segurança das comunicações no ESP2.
 * 
 * DIFERENÇAS PARA O ESP1:
 *   • ESP2 NUNCA recebe comandos diretamente da GS (tudo vem via ESP1)
 *   • ESP2 apenas VERIFICA a autenticidade dos comandos recebidos via UART
 *   • Não precisa de gerir timeouts de link com GS (feito pelo ESP1)
 *   • Mantém validação de sanidade de dados e estado de voo
 * 
 * =================================================================================
 * RESPONSABILIDADES
 * =================================================================================
 * 
 *   1. Autenticação HMAC-SHA256 (verificar integridade de comandos)
 *   2. Anti-replay (verificar números de sequência)
 *   3. Rate limiting (proteção contra flood)
 *   4. Sanidade de comandos (limites físicos)
 *   5. Sanidade de sensores (valores impossíveis são rejeitados)
 *   6. Lockdown (bloqueio total após violações repetidas)
 * 
 * =================================================================================
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include "Protocol.h"

/* =================================================================================
 * CONFIGURAÇÃO DE SEGURANÇA
 * =================================================================================
 */

#define SECURITY_HMAC_KEY_SIZE      32
#define SECURITY_HMAC_SIZE          32
#define SECURITY_SEQ_WINDOW         32
#define SECURITY_MAX_VIOLATIONS     10

/* =================================================================================
 * RATE LIMITING — Intervalos mínimos entre comandos (ms)
 * =================================================================================
 */

#define RATE_LIMIT_CRITICAL_MS      500
#define RATE_LIMIT_CONTROL_MS       20
#define RATE_LIMIT_MAINTENANCE_MS   5000

/* =================================================================================
 * LIMITES DE SANIDADE — COMANDOS
 * =================================================================================
 */

#define SANITY_ROLL_MAX             60.0f
#define SANITY_PITCH_MAX            45.0f
#define SANITY_YAW_MAX              180.0f
#define SANITY_THROTTLE_MAX         1.0f
#define SANITY_ALT_MAX              1000.0f

/* =================================================================================
 * LIMITES DE SANIDADE — SENSORES
 * =================================================================================
 */

#define SANITY_ACCEL_MAX            100.0f
#define SANITY_GYRO_MAX             2000.0f
#define SANITY_BATT_V_MIN           6.0f
#define SANITY_BATT_V_MAX           30.0f
#define SANITY_TEMP_MIN             -20.0f
#define SANITY_TEMP_MAX             80.0f

/* =================================================================================
 * ENUMS
 * =================================================================================
 */

enum SecurityResult : uint8_t {
    SEC_OK                    = 0,
    SEC_INVALID_HMAC          = 1,
    SEC_REPLAY_DETECTED       = 2,
    SEC_RATE_LIMITED          = 3,
    SEC_SANITY_FAILED         = 4,
    SEC_LOCKDOWN              = 5,
    SEC_WRONG_STATE           = 6,
    SEC_ENVELOPE_EXCEEDED     = 7,
    SEC_NO_COMMAND            = 8
};

enum FlightState : uint8_t {
    STATE_GROUND_DISARMED = 0,
    STATE_GROUND_ARMED    = 1,
    STATE_TAKEOFF         = 2,
    STATE_FLYING          = 3,
    STATE_LANDING         = 4,
    STATE_FAILSAFE        = 5
};

enum SecurityLevel : uint8_t {
    SECURITY_LEVEL_NORMAL    = 0,
    SECURITY_LEVEL_CAUTION   = 1,
    SECURITY_LEVEL_WARNING   = 2,
    SECURITY_LEVEL_CRITICAL  = 3,
    SECURITY_LEVEL_LOCKDOWN  = 4
};

/* =================================================================================
 * ESTRUTURAS
 * =================================================================================
 */

struct SecurityStats {
    uint32_t packetsVerified = 0;
    uint32_t hmacFailures = 0;
    uint32_t replayAttempts = 0;
    uint32_t rateLimitHits = 0;
    uint32_t sanityFailures = 0;
    uint32_t wrongStateBlocks = 0;
    uint32_t envelopeViolations = 0;
    uint32_t lockdownEvents = 0;
    uint32_t lastViolationTime = 0;
    SecurityResult lastViolationReason = SEC_OK;
};

struct SecurityConfig {
    bool enableFailSecure = true;
    bool enableWatchdog = true;
};

/* =================================================================================
 * CLASSE SECURITY
 * =================================================================================
 */

class Security {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    Security();
    void begin();
    bool loadKey(const uint8_t* key, size_t keyLen);

    /* =========================================================================
     * VERIFICAÇÃO DE PACOTES (COMANDOS VIA UART DO ESP1)
     * ========================================================================= */
    
    SecurityResult verifyPacket(const TLVMessage& msg, uint32_t seqNum, const uint8_t* hmac);
    SecurityResult verifyCommand(uint8_t cmdID, float value);
    SecurityResult verifySensorData(uint8_t fieldID, float value);

    /* =========================================================================
     * CONTEXTO DE VOO
     * ========================================================================= */
    
    void setFlightState(FlightState state);
    FlightState getFlightState() const;
    SecurityLevel getSecurityLevel() const;

    /* =========================================================================
     * HMAC
     * ========================================================================= */
    
    void computeHMAC(const uint8_t* data, size_t len, uint8_t* output);

    /* =========================================================================
     * LOCKDOWN E VIOLAÇÕES
     * ========================================================================= */
    
    void triggerLockdown(const char* reason);
    bool isInLockdown() const;
    void resetViolationCount();

    /* =========================================================================
     * ESTATÍSTICAS
     * ========================================================================= */
    
    const SecurityStats& getStats() const;
    void resetStats();

    /* =========================================================================
     * CONFIGURAÇÃO
     * ========================================================================= */
    
    void configure(const SecurityConfig& config);
    void setDebug(bool enable);

private:
    /* =========================================================================
     * CHAVE E ESTADO
     * ========================================================================= */
    
    uint8_t _hmacKey[SECURITY_HMAC_KEY_SIZE];
    bool    _keyLoaded;

    /* =========================================================================
     * ANTI-REPLAY
     * ========================================================================= */
    
    uint32_t _lastSeqNum;
    uint32_t _seqBitmap;

    /* =========================================================================
     * RATE LIMITING
     * ========================================================================= */
    
    uint32_t _lastCriticalMs;
    uint32_t _lastControlMs;
    uint32_t _lastMaintenanceMs;

    /* =========================================================================
     * CONTEXTO DE VOO
     * ========================================================================= */
    
    FlightState   _flightState;
    SecurityLevel _currentLevel;

    /* =========================================================================
     * LOCKDOWN E VIOLAÇÕES
     * ========================================================================= */
    
    bool          _lockdown;
    uint8_t       _violationCount;
    SecurityStats _stats;
    SecurityConfig _config;

    /* =========================================================================
     * DEBUG
     * ========================================================================= */
    
    bool _debugEnabled;

    /* =========================================================================
     * FUNÇÕES PRIVADAS
     * ========================================================================= */
    
    bool _checkHMAC(const TLVMessage& msg, const uint8_t* hmac);
    bool _checkSequence(uint32_t seqNum);
    bool _checkRateLimit(uint8_t cmdID);
    bool _checkCommandSanity(uint8_t cmdID, float value);
    bool _checkCommandState(uint8_t cmdID);
    bool _checkEnvelope(uint8_t cmdID, float value);
    bool _checkSensorSanity(uint8_t fieldID, float value);
    
    void _recordViolation(SecurityResult reason);
    void _evaluateFailSecure(SecurityResult reason);
    void _updateSecurityLevel();
    
    size_t _serializeForHMAC(const TLVMessage& msg, uint8_t* output, size_t maxLen);
    void _hmacSHA256(const uint8_t* key, size_t keyLen,
                     const uint8_t* data, size_t dataLen,
                     uint8_t* output);
};

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* securityResultToString(SecurityResult result);
const char* flightStateToString(FlightState state);
const char* securityLevelToString(SecurityLevel level);
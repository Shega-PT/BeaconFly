/**
 * =================================================================================
 * FAILSAFE.CPP — IMPLEMENTAÇÃO DO MÓDULO DE SEGURANÇA CRÍTICA
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-27
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. O Failsafe NUNCA controla atuadores diretamente
 * 2. Apenas comanda o FlightControl do ESP2
 * 3. Alarmes são geridos autonomamente
 * 4. Timeouts adaptam-se ao modo de voo (MANUAL vs automáticos)
 * 5. A decisão é baseada em múltiplos critérios com persistência temporal
 * 
 * =================================================================================
 */

#include "Failsafe.h"
#include <Arduino.h>

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define FAILSAFE_DEBUG

#ifdef FAILSAFE_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[Failsafe] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

Failsafe::Failsafe()
    : _currentLevel(FS_LEVEL_NORMAL)
    , _currentReason(FS_REASON_NONE)
    , _lastActivationTime(0)
    , _active(false)
    , _debugEnabled(false)
    , _heartbeatESP1(0)
    , _lastCommandGS(0)
    , _flightMode(0)
    , _gpsValid(false)
    , _battVoltage(0.0f)
    , _temperature(0.0f)
    , _angleExceedStartMs(0)
    , _angleExceedFlag(false)
    , _actuatorSatStartMs(0)
    , _actuatorSatFlag(false)
    , _gpsLostStartMs(0)
    , _lastAlarmBlinkMs(0)
    , _alarmLedState(false)
    , _alarmBuzzerState(false)
{
    memset(&_state, 0, sizeof(FlightState));
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO
 * =================================================================================
 */

void Failsafe::begin() {
    DEBUG_PRINT("begin() — Inicializando módulo Failsafe...\n");
    
    pinMode(FS_LED_PIN, OUTPUT);
    pinMode(FS_BUZZER_PIN, OUTPUT);
    
    digitalWrite(FS_LED_PIN, LOW);
    digitalWrite(FS_BUZZER_PIN, LOW);
    
    _currentLevel = FS_LEVEL_NORMAL;
    _currentReason = FS_REASON_NONE;
    _active = false;
    _lastActivationTime = 0;
    
    _angleExceedStartMs = 0;
    _angleExceedFlag = false;
    _actuatorSatStartMs = 0;
    _actuatorSatFlag = false;
    _gpsLostStartMs = 0;
    
    DEBUG_PRINT("begin() — Failsafe inicializado. LED no pino %d, Buzzer no pino %d\n", 
                FS_LED_PIN, FS_BUZZER_PIN);
}

/* =================================================================================
 * UPDATE INPUTS — ATUALIZAÇÃO DOS DADOS DE ENTRADA
 * =================================================================================
 */

void Failsafe::updateInputs(uint32_t heartbeatESP1, uint32_t lastCommandGS, 
                            uint8_t flightMode, const FlightState& state,
                            bool gpsValid, float battVoltage, float temperature) {
    _heartbeatESP1 = heartbeatESP1;
    _lastCommandGS = lastCommandGS;
    _flightMode = flightMode;
    _state = state;
    _gpsValid = gpsValid;
    _battVoltage = battVoltage;
    _temperature = temperature;
}

/* =================================================================================
 * CHECK ESP1 TIMEOUT — VERIFICA TIMEOUT DO ESP1
 * =================================================================================
 */

bool Failsafe::_checkESP1Timeout() {
    uint32_t now = millis();
    uint32_t timeout;
    
    if (_flightMode == MODE_MANUAL) {
        timeout = FS_TIMEOUT_ESP1_MANUAL;
    } else {
        timeout = FS_TIMEOUT_ESP1_AUTO;
    }
    
    return ((now - _heartbeatESP1) > timeout);
}

/* =================================================================================
 * CHECK GS TIMEOUT — VERIFICA TIMEOUT DA GROUND STATION
 * =================================================================================
 */

bool Failsafe::_checkGSTimeout() {
    uint32_t now = millis();
    uint32_t timeout;
    
    if (_flightMode == MODE_MANUAL) {
        timeout = FS_TIMEOUT_GS_MANUAL;
    } else {
        timeout = FS_TIMEOUT_GS_AUTO;
    }
    
    return ((now - _lastCommandGS) > timeout);
}

/* =================================================================================
 * CHECK ANGLE LIMITS — VERIFICA LIMITES DE ÂNGULOS
 * =================================================================================
 */

bool Failsafe::_checkAngleLimits() {
    float rollAbs = fabs(_state.roll);
    float pitchAbs = fabs(_state.pitch);
    
    bool rollExceed = (rollAbs > FS_ANGLE_ROLL_LIMIT);
    bool pitchExceed = (pitchAbs > FS_ANGLE_PITCH_LIMIT);
    
    if (rollExceed || pitchExceed) {
        if (!_angleExceedFlag) {
            _angleExceedFlag = true;
            _angleExceedStartMs = millis();
        }
        return ((millis() - _angleExceedStartMs) >= FS_ANGLE_DURATION_MS);
    } else {
        _angleExceedFlag = false;
        _angleExceedStartMs = 0;
        return false;
    }
}

/* =================================================================================
 * CHECK ANGULAR RATE — VERIFICA VELOCIDADE ANGULAR
 * =================================================================================
 */

bool Failsafe::_checkAngularRate() {
    float rollRateAbs = fabs(_state.rollRate);
    float pitchRateAbs = fabs(_state.pitchRate);
    float yawRateAbs = fabs(_state.yawRate);
    
    return (rollRateAbs > FS_ANGULAR_RATE_LIMIT ||
            pitchRateAbs > FS_ANGULAR_RATE_LIMIT ||
            yawRateAbs > FS_ANGULAR_RATE_LIMIT);
}

/* =================================================================================
 * CHECK SINK RATE — VERIFICA QUEDA VERTICAL
 * =================================================================================
 */

bool Failsafe::_checkSinkRate() {
    return (_state.vz < FS_SINK_RATE_LIMIT);
}

/* =================================================================================
 * CHECK BATTERY STATUS — VERIFICA ESTADO DA BATERIA
 * =================================================================================
 */

uint8_t Failsafe::_checkBatteryStatus() {
    if (_battVoltage <= FS_BATT_DEAD_VOLTAGE) return 3;
    if (_battVoltage <= FS_BATT_CRITICAL_VOLTAGE) return 2;
    if (_battVoltage <= FS_BATT_LOW_VOLTAGE) return 1;
    return 0;
}

/* =================================================================================
 * CHECK TEMPERATURE — VERIFICA TEMPERATURA
 * =================================================================================
 */

bool Failsafe::_checkTemperature() {
    return (_temperature >= FS_TEMP_CRITICAL_LIMIT);
}

/* =================================================================================
 * CHECK GPS LOST — VERIFICA PERDA DE GPS
 * =================================================================================
 */

bool Failsafe::_checkGPSLost() {
    if (!_gpsValid) {
        if (_gpsLostStartMs == 0) {
            _gpsLostStartMs = millis();
        }
        return ((millis() - _gpsLostStartMs) >= FS_GPS_LOST_TIMEOUT);
    } else {
        _gpsLostStartMs = 0;
        return false;
    }
}

/* =================================================================================
 * CHECK IMU INTEGRITY — VERIFICA INTEGRIDADE DA IMU
 * =================================================================================
 */

bool Failsafe::_checkIMUIntegrity() {
    float accelTotal = sqrt(_state.accelX * _state.accelX +
                            _state.accelY * _state.accelY +
                            _state.accelZ * _state.accelZ);
    
    float deviation = fabs(accelTotal - FS_GRAVITY);
    return (deviation > FS_IMU_ACCEL_TOLERANCE);
}

/* =================================================================================
 * CHECK ACTUATOR SATURATION — VERIFICA SATURAÇÃO DOS ATUADORES
 * =================================================================================
 */

bool Failsafe::_checkActuatorSaturation() {
    bool saturated = (_state.servoWingR <= FS_SERVO_MIN_US + 50 ||
                      _state.servoWingR >= FS_SERVO_MAX_US - 50 ||
                      _state.servoWingL <= FS_SERVO_MIN_US + 50 ||
                      _state.servoWingL >= FS_SERVO_MAX_US - 50);
    
    if (saturated) {
        if (!_actuatorSatFlag) {
            _actuatorSatFlag = true;
            _actuatorSatStartMs = millis();
        }
        return ((millis() - _actuatorSatStartMs) >= FS_ACTUATOR_SATURATION_MS);
    } else {
        _actuatorSatFlag = false;
        _actuatorSatStartMs = 0;
        return false;
    }
}

/* =================================================================================
 * DETERMINE REASON — DETERMINA A RAZÃO DO FAILSAFE
 * =================================================================================
 */

FailsafeReason Failsafe::_determineReason() {
    /* Nível 5 — Criticidades máximas */
    if (_checkIMUIntegrity()) return FS_REASON_IMU_FAILURE;
    if (_checkBatteryStatus() == 3) return FS_REASON_BATT_DEAD;
    
    /* Nível 4 — Perda de controlo */
    if (_checkAngularRate()) return FS_REASON_ANGULAR_RATE;
    if (_checkAngleLimits()) {
        if (fabs(_state.roll) > FS_ANGLE_ROLL_LIMIT) return FS_REASON_ANGLE_ROLL;
        return FS_REASON_ANGLE_PITCH;
    }
    if (_checkSinkRate()) return FS_REASON_SINK_RATE;
    if (_checkActuatorSaturation()) return FS_REASON_ACTUATOR_SAT;
    
    /* Nível 3 — Aterragem forçada */
    if (_checkBatteryStatus() == 2) return FS_REASON_BATT_CRITICAL;
    if (_checkTemperature()) return FS_REASON_TEMP_CRITICAL;
    if (_checkESP1Timeout()) return FS_REASON_LOST_ESP1;
    
    /* Nível 2 — RTL */
    if (_checkGSTimeout()) return FS_REASON_LOST_GS;
    if (_checkBatteryStatus() == 1) return FS_REASON_BATT_LOW;
    
    /* Nível 1 — Apenas alerta */
    if (_checkGPSLost()) return FS_REASON_GPS_LOST;
    
    return FS_REASON_NONE;
}

/* =================================================================================
 * DETERMINE LEVEL — DETERMINA O NÍVEL DO FAILSAFE
 * =================================================================================
 */

FailsafeLevel Failsafe::_determineLevel() {
    /* Nível 5 — Corte total (queda livre) */
    if (_checkIMUIntegrity()) return FS_LEVEL_FREE_FALL;
    if (_checkBatteryStatus() == 3) return FS_LEVEL_FREE_FALL;
    
    /* Nível 4 — Planagem controlada */
    if (_checkAngularRate()) return FS_LEVEL_GLIDE;
    if (_checkAngleLimits()) return FS_LEVEL_GLIDE;
    if (_checkSinkRate()) return FS_LEVEL_GLIDE;
    if (_checkActuatorSaturation()) return FS_LEVEL_GLIDE;
    
    /* Nível 3 — Aterragem forçada */
    if (_checkBatteryStatus() == 2) return FS_LEVEL_LAND;
    if (_checkTemperature()) return FS_LEVEL_LAND;
    if (_checkESP1Timeout()) return FS_LEVEL_LAND;
    
    /* Nível 2 — RTL */
    if (_checkGSTimeout()) return FS_LEVEL_RTL;
    if (_checkBatteryStatus() == 1) return FS_LEVEL_RTL;
    
    /* Nível 1 — Alerta */
    if (_checkGPSLost()) return FS_LEVEL_ALERT;
    
    /* Nível 0 — Normal */
    return FS_LEVEL_NORMAL;
}

/* =================================================================================
 * PROCESS — PROCESSAMENTO PRINCIPAL
 * =================================================================================
 */

FailsafeLevel Failsafe::process() {
    FailsafeLevel newLevel = _determineLevel();
    FailsafeReason newReason = _determineReason();
    
    if (newLevel != _currentLevel) {
        DEBUG_PRINT("process() — Mudança de nível: %d → %d\n", _currentLevel, newLevel);
        
        if (newLevel >= FS_LEVEL_RTL && _currentLevel < FS_LEVEL_RTL) {
            _active = true;
            _lastActivationTime = millis();
            DEBUG_PRINT("process() — FAILSAFE ATIVADO! Nível: %d, Razão: %d\n", 
                        newLevel, newReason);
        } else if (newLevel < FS_LEVEL_RTL && _currentLevel >= FS_LEVEL_RTL) {
            _active = false;
            DEBUG_PRINT("process() — FAILSAFE DESATIVADO\n");
        }
        
        _currentLevel = newLevel;
        _currentReason = newReason;
    }
    
    return _currentLevel;
}

/* =================================================================================
 * SET LED BY LEVEL — CONFIGURA LED CONFORME NÍVEL
 * =================================================================================
 */

void Failsafe::_setLedByLevel() {
    uint32_t now = millis();
    uint32_t periodMs = 0;
    bool shouldBeOn = false;
    
    switch (_currentLevel) {
        case FS_LEVEL_NORMAL:
        case FS_LEVEL_ALERT:
            shouldBeOn = false;
            break;
            
        case FS_LEVEL_RTL:
            periodMs = 1000;
            shouldBeOn = ((now % periodMs) < 500);
            break;
            
        case FS_LEVEL_LAND:
            periodMs = 500;
            shouldBeOn = ((now % periodMs) < 250);
            break;
            
        case FS_LEVEL_GLIDE:
            periodMs = 332;
            shouldBeOn = ((now % periodMs) < 166);
            break;
            
        case FS_LEVEL_FREE_FALL:
            periodMs = 200;
            shouldBeOn = ((now % periodMs) < 100);
            break;
            
        default:
            shouldBeOn = false;
            break;
    }
    
    if (shouldBeOn != _alarmLedState) {
        _alarmLedState = shouldBeOn;
        digitalWrite(FS_LED_PIN, shouldBeOn ? HIGH : LOW);
    }
}

/* =================================================================================
 * SET BUZZER BY LEVEL — CONFIGURA BUZZER CONFORME NÍVEL
 * =================================================================================
 */

void Failsafe::_setBuzzerByLevel() {
    uint32_t now = millis();
    uint32_t periodMs = 0;
    bool shouldBeOn = false;
    uint32_t beepDuration = 100;
    
    switch (_currentLevel) {
        case FS_LEVEL_NORMAL:
        case FS_LEVEL_ALERT:
        case FS_LEVEL_RTL:
            shouldBeOn = false;
            break;
            
        case FS_LEVEL_LAND:
            periodMs = 1000;
            shouldBeOn = ((now % periodMs) < beepDuration);
            break;
            
        case FS_LEVEL_GLIDE:
            periodMs = 1000;
            {
                uint32_t offset = now % periodMs;
                if (offset < beepDuration) shouldBeOn = true;
                else if (offset >= 150 && offset < 150 + beepDuration) shouldBeOn = true;
                else shouldBeOn = false;
            }
            break;
            
        case FS_LEVEL_FREE_FALL:
            periodMs = 1000;
            shouldBeOn = ((now % periodMs) < 500);
            break;
            
        default:
            shouldBeOn = false;
            break;
    }
    
    if (shouldBeOn != _alarmBuzzerState) {
        _alarmBuzzerState = shouldBeOn;
        digitalWrite(FS_BUZZER_PIN, shouldBeOn ? HIGH : LOW);
    }
}

/* =================================================================================
 * UPDATE ALARMS — ATUALIZA LED E BUZZER
 * =================================================================================
 */

void Failsafe::_updateAlarms() {
    _setLedByLevel();
    _setBuzzerByLevel();
}

/* =================================================================================
 * EXECUTE RTL — EXECUTA AÇÕES DO NÍVEL RTL
 * =================================================================================
 */

void Failsafe::_executeRTL(FlightControl* fc) {
    if (fc == nullptr) return;
    
    if (_gpsValid) {
        fc->setMode(MODE_RTL);
        DEBUG_PRINT("_executeRTL() — Comando RTL enviado ao FlightControl\n");
    } else {
        _executeLand(fc);
        DEBUG_PRINT("_executeRTL() — GPS inválido, fallback para aterragem\n");
    }
}

/* =================================================================================
 * EXECUTE LAND — EXECUTA AÇÕES DO NÍVEL ATERRAGEM
 * =================================================================================
 */

void Failsafe::_executeLand(FlightControl* fc) {
    if (fc == nullptr) return;
    
    FlightSetpoints sp = fc->getSetpoints();
    sp.roll = 0.0f;
    sp.pitch = 0.0f;
    sp.throttle = 0.3f;
    sp.altitude = 0.0f;
    fc->setSetpoints(sp);
    fc->setMode(MODE_ALT_HOLD);
    
    DEBUG_PRINT("_executeLand() — Aterragem forçada: throttle=30%%, modo ALT_HOLD\n");
}

/* =================================================================================
 * EXECUTE GLIDE — EXECUTA AÇÕES DO NÍVEL PLANAGEM
 * =================================================================================
 */

void Failsafe::_executeGlide(FlightControl* fc) {
    if (fc == nullptr) return;
    
    FlightSetpoints sp = fc->getSetpoints();
    sp.roll = 0.0f;
    sp.pitch = 0.0f;
    sp.throttle = 0.0f;
    fc->setSetpoints(sp);
    fc->setMode(MODE_STABILIZE);
    
    DEBUG_PRINT("_executeGlide() — Planagem: throttle=0%%, modo STABILIZE\n");
}

/* =================================================================================
 * EXECUTE FREE FALL — EXECUTA AÇÕES DO NÍVEL CORTE TOTAL
 * =================================================================================
 */

void Failsafe::_executeFreeFall(FlightControl* fc) {
    if (fc == nullptr) return;
    
    FlightSetpoints sp = fc->getSetpoints();
    sp.roll = 0.0f;
    sp.pitch = 0.0f;
    sp.throttle = 0.0f;
    fc->setSetpoints(sp);
    fc->setMode(MODE_MANUAL);
    
    DEBUG_PRINT("_executeFreeFall() — ⚠️ CORTE TOTAL ⚠️ Modo MANUAL, throttle=0\n");
}

/* =================================================================================
 * EXECUTE — EXECUTA AÇÕES DO NÍVEL ATUAL
 * =================================================================================
 */

void Failsafe::execute(FlightControl* flightControl) {
    _updateAlarms();
    
    if (!_active) return;
    
    switch (_currentLevel) {
        case FS_LEVEL_RTL:
            _executeRTL(flightControl);
            break;
        case FS_LEVEL_LAND:
            _executeLand(flightControl);
            break;
        case FS_LEVEL_GLIDE:
            _executeGlide(flightControl);
            break;
        case FS_LEVEL_FREE_FALL:
            _executeFreeFall(flightControl);
            break;
        default:
            break;
    }
}

/* =================================================================================
 * RESET — RESETA O FAILSAFE
 * =================================================================================
 */

void Failsafe::reset() {
    _currentLevel = FS_LEVEL_NORMAL;
    _currentReason = FS_REASON_NONE;
    _active = false;
    _lastActivationTime = 0;
    
    _angleExceedFlag = false;
    _angleExceedStartMs = 0;
    _actuatorSatFlag = false;
    _actuatorSatStartMs = 0;
    _gpsLostStartMs = 0;
    
    digitalWrite(FS_LED_PIN, LOW);
    digitalWrite(FS_BUZZER_PIN, LOW);
    _alarmLedState = false;
    _alarmBuzzerState = false;
    
    DEBUG_PRINT("reset() — Failsafe resetado para nível NORMAL\n");
}

/* =================================================================================
 * GETTERS
 * =================================================================================
 */

FailsafeLevel Failsafe::getLevel() const {
    return _currentLevel;
}

FailsafeReason Failsafe::getReason() const {
    return _currentReason;
}

bool Failsafe::isActive() const {
    return _active;
}

uint32_t Failsafe::getLastActivationTime() const {
    return _lastActivationTime;
}

void Failsafe::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* failsafeLevelToString(FailsafeLevel level) {
    switch (level) {
        case FS_LEVEL_NORMAL:     return "NORMAL";
        case FS_LEVEL_ALERT:      return "ALERTA";
        case FS_LEVEL_RTL:        return "RTL";
        case FS_LEVEL_LAND:       return "ATERRA&cedil;AGEM";
        case FS_LEVEL_GLIDE:      return "PLANAGEM";
        case FS_LEVEL_FREE_FALL:  return "CORTE_TOTAL";
        default:                  return "UNKNOWN";
    }
}

const char* failsafeReasonToString(FailsafeReason reason) {
    switch (reason) {
        case FS_REASON_NONE:            return "NENHUMA";
        case FS_REASON_LOST_ESP1:       return "PERDA_ESP1";
        case FS_REASON_LOST_GS:         return "PERDA_GS";
        case FS_REASON_ANGLE_ROLL:      return "ANGULO_ROLL";
        case FS_REASON_ANGLE_PITCH:     return "ANGULO_PITCH";
        case FS_REASON_ANGULAR_RATE:    return "VELOCIDADE_ANGULAR";
        case FS_REASON_SINK_RATE:       return "QUEDA_VERTICAL";
        case FS_REASON_BATT_LOW:        return "BATERIA_BAIXA";
        case FS_REASON_BATT_CRITICAL:   return "BATERIA_CRITICA";
        case FS_REASON_BATT_DEAD:       return "BATERIA_MORTA";
        case FS_REASON_TEMP_CRITICAL:   return "TEMPERATURA_CRITICA";
        case FS_REASON_GPS_LOST:        return "GPS_PERDIDO";
        case FS_REASON_IMU_FAILURE:     return "FALHA_IMU";
        case FS_REASON_ACTUATOR_SAT:    return "ATUADOR_SATURADO";
        case FS_REASON_SYSTEM_HANG:     return "SISTEMA_TRAVADO";
        default:                        return "DESCONHECIDA";
    }
}
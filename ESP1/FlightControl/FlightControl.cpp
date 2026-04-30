/**
 * =================================================================================
 * FLIGHTCONTROL.CPP — IMPLEMENTAÇÃO DO CONTROLADOR DE VOO
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-17
 * VERSÃO:     2.0.1 (Correção: navegação em DEBUG_AUTO_MANUAL)
 * 
 * =================================================================================
 */

#include "FlightControl.h"
#include <Arduino.h>
#include <cmath>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define DEG_TO_RAD (PI / 180.0f)
#define RAD_TO_DEG (180.0f / PI)

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define FC_DEBUG

#ifdef FC_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[FlightControl] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

FlightControl::FlightControl() {
    _setpoints = {0.0f, 0.0f, 0.0f, 50.0f, 0.0f};
    _outputs = {FC_SERVO_NEUTRAL_US, FC_SERVO_NEUTRAL_US, FC_SERVO_NEUTRAL_US,
                FC_SERVO_NEUTRAL_US, FC_SERVO_NEUTRAL_US, FC_MOTOR_MIN_US};
    
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO
 * =================================================================================
 */

void FlightControl::begin() {
    DEBUG_PRINT("begin() — Configurando PID e estado inicial...\n");

    _pidRoll.Kp = 1.25f;   _pidRoll.Ki = 0.06f;   _pidRoll.Kd = 0.28f;
    _pidRoll.iMax = 160.0f; _pidRoll.iMin = -160.0f;
    _pidRoll.lpfAlpha = FC_LPF_ALPHA_ROLL;

    _pidPitch.Kp = 1.10f;  _pidPitch.Ki = 0.05f;  _pidPitch.Kd = 0.25f;
    _pidPitch.iMax = 160.0f; _pidPitch.iMin = -160.0f;
    _pidPitch.lpfAlpha = FC_LPF_ALPHA_PITCH;

    _pidYaw.Kp = 0.85f;    _pidYaw.Ki = 0.07f;    _pidYaw.Kd = 0.12f;
    _pidYaw.iMax = 130.0f;  _pidYaw.iMin = -130.0f;
    _pidYaw.lpfAlpha = FC_LPF_ALPHA_YAW;

    _pidAltitude.Kp = 1.8f;  _pidAltitude.Ki = 0.025f; _pidAltitude.Kd = 0.45f;
    _pidAltitude.iMax = 320.0f; _pidAltitude.iMin = -320.0f;
    _pidAltitude.lpfAlpha = FC_LPF_ALPHA_ALT;

    reset();
    _mode = MODE_STABILIZE;
    _lastUpdateMs = millis();
    
    _lastManualCommandMs = 0;
    _manualOverrideActive = false;
    _debugManualTimeoutMs = FC_DEBUG_MANUAL_TIMEOUT_MS;
    _debugTrajectoryCount = 0;
    _debugCurrentPoint = 0;

    DEBUG_PRINT("FlightControl inicializado com sucesso (Modo: STABILIZE)\n");
}

/* =================================================================================
 * RESET — ESTADO SEGURO
 * =================================================================================
 */

void FlightControl::reset() {
    DEBUG_PRINT("reset() — limpando estado\n");

    _setpoints = {0.0f, 0.0f, 0.0f, 50.0f, 0.0f};

    _pidRoll.integral = _pidPitch.integral = _pidYaw.integral = _pidAltitude.integral = 0.0f;
    _pidRoll.lastError = _pidPitch.lastError = _pidYaw.lastError = _pidAltitude.lastError = 0.0f;
    _pidRoll.filteredD = _pidPitch.filteredD = _pidYaw.filteredD = _pidAltitude.filteredD = 0.0f;

    _outputs = {FC_SERVO_NEUTRAL_US, FC_SERVO_NEUTRAL_US, FC_SERVO_NEUTRAL_US,
                FC_SERVO_NEUTRAL_US, FC_SERVO_NEUTRAL_US, FC_MOTOR_MIN_US};
                
    _missionActive = false;
    _currentWaypoint = 0;
    _debugCurrentPoint = 0;
    _manualOverrideActive = false;
}

/* =================================================================================
 * UPDATE STATE — DADOS DO ESP2
 * =================================================================================
 */

void FlightControl::updateState(const TLVMessage& msg) {
    if (msg.msgID != MSG_TELEMETRY && msg.msgID != MSG_SI_DATA) {
        return;
    }

    bool gotAttitude = false;
    bool gotAltitude = false;
    bool gotVelocity = false;
    bool gotGPS = false;

    for (uint8_t i = 0; i < msg.tlvCount; i++) {
        const TLVField& f = msg.tlvs[i];
        if (f.len != 4) continue;

        float val = bytesToFloat(f.data);

        switch (f.id) {
            case FLD_ROLL:    _state.roll = val; gotAttitude = true; break;
            case FLD_PITCH:   _state.pitch = val; gotAttitude = true; break;
            case FLD_YAW:     _state.yaw = val; gotAttitude = true; break;
            case FLD_HEADING: _state.heading = val; break;
            
            case 0x37: _state.rollRate = val; break;
            case 0x38: _state.pitchRate = val; break;
            case 0x39: _state.yawRate = val; break;
            
            /* GPS */
            case FLD_GPS_LAT: _state.latitude = val; gotGPS = true; break;
            case FLD_GPS_LON: _state.longitude = val; gotGPS = true; break;
            case FLD_ALT_GPS:  _state.altGPS = val; gotAltitude = true; break;
            case FLD_ALT_BARO: _state.altBaro = val; gotAltitude = true; break;
            
            case FLD_VX: _state.vx = val; gotVelocity = true; break;
            case FLD_VY: _state.vy = val; gotVelocity = true; break;
            case FLD_VZ: _state.vz = val; gotVelocity = true; break;
            
            case FLD_BATT_V: _state.battV = val; break;
            case FLD_BATT_A: _state.battA = val; break;
            
            default: break;
        }
    }

    _state.attitudeValid = gotAttitude;
    _state.altitudeValid = gotAltitude;
    _state.velocityValid = gotVelocity;
    _state.gpsValid = gotGPS;
    _state.timestamp = millis();

    if (_state.altitudeValid) {
        _state.altFused = _fuseAltitude(_state.altGPS, _state.altBaro);
    }
}

/* =================================================================================
 * PROCESS COMMAND — COMANDOS DA GROUND STATION
 * =================================================================================
 */

void FlightControl::processCommand(const TLVMessage& msg) {
    if (msg.msgID != MSG_COMMAND) return;

    for (uint8_t i = 0; i < msg.tlvCount; i++) {
        const TLVField& f = msg.tlvs[i];
        
        /* Comandos manuais ativam override no modo DEBUG_AUTO_MANUAL */
        if (f.id == CMD_SET_ROLL || f.id == CMD_SET_PITCH || 
            f.id == CMD_SET_YAW || f.id == CMD_SET_THROTTLE) {
            registerManualCommand();
        }
        
        float val = 0;
        if (f.len == 4) val = bytesToFloat(f.data);

        switch (f.id) {
            case CMD_SET_MODE:
                if (f.len >= 1) setMode((FlightMode)f.data[0]);
                break;

            case CMD_SET_ROLL:
                _setpoints.roll = constrain(val, -MAX_ROLL_ANGLE, MAX_ROLL_ANGLE);
                break;
            case CMD_SET_PITCH:
                _setpoints.pitch = constrain(val, -MAX_PITCH_ANGLE, MAX_PITCH_ANGLE);
                break;
            case CMD_SET_YAW:
                _setpoints.yaw = val;
                break;
            case CMD_SET_ALT_TARGET:
                _setpoints.altitude = constrain(val, MIN_ALTITUDE, MAX_ALTITUDE);
                _pidAltitude.integral = 0.0f;
                break;
            case CMD_SET_THROTTLE:
                _setpoints.throttle = constrain(val, 0.0f, 1.0f);
                break;

            default:
                DEBUG_PRINT("Comando não tratado: 0x%02X\n", f.id);
                break;
        }
    }
}

/* =================================================================================
 * UPDATE — CICLO PRINCIPAL (100Hz)
 * =================================================================================
 */

void FlightControl::update() {
    uint32_t now = millis();
    float dt = (now - _lastUpdateMs) / 1000.0f;
    dt = constrain(dt, 0.001f, 0.1f);
    _lastUpdateMs = now;

    if ((now - _state.timestamp) > FC_DATA_STALE_MS) {
        DEBUG_PRINT("AVISO: Dados SI stale (>%dms)\n", FC_DATA_STALE_MS);
        return;
    }

    switch (_mode) {
        case MODE_MANUAL:             _updateManual();                break;
        case MODE_STABILIZE:          _updateStabilize(dt);           break;
        case MODE_ALT_HOLD:           _updateAltHold(dt);             break;
        case MODE_POSHOLD:            _updatePosHold(dt);             break;
        case MODE_AUTO:               _updateAuto(dt);                break;
        case MODE_RTL:                _updateRTL(dt);                 break;
        case MODE_DEBUG_AUTO_MANUAL:  _updateDebugAutoManual(dt);     break;
        default:                      _updateStabilize(dt);           break;
    }
}

/* =================================================================================
 * MODO MANUAL
 * =================================================================================
 */

void FlightControl::_updateManual() {
    _outputs.wingR = _applySlewRate(_outputs.wingR, 
        _applyTrim(FC_SERVO_NEUTRAL_US + _setpoints.roll * 500, _trim.wingR, 
                   FC_SERVO_MIN_US, FC_SERVO_MAX_US), FC_SLEW_RATE_SERVO);
    _outputs.wingL = _applySlewRate(_outputs.wingL,
        _applyTrim(FC_SERVO_NEUTRAL_US - _setpoints.roll * 500, _trim.wingL,
                   FC_SERVO_MIN_US, FC_SERVO_MAX_US), FC_SLEW_RATE_SERVO);
    _outputs.rudder = _applySlewRate(_outputs.rudder,
        _applyTrim(FC_SERVO_NEUTRAL_US + _setpoints.yaw * 500, _trim.rudder,
                   FC_SERVO_MIN_US, FC_SERVO_MAX_US), FC_SLEW_RATE_SERVO);
    
    _outputs.motor = _applySlewRate(_outputs.motor,
        _applyTrim(FC_MOTOR_MIN_US + _setpoints.throttle * (FC_MOTOR_MAX_US - FC_MOTOR_MIN_US),
                   _trim.motor, FC_MOTOR_MIN_US, FC_MOTOR_MAX_US), FC_SLEW_RATE_MOTOR);
}

/* =================================================================================
 * MODO STABILIZE
 * =================================================================================
 */

void FlightControl::_updateStabilize(float dt) {
    if (!_state.attitudeValid) return;

    float errRoll = _setpoints.roll - _state.roll;
    float errPitch = _setpoints.pitch - _state.pitch;
    float errYaw = _setpoints.yaw - _state.heading;
    
    if (errYaw > 180) errYaw -= 360;
    if (errYaw < -180) errYaw += 360;

    float corrRoll = _runPID(_pidRoll, errRoll, dt, FC_PID_ROLL_LIMIT);
    float corrPitch = _runPID(_pidPitch, errPitch, dt, FC_PID_PITCH_LIMIT);
    float corrYaw = _runPID(_pidYaw, errYaw, dt, FC_PID_YAW_LIMIT);

    _mixElevons(corrPitch, corrRoll, _outputs.elevonR, _outputs.elevonL);
    
    uint16_t rudderTarget = FC_SERVO_NEUTRAL_US + corrYaw;
    _outputs.rudder = _applySlewRate(_outputs.rudder, 
        constrain(rudderTarget, FC_SERVO_MIN_US, FC_SERVO_MAX_US), FC_SLEW_RATE_SERVO);
    
    uint16_t motorTarget = FC_MOTOR_MIN_US + _setpoints.throttle * (FC_MOTOR_MAX_US - FC_MOTOR_MIN_US);
    _outputs.motor = _applySlewRate(_outputs.motor, motorTarget, FC_SLEW_RATE_MOTOR);
}

/* =================================================================================
 * MODO ALT_HOLD
 * =================================================================================
 */

void FlightControl::_updateAltHold(float dt) {
    if (!_state.attitudeValid || !_state.altitudeValid) {
        _updateStabilize(dt);
        return;
    }

    float errAlt = _setpoints.altitude - _state.altFused;
    float altCorr = _runPID(_pidAltitude, errAlt, dt, FC_PID_ALT_LIMIT);
    
    float baseThrottle = 0.3f;
    float throttle = constrain(baseThrottle + altCorr / 1000.0f, 0.0f, 1.0f);
    
    if (_manualOverrideActive) {
        throttle = _setpoints.throttle;
    }
    
    float errRoll = _setpoints.roll - _state.roll;
    float errPitch = _setpoints.pitch - _state.pitch;
    float errYaw = _setpoints.yaw - _state.heading;
    
    if (errYaw > 180) errYaw -= 360;
    if (errYaw < -180) errYaw += 360;

    float corrRoll = _runPID(_pidRoll, errRoll, dt, FC_PID_ROLL_LIMIT);
    float corrPitch = _runPID(_pidPitch, errPitch, dt, FC_PID_PITCH_LIMIT);
    float corrYaw = _runPID(_pidYaw, errYaw, dt, FC_PID_YAW_LIMIT);

    _mixElevons(corrPitch, corrRoll, _outputs.elevonR, _outputs.elevonL);
    
    uint16_t rudderTarget = FC_SERVO_NEUTRAL_US + corrYaw;
    _outputs.rudder = _applySlewRate(_outputs.rudder, 
        constrain(rudderTarget, FC_SERVO_MIN_US, FC_SERVO_MAX_US), FC_SLEW_RATE_SERVO);
    
    uint16_t motorTarget = FC_MOTOR_MIN_US + throttle * (FC_MOTOR_MAX_US - FC_MOTOR_MIN_US);
    _outputs.motor = _applySlewRate(_outputs.motor, motorTarget, FC_SLEW_RATE_MOTOR);
}

/* =================================================================================
 * MODO POSHOLD
 * =================================================================================
 */

void FlightControl::_updatePosHold(float dt) {
    if (!_state.gpsValid || !_state.velocityValid) {
        _updateAltHold(dt);
        return;
    }
    _updateAltHold(dt);
}

/* =================================================================================
 * MODO AUTO
 * =================================================================================
 */

void FlightControl::_updateAuto(float dt) {
    if (!_missionActive || _waypointCount == 0) {
        _updateAltHold(dt);
        return;
    }
    
    if (!_state.gpsValid) {
        _updateAltHold(dt);
        return;
    }
    
    bool arrived = _navigateToWaypoint(_waypoints[_currentWaypoint], dt);
    
    if (arrived) {
        _currentWaypoint++;
        if (_currentWaypoint >= _waypointCount) {
            _missionActive = false;
            _setpoints.throttle = 0.0f;
            DEBUG_PRINT("_updateAuto: MISSÃO COMPLETA!\n");
        } else {
            DEBUG_PRINT("_updateAuto: Chegou ao waypoint %d, próximo: %d\n", 
                        _currentWaypoint - 1, _currentWaypoint);
        }
    }
}

/* =================================================================================
 * MODO RTL
 * =================================================================================
 */

void FlightControl::_updateRTL(float dt) {
    if (!_state.gpsValid) {
        _updateAltHold(dt);
        return;
    }
    
    Waypoint home = {0.0f, 0.0f, 50.0f, 0.0f, 10.0f};
    
    bool arrived = _navigateToWaypoint(home, dt);
    
    if (arrived) {
        _setpoints.throttle = 0.2f;
        _updateAltHold(dt);
        
        if (_state.altFused < 2.0f) {
            DEBUG_PRINT("_updateRTL: Aterragem completa!\n");
        }
    }
}

/* =================================================================================
 * MODO DEBUG_AUTO_MANUAL — HÍBRIDO PARA TESTES
 * =================================================================================
 * 
 * Comportamento:
 *   - Autónomo: segue trajeto pré-definido
 *   - Override manual: quando recebe comandos do utilizador
 *   - Timeout: após X segundos sem comandos, retorna ao waypoint MAIS PRÓXIMO
 *   - Não volta ao início, mas sim ao ponto da trajetória mais perto da posição atual
 */

void FlightControl::_updateDebugAutoManual(float dt) {
    uint32_t now = millis();
    
    /* Verificar se o override manual expirou */
    if (_manualOverrideActive && (now - _lastManualCommandMs) > _debugManualTimeoutMs) {
        _manualOverrideActive = false;
        
        /* Calcular o waypoint mais próximo da posição atual para retomar */
        if (_debugTrajectoryCount > 0 && _state.gpsValid) {
            uint8_t nearest = _findNearestWaypoint(_debugTrajectory, _debugTrajectoryCount);
            _debugCurrentPoint = nearest;
            DEBUG_PRINT("_updateDebugAutoManual: Timeout expirado → retomando no waypoint %d (mais próximo)\n", nearest);
        } else {
            DEBUG_PRINT("_updateDebugAutoManual: Timeout expirado → sem GPS, mantendo posição\n");
        }
    }
    
    /* Verificar se há trajeto definido */
    if (_debugTrajectoryCount == 0) {
        _updateStabilize(dt);
        return;
    }
    
    if (_manualOverrideActive) {
        /* =====================================================================
         * OVERRIDE MANUAL ATIVO
         * =====================================================================
         * Comporta-se como modo STABILIZE, com setpoints manuais.
         */
        
        if (_state.attitudeValid) {
            float errRoll = _setpoints.roll - _state.roll;
            float errPitch = _setpoints.pitch - _state.pitch;
            float errYaw = _setpoints.yaw - _state.heading;
            
            if (errYaw > 180) errYaw -= 360;
            if (errYaw < -180) errYaw += 360;
            
            float corrRoll = _runPID(_pidRoll, errRoll, dt, FC_PID_ROLL_LIMIT);
            float corrPitch = _runPID(_pidPitch, errPitch, dt, FC_PID_PITCH_LIMIT);
            float corrYaw = _runPID(_pidYaw, errYaw, dt, FC_PID_YAW_LIMIT);
            
            _mixElevons(corrPitch, corrRoll, _outputs.elevonR, _outputs.elevonL);
            
            uint16_t rudderTarget = FC_SERVO_NEUTRAL_US + corrYaw;
            _outputs.rudder = _applySlewRate(_outputs.rudder, 
                constrain(rudderTarget, FC_SERVO_MIN_US, FC_SERVO_MAX_US), FC_SLEW_RATE_SERVO);
        }
        
        uint16_t motorTarget = FC_MOTOR_MIN_US + _setpoints.throttle * (FC_MOTOR_MAX_US - FC_MOTOR_MIN_US);
        _outputs.motor = _applySlewRate(_outputs.motor, motorTarget, FC_SLEW_RATE_MOTOR);
        
        _outputs.wingR = _outputs.elevonR;
        _outputs.wingL = _outputs.elevonL;
        
        DEBUG_PRINT("_updateDebugAutoManual: OVERRIDE MANUAL — timeout em %lu ms\n", 
                    _debugManualTimeoutMs - (now - _lastManualCommandMs));
                    
    } else {
        /* =====================================================================
         * MODO AUTÓNOMO (sem override)
         * =====================================================================
         * Segue o trajeto pré-definido a partir do waypoint mais próximo
         */
        
        if (!_state.gpsValid) {
            _updateAltHold(dt);
            return;
        }
        
        bool arrived = _navigateToWaypoint(_debugTrajectory[_debugCurrentPoint], dt);
        
        if (arrived) {
            _debugCurrentPoint++;
            if (_debugCurrentPoint >= _debugTrajectoryCount) {
                _debugCurrentPoint = 0;
                DEBUG_PRINT("_updateDebugAutoManual: Trajeto completo — reiniciando ciclo\n");
            } else {
                DEBUG_PRINT("_updateDebugAutoManual: Chegou ao ponto %d, próximo: %d\n", 
                            _debugCurrentPoint - 1, _debugCurrentPoint);
            }
        }
    }
}

/* =================================================================================
 * FUNÇÕES DE NAVEGAÇÃO
 * =================================================================================
 */

float FlightControl::_calculateBearing(float lat1, float lon1, float lat2, float lon2) {
    float lat1Rad = lat1 * DEG_TO_RAD;
    float lat2Rad = lat2 * DEG_TO_RAD;
    float deltaLon = (lon2 - lon1) * DEG_TO_RAD;
    
    float y = sin(deltaLon) * cos(lat2Rad);
    float x = cos(lat1Rad) * sin(lat2Rad) - sin(lat1Rad) * cos(lat2Rad) * cos(deltaLon);
    
    float bearing = atan2(y, x) * RAD_TO_DEG;
    if (bearing < 0) bearing += 360;
    
    return bearing;
}

float FlightControl::_calculateDistance(float lat1, float lon1, float lat2, float lon2) {
    float lat1Rad = lat1 * DEG_TO_RAD;
    float lat2Rad = lat2 * DEG_TO_RAD;
    float deltaLat = (lat2 - lat1) * DEG_TO_RAD;
    float deltaLon = (lon2 - lon1) * DEG_TO_RAD;
    
    float a = sin(deltaLat / 2) * sin(deltaLat / 2) +
              cos(lat1Rad) * cos(lat2Rad) *
              sin(deltaLon / 2) * sin(deltaLon / 2);
    float c = 2 * atan2(sqrt(a), sqrt(1 - a));
    
    return 6371000.0f * c;
}

float FlightControl::_bearingToRollError(float targetBearing, float currentHeading) {
    float error = targetBearing - currentHeading;
    
    if (error > 180) error -= 360;
    if (error < -180) error += 360;
    
    float rollCommand = error * 0.5f;
    return constrain(rollCommand, -MAX_ROLL_ANGLE, MAX_ROLL_ANGLE);
}

bool FlightControl::_navigateToWaypoint(const Waypoint& target, float dt) {
    if (!_state.gpsValid) return false;
    
    float distance = _calculateDistance(_state.latitude, _state.longitude,
                                         target.latitude, target.longitude);
    float bearing = _calculateBearing(_state.latitude, _state.longitude,
                                       target.latitude, target.longitude);
    
    if (distance < target.tolerance) {
        return true;
    }
    
    float rollError = _bearingToRollError(bearing, _state.heading);
    rollError = constrain(rollError, -MAX_ROLL_ANGLE, MAX_ROLL_ANGLE);
    
    _setpoints.roll = rollError;
    _setpoints.pitch = 5.0f;
    _setpoints.altitude = target.altitude;
    _setpoints.yaw = target.heading;
    
    _updateAltHold(dt);
    
    return false;
}

uint8_t FlightControl::_findNearestWaypoint(const Waypoint* waypoints, uint8_t count) {
    if (!_state.gpsValid || count == 0) return 0;
    
    float minDist = 1e9f;
    uint8_t nearest = 0;
    
    for (uint8_t i = 0; i < count; i++) {
        float dist = _calculateDistance(_state.latitude, _state.longitude,
                                         waypoints[i].latitude, waypoints[i].longitude);
        if (dist < minDist) {
            minDist = dist;
            nearest = i;
        }
    }
    
    return nearest;
}

/* =================================================================================
 * FUNÇÕES DO MODO DEBUG_AUTO_MANUAL
 * =================================================================================
 */

void FlightControl::setDebugTrajectory(const Waypoint* waypoints, uint8_t count) {
    if (count > 16) count = 16;
    _debugTrajectoryCount = count;
    for (uint8_t i = 0; i < count; i++) {
        _debugTrajectory[i] = waypoints[i];
    }
    _debugCurrentPoint = 0;
    DEBUG_PRINT("setDebugTrajectory: %d waypoints carregados\n", count);
}

void FlightControl::registerManualCommand() {
    _lastManualCommandMs = millis();
    if (!_manualOverrideActive) {
        _manualOverrideActive = true;
        DEBUG_PRINT("registerManualCommand: OVERRIDE MANUAL ATIVADO\n");
    }
}

uint32_t FlightControl::getDebugManualTimeoutRemaining() const {
    if (!_manualOverrideActive) return 0;
    uint32_t elapsed = millis() - _lastManualCommandMs;
    if (elapsed >= _debugManualTimeoutMs) return 0;
    return _debugManualTimeoutMs - elapsed;
}

bool FlightControl::isManualOverrideActive() const {
    return _manualOverrideActive;
}

void FlightControl::setDebugManualTimeout(uint32_t timeoutMs) {
    _debugManualTimeoutMs = timeoutMs;
    DEBUG_PRINT("setDebugManualTimeout: %lu ms\n", timeoutMs);
}

/* =================================================================================
 * GESTÃO DE MISSÃO (WAYPOINTS)
 * =================================================================================
 */

void FlightControl::setWaypoints(const Waypoint* waypoints, uint8_t count) {
    if (count > 32) count = 32;
    _waypointCount = count;
    for (uint8_t i = 0; i < count; i++) {
        _waypoints[i] = waypoints[i];
    }
    _currentWaypoint = 0;
    _missionActive = true;
    DEBUG_PRINT("setWaypoints: %d waypoints carregados, missão ativa\n", count);
}

void FlightControl::resetMission() {
    _currentWaypoint = 0;
    _missionActive = true;
    DEBUG_PRINT("resetMission: missão reiniciada do waypoint 0 (método de segurança)\n");
}

void FlightControl::resumeFromNearestWaypoint() {
    if (!_state.gpsValid || _waypointCount == 0) {
        DEBUG_PRINT("resumeFromNearestWaypoint: GPS inválido ou sem waypoints\n");
        return;
    }
    
    uint8_t nearest = _findNearestWaypoint(_waypoints, _waypointCount);
    _currentWaypoint = nearest;
    _missionActive = true;
    DEBUG_PRINT("resumeFromNearestWaypoint: retomando no waypoint %d (mais próximo)\n", nearest);
}

/* =================================================================================
 * VERIFICAÇÕES DE SEGURANÇA
 * =================================================================================
 */

bool FlightControl::_isOnGround() const {
    return (_state.altFused < FC_GROUND_ALTITUDE_THRESHOLD);
}

bool FlightControl::_isShellCommandSafe(uint8_t commandID) const {
    bool inFlight = !_isOnGround();
    
    switch (commandID) {
        case CMD_ARM:
        case CMD_DISARM:
            return !inFlight;
        case CMD_REBOOT:
        case CMD_SHUTDOWN:
            return !inFlight;
        case CMD_SET_PARAM:
            return !inFlight;
        default:
            return true;
    }
}

/* =================================================================================
 * FUNÇÕES AUXILIARES (PID, MIXER, SLEW RATE, TRIM)
 * =================================================================================
 */

float FlightControl::_runPID(PIDController& pid, float error, float dt, float limit) {
    float P = pid.Kp * error;
    
    pid.integral += pid.Ki * error * dt;
    pid.integral = constrain(pid.integral, pid.iMin, pid.iMax);
    float I = pid.integral;
    
    float rawD = (error - pid.lastError) / dt;
    pid.filteredD = pid.lpfAlpha * rawD + (1.0f - pid.lpfAlpha) * pid.filteredD;
    float D = pid.Kd * pid.filteredD;
    
    pid.lastError = error;
    
    float output = P + I + D;
    return constrain(output, -limit, limit);
}

float FlightControl::_fuseAltitude(float gps, float baro) {
    static float fused = 0.0f;
    static bool initialized = false;
    
    if (!initialized) {
        fused = baro;
        initialized = true;
    }
    
    const float alpha = 0.02f;
    fused = (1.0f - alpha) * fused + alpha * gps;
    
    return fused;
}

void FlightControl::_mixElevons(float pitchCorr, float rollCorr, uint16_t& elevonR, uint16_t& elevonL) {
    float r = FC_SERVO_NEUTRAL_US + pitchCorr + rollCorr;
    float l = FC_SERVO_NEUTRAL_US + pitchCorr - rollCorr;
    
    uint16_t targetR = constrain((int)r, FC_SERVO_MIN_US, FC_SERVO_MAX_US);
    uint16_t targetL = constrain((int)l, FC_SERVO_MIN_US, FC_SERVO_MAX_US);
    
    elevonR = _applySlewRate(elevonR, targetR, FC_SLEW_RATE_SERVO);
    elevonL = _applySlewRate(elevonL, targetL, FC_SLEW_RATE_SERVO);
}

uint16_t FlightControl::_applySlewRate(uint16_t current, uint16_t target, uint16_t maxDelta) {
    if (target > current) {
        return (target - current <= maxDelta) ? target : current + maxDelta;
    }
    if (target < current) {
        return (current - target <= maxDelta) ? target : current - maxDelta;
    }
    return current;
}

uint16_t FlightControl::_applyTrim(uint16_t value, int16_t trim, uint16_t minUs, uint16_t maxUs) {
    int32_t trimmed = (int32_t)value + trim;
    return (uint16_t)constrain(trimmed, (int32_t)minUs, (int32_t)maxUs);
}

/* =================================================================================
 * GETTERS E SETTERS
 * =================================================================================
 */

const ActuatorSignals& FlightControl::getOutputs() const { return _outputs; }
const FlightState& FlightControl::getState() const { return _state; }
const FlightSetpoints& FlightControl::getSetpoints() const { return _setpoints; }
const TrimValues& FlightControl::getTrim() const { return _trim; }
FlightMode FlightControl::getMode() const { return _mode; }

void FlightControl::setMode(FlightMode mode) {
    if (_mode == mode) return;
    
    DEBUG_PRINT("Mudança de modo: %d → %d\n", (int)_mode, (int)mode);
    
    _pidRoll.integral = _pidPitch.integral = _pidYaw.integral = _pidAltitude.integral = 0.0f;
    
    if (_mode == MODE_DEBUG_AUTO_MANUAL && mode != MODE_DEBUG_AUTO_MANUAL) {
        _manualOverrideActive = false;
    }
    
    if (mode == MODE_DEBUG_AUTO_MANUAL && _debugTrajectoryCount == 0) {
        DEBUG_PRINT("AVISO: Modo DEBUG_AUTO_MANUAL sem trajeto definido!\n");
    }
    
    _mode = mode;
}

void FlightControl::setSetpoints(const FlightSetpoints& sp) {
    _setpoints = sp;
}

void FlightControl::setTrim(const TrimValues& trim) {
    _trim = trim;
    DEBUG_PRINT("Trim atualizado\n");
}

void FlightControl::setPIDGains(uint8_t axisID, float Kp, float Ki, float Kd) {
    PIDController* pid = nullptr;
    switch (axisID) {
        case 0: pid = &_pidRoll; break;
        case 1: pid = &_pidPitch; break;
        case 2: pid = &_pidYaw; break;
        case 3: pid = &_pidAltitude; break;
        default: return;
    }
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->integral = 0.0f;
    DEBUG_PRINT("PID[%d] atualizado: Kp=%.3f Ki=%.3f Kd=%.3f\n", axisID, Kp, Ki, Kd);
}

void FlightControl::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug: %s\n", enable ? "ativado" : "desativado");
}
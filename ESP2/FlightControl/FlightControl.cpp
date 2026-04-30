/**
 * =================================================================================
 * FLIGHTCONTROL.CPP — IMPLEMENTAÇÃO DO CONTROLADOR DE VOO (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:      2026-04-30
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. Este controlador só é ativo quando o Failsafe assim o determina
 * 2. Suporta 4 modos: RTL, LAND, GLIDE, FREE_FALL
 * 3. Utiliza PIDs apenas para estabilização básica (roll/pitch)
 * 4. Mixer para avião convencional (ailerons + leme)
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
#define EARTH_RADIUS_M 6371000.0

/* =================================================================================
 * DEBUG
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

FlightControl::FlightControl()
    : _actuators(nullptr)
    , _si(nullptr)
    , _gps(nullptr)
    , _mode(MODE_IDLE)
    , _active(false)
    , _debugEnabled(false)
    , _rtlInProgress(false)
    , _lastDistanceToHome(0.0f)
    , _landingStartAlt(0.0f)
    , _landingStartTime(0)
{
    memset(&_state, 0, sizeof(FlightState));
    memset(&_setpoints, 0, sizeof(FlightSetpoints));
    memset(&_outputs, 0, sizeof(ActuatorSignals));
    
    /* Valores padrão seguros */
    _outputs.wingR = FC_SERVO_NEUTRAL_US;
    _outputs.wingL = FC_SERVO_NEUTRAL_US;
    _outputs.rudder = FC_SERVO_NEUTRAL_US;
    _outputs.motor = FC_MOTOR_STOP_US;
    
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO
 * =================================================================================
 */

void FlightControl::begin(Actuators* actuators, SIConverter* si, GPS* gps) {
    if (!actuators || !si || !gps) {
        DEBUG_PRINT("begin() falhou: ponteiros nulos\n");
        return;
    }
    
    _actuators = actuators;
    _si = si;
    _gps = gps;
    
    /* Configurar PIDs para estabilização básica */
    _pidRoll.Kp = 1.25f;   _pidRoll.Ki = 0.06f;   _pidRoll.Kd = 0.28f;
    _pidRoll.iMax = 160.0f; _pidRoll.iMin = -160.0f;
    _pidRoll.lpfAlpha = FC_LPF_ALPHA_ROLL;
    
    _pidPitch.Kp = 1.10f;  _pidPitch.Ki = 0.05f;  _pidPitch.Kd = 0.25f;
    _pidPitch.iMax = 160.0f; _pidPitch.iMin = -160.0f;
    _pidPitch.lpfAlpha = FC_LPF_ALPHA_PITCH;
    
    reset();
    
    DEBUG_PRINT("begin() — FlightControl inicializado (modo IDLE)\n");
}

/* =================================================================================
 * RESET — RESETA O CONTROLADOR
 * =================================================================================
 */

void FlightControl::reset() {
    _pidRoll.integral = 0.0f;
    _pidRoll.lastError = 0.0f;
    _pidRoll.filteredD = 0.0f;
    
    _pidPitch.integral = 0.0f;
    _pidPitch.lastError = 0.0f;
    _pidPitch.filteredD = 0.0f;
    
    _outputs.wingR = FC_SERVO_NEUTRAL_US;
    _outputs.wingL = FC_SERVO_NEUTRAL_US;
    _outputs.rudder = FC_SERVO_NEUTRAL_US;
    _outputs.motor = FC_MOTOR_STOP_US;
    
    _rtlInProgress = false;
    _lastDistanceToHome = 0.0f;
    _active = false;
    
    DEBUG_PRINT("reset() — FlightControl resetado\n");
}

/* =================================================================================
 * UPDATE STATE — ATUALIZA ESTADO ATUAL
 * =================================================================================
 */

void FlightControl::updateState() {
    if (!_si) return;
    
    const SIData& si = _si->getData();
    
    /* Dados de atitude a partir do SI (velocidades angulares integradas) */
    /* Nota: Em produção, o SIConverter deve fornecer ângulos reais */
    _state.roll = si.gyroX_dps * 0.01f;   /* Placeholder: integrar gyro */
    _state.pitch = si.gyroY_dps * 0.01f;
    _state.yaw = si.gyroZ_dps * 0.01f;
    _state.altFused = si.altitude_m;
    
    /* Dados de GPS */
    if (_gps && _gps->hasFix()) {
        const GPSData& gpsData = _gps->getData();
        _state.latitude = gpsData.latitude;
        _state.longitude = gpsData.longitude;
        _state.heading = gpsData.heading;
    }
    
    DEBUG_PRINT("updateState() — Roll=%.2f, Pitch=%.2f, Alt=%.2f\n",
                _state.roll, _state.pitch, _state.altFused);
}

/* =================================================================================
 * SET HOME POSITION — DEFINE PONTO DE ORIGEM
 * =================================================================================
 */

void FlightControl::setHomePosition(double lat, double lon, float alt) {
    _state.homeLat = lat;
    _state.homeLon = lon;
    _state.homeAlt = alt;
    _state.homeSet = true;
    
    DEBUG_PRINT("setHomePosition() — Home: %.6f, %.6f, %.1fm\n", lat, lon, alt);
}

/* =================================================================================
 * SET COMMAND — DEFINE COMANDO DO FAILSAFE
 * =================================================================================
 */

void FlightControl::setCommand(FlightMode mode, const FlightSetpoints& setpoints) {
    _mode = mode;
    _setpoints = setpoints;
    _active = true;
    
    /* Resetar PIDs ao mudar de modo */
    _pidRoll.integral = 0.0f;
    _pidPitch.integral = 0.0f;
    
    /* Configurações específicas por modo */
    switch (mode) {
        case MODE_RTL:
            _rtlInProgress = true;
            _lastDistanceToHome = 0.0f;
            DEBUG_PRINT("setCommand() — Modo RTL ativado\n");
            break;
            
        case MODE_LAND:
            _landingStartAlt = _state.altFused;
            _landingStartTime = millis();
            DEBUG_PRINT("setCommand() — Modo LAND ativado (alt=%.1fm)\n", _landingStartAlt);
            break;
            
        case MODE_GLIDE:
            DEBUG_PRINT("setCommand() — Modo GLIDE ativado\n");
            break;
            
        case MODE_FREE_FALL:
            DEBUG_PRINT("setCommand() — Modo FREE_FALL ativado\n");
            break;
            
        default:
            break;
    }
}

/* =================================================================================
 * RUN PID — EXECUTA PID COM FILTRO LPF
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

/* =================================================================================
 * MIX AILERONS — MISTURA ROLL PARA AILERONS
 * =================================================================================
 * 
 * Aileron Direito:  neutro + rollCorr
 * Aileron Esquerdo: neutro - rollCorr
 */

void FlightControl::_mixAilerons(float rollCorr, uint16_t& wingR, uint16_t& wingL) {
    float r = FC_SERVO_NEUTRAL_US + rollCorr;
    float l = FC_SERVO_NEUTRAL_US - rollCorr;
    
    wingR = constrain((int)r, FC_SERVO_MIN_US, FC_SERVO_MAX_US);
    wingL = constrain((int)l, FC_SERVO_MIN_US, FC_SERVO_MAX_US);
}

/* =================================================================================
 * APPLY OUTPUTS — APLICA SINAIS AOS ATUADORES
 * =================================================================================
 */

void FlightControl::_applyOutputs() {
    if (_actuators && _actuators->isArmed() && !_actuators->isFailsafeActive()) {
        _actuators->update(_outputs);
    }
}

/* =================================================================================
 * CÁLCULO DE DISTÂNCIA (HAVERSINE)
 * =================================================================================
 */

float FlightControl::_calculateDistance(float lat1, float lon1, float lat2, float lon2) {
    float lat1Rad = lat1 * DEG_TO_RAD;
    float lat2Rad = lat2 * DEG_TO_RAD;
    float deltaLat = (lat2 - lat1) * DEG_TO_RAD;
    float deltaLon = (lon2 - lon1) * DEG_TO_RAD;
    
    float a = sin(deltaLat / 2) * sin(deltaLat / 2) +
              cos(lat1Rad) * cos(lat2Rad) *
              sin(deltaLon / 2) * sin(deltaLon / 2);
    float c = 2 * atan2(sqrt(a), sqrt(1 - a));
    
    return EARTH_RADIUS_M * c;
}

/* =================================================================================
 * CÁLCULO DE BEARING
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

/* =================================================================================
 * BEARING TO ROLL ERROR
 * =================================================================================
 */

float FlightControl::_bearingToRollError(float targetBearing, float currentHeading) {
    float error = targetBearing - currentHeading;
    if (error > 180) error -= 360;
    if (error < -180) error += 360;
    
    /* Ganho de navegação: erro de 90° → roll de 45° */
    float rollCommand = error * 0.5f;
    return constrain(rollCommand, -MAX_ROLL_ANGLE, MAX_ROLL_ANGLE);
}

/* =================================================================================
 * UPDATE RTL — RETURN TO LAUNCH
 * =================================================================================
 */

void FlightControl::_updateRTL(float dt) {
    if (!_state.homeSet) {
        DEBUG_PRINT("_updateRTL() — Home não definido! Usando posição atual\n");
        setHomePosition(_state.latitude, _state.longitude, _state.altFused);
    }
    
    float distance = _calculateDistance(_state.latitude, _state.longitude,
                                         _state.homeLat, _state.homeLon);
    
    /* Verificar se chegou a casa */
    if (distance < RTL_TOLERANCE) {
        DEBUG_PRINT("_updateRTL() — Chegou a casa! Iniciando aterragem\n");
        FlightSetpoints sp;
        sp.altitude = 0.0f;
        sp.throttle = 0.3f;
        setCommand(MODE_LAND, sp);
        return;
    }
    
    /* Calcular bearing para casa */
    float bearing = _calculateBearing(_state.latitude, _state.longitude,
                                       _state.homeLat, _state.homeLon);
    
    /* Converter bearing para erro de roll */
    float rollError = _bearingToRollError(bearing, _state.heading);
    _setpoints.roll = constrain(rollError, -MAX_ROLL_ANGLE, MAX_ROLL_ANGLE);
    
    /* Pitch para manter velocidade */
    _setpoints.pitch = 5.0f;
    
    /* Throttle de cruzeiro */
    _setpoints.throttle = 0.5f;
    
    /* Altitude alvo para RTL */
    _setpoints.altitude = RTL_ALTITUDE;
    
    DEBUG_PRINT("_updateRTL() — Dist=%.1fm, Roll=%.1f°, Pitch=%.1f°, Throttle=%.2f\n",
                distance, _setpoints.roll, _setpoints.pitch, _setpoints.throttle);
}

/* =================================================================================
 * UPDATE LAND — ATERRAGEM FORÇADA
 * =================================================================================
 */

void FlightControl::_updateLand(float dt) {
    float descentTime = (_landingStartAlt - _state.altFused) / LAND_DESCENT_RATE;
    float throttle = 0.3f;
    
    /* Reduzir throttle à medida que desce */
    if (_state.altFused < 10.0f) {
        throttle = 0.2f;
    }
    if (_state.altFused < 5.0f) {
        throttle = 0.1f;
    }
    if (_state.altFused < 2.0f) {
        throttle = 0.0f;
    }
    
    _setpoints.throttle = throttle;
    _setpoints.roll = 0.0f;
    _setpoints.pitch = 0.0f;
    
    /* Verificar se já aterrou */
    if (_state.altFused < 0.5f && _actuators) {
        DEBUG_PRINT("_updateLand() — Aterragem concluída! Desarmando\n");
        _actuators->disarm();
        _active = false;
    }
    
    DEBUG_PRINT("_updateLand() — Alt=%.1fm, Throttle=%.2f\n", _state.altFused, throttle);
}

/* =================================================================================
 * UPDATE GLIDE — PLANAGEM CONTROLADA
 * =================================================================================
 */

void FlightControl::_updateGlide(float dt) {
    /* Planagem: motor desligado, apenas estabilidade */
    _setpoints.throttle = 0.0f;
    _setpoints.roll = 0.0f;
    _setpoints.pitch = 0.0f;
    
    /* Limites reduzidos para planagem */
    float rollLimit = GLIDE_ROLL_ANGLE;
    float pitchLimit = GLIDE_PITCH_ANGLE;
    
    /* Erros de atitude */
    float errRoll = _setpoints.roll - _state.roll;
    float errPitch = _setpoints.pitch - _state.pitch;
    
    /* Aplicar limites reduzidos */
    errRoll = constrain(errRoll, -rollLimit, rollLimit);
    errPitch = constrain(errPitch, -pitchLimit, pitchLimit);
    
    float corrRoll = _runPID(_pidRoll, errRoll, dt, FC_PID_ROLL_LIMIT);
    float corrPitch = _runPID(_pidPitch, errPitch, dt, FC_PID_PITCH_LIMIT);
    
    /* Mixer de ailerons (apenas roll) */
    _mixAilerons(corrRoll, _outputs.wingR, _outputs.wingL);
    
    /* Pitch não utilizado em planagem (sem elevador) */
    
    DEBUG_PRINT("_updateGlide() — Roll=%.2f°, CorrRoll=%.2f\n", _state.roll, corrRoll);
}

/* =================================================================================
 * UPDATE FREE FALL — CORTE TOTAL
 * =================================================================================
 */

void FlightControl::_updateFreeFall(float dt) {
    /* Corte total: desarmar atuadores */
    _setpoints.throttle = 0.0f;
    _setpoints.roll = 0.0f;
    _setpoints.pitch = 0.0f;
    
    _outputs.wingR = FC_SERVO_NEUTRAL_US;
    _outputs.wingL = FC_SERVO_NEUTRAL_US;
    _outputs.rudder = FC_SERVO_NEUTRAL_US;
    _outputs.motor = FC_MOTOR_STOP_US;
    
    if (_actuators) {
        _actuators->failsafeBlock();
    }
    
    DEBUG_PRINT("_updateFreeFall() —⚠️ CORTE TOTAL ATIVADO ⚠️\n");
}

/* =================================================================================
 * UPDATE — CICLO PRINCIPAL
 * =================================================================================
 */

void FlightControl::update(float dt) {
    if (!_active) return;
    
    /* Atualizar estado atual */
    updateState();
    
    /* Executar controlador conforme modo */
    switch (_mode) {
        case MODE_RTL:
            _updateRTL(dt);
            break;
        case MODE_LAND:
            _updateLand(dt);
            break;
        case MODE_GLIDE:
            _updateGlide(dt);
            break;
        case MODE_FREE_FALL:
            _updateFreeFall(dt);
            break;
        default:
            return;
    }
    
    /* Calcular erros de atitude */
    float errRoll = _setpoints.roll - _state.roll;
    float errPitch = _setpoints.pitch - _state.pitch;
    
    /* Executar PIDs */
    float corrRoll = _runPID(_pidRoll, errRoll, dt, FC_PID_ROLL_LIMIT);
    float corrPitch = _runPID(_pidPitch, errPitch, dt, FC_PID_PITCH_LIMIT);
    
    /* Aplicar mixer (ailerons para roll) */
    _mixAilerons(corrRoll, _outputs.wingR, _outputs.wingL);
    
    /* Aplicar throttle */
    _outputs.motor = FC_MOTOR_STOP_US + _setpoints.throttle * (FC_MOTOR_MAX_US - FC_MOTOR_STOP_US);
    _outputs.motor = constrain(_outputs.motor, FC_MOTOR_STOP_US, FC_MOTOR_MAX_US);
    
    /* Aplicar aos atuadores */
    _applyOutputs();
    
    if (_debugEnabled && (millis() % 1000 < 20)) {
        DEBUG_PRINT("update() — Mode=%d, Roll=%.2f, Pitch=%.2f, Throttle=%.2f\n",
                    _mode, _setpoints.roll, _setpoints.pitch, _setpoints.throttle);
    }
}

/* =================================================================================
 * GETTERS
 * =================================================================================
 */

FlightMode FlightControl::getMode() const {
    return _mode;
}

ActuatorSignals FlightControl::getOutputs() const {
    return _outputs;
}

const FlightState& FlightControl::getState() const {
    return _state;
}

const FlightSetpoints& FlightControl::getSetpoints() const {
    return _setpoints;
}

bool FlightControl::isActive() const {
    return _active;
}

void FlightControl::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* flightModeToString(FlightMode mode) {
    switch (mode) {
        case MODE_IDLE:      return "IDLE";
        case MODE_RTL:       return "RTL";
        case MODE_LAND:      return "LAND";
        case MODE_GLIDE:     return "GLIDE";
        case MODE_FREE_FALL: return "FREE_FALL";
        default:             return "UNKNOWN";
    }
}
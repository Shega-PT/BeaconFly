/**
 * =================================================================================
 * ACTUATORS.CPP — IMPLEMENTAÇÃO DO CONTROLO DE ATUADORES (ESP2)
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
 * 1. Utiliza LEDC (periférico hardware PWM do ESP32) — NÃO BLOQUEANTE
 * 2. 4 canais: Aileron D, Aileron E, Leme, Motor
 * 3. Slew rate para suavizar movimentos bruscos
 * 4. Failsafe bloqueia fisicamente os canais (detach)
 * 
 * =================================================================================
 */

#include "Actuators.h"

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define ACTUATORS_DEBUG

#ifdef ACTUATORS_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[Actuators] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * TABELA DE PINOS (ordem corresponde a ActuatorChannel)
 * =================================================================================
 */

const uint8_t Actuators::_pins[ACTUATOR_COUNT] = {
    PIN_SERVO_WING_R,   /* CH_WING_R   */
    PIN_SERVO_WING_L,   /* CH_WING_L   */
    PIN_SERVO_RUDDER,   /* CH_RUDDER   */
    PIN_MOTOR_ECU       /* CH_MOTOR    */
};

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

Actuators::Actuators()
    : _armed(false)
    , _failsafeActive(false)
    , _debugEnabled(false)
{
    /* Inicializar _lastSignals com posição segura */
    _lastSignals.wingR   = PWM_SERVO_NEUTRAL;
    _lastSignals.wingL   = PWM_SERVO_NEUTRAL;
    _lastSignals.rudder  = PWM_SERVO_NEUTRAL;
    _lastSignals.motor   = PWM_MOTOR_STOP;
    
    /* Inicializar _currentPWM com os mesmos valores */
    _currentPWM = _lastSignals;
    
    DEBUG_PRINT("Construtor — estado inicial: DESARMADO, FAILSAFE=OFF\n");
}

/* =================================================================================
 * CAN SEND SIGNALS — Verifica se é seguro enviar sinais
 * =================================================================================
 */

bool Actuators::_canSendSignals() const {
    return (_armed && !_failsafeActive);
}

/* =================================================================================
 * APPLY SLEW RATE — Aplica suavização a um canal
 * =================================================================================
 */

uint16_t Actuators::_applySlewRate(uint16_t current, uint16_t target, uint16_t maxDelta) {
    if (target > current) {
        return (target - current <= maxDelta) ? target : current + maxDelta;
    }
    if (target < current) {
        return (current - target <= maxDelta) ? target : current - maxDelta;
    }
    return current;
}

/* =================================================================================
 * SETUP PWM — Configuração dos canais LEDC
 * =================================================================================
 */

void Actuators::_setupPWM() {
    DEBUG_PRINT("_setupPWM() — Configurando LEDC (ESP32)\n");
    
    for (uint8_t i = 0; i < ACTUATOR_COUNT; i++) {
        /* Determinar frequência por canal (motor pode ser diferente) */
        uint32_t freq = (i == CH_MOTOR) ? PWM_FREQUENCY_MOTOR : PWM_FREQUENCY_SERVO;
        
        /* Configurar canal LEDC */
        ledcSetup(i, freq, PWM_RESOLUTION_BITS);
        
        /* Associar pino ao canal */
        ledcAttachPin(_pins[i], i);
        
        DEBUG_PRINT("  Canal %d: pino=%d, freq=%d Hz\n", i, _pins[i], freq);
    }
}

/* =================================================================================
 * WRITE CHANNEL — Escreve valor PWM num canal (com constrain e slew rate)
 * =================================================================================
 */

void Actuators::_writeChannel(uint8_t channel, uint16_t us, uint16_t minUs, uint16_t maxUs) {
    if (channel >= ACTUATOR_COUNT) {
        DEBUG_PRINT("ERRO: _writeChannel — canal inválido %d\n", channel);
        return;
    }
    
    /* Constrain do valor para limites seguros */
    uint16_t safeUs = constrain(us, minUs, maxUs);
    
    /* Obter valor atual do canal (se disponível) */
    uint16_t* currentPtr = nullptr;
    switch (channel) {
        case CH_WING_R:  currentPtr = &_currentPWM.wingR; break;
        case CH_WING_L:  currentPtr = &_currentPWM.wingL; break;
        case CH_RUDDER:  currentPtr = &_currentPWM.rudder; break;
        case CH_MOTOR:   currentPtr = &_currentPWM.motor; break;
    }
    
    /* Aplicar slew rate se tivermos valor atual */
    if (currentPtr) {
        uint16_t slewLimit = (channel == CH_MOTOR) ? SLEW_RATE_MOTOR : SLEW_RATE_SERVO;
        safeUs = _applySlewRate(*currentPtr, safeUs, slewLimit);
    }
    
    /* Converter microssegundos para ticks do LEDC */
    uint32_t ticks = US_TO_TICKS(safeUs);
    
    /* Escrever no hardware — NÃO BLOQUEANTE */
    ledcWrite(channel, ticks);
    
    /* Atualizar valor atual */
    if (currentPtr) *currentPtr = safeUs;
    
    if (_debugEnabled && safeUs != us) {
        DEBUG_PRINT("_writeChannel: canal %d, us=%d → constrain para %d\n", channel, us, safeUs);
    }
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO DO SISTEMA
 * =================================================================================
 */

void Actuators::begin() {
    DEBUG_PRINT("begin() — Inicializando sistema de atuadores (ESP2)...\n");
    
    /* Configurar hardware PWM */
    _setupPWM();
    
    /* Garantir posição segura antes de qualquer operação */
    safePosition();
    
    /* Estado inicial: desarmado, sem failsafe */
    _armed = false;
    _failsafeActive = false;
    
    DEBUG_PRINT("begin() — concluído. Sistema DESARMADO.\n");
    DEBUG_PRINT("  Atuadores: Aileron D, Aileron E, Leme, Motor\n");
    DEBUG_PRINT("  Servos: %d-%d µs | Motor: %d-%d µs\n", 
                PWM_SERVO_MIN, PWM_SERVO_MAX, PWM_MOTOR_STOP, PWM_MOTOR_MAX);
}

/* =================================================================================
 * ARM — ARMAR O SISTEMA
 * =================================================================================
 */

void Actuators::arm() {
    if (_failsafeActive) {
        DEBUG_PRINT("arm() BLOQUEADO — Failsafe ativo. Execute failsafeClear() primeiro.\n");
        return;
    }
    
    if (_armed) {
        DEBUG_PRINT("arm() ignorado — sistema já armado.\n");
        return;
    }
    
    DEBUG_PRINT("arm() — Iniciando sequência de armamento...\n");
    
    /* Pulso de armamento do ESC (1000µs por aproximadamente 2 segundos) */
    _writeChannel(CH_MOTOR, PWM_MOTOR_ARM, PWM_MOTOR_STOP, PWM_MOTOR_MAX);
    delay(2000);  /* Aguardar ESC processar o pulso de armamento */
    
    _armed = true;
    DEBUG_PRINT("arm() — SISTEMA ARMADO com sucesso.\n");
}

/* =================================================================================
 * DISARM — DESARMAR O SISTEMA
 * =================================================================================
 */

void Actuators::disarm() {
    if (!_armed && !_failsafeActive) {
        DEBUG_PRINT("disarm() ignorado — sistema já desarmado.\n");
        return;
    }
    
    DEBUG_PRINT("disarm() — Desarmando sistema...\n");
    
    /* Forçar posição segura antes de desarmar */
    safePosition();
    
    _armed = false;
    DEBUG_PRINT("disarm() — Sistema DESARMADO.\n");
}

/* =================================================================================
 * UPDATE — ATUALIZAÇÃO PRINCIPAL (CAMINHO CRÍTICO)
 * =================================================================================
 */

void Actuators::update(const ActuatorSignals& signals) {
    /* Verificar se é seguro enviar sinais */
    if (!_canSendSignals()) {
        if (_debugEnabled && !_armed && !_failsafeActive) {
            DEBUG_PRINT("update() ignorado — sistema não armado.\n");
        } else if (_debugEnabled && _failsafeActive) {
            DEBUG_PRINT("update() BLOQUEADO — Failsafe ativo.\n");
        }
        return;
    }
    
    /* Aplicar constrain e escrever nos canais */
    _writeChannel(CH_WING_R,   signals.wingR,   PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writeChannel(CH_WING_L,   signals.wingL,   PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writeChannel(CH_RUDDER,   signals.rudder,  PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writeChannel(CH_MOTOR,    signals.motor,   PWM_MOTOR_STOP, PWM_MOTOR_MAX);
    
    /* Guardar último estado para telemetria */
    _lastSignals = signals;
    
    if (_debugEnabled) {
        DEBUG_PRINT("update → WR:%d WL:%d Rud:%d Mot:%d\n",
                    signals.wingR, signals.wingL, signals.rudder, signals.motor);
    }
}

/* =================================================================================
 * SAFE POSITION — POSIÇÃO SEGURA IMEDIATA
 * =================================================================================
 */

void Actuators::safePosition() {
    DEBUG_PRINT("safePosition() — Aplicando posição segura...\n");
    
    /* Ailerons para neutro */
    _writeChannel(CH_WING_R,   PWM_SERVO_NEUTRAL, PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writeChannel(CH_WING_L,   PWM_SERVO_NEUTRAL, PWM_SERVO_MIN, PWM_SERVO_MAX);
    
    /* Leme para neutro */
    _writeChannel(CH_RUDDER,   PWM_SERVO_NEUTRAL, PWM_SERVO_MIN, PWM_SERVO_MAX);
    
    /* Motor para STOP */
    _writeChannel(CH_MOTOR,    PWM_MOTOR_STOP, PWM_MOTOR_STOP, PWM_MOTOR_MAX);
    
    /* Atualizar último estado conhecido */
    _lastSignals.wingR   = PWM_SERVO_NEUTRAL;
    _lastSignals.wingL   = PWM_SERVO_NEUTRAL;
    _lastSignals.rudder  = PWM_SERVO_NEUTRAL;
    _lastSignals.motor   = PWM_MOTOR_STOP;
    
    /* Também atualizar valores atuais para o slew rate */
    _currentPWM = _lastSignals;
    
    DEBUG_PRINT("safePosition() — Posição segura aplicada.\n");
}

/* =================================================================================
 * FAILSAFE BLOCK — ATIVA BLOQUEIO DE EMERGÊNCIA
 * =================================================================================
 */

void Actuators::failsafeBlock() {
    if (_failsafeActive) {
        DEBUG_PRINT("failsafeBlock() ignorado — já ativo.\n");
        return;
    }
    
    DEBUG_PRINT("⚠️⚠️⚠️ FAILSAFE BLOCK ATIVADO ⚠️⚠️⚠️\n");
    DEBUG_PRINT("failsafeBlock() — Bloqueando todas as saídas.\n");
    
    /* Garantir posição segura antes de bloquear */
    safePosition();
    
    /* Detachar canais LEDC para segurança máxima */
    for (uint8_t i = 0; i < ACTUATOR_COUNT; i++) {
        ledcDetachPin(_pins[i]);
        pinMode(_pins[i], OUTPUT);
        digitalWrite(_pins[i], LOW);
    }
    DEBUG_PRINT("failsafeBlock() — Canais LEDC detachados.\n");
    
    _failsafeActive = true;
    _armed = false;
    
    DEBUG_PRINT("failsafeBlock() — Sistema em modo de SEGURANÇA MÁXIMA.\n");
}

/* =================================================================================
 * FAILSAFE CLEAR — REMOVE BLOQUEIO DE EMERGÊNCIA
 * =================================================================================
 */

void Actuators::failsafeClear() {
    if (!_failsafeActive) {
        DEBUG_PRINT("failsafeClear() ignorado — failsafe não estava ativo.\n");
        return;
    }
    
    DEBUG_PRINT("failsafeClear() — Removendo bloqueio de failsafe...\n");
    
    /* Reconfigurar canais LEDC */
    _setupPWM();
    DEBUG_PRINT("failsafeClear() — Canais LEDC reconfigurados.\n");
    
    _failsafeActive = false;
    _armed = false;     /* Exige novo armamento explícito */
    
    /* Garantir posição segura após sair do failsafe */
    safePosition();
    
    DEBUG_PRINT("failsafeClear() — FAILSAFE REMOVIDO. Sistema desarmado.\n");
    DEBUG_PRINT("failsafeClear() — Execute arm() para retomar operação.\n");
}

/* =================================================================================
 * GETTERS
 * =================================================================================
 */

bool Actuators::isArmed() const {
    return _armed;
}

bool Actuators::isFailsafeActive() const {
    return _failsafeActive;
}

ActuatorSignals Actuators::getLastSignals() const {
    return _lastSignals;
}

void Actuators::getCurrentPWM(uint16_t* wingR, uint16_t* wingL, uint16_t* rudder, uint16_t* motor) const {
    if (wingR) *wingR = _currentPWM.wingR;
    if (wingL) *wingL = _currentPWM.wingL;
    if (rudder) *rudder = _currentPWM.rudder;
    if (motor) *motor = _currentPWM.motor;
}

/* =================================================================================
 * SET DEBUG — ATIVA/DESATIVA DEBUG
 * =================================================================================
 */

void Actuators::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* actuatorChannelToString(ActuatorChannel channel) {
    switch (channel) {
        case CH_WING_R:  return "WING_R (Aileron D)";
        case CH_WING_L:  return "WING_L (Aileron E)";
        case CH_RUDDER:  return "RUDDER (Leme)";
        case CH_MOTOR:   return "MOTOR (ESC)";
        default:         return "UNKNOWN";
    }
}
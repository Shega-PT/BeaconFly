/**
 * =================================================================================
 * ACTUATORS.CPP — IMPLEMENTAÇÃO DO CONTROLO DE ATUADORES
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-17
 * VERSÃO:     2.0.0
 * 
 * =================================================================================
 * DESCRIÇÃO
 * =================================================================================
 * 
 * Implementação do controlo de servos e motor com suporte multiplataforma:
 *   - ESP32: Periférico LEDC (hardware PWM) — NÃO BLOQUEANTE, DETERMINÍSTICO
 *   - Outras: PWM por software com timer (fallback portável)
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. ESP32 (LEDC):
 *    - Configuração de 6 canais (0-5) com resolução de 16 bits
 *    - Frequência: 50Hz para servos e motor (configurável)
 *    - Conversão µs → ticks: US_TO_TICKS(us)
 *    - Não bloqueante — o hardware gera o sinal autonomamente
 * 
 * 2. Outras plataformas:
 *    - Fallback para PWM por software com timer (em desenvolvimento)
 *    - Atualmente usa método básico (avisar em compile time)
 * 
 * 3. Segurança:
 *    - Constrain em todos os valores de saída
 *    - Failsafe bloqueia fisicamente os canais (detach no ESP32)
 *    - Posição segura garantida em disarm e failsafe
 * 
 * =================================================================================
 */

#include "Actuators.h"

/* =================================================================================
 * DEBUG CONFIGURÁVEL
 * =================================================================================
 * 
 * Define ACTUATORS_DEBUG para ativar mensagens de diagnóstico.
 * O prefixo "[Actuators]" permite filtrar no monitor série.
 */

#define ACTUATORS_DEBUG

#ifdef ACTUATORS_DEBUG
    #define DEBUG_PRINT(fmt, ...) Serial.printf("[Actuators] " fmt, ##__VA_ARGS__)
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
    PIN_SERVO_ELEVON_R, /* CH_ELEVON_R */
    PIN_SERVO_ELEVON_L, /* CH_ELEVON_L */
    PIN_MOTOR_ECU       /* CH_MOTOR    */
};

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 * 
 * Inicializa todas as variáveis de estado com valores seguros.
 * O hardware ainda não foi configurado — isso ocorre em begin().
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
    _lastSignals.elevonR = PWM_SERVO_NEUTRAL;
    _lastSignals.elevonL = PWM_SERVO_NEUTRAL;
    _lastSignals.motor   = PWM_MOTOR_STOP;
    
    DEBUG_PRINT("Construtor — estado inicial: DESARMADO, FAILSAFE=OFF\n");
}

/* =================================================================================
 * _CAN SEND SIGNALS — Verifica se é seguro enviar sinais
 * =================================================================================
 */

bool Actuators::_canSendSignals() const {
    return (_armed && !_failsafeActive);
}

/* =================================================================================
 * _SETUP PWM — Configuração específica por plataforma
 * =================================================================================
 */

void Actuators::_setupPWM() {
#ifdef ESP32
    /* =====================================================================
     * ESP32: Configuração do periférico LEDC (hardware PWM)
     * =====================================================================
     * 
     * LEDC (LED Controller) é um periférico dedicado do ESP32 para gerar
     * sinais PWM com alta precisão e sem intervenção da CPU após configurado.
     * 
     * Vantagens:
     *   - Não bloqueante: o hardware gera o sinal autonomamente
     *   - Determinístico: não afeta o loop de 100Hz
     *   - Precisão: resolução de 16 bits (0.305µs por tick)
     */
    
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
    
#else
    /* =====================================================================
     * OUTRAS PLATAFORMAS: PWM por software (fallback portável)
     * =====================================================================
     * 
     * Para Arduinos, RP2040, Jetson, etc., utilizamos PWM por software
     * com timers para minimizar o bloqueio.
     * 
     * NOTA: Esta implementação é menos precisa que o LEDC do ESP32,
     *       mas garante portabilidade para todos os targets.
     */
    
    DEBUG_PRINT("_setupPWM() — Configurando PWM por software (fallback)\n");
    
    /* Configurar todos os pinos como saída digital */
    for (uint8_t i = 0; i < ACTUATOR_COUNT; i++) {
        pinMode(_pins[i], OUTPUT);
        digitalWrite(_pins[i], LOW);
        DEBUG_PRINT("  Pino %d configurado como OUTPUT\n", _pins[i]);
    }
    
    /* TODO: Configurar timer para gerar PWM por interrupção */
    /* Por agora, usamos método básico (com aviso) */
    DEBUG_PRINT("  AVISO: Usando delayMicroseconds() — pode afetar determinismo!\n");
#endif
}

/* =================================================================================
 * _WRITE LEDC (ESP32) — Escreve valor PWM via hardware
 * =================================================================================
 */

#ifdef ESP32
void Actuators::_writeLEDC(uint8_t channel, uint16_t us, uint16_t minUs, uint16_t maxUs) {
    /* Validar canal */
    if (channel >= ACTUATOR_COUNT) {
        DEBUG_PRINT("ERRO: _writeLEDC — canal inválido %d\n", channel);
        return;
    }
    
    /* Constrain do valor para limites seguros */
    uint16_t safeUs = constrain(us, minUs, maxUs);
    
    /* Converter microssegundos para ticks do LEDC */
    uint32_t ticks = US_TO_TICKS(safeUs);
    
    /* Escrever no hardware — NÃO BLOQUEANTE */
    ledcWrite(channel, ticks);
    
    if (_debugEnabled && safeUs != us) {
        DEBUG_PRINT("_writeLEDC: canal %d, us=%d → constrain para %d\n", 
                    channel, us, safeUs);
    }
}
#endif

/* =================================================================================
 * _WRITE PWM — Função genérica (dispatcher por plataforma)
 * =================================================================================
 */

void Actuators::_writePWM(uint8_t pin, uint16_t us, uint16_t minUs, uint16_t maxUs) {
    /* Constrain do valor para limites seguros — camada de segurança */
    uint16_t safeUs = constrain(us, minUs, maxUs);
    
#ifdef ESP32
    /* =====================================================================
     * ESP32: Usar LEDC
     * =====================================================================
     * 
     * Encontrar o canal correspondente ao pino.
     * Como temos um mapeamento fixo, usamos o índice no array _pins.
     */
    for (uint8_t i = 0; i < ACTUATOR_COUNT; i++) {
        if (_pins[i] == pin) {
            _writeLEDC(i, safeUs, minUs, maxUs);
            return;
        }
    }
    DEBUG_PRINT("ERRO: _writePWM — pino %d não encontrado nos canais\n", pin);
    
#else
    /* =====================================================================
     * OUTRAS PLATAFORMAS: PWM por software com delayMicroseconds
     * =====================================================================
     * 
     * ATENÇÃO: Este método é BLOQUEANTE e AFETA O DETERMINISMO.
     *          Utilizar apenas em plataformas sem suporte a hardware PWM.
     *          Para produção no ESP32, use a implementação LEDC acima.
     */
    
    /* Garantir que o período total é respeitado (50Hz = 20ms) */
    uint32_t highTime = safeUs;
    uint32_t lowTime = PWM_PERIOD_US - safeUs;
    
    digitalWrite(pin, HIGH);
    delayMicroseconds(highTime);
    digitalWrite(pin, LOW);
    delayMicroseconds(lowTime);
    
    if (_debugEnabled && safeUs != us) {
        DEBUG_PRINT("_writePWM: pino %d, us=%d → constrain para %d\n", pin, us, safeUs);
    }
#endif
}

/* =================================================================================
 * BEGIN — Inicialização do sistema
 * =================================================================================
 */

void Actuators::begin() {
    DEBUG_PRINT("begin() — Inicializando sistema de atuadores...\n");
    
    /* Configurar hardware PWM */
    _setupPWM();
    
    /* Garantir posição segura antes de qualquer operação */
    safePosition();
    
    /* Estado inicial: desarmado, sem failsafe */
    _armed = false;
    _failsafeActive = false;
    
    DEBUG_PRINT("begin() — concluído. Sistema DESARMADO.\n");
}

/* =================================================================================
 * ARM — Armar o sistema
 * =================================================================================
 * 
 * Sequência de armamento:
 *   1. Verificar se failsafe está ativo (bloqueia)
 *   2. Verificar se já está armado (ignora)
 *   3. Enviar pulso de armamento ao ESC (1000µs por 2 segundos)
 *   4. Marcar sistema como armado
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
    _writePWM(PIN_MOTOR_ECU, PWM_MOTOR_ARM, PWM_MOTOR_STOP, PWM_MOTOR_MAX);
    delay(2000);  /* Aguardar ESC processar o pulso de armamento */
    
    _armed = true;
    DEBUG_PRINT("arm() — SISTEMA ARMADO com sucesso.\n");
}

/* =================================================================================
 * DISARM — Desarmar o sistema
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
 * UPDATE — Atualização principal (caminho crítico)
 * =================================================================================
 * 
 * Esta função é chamada a 100Hz pelo FlightControl.
 * Deve ser o mais rápida possível — no ESP32, o LEDC torna-a não bloqueante.
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
    
    /* Servos — com constrain automático */
    _writePWM(PIN_SERVO_WING_R,   signals.wingR,   PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writePWM(PIN_SERVO_WING_L,   signals.wingL,   PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writePWM(PIN_SERVO_RUDDER,   signals.rudder,  PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writePWM(PIN_SERVO_ELEVON_R, signals.elevonR, PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writePWM(PIN_SERVO_ELEVON_L, signals.elevonL, PWM_SERVO_MIN, PWM_SERVO_MAX);
    
    /* Motor — limite mínimo é STOP, não MIN (para segurança) */
    _writePWM(PIN_MOTOR_ECU, signals.motor, PWM_MOTOR_STOP, PWM_MOTOR_MAX);
    
    /* Guardar último estado para telemetria */
    _lastSignals = signals;
    
    if (_debugEnabled) {
        DEBUG_PRINT("update → WR:%d WL:%d Rud:%d ER:%d EL:%d Mot:%d\n",
                    signals.wingR, signals.wingL, signals.rudder,
                    signals.elevonR, signals.elevonL, signals.motor);
    }
}

/* =================================================================================
 * SAFE POSITION — Posição segura imediata
 * =================================================================================
 * 
 * Coloca todos os atuadores em posição segura:
 *   - Servos: neutro (1500µs)
 *   - Motor: STOP (1000µs)
 */

void Actuators::safePosition() {
    DEBUG_PRINT("safePosition() — Aplicando posição segura...\n");
    
    /* Servos para neutro */
    _writePWM(PIN_SERVO_WING_R,   PWM_SERVO_NEUTRAL, PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writePWM(PIN_SERVO_WING_L,   PWM_SERVO_NEUTRAL, PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writePWM(PIN_SERVO_RUDDER,   PWM_SERVO_NEUTRAL, PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writePWM(PIN_SERVO_ELEVON_R, PWM_SERVO_NEUTRAL, PWM_SERVO_MIN, PWM_SERVO_MAX);
    _writePWM(PIN_SERVO_ELEVON_L, PWM_SERVO_NEUTRAL, PWM_SERVO_MIN, PWM_SERVO_MAX);
    
    /* Motor para STOP */
    _writePWM(PIN_MOTOR_ECU, PWM_MOTOR_STOP, PWM_MOTOR_STOP, PWM_MOTOR_MAX);
    
    /* Atualizar último estado conhecido */
    _lastSignals.wingR   = PWM_SERVO_NEUTRAL;
    _lastSignals.wingL   = PWM_SERVO_NEUTRAL;
    _lastSignals.rudder  = PWM_SERVO_NEUTRAL;
    _lastSignals.elevonR = PWM_SERVO_NEUTRAL;
    _lastSignals.elevonL = PWM_SERVO_NEUTRAL;
    _lastSignals.motor   = PWM_MOTOR_STOP;
    
    DEBUG_PRINT("safePosition() — Posição segura aplicada.\n");
}

/* =================================================================================
 * FAILSAFE BLOCK — Ativa bloqueio de emergência
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
    
#ifdef ESP32
    /* ESP32: Detachar canais LEDC para segurança máxima */
    for (uint8_t i = 0; i < ACTUATOR_COUNT; i++) {
        ledcDetachPin(_pins[i]);
        pinMode(_pins[i], OUTPUT);
        digitalWrite(_pins[i], LOW);
    }
    DEBUG_PRINT("failsafeBlock() — Canais LEDC detachados.\n");
#endif
    
    _failsafeActive = true;
    _armed = false;
    
    DEBUG_PRINT("failsafeBlock() — Sistema em modo de SEGURANÇA MÁXIMA.\n");
}

/* =================================================================================
 * FAILSAFE CLEAR — Remove bloqueio de emergência
 * =================================================================================
 */

void Actuators::failsafeClear() {
    if (!_failsafeActive) {
        DEBUG_PRINT("failsafeClear() ignorado — failsafe não estava ativo.\n");
        return;
    }
    
    DEBUG_PRINT("failsafeClear() — Removendo bloqueio de failsafe...\n");
    
#ifdef ESP32
    /* ESP32: Reconfigurar canais LEDC */
    for (uint8_t i = 0; i < ACTUATOR_COUNT; i++) {
        uint32_t freq = (i == CH_MOTOR) ? PWM_FREQUENCY_MOTOR : PWM_FREQUENCY_SERVO;
        ledcSetup(i, freq, PWM_RESOLUTION_BITS);
        ledcAttachPin(_pins[i], i);
    }
    DEBUG_PRINT("failsafeClear() — Canais LEDC reconfigurados.\n");
#endif
    
    _failsafeActive = false;
    _armed = false;     /* Exige novo armamento explícito */
    
    /* Garantir posição segura após sair do failsafe */
    safePosition();
    
    DEBUG_PRINT("failsafeClear() — FAILSAFE REMOVIDO. Sistema desarmado.\n");
    DEBUG_PRINT("failsafeClear() — Execute arm() para retomar operação.\n");
}

/* =================================================================================
 * MÉTODOS DE CONSULTA (GETTERS)
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

void Actuators::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}
/**
 * =================================================================================
 * FAILSAFE.H — MÓDULO DE SEGURANÇA CRÍTICA (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-27
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * O Failsafe é o módulo mais importante de todo o sistema BeaconFly.
 * 
 * RESPONSABILIDADES:
 *   1. Analisar CONSTANTEMENTE todos os dados que possam causar a queda da aeronave
 *   2. Detetar anomalias e classificar a sua gravidade (níveis 0 a 5)
 *   3. Ativar o modo de failsafe mais adequado para cada situação
 *   4. Comandar o FlightControl do ESP2 para executar ações (RTL, aterragem, planagem)
 *   5. Controlar alarmes sonoros e luminosos para alertar pilotos e pessoas no solo
 *   6. Comunicar o estado com ESP1 e Ground Station
 * 
 * =================================================================================
 * NÍVEIS DE FAILSAFE
 * =================================================================================
 * 
 *   ┌────────┬─────────────────┬────────────────────────────────────────────────┐
 *   │ NÍVEL  │ NOME            │ DESCRIÇÃO                                      │
 *   ├────────┼─────────────────┼────────────────────────────────────────────────┤
 *   │ 0      │ NORMAL          │ Nenhuma anomalia. Operação normal.             │
 *   │ 1      │ ALERTA          │ Bateria baixa, GPS lost, temp alta. Apenas     │
 *   │        │                 │ alerta via telemetria.                          │
 *   │ 2      │ RTL             │ Perda de link, bateria baixa com GPS válido.    │
 *   │        │                 │ Regressa à origem.                              │
 *   │ 3      │ ATERRAGEM       │ Bateria crítica, GPS inválido, falha ESP1.      │
 *   │        │                 │ Desce verticalmente de forma controlada.        │
 *   │ 4      │ PLANAGEM        │ Falha de motor, ângulos extremos, perda de      │
 *   │        │                 │ ESP1 sem GPS. Apenas estabilidade, throttle=0.  │
 *   │ 5      │ CORTE TOTAL     │ Falha crítica IMU, bateria morta, sistema       │
 *   │        │                 │ travado. Corta tudo. Avião cai livremente.      │
 *   └────────┴─────────────────┴────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * ALARMES (Luz + Som)
 * =================================================================================
 * 
 *   ┌────────┬────────────┬─────────────┬───────────────────────────────────────┐
 *   │ NÍVEL  │ LUZ (LED)  │ SOM (BUZZER)│ SIGNIFICADO                           │
 *   ├────────┼────────────┼─────────────┼───────────────────────────────────────┤
 *   │ 0      │ OFF        │ OFF         │ Operação normal                        │
 *   │ 1      │ OFF        │ OFF         │ Apenas telemetria                      │
 *   │ 2      │ Azul 1Hz   │ OFF         │ RTL em progresso                       │
 *   │ 3      │ Amarelo 2Hz│ 1 beep/s    │ Aterragem iminente                     │
 *   │ 4      │ Laranja 3Hz│ 2 beeps/s   │ Planagem sem motor (PERIGO)            │
 *   │ 5      │ Vermelho   │ Alarme      │ ⚠️ PERIGO IMINENTE ⚠️ Afaste-se!      │
 *   │        │ 5Hz+strobo │ contínuo    │                                       │
 *   └────────┴────────────┴─────────────┴───────────────────────────────────────┘
 * 
 * =================================================================================
 * TIMEOUTS (Adaptáveis conforme o modo de voo)
 * =================================================================================
 * 
 *   Em modos automáticos (STABILIZE, ALT_HOLD, AUTO, RTL):
 *     • ESP1 heartbeat timeout: 5000 ms
 *     • GS link timeout: 10000 ms
 * 
 *   Em modo MANUAL:
 *     • ESP1 heartbeat timeout: 3000 ms
 *     • GS link timeout: 2000 ms
 * 
 * =================================================================================
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "Protocol.h"
#include "FlightControl.h"

/* =================================================================================
 * CONSTANTES DE CONFIGURAÇÃO (Timeouts)
 * =================================================================================
 */

/* Timeouts para modos automáticos */
#define FS_TIMEOUT_ESP1_AUTO     5000    /* Heartbeat ESP1 (ms) */
#define FS_TIMEOUT_GS_AUTO       10000   /* Comandos GS (ms) */

/* Timeouts para modo MANUAL */
#define FS_TIMEOUT_ESP1_MANUAL   3000    /* Heartbeat ESP1 (ms) */
#define FS_TIMEOUT_GS_MANUAL     2000    /* Comandos GS (ms) */

/* =================================================================================
 * CONSTANTES DE CONFIGURAÇÃO (Limiares de deteção)
 * =================================================================================
 */

/* Ângulos críticos (asa fixa) */
#define FS_ANGLE_ROLL_LIMIT      60.0f   /* Roll máximo (graus) */
#define FS_ANGLE_PITCH_LIMIT     45.0f   /* Pitch máximo (graus) */
#define FS_ANGLE_DURATION_MS     1000    /* Tempo mínimo para ativar (ms) */

/* Velocidades angulares críticas */
#define FS_ANGULAR_RATE_LIMIT    300.0f  /* °/s */

/* Queda vertical crítica */
#define FS_SINK_RATE_LIMIT       -15.0f  /* m/s (negativo = descida rápida) */

/* Bateria */
#define FS_BATT_LOW_VOLTAGE      10.5f   /* Bateria baixa (volts) */
#define FS_BATT_CRITICAL_VOLTAGE 9.0f    /* Bateria crítica (volts) */
#define FS_BATT_DEAD_VOLTAGE     8.5f    /* Bateria morta (volts) */

/* Temperatura */
#define FS_TEMP_WARNING_LIMIT    65.0f   /* °C - alerta */
#define FS_TEMP_CRITICAL_LIMIT   80.0f   /* °C - ação necessária */

/* GPS */
#define FS_GPS_LOST_TIMEOUT      10000   /* ms sem GPS válido */

/* IMU integridade (aceleração total vs gravidade) */
#define FS_IMU_ACCEL_TOLERANCE   0.5f    /* m/s² (desvio permitido) */
#define FS_GRAVITY               9.81f   /* m/s² */

/* Saturação de atuadores */
#define FS_ACTUATOR_SATURATION_MS 5000   /* ms no extremo para considerar saturação */

/* =================================================================================
 * CONSTANTES DE HARDWARE (Pinos para alarmes)
 * =================================================================================
 */

#define FS_LED_PIN               32      /* LED RGB (ou LED comum) */
#define FS_BUZZER_PIN            33      /* Buzzer ativo (PWM) */

/* =================================================================================
 * ENUMS
 * =================================================================================
 */

/**
 * FailsafeLevel — Nível atual do failsafe
 */
enum FailsafeLevel : uint8_t {
    FS_LEVEL_NORMAL       = 0,   /* Operação normal */
    FS_LEVEL_ALERT        = 1,   /* Atenção (apenas telemetria) */
    FS_LEVEL_RTL          = 2,   /* Return To Launch */
    FS_LEVEL_LAND         = 3,   /* Aterragem forçada */
    FS_LEVEL_GLIDE        = 4,   /* Planagem controlada */
    FS_LEVEL_FREE_FALL    = 5    /* Corte total (queda livre) */
};

/**
 * FailsafeReason — Razão da ativação do failsafe
 */
enum FailsafeReason : uint8_t {
    FS_REASON_NONE            = 0,   /* Sem razão (nível 0) */
    FS_REASON_LOST_ESP1       = 1,   /* Perda de comunicação com ESP1 */
    FS_REASON_LOST_GS         = 2,   /* Perda de link com Ground Station */
    FS_REASON_ANGLE_ROLL      = 3,   /* Ângulo de roll excessivo */
    FS_REASON_ANGLE_PITCH     = 4,   /* Ângulo de pitch excessivo */
    FS_REASON_ANGULAR_RATE    = 5,   /* Velocidade angular excessiva */
    FS_REASON_SINK_RATE       = 6,   /* Queda vertical rápida */
    FS_REASON_BATT_LOW        = 7,   /* Bateria baixa */
    FS_REASON_BATT_CRITICAL   = 8,   /* Bateria crítica */
    FS_REASON_BATT_DEAD       = 9,   /* Bateria morta */
    FS_REASON_TEMP_CRITICAL   = 10,  /* Temperatura crítica */
    FS_REASON_GPS_LOST        = 11,  /* Perda de sinal GPS */
    FS_REASON_IMU_FAILURE     = 12,  /* Falha da IMU */
    FS_REASON_ACTUATOR_SAT    = 13,  /* Saturação de atuadores */
    FS_REASON_SYSTEM_HANG     = 14   /* Sistema travado (watchdog) */
};

/* =================================================================================
 * CLASSE FAILSAFE
 * =================================================================================
 */

class Failsafe {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    Failsafe();
    
    /**
     * Inicializa o módulo Failsafe
     * 
     * Configura pinos de LED e buzzer, inicializa variáveis de estado.
     * Deve ser chamada uma vez no setup().
     */
    void begin();
    
    /* =========================================================================
     * ATUALIZAÇÃO DE DADOS (CHAMAR NO LOOP)
     * ========================================================================= */
    
    /**
     * Atualiza os dados de entrada para análise
     * 
     * @param heartbeatESP1  Timestamp do último heartbeat do ESP1 (millis)
     * @param lastCommandGS  Timestamp do último comando da GS (millis)
     * @param flightMode     Modo de voo atual do ESP1 (para timeouts adaptativos)
     * @param state          Estado atual do FlightControl (ângulos, etc.)
     * @param gpsValid       Flag indicando se GPS está válido
     * @param battVoltage    Tensão da bateria (volts)
     * @param temperature    Temperatura do ESC/motor (°C)
     */
    void updateInputs(uint32_t heartbeatESP1, uint32_t lastCommandGS, 
                      uint8_t flightMode, const FlightState& state,
                      bool gpsValid, float battVoltage, float temperature);
    
    /**
     * Processa a análise e decisão do failsafe
     * 
     * Deve ser chamada a cada ciclo (100Hz). Analisa os dados,
     * determina o nível e razão, e atualiza o estado interno.
     * 
     * @return Nível atual do failsafe
     */
    FailsafeLevel process();
    
    /* =========================================================================
     * EXECUÇÃO DE AÇÕES (CHAMAR NO LOOP)
     * ========================================================================= */
    
    /**
     * Executa as ações correspondentes ao nível atual
     * 
     * Deve ser chamada APÓS process(). Comanda o FlightControl,
     * controla alarmes, e envia alertas.
     * 
     * @param flightControl  Ponteiro para o FlightControl do ESP2
     */
    void execute(FlightControl* flightControl);
    
    /* =========================================================================
     * CONSULTA DE ESTADO
     * ========================================================================= */
    
    /**
     * Retorna o nível atual do failsafe
     */
    FailsafeLevel getLevel() const;
    
    /**
     * Retorna a razão da ativação do failsafe
     */
    FailsafeReason getReason() const;
    
    /**
     * Verifica se o failsafe está ativo (nível >= 2)
     */
    bool isActive() const;
    
    /**
     * Retorna o timestamp da última ativação (millis)
     */
    uint32_t getLastActivationTime() const;
    
    /* =========================================================================
     * RESET E CONTROLO
     * ========================================================================= */
    
    /**
     * Reseta o failsafe para nível 0 (apenas chamar quando condições normalizam)
     */
    void reset();
    
    /* =========================================================================
     * DEBUG
     * ========================================================================= */
    
    void setDebug(bool enable);

private:
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    FailsafeLevel   _currentLevel;
    FailsafeReason  _currentReason;
    uint32_t        _lastActivationTime;
    bool            _active;
    bool            _debugEnabled;
    
    /* =========================================================================
     * DADOS DE ENTRADA ATUALIZADOS
     * ========================================================================= */
    
    uint32_t    _heartbeatESP1;
    uint32_t    _lastCommandGS;
    uint8_t     _flightMode;
    FlightState _state;
    bool        _gpsValid;
    float       _battVoltage;
    float       _temperature;
    
    /* =========================================================================
     * TEMPORIZADORES PARA CONDIÇÕES PERSISTENTES
     * ========================================================================= */
    
    uint32_t    _angleExceedStartMs;
    bool        _angleExceedFlag;
    
    uint32_t    _actuatorSatStartMs;
    bool        _actuatorSatFlag;
    
    uint32_t    _gpsLostStartMs;
    
    /* =========================================================================
     * ALARMES (LED + BUZZER)
     * ========================================================================= */
    
    uint32_t    _lastAlarmBlinkMs;
    bool        _alarmLedState;
    bool        _alarmBuzzerState;
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — ANÁLISE DE CONDIÇÕES
     * ========================================================================= */
    
    bool _checkESP1Timeout();
    bool _checkGSTimeout();
    bool _checkAngleLimits();
    bool _checkAngularRate();
    bool _checkSinkRate();
    uint8_t _checkBatteryStatus();
    bool _checkTemperature();
    bool _checkGPSLost();
    bool _checkIMUIntegrity();
    bool _checkActuatorSaturation();
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — DECISÃO DO NÍVEL
     * ========================================================================= */
    
    FailsafeLevel _determineLevel();
    FailsafeReason _determineReason();
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — ALARMES
     * ========================================================================= */
    
    void _updateAlarms();
    void _setLedByLevel();
    void _setBuzzerByLevel();
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — EXECUÇÃO DE AÇÕES
     * ========================================================================= */
    
    void _executeRTL(FlightControl* fc);
    void _executeLand(FlightControl* fc);
    void _executeGlide(FlightControl* fc);
    void _executeFreeFall(FlightControl* fc);
};

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* failsafeLevelToString(FailsafeLevel level);
const char* failsafeReasonToString(FailsafeReason reason);
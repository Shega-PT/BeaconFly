/**
 * =================================================================================
 * FLIGHTCONTROL.H — CONTROLADOR DE VOO (ESP2)
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
 * Este módulo é o controlador de voo do ESP2. Ao contrário do ESP1 (que tem
 * autoridade total sobre o voo), o ESP2 apenas executa ordens do Failsafe
 * quando este está ativo.
 * 
 * FLUXO DE AUTORIDADE:
 *   • Modo NORMAL: ESP1 tem autoridade total sobre o voo
 *   • Modo FAILSAFE: Failsafe assume controlo e comanda o FlightControl do ESP2
 * 
 * =================================================================================
 * DIFERENÇAS PARA O ESP1
 * =================================================================================
 * 
 *   ┌─────────────────┬─────────────────────────────────────────────────────────┐
 *   │ ESP1            │ ESP2                                                     │
 *   ├─────────────────┼─────────────────────────────────────────────────────────┤
 *   │ Autoridade      │ Autoridade total sobre o voo                            │
 *   │ Modos de voo    │ MANUAL, STABILIZE, ALT_HOLD, AUTO, RTL                  │
 *   │ PIDs            │ Roll, Pitch, Yaw, Altitude                              │
 *   │ Mixer           │ Elevons (asa delta)                                     │
 *   │ Failsafe        │ Apenas reage (não toma controlo)                        │
 *   └─────────────────┴─────────────────────────────────────────────────────────┘
 * 
 *   ┌─────────────────┬─────────────────────────────────────────────────────────┐
 *   │ ESP2            │ ESP2                                                     │
 *   ├─────────────────┼─────────────────────────────────────────────────────────┤
 *   │ Autoridade      │ Apenas quando Failsafe ativo                            │
 *   │ Modos de voo    │ RTL, LAND, GLIDE, FREE_FALL (após failsafe)             │
 *   │ PIDs            │ Roll, Pitch (apenas estabilização básica)               │
 *   │ Mixer           │ Ailerons + Leme (avião convencional)                    │
 *   │ Failsafe        │ Executa ordens do módulo Failsafe                       │
 *   └─────────────────┴─────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * MODOS DE VOO (APENAS PARA FAILSAFE)
 * =================================================================================
 * 
 *   ┌────────────┬─────────────────────────────────────────────────────────────┐
 *   │ Modo       │ Descrição                                                   │
 *   ├────────────┼─────────────────────────────────────────────────────────────┤
 *   │ RTL        │ Return To Launch — regressar ao ponto de descolagem         │
 *   │ LAND       │ Aterragem forçada — descer verticalmente de forma controlada│
 *   │ GLIDE      │ Planagem — apenas estabilidade, motor desligado             │
 *   │ FREE_FALL  │ Corte total — queda livre (sem controlo)                    │
 *   └────────────┴─────────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "Actuators.h"
#include "SIConverter.h"
#include "GPS.h"

/* =================================================================================
 * CONSTANTES DE SEGURANÇA E LIMITES
 * =================================================================================
 */

/* Limites dos atuadores (µs) — consistentes com Actuators.h */
#define FC_SERVO_NEUTRAL_US   1500
#define FC_SERVO_MIN_US       1000
#define FC_SERVO_MAX_US       2000
#define FC_MOTOR_STOP_US      1000
#define FC_MOTOR_MIN_US       1050
#define FC_MOTOR_MAX_US       1900

/* Limites dos PIDs (correção máxima em µs) */
#define FC_PID_ROLL_LIMIT     400.0f
#define FC_PID_PITCH_LIMIT    400.0f

/* Slew rate (µs por ciclo) */
#define FC_SLEW_RATE_SERVO    30
#define FC_SLEW_RATE_MOTOR    20

/* Filtros LPF para o termo D dos PIDs */
#define FC_LPF_ALPHA_ROLL     0.72f
#define FC_LPF_ALPHA_PITCH    0.72f

/* Limites físicos da aeronave (para modos failsafe) */
#define MAX_ROLL_ANGLE        45.0f
#define MAX_PITCH_ANGLE       35.0f
#define GLIDE_ROLL_ANGLE      10.0f   /* Roll máximo durante planagem */
#define GLIDE_PITCH_ANGLE     5.0f    /* Pitch máximo durante planagem */

/* Constantes de navegação (para RTL) */
#define RTL_ALTITUDE          50.0f   /* Altitude para RTL (metros) */
#define RTL_TOLERANCE         10.0f   /* Distância para considerar "chegou" (m) */
#define LAND_DESCENT_RATE     2.0f    /* Taxa de descida na aterragem (m/s) */

/* =================================================================================
 * ESTRUTURA PID CONTROLLER
 * =================================================================================
 */

struct PIDController {
    float Kp = 0.0f;
    float Ki = 0.0f;
    float Kd = 0.0f;

    float integral    = 0.0f;
    float lastError   = 0.0f;
    float filteredD   = 0.0f;
    float lastOutput  = 0.0f;

    float iMax =  250.0f;
    float iMin = -250.0f;

    float lpfAlpha = 0.7f;
};

/* =================================================================================
 * ESTRUTURA DE SETPOINTS (VALORES DESEJADOS)
 * =================================================================================
 */

struct FlightSetpoints {
    float roll      = 0.0f;    /* Graus — ângulo de rolamento desejado */
    float pitch     = 0.0f;    /* Graus — ângulo de inclinação desejado */
    float throttle  = 0.0f;    /* 0.0 a 1.0 — potência do motor */
    float altitude  = 50.0f;   /* Metros — altitude desejada (para LAND) */
};

/* =================================================================================
 * ESTRUTURA DE ESTADO (DADOS SI ATUAIS)
 * =================================================================================
 */

struct FlightState {
    float roll      = 0.0f;    /* Graus — ângulo de rolamento atual */
    float pitch     = 0.0f;    /* Graus — ângulo de inclinação atual */
    float yaw       = 0.0f;    /* Graus — ângulo de guinada atual */
    float heading   = 0.0f;    /* Graus — rumo atual */
    float altFused  = 0.0f;    /* Metros — altitude fundida */
    float latitude  = 0.0f;    /* Graus — latitude atual */
    float longitude = 0.0f;    /* Graus — longitude atual */
    float homeLat   = 0.0f;    /* Graus — latitude do ponto de origem (RTL) */
    float homeLon   = 0.0f;    /* Graus — longitude do ponto de origem (RTL) */
    float homeAlt   = 0.0f;    /* Metros — altitude do ponto de origem */
    bool  homeSet   = false;   /* Ponto de origem definido? */
};

/* =================================================================================
 * ENUMS
 * =================================================================================
 */

/**
 * FlightMode — Modos de voo do ESP2 (executados sob comando do Failsafe)
 */
enum FlightMode : uint8_t {
    MODE_IDLE       = 0,   /* Nenhum comando — aguardar */
    MODE_RTL        = 1,   /* Return To Launch */
    MODE_LAND       = 2,   /* Aterragem forçada */
    MODE_GLIDE      = 3,   /* Planagem controlada */
    MODE_FREE_FALL  = 4    /* Corte total (queda livre) */
};

/* =================================================================================
 * CLASSE FLIGHTCONTROL
 * =================================================================================
 */

class FlightControl {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    FlightControl();
    
    /**
     * Inicializa o controlador de voo do ESP2
     * 
     * @param actuators Ponteiro para o módulo Actuators
     * @param si        Ponteiro para o SIConverter (dados SI)
     * @param gps       Ponteiro para o GPS
     */
    void begin(Actuators* actuators, SIConverter* si, GPS* gps);
    
    /**
     * Reseta o controlador para estado seguro
     */
    void reset();
    
    /* =========================================================================
     * ATUALIZAÇÃO DE ESTADO (DADOS ATUAIS)
     * ========================================================================= */
    
    /**
     * Atualiza o estado atual da aeronave a partir dos sensores
     * 
     * Deve ser chamada a cada ciclo antes de update().
     */
    void updateState();
    
    /**
     * Define o ponto de origem (home) para RTL
     * 
     * @param lat Latitude (graus)
     * @param lon Longitude (graus)
     * @param alt Altitude (metros)
     */
    void setHomePosition(double lat, double lon, float alt);
    
    /* =========================================================================
     * CONTROLO POR COMANDO (CHAMADO PELO FAILSAFE)
     * ========================================================================= */
    
    /**
     * Define o modo de voo e os setpoints
     * 
     * @param mode Modo de voo (RTL, LAND, GLIDE, FREE_FALL)
     * @param setpoints Setpoints desejados (opcional)
     */
    void setCommand(FlightMode mode, const FlightSetpoints& setpoints = FlightSetpoints());
    
    /**
     * Executa o controlador de voo
     * 
     * Deve ser chamada a cada ciclo (100Hz). Calcula os PIDs e
     * atualiza os atuadores conforme o modo atual.
     * 
     * @param dt Tempo decorrido desde o último ciclo (segundos)
     */
    void update(float dt);
    
    /* =========================================================================
     * GETTERS
     * ========================================================================= */
    
    /**
     * Retorna o modo de voo atual
     */
    FlightMode getMode() const;
    
    /**
     * Retorna os sinais atuais para os atuadores
     */
    ActuatorSignals getOutputs() const;
    
    /**
     * Retorna o estado atual da aeronave
     */
    const FlightState& getState() const;
    
    /**
     * Retorna os setpoints atuais
     */
    const FlightSetpoints& getSetpoints() const;
    
    /**
     * Verifica se o controlador está ativo (recebeu comando)
     */
    bool isActive() const;
    
    /* =========================================================================
     * DIAGNÓSTICO
     * ========================================================================= */
    
    /**
     * Ativa/desativa modo debug
     */
    void setDebug(bool enable);

private:
    /* =========================================================================
     * PONTEIROS PARA SUBSISTEMAS
     * ========================================================================= */
    
    Actuators*    _actuators;
    SIConverter*  _si;
    GPS*          _gps;
    
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    FlightState     _state;
    FlightSetpoints _setpoints;
    ActuatorSignals _outputs;
    FlightMode      _mode;
    bool            _active;
    bool            _debugEnabled;
    
    /* =========================================================================
     * PIDS
     * ========================================================================= */
    
    PIDController   _pidRoll;
    PIDController   _pidPitch;
    
    /* =========================================================================
     * VARIÁVEIS DE NAVEGAÇÃO (RTL)
     * ========================================================================= */
    
    bool            _rtlInProgress;
    float           _lastDistanceToHome;
    
    /* =========================================================================
     * VARIÁVEIS DE ATERRAGEM (LAND)
     * ========================================================================= */
    
    float           _landingStartAlt;
    uint32_t        _landingStartTime;
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — CONTROLADORES POR MODO
     * ========================================================================= */
    
    void _updateRTL(float dt);
    void _updateLand(float dt);
    void _updateGlide(float dt);
    void _updateFreeFall(float dt);
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — FUNÇÕES AUXILIARES
     * ========================================================================= */
    
    float _runPID(PIDController& pid, float error, float dt, float limit);
    void  _mixAilerons(float rollCorr, uint16_t& wingR, uint16_t& wingL);
    void  _applyOutputs();
    float _calculateBearing(float lat1, float lon1, float lat2, float lon2);
    float _calculateDistance(float lat1, float lon1, float lat2, float lon2);
    float _bearingToRollError(float targetBearing, float currentHeading);
};

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* flightModeToString(FlightMode mode);
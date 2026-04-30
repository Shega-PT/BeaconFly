#pragma once

/**
 * =================================================================================
 * FLIGHTCONTROL.H — CONTROLADOR DE VOO (ESP1)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-17
 * VERSÃO:     2.0.1 (Correção: navegação em DEBUG_AUTO_MANUAL)
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * Este módulo é o cérebro do sistema BeaconFly. Responsável por:
 * 
 *   • Estabilização da aeronave (PIDs Roll, Pitch, Yaw)
 *   • Manutenção de altitude (fusão GPS + Barómetro)
 *   • Navegação autónoma (waypoints, RTL)
 *   • Processamento de comandos da Ground Station
 *   • Geração de sinais para o módulo Actuators (µs)
 * 
 * O FlightControl opera num loop determinístico de 50-100Hz e recebe dados
 * em unidades SI (SI - Système International) do ESP2 via protocolo TLV.
 * 
 * =================================================================================
 * MODOS DE VOO
 * =================================================================================
 * 
 * ┌──────────────────────┬────────────────────────────────────────────────────────┐
 * │ MODO                 │ DESCRIÇÃO                                              │
 * ├──────────────────────┼────────────────────────────────────────────────────────┤
 * │ MANUAL               │ Passthrough direto dos comandos da GS → atuadores     │
 * │ STABILIZE            │ Estabilização angular (Roll/Pitch/Yaw com PIDs)        │
 * │ ALT_HOLD             │ STABILIZE + manutenção de altitude                     │
 * │ POSHOLD              │ Manutenção de posição GPS                              │
 * │ AUTO                 │ Navegação autónoma por waypoints                       │
 * │ RTL                  │ Return To Launch — regressar ao ponto de descolagem    │
 * │ DEBUG_AUTO_MANUAL    │ Modo híbrido para testes: autónomo + override manual   │
 * └──────────────────────┴────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * MODO DEBUG_AUTO_MANUAL (MODELO HÍBRIDO PARA TESTES)
 * =================================================================================
 * 
 * Este modo foi concebido especificamente para testes em campo, permitindo:
 * 
 *   1. Comportamento autónomo: Segue um trajeto pré-definido (array de waypoints)
 *   2. Override manual: Se receber comandos manuais (joystick/RC), sobrepõe-se
 *   3. Timeout manual: Após X segundos sem comandos, regressa ao MODO AUTÓNOMO
 *   4. Retoma do waypoint MAIS PRÓXIMO: Não volta ao início, mas sim ao ponto
 *      da trajetória mais próximo da posição atual do UAS.
 *   5. Shell expandido: Quase todos os comandos shell são permitidos (exceto
 *      os que comprometem a segurança imediata em voo)
 * 
 * FLUXO DO MODO DEBUG_AUTO_MANUAL:
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                    MODO AUTÓNOMO ATIVO                                  │
 *   │         (segue trajeto, waypoint mais próximo como referência)         │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *                                      │
 *                                      ▼ (comando manual recebido)
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                    OVERRIDE MANUAL ATIVO                                │
 *   │         (responde a joystick/RC, estabiliza como STABILIZE)            │
 *   │                      Timer de timeout a correr                          │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *                                      │
 *                                      ▼ (timeout expirado)
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │              CALCULAR WAYPOINT MAIS PRÓXIMO DA POSIÇÃO ATUAL            │
 *   │                  Retomar navegação a partir desse ponto                 │
 *   └─────────────────────────────────────────────────────────────────────────┘
 * 
 * REGRAS DE SEGURANÇA NO MODO DEBUG_AUTO_MANUAL:
 *   • ARM/DISARM: Permitido apenas em solo (altitude < 0.5m)
 *   • REBOOT/SHUTDOWN: Bloqueado em voo
 *   • SET_PARAM críticos (PIDs, limites): Bloqueados em voo
 *   • SET_MODE: Permitido (pode sair do modo Debug)
 * 
 * =================================================================================
 */

#include <Arduino.h>
#include <stdint.h>
#include "Protocol.h"
#include "Actuators.h"

/* =================================================================================
 * MODOS DE VOO
 * =================================================================================
 */

enum FlightMode : uint8_t {
    MODE_MANUAL           = 0,   /* Passthrough direto (sem PID) */
    MODE_STABILIZE        = 1,   /* Estabilização angular (Roll/Pitch/Yaw) */
    MODE_ALT_HOLD         = 2,   /* STABILIZE + manutenção de altitude */
    MODE_POSHOLD          = 3,   /* Manutenção de posição (GPS) */
    MODE_AUTO             = 4,   /* Navegação autónoma por waypoints */
    MODE_RTL              = 5,   /* Return To Launch */
    MODE_DEBUG_AUTO_MANUAL = 6   /* MODO HÍBRIDO PARA TESTES (ver secção acima) */
};

/* =================================================================================
 * CONSTANTES DE SEGURANÇA E LIMITES FÍSICOS
 * =================================================================================
 */

/* Limites dos atuadores (µs) */
#define FC_SERVO_NEUTRAL_US   1500
#define FC_SERVO_MIN_US       1000
#define FC_SERVO_MAX_US       2000
#define FC_MOTOR_MIN_US       1100
#define FC_MOTOR_MAX_US       1900

/* Limites dos PIDs (correção máxima em µs) */
#define FC_PID_ROLL_LIMIT     400.0f
#define FC_PID_PITCH_LIMIT    400.0f
#define FC_PID_YAW_LIMIT      350.0f
#define FC_PID_ALT_LIMIT      600.0f

/* Slew rate (µs por ciclo — suaviza movimentos bruscos) */
#define FC_SLEW_RATE_SERVO    30       /* Servos: 30µs/ciclo */
#define FC_SLEW_RATE_MOTOR    20       /* Motor: 20µs/ciclo */

/* Filtros LPF para o termo D dos PIDs (reduz ruído) */
#define FC_LPF_ALPHA_ROLL     0.72f
#define FC_LPF_ALPHA_PITCH    0.72f
#define FC_LPF_ALPHA_YAW      0.55f
#define FC_LPF_ALPHA_ALT      0.35f

/* Limites físicos da aeronave */
#define MAX_ROLL_ANGLE        45.0f    /* Graus — máximo ângulo de rolamento */
#define MAX_PITCH_ANGLE       35.0f    /* Graus — máximo ângulo de inclinação */
#define MAX_YAW_RATE          180.0f   /* Graus/segundo — máximo de guinada */
#define MIN_ALTITUDE          0.0f     /* Metros — altitude mínima */
#define MAX_ALTITUDE          400.0f   /* Metros — altitude máxima legal (Europa) */

/* Timeouts e segurança */
#define FC_DATA_STALE_MS      500      /* ms sem dados do ESP2 → considera stale */
#define FC_DEBUG_MANUAL_TIMEOUT_MS 3000 /* ms sem comando manual → volta a autónomo */
#define FC_GROUND_ALTITUDE_THRESHOLD 0.5f /* metros — considerado "em solo" */

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
 * ESTRUTURA FLIGHT STATE (DADOS SI DO ESP2)
 * =================================================================================
 */

struct FlightState {
    /* Atitude (graus) */
    float roll      = 0.0f;
    float pitch     = 0.0f;
    float yaw       = 0.0f;
    float heading   = 0.0f;

    /* Velocidades angulares (graus/segundo) */
    float rollRate  = 0.0f;
    float pitchRate = 0.0f;
    float yawRate   = 0.0f;

    /* Posição GPS (graus) */
    float latitude  = 0.0f;
    float longitude = 0.0f;

    /* Altitude (metros) */
    float altGPS    = 0.0f;
    float altBaro   = 0.0f;
    float altFused  = 0.0f;

    /* Velocidades lineares (m/s) */
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;

    /* Estado da bateria */
    float battV = 0.0f;
    float battA = 0.0f;
    float battSOC = 0.0f;

    /* Flags de validade */
    bool attitudeValid = false;
    bool altitudeValid = false;
    bool velocityValid = false;
    bool gpsValid = false;

    /* Timestamp da última atualização (ms) */
    uint32_t timestamp = 0;
};

/* =================================================================================
 * ESTRUTURA FLIGHT SETPOINTS
 * =================================================================================
 */

struct FlightSetpoints {
    float roll      = 0.0f;
    float pitch     = 0.0f;
    float yaw       = 0.0f;
    float altitude  = 50.0f;
    float throttle  = 0.0f;
};

/* =================================================================================
 * ESTRUTURA TRIM (OFFSETS POR CANAL)
 * =================================================================================
 */

struct TrimValues {
    int16_t wingR   = 0;
    int16_t wingL   = 0;
    int16_t rudder  = 0;
    int16_t elevonR = 0;
    int16_t elevonL = 0;
    int16_t motor   = 0;
};

/* =================================================================================
 * ESTRUTURA WAYPOINT (PARA NAVEGAÇÃO AUTÓNOMA)
 * =================================================================================
 */

struct Waypoint {
    float latitude;            /* Graus (WGS84) */
    float longitude;           /* Graus (WGS84) */
    float altitude;            /* Metros */
    float heading;             /* Graus (0-360) — heading desejado ao chegar */
    float tolerance;           /* Metros — distância para considerar "chegou" */
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
    void begin();
    void reset();

    /* =========================================================================
     * ATUALIZAÇÃO DE ESTADO (DADOS DO ESP2)
     * ========================================================================= */
    
    void updateState(const TLVMessage& msg);

    /* =========================================================================
     * PROCESSAMENTO DE COMANDOS (GROUND STATION)
     * ========================================================================= */
    
    void processCommand(const TLVMessage& msg);

    /* =========================================================================
     * CICLO PRINCIPAL DE CONTROLO (CHAMAR A 50-100Hz)
     * ========================================================================= */
    
    void update();

    /* =========================================================================
     * MODO DEBUG_AUTO_MANUAL (FUNÇÕES ESPECÍFICAS)
     * ========================================================================= */
    
    /**
     * Define o trajeto para o modo DEBUG_AUTO_MANUAL
     */
    void setDebugTrajectory(const Waypoint* waypoints, uint8_t count);
    
    /**
     * Regista comando manual recebido (reset do timeout)
     * Deve ser chamado sempre que um comando manual (joystick/RC) é recebido.
     */
    void registerManualCommand();

    /* =========================================================================
     * CONFIGURAÇÃO DE WAYPOINTS (MODO AUTO)
     * ========================================================================= */
    
    void setWaypoints(const Waypoint* waypoints, uint8_t count);
    
    /**
     * Reinicia a missão do zero (volta ao waypoint 0)
     * Método de segurança — útil se o UAS se "perder" ou algo correr mal
     */
    void resetMission();
    
    /**
     * Retoma a missão a partir do waypoint mais próximo da posição atual
     * Utilizado no modo DEBUG_AUTO_MANUAL após timeout do override manual
     */
    void resumeFromNearestWaypoint();

    /* =========================================================================
     * GETTERS
     * ========================================================================= */
    
    const ActuatorSignals& getOutputs() const;
    const FlightState& getState() const;
    const FlightSetpoints& getSetpoints() const;
    const TrimValues& getTrim() const;
    FlightMode getMode() const;
    uint32_t getDebugManualTimeoutRemaining() const;
    bool isManualOverrideActive() const;

    /* =========================================================================
     * SETTERS
     * ========================================================================= */
    
    void setMode(FlightMode mode);
    void setSetpoints(const FlightSetpoints& sp);
    void setTrim(const TrimValues& trim);
    void setPIDGains(uint8_t axisID, float Kp, float Ki, float Kd);
    void setDebugManualTimeout(uint32_t timeoutMs);
    void setDebug(bool enable);

private:
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    FlightState     _state;
    FlightSetpoints _setpoints;
    TrimValues      _trim;
    ActuatorSignals _outputs;
    
    PIDController   _pidRoll;
    PIDController   _pidPitch;
    PIDController   _pidYaw;
    PIDController   _pidAltitude;
    
    FlightMode      _mode = MODE_STABILIZE;
    
    uint32_t        _lastUpdateMs = 0;
    bool            _debugEnabled = false;
    
    /* =========================================================================
     * NAVEGAÇÃO AUTÓNOMA
     * ========================================================================= */
    
    Waypoint        _waypoints[32];
    uint8_t         _waypointCount = 0;
    uint8_t         _currentWaypoint = 0;
    bool            _missionActive = false;
    
    /* =========================================================================
     * MODO DEBUG_AUTO_MANUAL (HÍBRIDO)
     * ========================================================================= */
    
    Waypoint        _debugTrajectory[16];
    uint8_t         _debugTrajectoryCount = 0;
    uint8_t         _debugCurrentPoint = 0;
    
    uint32_t        _lastManualCommandMs = 0;
    uint32_t        _debugManualTimeoutMs = FC_DEBUG_MANUAL_TIMEOUT_MS;
    bool            _manualOverrideActive = false;

    /* =========================================================================
     * FUNÇÕES AUXILIARES
     * ========================================================================= */
    
    float _runPID(PIDController& pid, float error, float dt, float limit);
    float _fuseAltitude(float gps, float baro);
    void  _mixElevons(float pitchCorr, float rollCorr, uint16_t& elevonR, uint16_t& elevonL);
    uint16_t _applySlewRate(uint16_t current, uint16_t target, uint16_t maxDelta);
    uint16_t _applyTrim(uint16_t value, int16_t trim, uint16_t minUs, uint16_t maxUs);
    bool _isOnGround() const;
    bool _isShellCommandSafe(uint8_t commandID) const;
    
    /* Funções de navegação */
    float _calculateBearing(float lat1, float lon1, float lat2, float lon2);
    float _calculateDistance(float lat1, float lon1, float lat2, float lon2);
    float _bearingToRollError(float targetBearing, float currentHeading);
    bool _navigateToWaypoint(const Waypoint& target, float dt);
    uint8_t _findNearestWaypoint(const Waypoint* waypoints, uint8_t count);
    
    /* =========================================================================
     * IMPLEMENTAÇÕES POR MODO DE VOO
     * ========================================================================= */
    
    void _updateManual();
    void _updateStabilize(float dt);
    void _updateAltHold(float dt);
    void _updatePosHold(float dt);
    void _updateAuto(float dt);
    void _updateRTL(float dt);
    void _updateDebugAutoManual(float dt);
};

#endif /* FLIGHTCONTROL_H */
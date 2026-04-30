/**
 * =================================================================================
 * ACTUATORS.H — CONTROLO DE ATUADORES (ESP2)
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
 * Este módulo é responsável pelo controlo físico dos atuadores ligados ao ESP2.
 * Ao contrário do ESP1 (que controla 5 servos + motor para asa delta), o ESP2
 * controla uma configuração diferente:
 * 
 *   • 2 Servos de asa (aileron direito e esquerdo) — controlo de roll
 *   • 1 Servo de leme (rudder) — controlo de yaw
 *   • 1 Motor (ESC) — controlo de throttle
 * 
 * TOTAL: 4 atuadores (2 servos + 1 leme + 1 motor)
 * 
 * =================================================================================
 * DIFERENÇAS PARA O ESP1
 * =================================================================================
 * 
 *   ┌─────────────────┬─────────────────────────────────────────────────────────┐
 *   │ ESP1            │ ESP2                                                     │
 *   ├─────────────────┼─────────────────────────────────────────────────────────┤
 *   │ Asa delta       │ Avião convencional (ailerons + leme)                     │
 *   │ 5 servos + motor│ 2 servos (ailerons) + 1 leme + 1 motor                   │
 *   │ Mixer de elevons│ Sem mixer (canais independentes)                         │
 *   │ LEDC (hardware) │ LEDC (hardware) — mesmo método                           │
 *   └─────────────────┴─────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * ATUADORES CONTROLADOS
 * =================================================================================
 * 
 *   • Aileron Direito (wingR)  → controlo de roll (movimento diferencial)
 *   • Aileron Esquerdo (wingL) → controlo de roll (movimento diferencial)
 *   • Leme (rudder)            → controlo de yaw
 *   • Motor (motor)            → controlo de throttle (ESC)
 * 
 * =================================================================================
 * INTERFACE COM FLIGHTCONTROL (ESP2)
 * =================================================================================
 * 
 *   O FlightControl do ESP2 calcula os valores desejados e chama:
 *     actuators.update(signals)
 * 
 *   Onde signals contém:
 *     wingR, wingL, rudder, motor (todos em microssegundos, 1000-2000)
 * 
 * =================================================================================
 */

#pragma once

#include <Arduino.h>
#include <cstdint>
#include <stddef.h>

/* =================================================================================
 * PINOS DE SAÍDA (Ajustar conforme PCB)
 * =================================================================================
 */

#define PIN_SERVO_WING_R       13      /* Aileron direito (roll) */
#define PIN_SERVO_WING_L       14      /* Aileron esquerdo (roll) */
#define PIN_SERVO_RUDDER       27      /* Leme (yaw) */
#define PIN_MOTOR_ECU          33      /* Sinal PWM para ESC do motor */

/* =================================================================================
 * CONFIGURAÇÃO PWM (LEDC - ESP32)
 * =================================================================================
 */

#define PWM_FREQUENCY_SERVO    50       /* 50 Hz (período de 20ms) para servos */
#define PWM_FREQUENCY_MOTOR    50       /* 50 Hz para ESC (seguro) */
#define PWM_RESOLUTION_BITS    16       /* 16 bits = 65535 níveis */
#define PWM_PERIOD_US          20000    /* Período de 20ms (50Hz) */

/* =================================================================================
 * LIMITES FÍSICOS DOS ATUADORES (em microssegundos)
 * =================================================================================
 */

/* Servos (ailerons e leme) */
#define PWM_SERVO_MIN          1000     /* Deflexão máxima negativa */
#define PWM_SERVO_NEUTRAL      1500     /* Posição neutra (centro) */
#define PWM_SERVO_MAX          2000     /* Deflexão máxima positiva */

/* Motor (ESC) */
#define PWM_MOTOR_STOP         1000     /* Motor parado (ESC armado) */
#define PWM_MOTOR_MIN          1050     /* Potência mínima útil (zona morta) */
#define PWM_MOTOR_MAX          1900     /* Potência máxima segura */
#define PWM_MOTOR_ARM          1000     /* Pulso de armamento do ESC (2 segundos) */

/* =================================================================================
 * SLEW RATE (Suavização de movimentos)
 * =================================================================================
 */

#define SLEW_RATE_SERVO        30       /* µs por ciclo (máx 30µs/ciclo) */
#define SLEW_RATE_MOTOR        20       /* µs por ciclo (máx 20µs/ciclo) */

/* =================================================================================
 * MACRO DE CONVERSÃO (µs → ticks LEDC)
 * =================================================================================
 */

#define US_TO_TICKS(us)        ((uint32_t)((us) * 65535UL / PWM_PERIOD_US))

/* =================================================================================
 * ESTRUTURA DE SINAIS (Interface com FlightControl)
 * =================================================================================
 * 
 * Todos os valores em microssegundos (1000-2000) para segurança e clareza.
 */

struct ActuatorSignals {
    uint16_t wingR;      /* Aileron direito (roll) — µs */
    uint16_t wingL;      /* Aileron esquerdo (roll) — µs */
    uint16_t rudder;     /* Leme (yaw) — µs */
    uint16_t motor;      /* Motor — throttle (µs, 1000-1900) */
};

/* =================================================================================
 * ENUMERAÇÃO DE CANAIS LEDC (ESP32)
 * =================================================================================
 */

enum ActuatorChannel : uint8_t {
    CH_WING_R = 0,       /* Canal 0 → PIN_SERVO_WING_R */
    CH_WING_L,           /* Canal 1 → PIN_SERVO_WING_L */
    CH_RUDDER,           /* Canal 2 → PIN_SERVO_RUDDER */
    CH_MOTOR,            /* Canal 3 → PIN_MOTOR_ECU */
    ACTUATOR_COUNT       /* Número total de atuadores (4) */
};

/* =================================================================================
 * CLASSE ACTUATORS
 * =================================================================================
 */

class Actuators {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    Actuators();
    
    /**
     * Inicializa o hardware PWM (LEDC) e coloca atuadores em posição segura
     * 
     * Deve ser chamada uma vez no setup().
     * Configura os 4 canais LEDC e associa os pinos correspondentes.
     */
    void begin();
    
    /**
     * Arma o sistema de atuadores
     * 
     * Após arm(), o update() passa a ter efeito.
     * Envia pulso de armamento ao ESC (1000µs por 2 segundos).
     * 
     * @note Se failsafe estiver ativo, o armamento é bloqueado.
     */
    void arm();
    
    /**
     * Desarma o sistema de atuadores
     * 
     * Coloca imediatamente todos os atuadores em posição segura
     * e bloqueia futuras chamadas a update().
     */
    void disarm();
    
    /**
     * Atualiza todos os atuadores com novos valores PWM
     * 
     * Só tem efeito se o sistema estiver armado E não estiver em failsafe.
     * Todos os valores são constrainidos aos limites seguros.
     * 
     * @param signals Estrutura com os valores desejados em microssegundos
     */
    void update(const ActuatorSignals& signals);
    
    /**
     * Coloca todos os atuadores em posição segura imediatamente
     * 
     * Ailerons vão para neutro (1500µs), motor para STOP (1000µs).
     * Funciona mesmo em failsafe ou desarmado.
     */
    void safePosition();
    
    /**
     * Ativa o bloqueio de Failsafe
     * 
     * Quando ativo:
     *   • Força safePosition() imediatamente
     *   • Bloqueia completamente update() e arm()
     *   • Detacha os canais LEDC para segurança máxima
     */
    void failsafeBlock();
    
    /**
     * Desativa o bloqueio de Failsafe
     * 
     * Após clear, o sistema fica desarmado.
     * É necessário chamar arm() novamente para retomar operação.
     */
    void failsafeClear();
    
    /* =========================================================================
     * CONSULTAS DE ESTADO
     * ========================================================================= */
    
    /** Retorna true se o sistema está armado */
    bool isArmed() const;
    
    /** Retorna true se o failsafe está ativo */
    bool isFailsafeActive() const;
    
    /** Retorna o último conjunto de sinais enviados (para telemetria) */
    ActuatorSignals getLastSignals() const;
    
    /** Retorna os valores PWM atuais de cada canal (para debug) */
    void getCurrentPWM(uint16_t* wingR, uint16_t* wingL, uint16_t* rudder, uint16_t* motor) const;
    
    /* =========================================================================
     * CONFIGURAÇÃO
     * ========================================================================= */
    
    /**
     * Ativa/desativa modo debug
     * 
     * @param enable true = ativar mensagens de diagnóstico
     */
    void setDebug(bool enable);

private:
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    bool _armed;               /* Sistema armado — permite update() */
    bool _failsafeActive;      /* Failsafe ativo — bloqueia tudo */
    bool _debugEnabled;        /* Debug ativo — imprime mensagens */
    
    ActuatorSignals _lastSignals;   /* Últimos sinais enviados (telemetria) */
    ActuatorSignals _currentPWM;    /* Valores PWM atuais (para slew rate) */
    
    /* =========================================================================
     * MATRIZ DE PINOS (ordem corresponde a ActuatorChannel)
     * ========================================================================= */
    
    static const uint8_t _pins[ACTUATOR_COUNT];
    
    /* =========================================================================
     * MÉTODOS PRIVADOS
     * ========================================================================= */
    
    /**
     * Verifica se é seguro enviar sinais neste momento
     * 
     * @return true se _armed && !_failsafeActive
     */
    bool _canSendSignals() const;
    
    /**
     * Escreve valor PWM num canal LEDC com constrain automático
     * 
     * @param channel Canal LEDC (0-3)
     * @param us      Valor desejado em microssegundos
     * @param minUs   Limite mínimo seguro
     * @param maxUs   Limite máximo seguro
     */
    void _writeChannel(uint8_t channel, uint16_t us, uint16_t minUs, uint16_t maxUs);
    
    /**
     * Aplica slew rate a um canal
     * 
     * @param current Valor atual (µs)
     * @param target  Valor desejado (µs)
     * @param maxDelta Variação máxima por ciclo (µs)
     * @return Novo valor após aplicar slew rate
     */
    uint16_t _applySlewRate(uint16_t current, uint16_t target, uint16_t maxDelta);
    
    /**
     * Configura todos os canais LEDC (chamado por begin())
     */
    void _setupPWM();
};

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* actuatorChannelToString(ActuatorChannel channel);

#endif /* ACTUATORS_H */
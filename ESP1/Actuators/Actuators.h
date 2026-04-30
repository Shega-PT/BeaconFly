#pragma once

/**
 * =================================================================================
 * ACTUATORS.H — CONTROLO DE SERVOS E MOTOR (ESP1)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-17
 * VERSÃO:     2.0.0 (Fusão VA + VB com melhorias)
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * Este módulo é responsável pelo controlo físico de todos os atuadores do BeaconFly:
 * 
 *   • 5 Servos: wingR (asa direita), wingL (asa esquerda), rudder (leme),
 *               elevonR (elevon direito), elevonL (elevon esquerdo)
 *   • 1 Motor:  ECU do motor principal (ESC)
 * 
 * O sistema opera com sinais PWM na gama padrão de 1000-2000 microssegundos,
 * onde 1500µs representa a posição neutra (servos) ou motor parado.
 * 
 * =================================================================================
 * ARQUITETURA DE PWM
 * =================================================================================
 * 
 * O módulo suporta duas implementações, selecionadas automaticamente em tempo
 * de compilação:
 * 
 *   • ESP32:      Utiliza periférico LEDC (hardware PWM) — NÃO BLOQUEANTE
 *                 → Recomendado para produção (determinístico, 100Hz)
 * 
 *   • Outras:     Utiliza PWM por software com timer/interrupção
 *                 → Fallback para portabilidade (Arduino, RP2040, Jetson)
 * 
 * =================================================================================
 * SEGURANÇA E DETERMINISMO
 * =================================================================================
 * 
 * 1. ARM/DISARM explícito: Nenhum atuador se move sem autorização.
 * 2. FAILSAFE BLOCK: Bloqueio físico das saídas em emergência.
 * 3. CONSTRAIN automático: Nenhum valor fora dos limites físicos é escrito.
 * 4. POSIÇÃO SEGURA: Neutro nos servos + motor parado.
 * 5. DEBUG condicional: Ativável sem comprometer performance.
 * 
 * =================================================================================
 * INTERFACE COM FLIGHTCONTROL
 * =================================================================================
 * 
 * O FlightControl envia um struct ActuatorSignals com valores em microssegundos.
 * O módulo Actuators valida, constrain e escreve nos pinos correspondentes.
 * 
 * Fluxo típico:
 *   FlightControl::update() → calcula PID → Actuators::update(signals)
 * 
 * =================================================================================
 */

#include <Arduino.h>
#include <cstdint>
#include <stddef.h>

/* =================================================================================
 * PINOS DE SAÍDA (Baseados na PCB v1.0)
 * =================================================================================
 * 
 * Estes valores podem ser ajustados conforme a PCB final.
 * Devem ser consistentes entre Actuators.h e a configuração de hardware.
 */

#define PIN_SERVO_WING_R    13   /* Asa direita — controlo de rolamento */
#define PIN_SERVO_WING_L    14   /* Asa esquerda — controlo de rolamento */
#define PIN_SERVO_RUDDER    27   /* Leme — controlo de guinada (yaw) */
#define PIN_SERVO_ELEVON_R  26   /* Elevon direito — controlo misto pitch/roll */
#define PIN_SERVO_ELEVON_L  25   /* Elevon esquerdo — controlo misto pitch/roll */
#define PIN_MOTOR_ECU       33   /* Sinal PWM para ECU do motor principal */

/* =================================================================================
 * CONFIGURAÇÃO PWM
 * =================================================================================
 */

/**
 * PWM_FREQUENCY_SERVO - Frequência padrão para servos
 * 
 * 50Hz (período de 20ms) é o padrão da indústria para servos analógicos
 * e digitais. A maioria dos servos aceita 50-333Hz.
 */
#define PWM_FREQUENCY_SERVO     50      /* Hertz — período de 20ms */

/**
 * PWM_FREQUENCY_MOTOR - Frequência para o ESC do motor
 * 
 * 50Hz é seguro para todos os ESCs. Alguns ESCs suportam 400Hz (modo
 * "high refresh"). Para alterar, modificar esta constante.
 */
#define PWM_FREQUENCY_MOTOR     50      /* Hertz — padrão seguro */

/**
 * PWM_RESOLUTION_BITS - Resolução do hardware PWM (ESP32)
 * 
 * 16 bits = 65535 níveis. Com período de 20ms, cada tick representa
 * aproximadamente 0.305 µs — precisão mais que suficiente.
 */
#define PWM_RESOLUTION_BITS     16      /* 2^16 = 65535 níveis */

/**
 * PWM_PERIOD_US - Período do sinal PWM em microssegundos
 * 
 * Calculado como 1.000.000 / 50Hz = 20.000 µs
 */
#define PWM_PERIOD_US           20000   /* 20ms — período completo */

/* =================================================================================
 * LIMITES FÍSICOS DOS ATUADORES (em microssegundos)
 * =================================================================================
 * 
 * Estes valores seguem o padrão RC (Radio Control) amplamente utilizado:
 *   - 1000 µs: Posição mínima / motor parado
 *   - 1500 µs: Posição neutra (centro)
 *   - 2000 µs: Posição máxima / potência máxima
 */

/* Servos — gama completa */
#define PWM_SERVO_MIN           1000    /* Deflexão máxima negativa */
#define PWM_SERVO_NEUTRAL       1500    /* Posição neutra / centro */
#define PWM_SERVO_MAX           2000    /* Deflexão máxima positiva */

/* Motor — gama operacional segura */
#define PWM_MOTOR_STOP          1000    /* Motor parado (ESC armado) */
#define PWM_MOTOR_MIN           1050    /* Potência mínima útil (zona morta) */
#define PWM_MOTOR_MAX           1900    /* Potência máxima segura */
#define PWM_MOTOR_ARM           1000    /* Pulso de armamento do ESC (2s) */

/* =================================================================================
 * MACROS DE CONVERSÃO (ESP32 — LEDC)
 * =================================================================================
 * 
 * Converte microssegundos para ticks do LEDC com precisão milimétrica.
 * 
 * Fórmula: ticks = us * (2^RESOLUTION) / PERIOD_US
 * Exemplo: 1500µs → 1500 * 65535 / 20000 = 4915 ticks
 */

#define US_TO_TICKS(us)         ((uint32_t)((us) * 65535UL / PWM_PERIOD_US))

/* =================================================================================
 * ESTRUTURA DE SINAIS (Interface com FlightControl)
 * =================================================================================
 * 
 * Todos os valores em microssegundos (1000-2000) para máxima clareza.
 * O FlightControl deve garantir que os valores estão dentro dos limites
 * esperados; o Actuators aplica constrain redundante por segurança.
 */

struct ActuatorSignals {
    uint16_t wingR;         /* Asa direita — controlo de rolamento */
    uint16_t wingL;         /* Asa esquerda — controlo de rolamento */
    uint16_t rudder;        /* Leme — controlo de guinada (yaw) */
    uint16_t elevonR;       /* Elevon direito — pitch/roll combinados */
    uint16_t elevonL;       /* Elevon esquerdo — pitch/roll combinados */
    uint16_t motor;         /* Motor — throttle (PWM_MOTOR_STOP a PWM_MOTOR_MAX) */
};

/* =================================================================================
 * ENUMERAÇÃO DE CANAIS LEDC (ESP32)
 * =================================================================================
 * 
 * Usado internamente para mapear atuadores → canais de hardware.
 * A ordem deve corresponder à ordem dos pinos no array _pins[].
 */

enum ActuatorChannel : uint8_t {
    CH_WING_R = 0,          /* Canal 0 → PIN_SERVO_WING_R */
    CH_WING_L,              /* Canal 1 → PIN_SERVO_WING_L */
    CH_RUDDER,              /* Canal 2 → PIN_SERVO_RUDDER */
    CH_ELEVON_R,            /* Canal 3 → PIN_SERVO_ELEVON_R */
    CH_ELEVON_L,            /* Canal 4 → PIN_SERVO_ELEVON_L */
    CH_MOTOR,               /* Canal 5 → PIN_MOTOR_ECU */
    ACTUATOR_COUNT          /* Número total de atuadores (6) */
};

/* =================================================================================
 * CLASSE ACTUATORS
 * =================================================================================
 */

class Actuators {
public:
    /**
     * Construtor — inicializa o estado interno com valores seguros
     */
    Actuators();

    /**
     * Inicializa o hardware PWM e coloca atuadores em posição segura
     * 
     * Deve ser chamado uma vez no setup() antes de qualquer outra operação.
     * Configura os pinos como saída e inicializa o periférico LEDC (ESP32)
     * ou configura timers para PWM por software (outras plataformas).
     */
    void begin();

    /**
     * Arma o sistema de atuadores
     * 
     * Após arm(), o método update() passa a ter efeito.
     * Envia pulso de armamento ao ESC (1000µs por 2 segundos) e aguarda.
     * 
     * @note Se failsafe estiver ativo, o armamento é bloqueado.
     * @note O sistema só pode ser armado uma vez após begin() ou failsafeClear().
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
     * Todos os valores são constrainidos aos limites seguros definidos.
     * 
     * @param signals Estrutura com os valores desejados em microssegundos
     */
    void update(const ActuatorSignals& signals);

    /**
     * Coloca todos os atuadores em posição segura imediatamente
     * 
     * Servos vão para neutro (1500µs), motor para STOP (1000µs).
     * Funciona mesmo em failsafe ou desarmado.
     */
    void safePosition();

    /**
     * Ativa o bloqueio de Failsafe
     * 
     * Quando ativo:
     *   - Força safePosition() imediatamente
     *   - Bloqueia completamente update() e arm()
     *   - (ESP32) Detacha os canais LEDC para segurança máxima
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

    /** Retorna o último conjunto de sinais enviados (para telemetria/diagnóstico) */
    ActuatorSignals getLastSignals() const;

    /**
     * Ativa/desativa modo debug
     * 
     * Quando ativo, imprime informações via Serial para diagnóstico.
     * @param enable true = ativar debug, false = desativar
     */
    void setDebug(bool enable);

private:
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */

    bool _armed;                /* Sistema armado — permite update() */
    bool _failsafeActive;       /* Failsafe ativo — bloqueia tudo */
    bool _debugEnabled;         /* Debug ativo — imprime mensagens */

    ActuatorSignals _lastSignals;   /* Últimos sinais enviados (telemetria) */

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
     * Escreve valor PWM num pino específico com constrain automático
     * 
     * Implementação selecionada automaticamente por plataforma:
     *   - ESP32: LEDC (hardware, não bloqueante)
     *   - Outras: PWM por software com timer (portável)
     * 
     * @param pin   Pino GPIO de saída
     * @param us    Valor desejado em microssegundos
     * @param minUs Limite mínimo seguro
     * @param maxUs Limite máximo seguro
     */
    void _writePWM(uint8_t pin, uint16_t us, uint16_t minUs, uint16_t maxUs);

    /**
     * Configura todos os pinos PWM
     * 
     * Inicializa o hardware específico da plataforma.
     * Chamado internamente por begin().
     */
    void _setupPWM();

    /**
     * (ESP32) Escreve valor PWM via LEDC
     * 
     * @param channel Canal LEDC (0-5)
     * @param us     Valor em microssegundos
     * @param minUs  Limite mínimo
     * @param maxUs  Limite máximo
     */
    void _writeLEDC(uint8_t channel, uint16_t us, uint16_t minUs, uint16_t maxUs);

    /**
     * Array de pinos correspondentes aos canais LEDC
     * 
     * Ordem: WING_R, WING_L, RUDDER, ELEVON_R, ELEVON_L, MOTOR
     */
    static const uint8_t _pins[ACTUATOR_COUNT];
};

#endif /* ACTUATORS_H */
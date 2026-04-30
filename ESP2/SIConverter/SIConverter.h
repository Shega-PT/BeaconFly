/**
 * =================================================================================
 * SICONVERTER.H — CONVERSÃO DE DADOS RAW PARA UNIDADES SI (ESP2)
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
 * Este módulo é responsável por converter os dados RAW recebidos do ESP1
 * (através da UART) em unidades do Sistema Internacional (SI).
 * 
 * Os dados convertidos são disponibilizados para:
 *   • Retorno ao ESP1 (via UART) — para controlo de voo
 *   • Construção de telemetria (enviada para GS via LoRa 868MHz)
 *   • Alimentação do Failsafe (análise de condições críticas)
 * 
 * =================================================================================
 * FLUXO DE DADOS
 * =================================================================================
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP1                                           │
 *   │                                                                             │
 *   │   Sensors (RAW) → UART (TX) →                                              │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 *                                        │
 *                                        ▼ (UART RX)
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP2                                           │
 *   │                                                                             │
 *   │   ┌─────────────────────────────────────────────────────────────────────┐  │
 *   │   │                    SIConverter::update()                            │  │
 *   │   │                                                                      │  │
 *   │   │   RAW Data → Conversões → SI Data                                   │  │
 *   │   │                                                                      │  │
 *   │   │   • IMU (acel, giro, temp)                                          │  │
 *   │   │   • Barómetro (pressão, temp)                                       │  │
 *   │   │   • Bateria (tensão, corrente)                                      │  │
 *   │   │   • Termopares (temperatura)                                        │  │
 *   │   └─────────────────────────────────────────────────────────────────────┘  │
 *   │                                        │                                   │
 *   │          ┌─────────────────────────────┼─────────────────────────────┐    │
 *   │          │                             │                             │    │
 *   │          ▼                             ▼                             ▼    │
 *   │   ┌─────────────┐               ┌─────────────┐               ┌─────────────┐│
 *   │   │  ESP1 (UART)│               │  Telemetry  │               │  Failsafe   ││
 *   │   │  (SI data)  │               │  (para GS)  │               │  (análise)  ││
 *   │   └─────────────┘               └─────────────┘               └─────────────┘│
 *   │                                                                             │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * CONVERSÕES REALIZADAS
 * =================================================================================
 * 
 *   ┌─────────────────┬─────────────────────────┬────────────────────────────────┐
 *   │ Sensor          │ RAW (do ESP1)           │ SI (saída)                     │
 *   ├─────────────────┼─────────────────────────┼────────────────────────────────┤
 *   │ Acelerómetro    │ counts (16 bits)        │ m/s²                           │
 *   │ Giroscópio      │ counts (16 bits)        │ °/s                            │
 *   │ Temperatura IMU │ counts (16 bits)        │ °C                             │
 *   │ Pressão Baro    │ counts (32 bits)        │ Pa (Pascal)                    │
 *   │ Temperatura Baro│ counts (32 bits)        │ °C                             │
 *   │ Tensão Bateria  │ ADC (0-4095)            │ V (Volts)                      │
 *   │ Corrente Bateria│ ADC (0-4095)            │ A (Amperes)                    │
 *   │ Termopares      │ counts (16 bits)        │ °C                             │
 *   └─────────────────┴─────────────────────────┴────────────────────────────────┘
 * 
 * =================================================================================
 * CONSTANTES DE CALIBRAÇÃO (Ajustar conforme hardware)
 * =================================================================================
 * 
 *   • Acelerómetro MPU6050: 16384 LSB/g (para range ±2g)
 *   • Giroscópio MPU6050:   131 LSB/°/s (para range ±250°/s)
 *   • Temperatura MPU6050:  340 LSB/°C, offset 36.53°C
 *   • Barómetro BMP280:     depende da calibração interna
 *   • Divisor de tensão:    depende do hardware (ex: 10k+10k = fator 2)
 *   • Shunt de corrente:    depende do hardware (ex: 0.1Ω, amp 50x)
 * 
 * =================================================================================
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "Protocol.h"
#include "Sensors.h"

/* =================================================================================
 * CONSTANTES DE CALIBRAÇÃO — ACELERÓMETRO (MPU6050, range ±2g)
 * =================================================================================
 */

#define ACCEL_LSB_PER_G         16384.0f    /* LSB/g */
#define GRAVITY_MS2             9.80665f    /* m/s² */

/* =================================================================================
 * CONSTANTES DE CALIBRAÇÃO — GIROSCÓPIO (MPU6050, range ±250°/s)
 * =================================================================================
 */

#define GYRO_LSB_PER_DPS        131.0f      /* LSB/°/s */

/* =================================================================================
 * CONSTANTES DE CALIBRAÇÃO — TEMPERATURA IMU (MPU6050)
 * =================================================================================
 */

#define IMU_TEMP_LSB_PER_C      340.0f      /* LSB/°C */
#define IMU_TEMP_OFFSET         36.53f      /* °C offset */

/* =================================================================================
 * CONSTANTES DE CALIBRAÇÃO — BATERIA (ADC)
 * =================================================================================
 */

#define ADC_MAX_VALUE           4095.0f     /* Resolução 12 bits */
#define ADC_VOLTAGE_REF         3.3f        /* Tensão de referência (V) */
#define VOLTAGE_DIVIDER_FACTOR  2.0f        /* Fator do divisor resistivo */
#define CURRENT_SHUNT_OHMS      0.001f      /* Shunt de 1mΩ (0.001Ω) */
#define CURRENT_AMP_GAIN        50.0f       /* Ganho do amplificador */

/* =================================================================================
 * CONSTANTES DE CALIBRAÇÃO — TERMOPARES (MAX31855)
 * =================================================================================
 */

#define THERMOCOUPLE_LSB_PER_C  0.25f       /* 0.25°C por LSB */

/* =================================================================================
 * ESTRUTURA DE DADOS SI (UNIDADES DO SISTEMA INTERNACIONAL)
 * =================================================================================
 * 
 * Esta estrutura contém todos os dados convertidos para unidades SI,
 * prontos para serem usados pelo FlightControl, Telemetry e Failsafe.
 */

struct SIData {
    /* IMU — Aceleração (m/s²) */
    float accelX_ms2;
    float accelY_ms2;
    float accelZ_ms2;
    
    /* IMU — Velocidade angular (°/s) */
    float gyroX_dps;
    float gyroY_dps;
    float gyroZ_dps;
    
    /* IMU — Temperatura (°C) */
    float imuTemp_c;
    
    /* Barómetro — Pressão (Pa) */
    float pressure_pa;
    
    /* Barómetro — Temperatura (°C) */
    float baroTemp_c;
    
    /* Bateria — Tensão (V) */
    float batteryVoltage_v;
    
    /* Bateria — Corrente (A) */
    float batteryCurrent_a;
    
    /* Bateria — Potência (W) */
    float batteryPower_w;
    
    /* Bateria — Carga consumida (Ah) */
    float batteryCharge_ah;
    
    /* Termopares — Temperatura (°C) */
    float thermocoupleTemp_c[4];
    
    /* Timestamp da aquisição (ms) */
    uint32_t timestamp;
    
    /* Flags de validade */
    bool imuValid;
    bool baroValid;
    bool batteryValid;
    bool thermocoupleValid[4];
};

/* =================================================================================
 * CLASSE SICONVERTER
 * =================================================================================
 */

class SIConverter {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    /**
     * Construtor — inicializa todas as variáveis internas
     */
    SIConverter();
    
    /**
     * Inicializa o módulo de conversão
     * 
     * Define valores padrão e prepara acumuladores (ex: carga da bateria)
     */
    void begin();
    
    /**
     * Reseta o acumulador de carga da bateria
     */
    void resetBatteryCharge();
    
    /* =========================================================================
     * CONVERSÃO DE DADOS
     * ========================================================================= */
    
    /**
     * Converte dados RAW do ESP1 para unidades SI
     * 
     * @param raw Dados RAW recebidos do ESP1 (via UART)
     * @return Dados convertidos para SI
     */
    SIData convert(const SensorsPayload& raw);
    
    /**
     * Atualiza o estado interno com novos dados RAW
     * 
     * @param raw Dados RAW recebidos do ESP1
     */
    void update(const SensorsPayload& raw);
    
    /* =========================================================================
     * ACESSO AOS DADOS CONVERTIDOS
     * ========================================================================= */
    
    /**
     * Retorna os dados SI mais recentes
     */
    const SIData& getData() const;
    
    /**
     * Retorna a aceleração X (m/s²)
     */
    float getAccelX() const;
    
    /**
     * Retorna a aceleração Y (m/s²)
     */
    float getAccelY() const;
    
    /**
     * Retorna a aceleração Z (m/s²)
     */
    float getAccelZ() const;
    
    /**
     * Retorna a velocidade angular X (°/s)
     */
    float getGyroX() const;
    
    /**
     * Retorna a velocidade angular Y (°/s)
     */
    float getGyroY() const;
    
    /**
     * Retorna a velocidade angular Z (°/s)
     */
    float getGyroZ() const;
    
    /**
     * Retorna a temperatura da IMU (°C)
     */
    float getIMUTemperature() const;
    
    /**
     * Retorna a pressão barométrica (Pa)
     */
    float getPressure() const;
    
    /**
     * Retorna a temperatura do barómetro (°C)
     */
    float getBaroTemperature() const;
    
    /**
     * Retorna a tensão da bateria (V)
     */
    float getBatteryVoltage() const;
    
    /**
     * Retorna a corrente da bateria (A)
     */
    float getBatteryCurrent() const;
    
    /**
     * Retorna a potência da bateria (W)
     */
    float getBatteryPower() const;
    
    /**
     * Retorna a carga consumida (Ah)
     */
    float getBatteryCharge() const;
    
    /**
     * Retorna a temperatura de um termopar (°C)
     * 
     * @param index Índice do termopar (0-3)
     */
    float getThermocoupleTemp(uint8_t index) const;
    
    /**
     * Verifica se os dados da IMU são válidos
     */
    bool isIMUValid() const;
    
    /**
     * Verifica se os dados do barómetro são válidos
     */
    bool isBaroValid() const;
    
    /**
     * Verifica se os dados da bateria são válidos
     */
    bool isBatteryValid() const;
    
    /**
     * Verifica se os dados de um termopar são válidos
     * 
     * @param index Índice do termopar (0-3)
     */
    bool isThermocoupleValid(uint8_t index) const;
    
    /**
     * Verifica se os dados críticos (IMU + Baro) são válidos
     */
    bool areCriticalValid() const;
    
    /* =========================================================================
     * CONSTANTES DE CALIBRAÇÃO (GETTERS PARA AJUSTE)
     * ========================================================================= */
    
    /**
     * Define o fator do divisor de tensão da bateria
     * 
     * @param factor Fator (ex: 2.0 para divisor 10k+10k)
     */
    void setVoltageDividerFactor(float factor);
    
    /**
     * Define a resistência do shunt de corrente
     * 
     * @param ohms Resistência em ohms (ex: 0.001 para 1mΩ)
     */
    void setCurrentShuntOhms(float ohms);
    
    /**
     * Define o ganho do amplificador de corrente
     * 
     * @param gain Ganho (ex: 50.0)
     */
    void setCurrentAmpGain(float gain);
    
    /**
     * Define o range do acelerómetro (para cálculo correto)
     * 
     * @param rangeG Range em G (ex: 2, 4, 8, 16)
     */
    void setAccelRangeG(float rangeG);
    
    /**
     * Define o range do giroscópio (para cálculo correto)
     * 
     * @param rangeDPS Range em °/s (ex: 250, 500, 1000, 2000)
     */
    void setGyroRangeDPS(float rangeDPS);
    
    /* =========================================================================
     * DEBUG
     * ========================================================================= */
    
    void setDebug(bool enable);

private:
    /* =========================================================================
     * DADOS INTERNOS
     * ========================================================================= */
    
    SIData _siData;
    bool   _debugEnabled;
    
    /* Acumulador para carga da bateria (Ah) */
    float  _batteryChargeAh;
    uint32_t _lastChargeUpdateMs;
    
    /* Parâmetros de calibração configuráveis */
    float _voltageDividerFactor;
    float _currentShuntOhms;
    float _currentAmpGain;
    float _accelRangeG;
    float _gyroRangeDPS;
    
    /* =========================================================================
     * MÉTODOS PRIVADOS DE CONVERSÃO
     * ========================================================================= */
    
    float _convertAccel(int16_t raw, float rangeG);
    float _convertGyro(int16_t raw, float rangeDPS);
    float _convertIMUTemp(int16_t raw);
    float _convertBaroPressure(int32_t raw);
    float _convertBaroTemp(int32_t raw);
    float _convertBatteryVoltage(uint16_t adc);
    float _convertBatteryCurrent(uint16_t adc);
    float _convertThermocouple(int32_t raw);
    
    /**
     * Atualiza o acumulador de carga da bateria
     * 
     * @param currentA Corrente atual (A)
     */
    void _updateBatteryCharge(float currentA);
};

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* siDataToString(const SIData& data);
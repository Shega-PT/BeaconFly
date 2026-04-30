/**
 * =================================================================================
 * SICONVERTER.CPP — IMPLEMENTAÇÃO DA CONVERSÃO RAW → SI
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-27
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. Assume MPU6050 para IMU (valores padrão)
 * 2. Assume BMP280 para barómetro
 * 3. Assume MAX31855 para termopares
 * 4. Os parâmetros de calibração podem ser ajustados via setters
 * 
 * =================================================================================
 */

#include "SIConverter.h"
#include <Arduino.h>
#include <math.h>

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define SICONVERTER_DEBUG

#ifdef SICONVERTER_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[SIConverter] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

SIConverter::SIConverter()
    : _debugEnabled(false)
    , _batteryChargeAh(0.0f)
    , _lastChargeUpdateMs(0)
    , _voltageDividerFactor(VOLTAGE_DIVIDER_FACTOR)
    , _currentShuntOhms(CURRENT_SHUNT_OHMS)
    , _currentAmpGain(CURRENT_AMP_GAIN)
    , _accelRangeG(2.0f)
    , _gyroRangeDPS(250.0f)
{
    memset(&_siData, 0, sizeof(SIData));
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO
 * =================================================================================
 */

void SIConverter::begin() {
    DEBUG_PRINT("begin() — Inicializando conversor RAW → SI\n");
    
    _batteryChargeAh = 0.0f;
    _lastChargeUpdateMs = millis();
    
    /* Validação dos parâmetros de calibração */
    DEBUG_PRINT("  Parâmetros de calibração:\n");
    DEBUG_PRINT("    Acelerómetro range: %.1f G\n", _accelRangeG);
    DEBUG_PRINT("    Giroscópio range: %.0f °/s\n", _gyroRangeDPS);
    DEBUG_PRINT("    Divisor tensão: %.2f\n", _voltageDividerFactor);
    DEBUG_PRINT("    Shunt corrente: %.4f Ω\n", _currentShuntOhms);
    DEBUG_PRINT("    Ganho corrente: %.1f\n", _currentAmpGain);
    
    DEBUG_PRINT("begin() — Conversor inicializado\n");
}

/* =================================================================================
 * RESET BATTERY CHARGE — RESETA ACUMULADOR DE CARGA
 * =================================================================================
 */

void SIConverter::resetBatteryCharge() {
    _batteryChargeAh = 0.0f;
    _lastChargeUpdateMs = millis();
    DEBUG_PRINT("resetBatteryCharge() — Carga da bateria resetada\n");
}

/* =================================================================================
 * CONVERT ACCEL — CONVERTE ACELERAÇÃO (RAW → m/s²)
 * =================================================================================
 */

float SIConverter::_convertAccel(int16_t raw, float rangeG) {
    /* LSB por G = 32768 / rangeG (para 16 bits) */
    float lsbPerG = 32768.0f / rangeG;
    float accelG = (float)raw / lsbPerG;
    return accelG * GRAVITY_MS2;
}

/* =================================================================================
 * CONVERT GYRO — CONVERTE GIROSCÓPIO (RAW → °/s)
 * =================================================================================
 */

float SIConverter::_convertGyro(int16_t raw, float rangeDPS) {
    /* LSB por °/s = 32768 / rangeDPS (para 16 bits) */
    float lsbPerDps = 32768.0f / rangeDPS;
    return (float)raw / lsbPerDps;
}

/* =================================================================================
 * CONVERT IMU TEMP — CONVERTE TEMPERATURA IMU (RAW → °C)
 * =================================================================================
 */

float SIConverter::_convertIMUTemp(int16_t raw) {
    return ((float)raw / IMU_TEMP_LSB_PER_C) + IMU_TEMP_OFFSET;
}

/* =================================================================================
 * CONVERT BARO PRESSURE — CONVERTE PRESSÃO (RAW → Pa)
 * =================================================================================
 * 
 * NOTA: Esta é uma conversão simplificada. O BMP280 tem calibração complexa.
 * Para produção, usar a biblioteca Adafruit_BMP280 que já fornece valores em Pa.
 */

float SIConverter::_convertBaroPressure(int32_t raw) {
    /* Conversão simplificada: raw de 20 bits → Pa */
    /* Valor típico para 101325 Pa ≈ 5300000 raw */
    return (float)raw * 101325.0f / 5300000.0f;
}

/* =================================================================================
 * CONVERT BARO TEMP — CONVERTE TEMPERATURA BARÓMETRO (RAW → °C)
 * =================================================================================
 */

float SIConverter::_convertBaroTemp(int32_t raw) {
    /* Conversão simplificada: raw de 20 bits → °C */
    /* Valor típico para 25°C ≈ 250000 raw */
    return ((float)raw - 250000.0f) / 10000.0f;
}

/* =================================================================================
 * CONVERT BATTERY VOLTAGE — CONVERTE TENSÃO (ADC → V)
 * =================================================================================
 */

float SIConverter::_convertBatteryVoltage(uint16_t adc) {
    float voltageAtAdc = (adc / ADC_MAX_VALUE) * ADC_VOLTAGE_REF;
    return voltageAtAdc * _voltageDividerFactor;
}

/* =================================================================================
 * CONVERT BATTERY CURRENT — CONVERTE CORRENTE (ADC → A)
 * =================================================================================
 * 
 * Fórmula: I = V_adc / (R_shunt * Ganho)
 */

float SIConverter::_convertBatteryCurrent(uint16_t adc) {
    float voltageAtAdc = (adc / ADC_MAX_VALUE) * ADC_VOLTAGE_REF;
    float voltageAtShunt = voltageAtAdc / _currentAmpGain;
    return voltageAtShunt / _currentShuntOhms;
}

/* =================================================================================
 * CONVERT THERMOCOUPLE — CONVERTE TERMOPAR (RAW → °C)
 * =================================================================================
 */

float SIConverter::_convertThermocouple(int32_t raw) {
    return (float)raw * THERMOCOUPLE_LSB_PER_C;
}

/* =================================================================================
 * UPDATE BATTERY CHARGE — ATUALIZA ACUMULADOR DE CARGA (Ah)
 * =================================================================================
 */

void SIConverter::_updateBatteryCharge(float currentA) {
    uint32_t now = millis();
    float deltaHours = (now - _lastChargeUpdateMs) / 3600000.0f;
    _batteryChargeAh += currentA * deltaHours;
    _lastChargeUpdateMs = now;
    
    /* Limitar a valores negativos (carga não pode ser negativa) */
    if (_batteryChargeAh < 0) _batteryChargeAh = 0;
}

/* =================================================================================
 * CONVERT — CONVERSÃO PRINCIPAL
 * =================================================================================
 */

SIData SIConverter::convert(const SensorsPayload& raw) {
    SIData si;
    
    /* =========================================================================
     * IMU — ACELERAÇÃO
     * ========================================================================= */
    
    si.accelX_ms2 = _convertAccel(raw.imu.accelX, _accelRangeG);
    si.accelY_ms2 = _convertAccel(raw.imu.accelY, _accelRangeG);
    si.accelZ_ms2 = _convertAccel(raw.imu.accelZ, _accelRangeG);
    
    /* =========================================================================
     * IMU — GIROSCÓPIO
     * ========================================================================= */
    
    si.gyroX_dps = _convertGyro(raw.imu.gyroX, _gyroRangeDPS);
    si.gyroY_dps = _convertGyro(raw.imu.gyroY, _gyroRangeDPS);
    si.gyroZ_dps = _convertGyro(raw.imu.gyroZ, _gyroRangeDPS);
    
    /* =========================================================================
     * IMU — TEMPERATURA
     * ========================================================================= */
    
    si.imuTemp_c = _convertIMUTemp(raw.imu.temperature);
    
    /* =========================================================================
     * BARÓMETRO
     * ========================================================================= */
    
    si.pressure_pa = _convertBaroPressure(raw.baro.pressure);
    si.baroTemp_c = _convertBaroTemp(raw.baro.temperature);
    
    /* =========================================================================
     * BATERIA
     * ========================================================================= */
    
    si.batteryVoltage_v = _convertBatteryVoltage(raw.battery.adcVoltage);
    si.batteryCurrent_a = _convertBatteryCurrent(raw.battery.adcCurrent);
    si.batteryPower_w = si.batteryVoltage_v * si.batteryCurrent_a;
    
    /* =========================================================================
     * TERMOPARES
     * ========================================================================= */
    
    for (uint8_t i = 0; i < 4; i++) {
        si.thermocoupleTemp_c[i] = _convertThermocouple(raw.thermocouples[i]);
    }
    
    /* =========================================================================
     * TIMESTAMP E VALIDADE
     * ========================================================================= */
    
    si.timestamp = millis();
    si.imuValid = raw.imu.valid;
    si.baroValid = raw.baro.valid;
    si.batteryValid = raw.battery.valid;
    for (uint8_t i = 0; i < 4; i++) {
        si.thermocoupleValid[i] = (raw.thermocouples[i] != 0);
    }
    
    /* =========================================================================
     * ATUALIZAR CARGA DA BATERIA
     * ========================================================================= */
    
    _updateBatteryCharge(si.batteryCurrent_a);
    si.batteryCharge_ah = _batteryChargeAh;
    
    DEBUG_PRINT("convert() — Raw → SI concluído\n");
    DEBUG_PRINT("  Accel: %.2f, %.2f, %.2f m/s²\n", si.accelX_ms2, si.accelY_ms2, si.accelZ_ms2);
    DEBUG_PRINT("  Gyro: %.2f, %.2f, %.2f °/s\n", si.gyroX_dps, si.gyroY_dps, si.gyroZ_dps);
    DEBUG_PRINT("  Bateria: %.2f V, %.2f A, %.2f W\n", si.batteryVoltage_v, si.batteryCurrent_a, si.batteryPower_w);
    
    return si;
}

/* =================================================================================
 * UPDATE — ATUALIZA ESTADO INTERNO
 * =================================================================================
 */

void SIConverter::update(const SensorsPayload& raw) {
    _siData = convert(raw);
}

/* =================================================================================
 * GET DATA — RETORNA DADOS SI MAIS RECENTES
 * =================================================================================
 */

const SIData& SIConverter::getData() const {
    return _siData;
}

/* =================================================================================
 * GETTERS INDIVIDUAIS
 * =================================================================================
 */

float SIConverter::getAccelX() const { return _siData.accelX_ms2; }
float SIConverter::getAccelY() const { return _siData.accelY_ms2; }
float SIConverter::getAccelZ() const { return _siData.accelZ_ms2; }
float SIConverter::getGyroX() const { return _siData.gyroX_dps; }
float SIConverter::getGyroY() const { return _siData.gyroY_dps; }
float SIConverter::getGyroZ() const { return _siData.gyroZ_dps; }
float SIConverter::getIMUTemperature() const { return _siData.imuTemp_c; }
float SIConverter::getPressure() const { return _siData.pressure_pa; }
float SIConverter::getBaroTemperature() const { return _siData.baroTemp_c; }
float SIConverter::getBatteryVoltage() const { return _siData.batteryVoltage_v; }
float SIConverter::getBatteryCurrent() const { return _siData.batteryCurrent_a; }
float SIConverter::getBatteryPower() const { return _siData.batteryPower_w; }
float SIConverter::getBatteryCharge() const { return _siData.batteryCharge_ah; }
float SIConverter::getThermocoupleTemp(uint8_t index) const {
    if (index < 4) return _siData.thermocoupleTemp_c[index];
    return 0.0f;
}

/* =================================================================================
 * VALIDADE DOS DADOS
 * =================================================================================
 */

bool SIConverter::isIMUValid() const { return _siData.imuValid; }
bool SIConverter::isBaroValid() const { return _siData.baroValid; }
bool SIConverter::isBatteryValid() const { return _siData.batteryValid; }

bool SIConverter::isThermocoupleValid(uint8_t index) const {
    if (index < 4) return _siData.thermocoupleValid[index];
    return false;
}

bool SIConverter::areCriticalValid() const {
    return (_siData.imuValid && _siData.baroValid);
}

/* =================================================================================
 * SETTERS DE CALIBRAÇÃO
 * =================================================================================
 */

void SIConverter::setVoltageDividerFactor(float factor) {
    _voltageDividerFactor = factor;
    DEBUG_PRINT("setVoltageDividerFactor() — Novo fator: %.2f\n", factor);
}

void SIConverter::setCurrentShuntOhms(float ohms) {
    _currentShuntOhms = ohms;
    DEBUG_PRINT("setCurrentShuntOhms() — Nova resistência: %.4f Ω\n", ohms);
}

void SIConverter::setCurrentAmpGain(float gain) {
    _currentAmpGain = gain;
    DEBUG_PRINT("setCurrentAmpGain() — Novo ganho: %.1f\n", gain);
}

void SIConverter::setAccelRangeG(float rangeG) {
    _accelRangeG = rangeG;
    DEBUG_PRINT("setAccelRangeG() — Novo range: %.1f G\n", rangeG);
}

void SIConverter::setGyroRangeDPS(float rangeDPS) {
    _gyroRangeDPS = rangeDPS;
    DEBUG_PRINT("setGyroRangeDPS() — Novo range: %.0f °/s\n", rangeDPS);
}

/* =================================================================================
 * DEBUG
 * =================================================================================
 */

void SIConverter::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}

/* =================================================================================
 * SI DATA TO STRING — FUNÇÃO AUXILIAR PARA DEBUG
 * =================================================================================
 */

const char* siDataToString(const SIData& data) {
    static char buffer[512];
    snprintf(buffer, sizeof(buffer),
        "SI Data:\n"
        "  Accel: %.2f, %.2f, %.2f m/s²\n"
        "  Gyro: %.2f, %.2f, %.2f °/s\n"
        "  IMU Temp: %.2f °C\n"
        "  Pressão: %.2f Pa\n"
        "  Baro Temp: %.2f °C\n"
        "  Bateria: %.2f V, %.2f A, %.2f W, %.3f Ah\n"
        "  TC0: %.2f°C, TC1: %.2f°C, TC2: %.2f°C, TC3: %.2f°C\n"
        "  Timestamp: %lu ms\n"
        "  Válido: IMU=%d, Baro=%d, Batt=%d",
        data.accelX_ms2, data.accelY_ms2, data.accelZ_ms2,
        data.gyroX_dps, data.gyroY_dps, data.gyroZ_dps,
        data.imuTemp_c,
        data.pressure_pa,
        data.baroTemp_c,
        data.batteryVoltage_v, data.batteryCurrent_a, data.batteryPower_w, data.batteryCharge_ah,
        data.thermocoupleTemp_c[0], data.thermocoupleTemp_c[1],
        data.thermocoupleTemp_c[2], data.thermocoupleTemp_c[3],
        data.timestamp,
        data.imuValid, data.baroValid, data.batteryValid);
    return buffer;
}
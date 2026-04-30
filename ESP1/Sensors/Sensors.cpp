/**
 * =================================================================================
 * SENSORS.CPP — IMPLEMENTAÇÃO DA AQUISIÇÃO DE SENSORES
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-17
 * VERSÃO:     2.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. IMU: MPU6050 (endereço 0x68) — Acel ±2g, Giro ±250°/s
 * 2. Barómetro: BMP280 (endereço 0x76) — Modo normal, oversampling ×16
 * 3. Termopares: MAX31855 via SPI — 4 termopares com CS dedicados
 * 4. Bateria: ADC com média móvel (8 amostras) para reduzir ruído
 * 
 * =================================================================================
 * BIBLIOTECAS NECESSÁRIAS
 * =================================================================================
 * 
 * Para usar sensores reais, instale as bibliotecas:
 *   • MPU6050: https://github.com/electroniccats/MPU6050
 *   • BMP280:  https://github.com/adafruit/Adafruit_BMP280_Library
 *   • MAX31855: https://github.com/adafruit/Adafruit_MAX31855
 * 
 * No PlatformIO, adicionar ao platformio.ini:
 *   lib_deps = 
 *     electroniccats/MPU6050
 *     adafruit/Adafruit BMP280 Library
 *     adafruit/Adafruit MAX31855 library
 * 
 * =================================================================================
 */

#include "Sensors.h"
#include <Wire.h>
#include <SPI.h>

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define SENSORS_DEBUG

#ifdef SENSORS_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[Sensors] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * CONSTANTES DE SENSORES
 * =================================================================================
 */

/* Endereços I2C */
#define IMU_ADDR        0x68    /* MPU6050 (padrão, AD0=0) */
#define BARO_ADDR       0x76    /* BMP280 (padrão, SDO=0) */
#define BARO_ADDR_ALT   0x77    /* BMP280 alternativo (SDO=1) */

/* Registradores MPU6050 */
#define MPU6050_ACCEL_XOUT_H   0x3B
#define MPU6050_GYRO_XOUT_H    0x43
#define MPU6050_TEMP_OUT_H     0x41
#define MPU6050_PWR_MGMT_1     0x6B
#define MPU6050_WHO_AM_I       0x75

/* Registradores BMP280 */
#define BMP280_REG_DIG_T1      0x88
#define BMP280_REG_DIG_T2      0x8A
#define BMP280_REG_DIG_T3      0x8C
#define BMP280_REG_DIG_P1      0x8E
#define BMP280_REG_DIG_P2      0x90
#define BMP280_REG_DIG_P3      0x92
#define BMP280_REG_DIG_P4      0x94
#define BMP280_REG_DIG_P5      0x96
#define BMP280_REG_DIG_P6      0x98
#define BMP280_REG_DIG_P7      0x9A
#define BMP280_REG_DIG_P8      0x9C
#define BMP280_REG_DIG_P9      0x9E
#define BMP280_REG_CTRL_MEAS   0xF4
#define BMP280_REG_CONFIG      0xF5
#define BMP280_REG_PRESS_MSB   0xF7
#define BMP280_REG_TEMP_MSB    0xFA

/* Pinos SPI para termopares (MAX31855) */
#define TC_CS1_PIN     15      /* Termopar 1 — Chip Select */
#define TC_CS2_PIN     16      /* Termopar 2 */
#define TC_CS3_PIN     17      /* Termopar 3 */
#define TC_CS4_PIN     18      /* Termopar 4 */
#define TC_MISO_PIN    19      /* SPI MISO (dados do termopar) */
#define TC_SCK_PIN     23      /* SPI SCK */

/* Parâmetros de leitura */
#define ADC_SAMPLES     8       /* Amostras para média móvel da bateria */
#define I2C_TIMEOUT_MS  10      /* Timeout para operações I2C (ms) */
#define SENSOR_RETRIES  3       /* Tentativas de leitura antes de marcar inválido */

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

Sensors::Sensors()
    : _driverCount(0)
    , _lastUpdateTime(0)
    , _debugEnabled(false)
    , _i2cInitialized(false)
{
    /* Inicializar estruturas com zeros */
    memset(&_imu, 0, sizeof(IMURaw));
    memset(&_baro, 0, sizeof(BaroRaw));
    memset(&_battery, 0, sizeof(BatteryRaw));
    memset(_thermocouples, 0, sizeof(_thermocouples));
    memset(_drivers, 0, sizeof(_drivers));
    
    /* Valores padrão para dados inválidos */
    _imu.valid = false;
    _baro.valid = false;
    _battery.valid = false;
    for (uint8_t i = 0; i < THERMOCOUPLE_COUNT; i++) {
        _thermocouples[i].valid = false;
    }
    
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * INIT I2C — INICIALIZA O BARRAMENTO I2C
 * =================================================================================
 */

void Sensors::_initI2C() {
    if (_i2cInitialized) return;
    
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_FREQUENCY_HZ);
    Wire.setTimeOut(I2C_TIMEOUT_MS);
    
    _i2cInitialized = true;
    DEBUG_PRINT("_initI2C() — I2C inicializado: SDA=%d, SCL=%d, freq=%d kHz\n",
                I2C_SDA, I2C_SCL, I2C_FREQUENCY_HZ / 1000);
}

/* =================================================================================
 * I2C PROBE — VERIFICA SE DISPOSITIVO RESPONDE
 * =================================================================================
 */

bool Sensors::_i2cProbe(uint8_t addr) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    return (error == 0);
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO DE TODOS OS SENSORES
 * =================================================================================
 */

void Sensors::begin() {
    DEBUG_PRINT("begin() — Inicializando sensores...\n");
    
    /* Inicializar I2C */
    _initI2C();
    
    /* Inicializar SPI para termopares */
    SPI.begin(TC_SCK_PIN, TC_MISO_PIN, -1, -1);  /* SCK, MISO, MOSI (não usado), CS (gerido manualmente) */
    pinMode(TC_CS1_PIN, OUTPUT);
    pinMode(TC_CS2_PIN, OUTPUT);
    pinMode(TC_CS3_PIN, OUTPUT);
    pinMode(TC_CS4_PIN, OUTPUT);
    digitalWrite(TC_CS1_PIN, HIGH);
    digitalWrite(TC_CS2_PIN, HIGH);
    digitalWrite(TC_CS3_PIN, HIGH);
    digitalWrite(TC_CS4_PIN, HIGH);
    
    /* Inicializar sensores internos */
    _readIMU();          /* Já faz a configuração do MPU6050 */
    _readBaro();         /* Já faz a configuração do BMP280 */
    _readBattery();      /* Leitura inicial */
    _readThermocouples(); /* Leitura inicial */
    
    /* Inicializar drivers externos registados */
    for (uint8_t i = 0; i < _driverCount; i++) {
        if (_drivers[i] && !_drivers[i]->begin()) {
            DEBUG_PRINT("begin() — Driver '%s' falhou na inicialização\n", _drivers[i]->name());
        }
    }
    
    _lastUpdateTime = millis();
    DEBUG_PRINT("begin() — Concluído. %d drivers externos registados.\n", _driverCount);
}

/* =================================================================================
 * UPDATE — LEITURA PERIÓDICA (CHAMAR A CADA CICLO)
 * =================================================================================
 */

void Sensors::update() {
    /* Ler sensores internos */
    _readIMU();
    _readBaro();
    _readThermocouples();
    _readBattery();
    
    /* Atualizar drivers externos */
    for (uint8_t i = 0; i < _driverCount; i++) {
        if (_drivers[i]) {
            _drivers[i]->update();
        }
    }
    
    _lastUpdateTime = millis();
    
    if (_debugEnabled) {
        DEBUG_PRINT("update() — IMU válida=%d, Baro válido=%d, Bateria válida=%d\n",
                    _imu.valid, _baro.valid, _battery.valid);
    }
}

/* =================================================================================
 * READ IMU — LEITURA DO MPU6050 VIA I2C
 * =================================================================================
 * 
 * Configuração:
 *   • Aceleração: range ±2g (16,384 LSB/g)
 *   • Giroscópio: range ±250°/s (131 LSB/°/s)
 *   • Saída do clock: ativado
 */

void Sensors::_readIMU() {
    _imu.timestamp = millis();
    
    /* Verificar se o sensor está presente */
    if (!_i2cProbe(IMU_ADDR)) {
        _imu.valid = false;
        DEBUG_PRINT("_readIMU() — MPU6050 não detectado no endereço 0x%02X\n", IMU_ADDR);
        return;
    }
    
    /* Configurar MPU6050 (apenas na primeira vez) */
    static bool configured = false;
    if (!configured) {
        Wire.beginTransmission(IMU_ADDR);
        Wire.write(MPU6050_PWR_MGMT_1);
        Wire.write(0x00);  /* Sair do sleep mode */
        if (Wire.endTransmission() == 0) {
            configured = true;
            DEBUG_PRINT("_readIMU() — MPU6050 configurado\n");
        } else {
            _imu.valid = false;
            return;
        }
        delay(100);  /* Aguardar estabilização */
    }
    
    /* Ler 14 bytes: Acel (6) + Temp (2) + Giro (6) */
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(MPU6050_ACCEL_XOUT_H);
    if (Wire.endTransmission() != 0) {
        _imu.valid = false;
        return;
    }
    
    Wire.requestFrom(IMU_ADDR, 14);
    if (Wire.available() < 14) {
        _imu.valid = false;
        return;
    }
    
    /* Aceleração (big-endian → little-endian) */
    _imu.accelX = (Wire.read() << 8) | Wire.read();
    _imu.accelY = (Wire.read() << 8) | Wire.read();
    _imu.accelZ = (Wire.read() << 8) | Wire.read();
    
    /* Temperatura */
    _imu.temperature = (Wire.read() << 8) | Wire.read();
    
    /* Giroscópio */
    _imu.gyroX = (Wire.read() << 8) | Wire.read();
    _imu.gyroY = (Wire.read() << 8) | Wire.read();
    _imu.gyroZ = (Wire.read() << 8) | Wire.read();
    
    _imu.valid = true;
    
    if (_debugEnabled) {
        DEBUG_PRINT("_readIMU() — Accel(%d,%d,%d) Gyro(%d,%d,%d) Temp=%d\n",
                    _imu.accelX, _imu.accelY, _imu.accelZ,
                    _imu.gyroX, _imu.gyroY, _imu.gyroZ,
                    _imu.temperature);
    }
}

/* =================================================================================
 * READ BARO — LEITURA DO BMP280 VIA I2C
 * =================================================================================
 * 
 * Configuração:
 *   • Modo: normal (medição contínua)
 *   • Oversampling: ×16 (alta resolução)
 *   • Filtro: ×16 (suavização)
 */

void Sensors::_readBaro() {
    _baro.timestamp = millis();
    
    /* Verificar qual endereço está presente */
    static uint8_t baroAddr = 0;
    static bool configured = false;
    
    if (baroAddr == 0) {
        if (_i2cProbe(BARO_ADDR)) {
            baroAddr = BARO_ADDR;
        } else if (_i2cProbe(BARO_ADDR_ALT)) {
            baroAddr = BARO_ADDR_ALT;
        } else {
            _baro.valid = false;
            DEBUG_PRINT("_readBaro() — BMP280 não detectado\n");
            return;
        }
        DEBUG_PRINT("_readBaro() — BMP280 detectado no endereço 0x%02X\n", baroAddr);
    }
    
    /* Configurar BMP280 (apenas na primeira vez) */
    if (!configured) {
        /* Configurar: modo normal, oversampling ×16, filtro ×16 */
        Wire.beginTransmission(baroAddr);
        Wire.write(BMP280_REG_CTRL_MEAS);
        Wire.write(0x57);  /* 0b01010111: temp_os=5, press_os=5, mode=3 */
        if (Wire.endTransmission() != 0) {
            _baro.valid = false;
            return;
        }
        
        Wire.beginTransmission(baroAddr);
        Wire.write(BMP280_REG_CONFIG);
        Wire.write(0xD0);  /* 0b11010000: filter=4, t_sb=5 */
        Wire.endTransmission();
        
        configured = true;
        DEBUG_PRINT("_readBaro() — BMP280 configurado\n");
        delay(100);  /* Aguardar primeira medição */
    }
    
    /* Ler pressão e temperatura (6 bytes) */
    Wire.beginTransmission(baroAddr);
    Wire.write(BMP280_REG_PRESS_MSB);
    if (Wire.endTransmission() != 0) {
        _baro.valid = false;
        return;
    }
    
    Wire.requestFrom(baroAddr, 6);
    if (Wire.available() < 6) {
        _baro.valid = false;
        return;
    }
    
    /* Pressão (20 bits) */
    uint32_t pressRaw = ((uint32_t)Wire.read() << 12) |
                        ((uint32_t)Wire.read() << 4) |
                        (Wire.read() >> 4);
    
    /* Temperatura (20 bits) */
    uint32_t tempRaw = ((uint32_t)Wire.read() << 12) |
                       ((uint32_t)Wire.read() << 4) |
                       (Wire.read() >> 4);
    
    _baro.pressure = (int32_t)pressRaw;
    _baro.temperature = (int32_t)tempRaw;
    _baro.valid = true;
    
    if (_debugEnabled) {
        DEBUG_PRINT("_readBaro() — PressRaw=%ld, TempRaw=%ld\n",
                    _baro.pressure, _baro.temperature);
    }
}

/* =================================================================================
 * READ THERMOCOUPLES — LEITURA DOS 4 TERMOPARES VIA SPI (MAX31855)
 * =================================================================================
 * 
 * O MAX31855 retorna 4 bytes:
 *   • Bits 31-18: Temperatura (12 bits, 0.25°C/LSB)
 *   • Bits 16-0:  Status de falha
 */

void Sensors::_readThermocouples() {
    const uint8_t csPins[THERMOCOUPLE_COUNT] = {TC_CS1_PIN, TC_CS2_PIN, TC_CS3_PIN, TC_CS4_PIN};
    
    for (uint8_t i = 0; i < THERMOCOUPLE_COUNT; i++) {
        _thermocouples[i].timestamp = millis();
        
        /* Selecionar o termopar */
        digitalWrite(csPins[i], LOW);
        delayMicroseconds(10);
        
        /* Ler 4 bytes (32 bits) */
        uint32_t raw = 0;
        for (uint8_t b = 0; b < 4; b++) {
            raw = (raw << 8) | SPI.transfer(0x00);
        }
        
        /* Deselecionar */
        digitalWrite(csPins[i], HIGH);
        
        /* Extrair temperatura (bits 31-18, 12 bits, 0.25°C/LSB) */
        int16_t tempRaw = (raw >> 18) & 0x0FFF;
        
        /* Verificar se é negativa (formato complemento para 2) */
        if (tempRaw & 0x0800) {
            tempRaw |= 0xF000;  /* Extender sinal */
        }
        
        /* Extrair falhas (bits 16-0) */
        uint16_t faultStatus = raw & 0x0001FFFF;
        
        _thermocouples[i].rawValue = tempRaw;
        _thermocouples[i].fault = (faultStatus != 0) ? 1 : 0;
        _thermocouples[i].valid = (faultStatus == 0);
        
        if (_debugEnabled && !_thermocouples[i].valid) {
            DEBUG_PRINT("_readThermocouples() — TC%d falha: 0x%04X\n", i, faultStatus);
        }
    }
    
    if (_debugEnabled) {
        DEBUG_PRINT("_readThermocouples() — TC0=%ld, TC1=%ld, TC2=%ld, TC3=%ld\n",
                    _thermocouples[0].rawValue, _thermocouples[1].rawValue,
                    _thermocouples[2].rawValue, _thermocouples[3].rawValue);
    }
}

/* =================================================================================
 * READ BATTERY — LEITURA DA BATERIA VIA ADC COM MÉDIA MÓVEL
 * =================================================================================
 * 
 * 8 amostras consecutivas para reduzir ruído.
 * Os pinos 34 e 35 são entradas analógicas puras (sem pull-up).
 */

void Sensors::_readBattery() {
    _battery.timestamp = millis();
    
    uint32_t sumVoltage = 0;
    uint32_t sumCurrent = 0;
    
    for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
        sumVoltage += analogRead(BATT_PIN_VOLTAGE);
        sumCurrent += analogRead(BATT_PIN_CURRENT);
    }
    
    _battery.adcVoltage = (uint16_t)(sumVoltage / ADC_SAMPLES);
    _battery.adcCurrent = (uint16_t)(sumCurrent / ADC_SAMPLES);
    _battery.valid = true;
    
    if (_debugEnabled) {
        DEBUG_PRINT("_readBattery() — V_ADC=%d (%.2fV), A_ADC=%d\n",
                    _battery.adcVoltage, _battery.adcVoltage * 3.3f / 4095.0f,
                    _battery.adcCurrent);
    }
}

/* =================================================================================
 * SISTEMA DE DRIVERS
 * =================================================================================
 */

bool Sensors::addDriver(SensorDriver* driver) {
    if (!driver) {
        DEBUG_PRINT("addDriver() — Driver NULL\n");
        return false;
    }
    
    if (_driverCount >= MAX_DRIVERS) {
        DEBUG_PRINT("addDriver() — Máximo de %d drivers atingido\n", MAX_DRIVERS);
        return false;
    }
    
    _drivers[_driverCount++] = driver;
    DEBUG_PRINT("addDriver() — Driver '%s' registado (%d/%d)\n",
                driver->name(), _driverCount, MAX_DRIVERS);
    return true;
}

uint8_t Sensors::getDriverCount() const {
    return _driverCount;
}

/* =================================================================================
 * GETTERS — DADOS INDIVIDUAIS
 * =================================================================================
 */

const IMURaw& Sensors::getIMU() const {
    return _imu;
}

const BaroRaw& Sensors::getBaro() const {
    return _baro;
}

const ThermocoupleRaw& Sensors::getThermocouple(uint8_t idx) const {
    static const ThermocoupleRaw invalid = {0, 0, false, 0};
    if (idx >= THERMOCOUPLE_COUNT) {
        return invalid;
    }
    return _thermocouples[idx];
}

const BatteryRaw& Sensors::getBattery() const {
    return _battery;
}

SensorsRaw Sensors::getAllRaw() const {
    SensorsRaw all;
    all.imu = _imu;
    all.baro = _baro;
    all.battery = _battery;
    for (uint8_t i = 0; i < THERMOCOUPLE_COUNT; i++) {
        all.thermocouples[i] = _thermocouples[i];
    }
    return all;
}

/* =================================================================================
 * GET PAYLOAD — ESTRUTURA COMPACTA PARA UART
 * =================================================================================
 * 
 * Esta estrutura é enviada byte a byte para o ESP2.
 * O uso de #pragma pack garante que não há padding entre campos.
 */

SensorsPayload Sensors::getPayload() const {
    SensorsPayload p;
    p.imu = _imu;
    p.baro = _baro;
    p.battery = _battery;
    for (uint8_t i = 0; i < THERMOCOUPLE_COUNT; i++) {
        p.thermocouples[i] = _thermocouples[i].rawValue;
    }
    return p;
}

/* =================================================================================
 * ESTADO E DIAGNÓSTICO
 * =================================================================================
 */

bool Sensors::areCriticalValid() const {
    return (_imu.valid && _baro.valid);
}

uint32_t Sensors::getLastUpdateTime() const {
    return _lastUpdateTime;
}

void Sensors::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}
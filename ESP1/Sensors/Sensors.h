#pragma once

/**
 * =================================================================================
 * SENSORS.H — AQUISIÇÃO DE DADOS RAW (ESP1)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-17
 * VERSÃO:     2.0.0 (Fusão VA + VB com sistema de drivers)
 * 
 * =================================================================================
 * FILOSOFIA DE DESIGN
 * =================================================================================
 * 
 * Este módulo é responsável por adquirir dados CRUS (raw) de todos os sensores
 * ligados ao ESP1. NÃO faz conversão para unidades SI — isso é responsabilidade
 * do ESP2.
 * 
 * RESPONSABILIDADES CLARAS:
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │  Sensors (ESP1)     → Adquire dados RAW (counts, ADC, etc.)             │
 *   │  ESP2               → Converte RAW → SI (unidades físicas)              │
 *   │  FlightControl      → Usa dados SI para controlo de voo                 │
 *   └─────────────────────────────────────────────────────────────────────────┘
 * 
 * PORQUÊ ESTA SEPARAÇÃO?
 * 
 *   1. Performance: ESP1 foca em controlo de voo (tempo real)
 *   2. Flexibilidade: Mudar sensores não requer alterar lógica de voo
 *   3. Manutenibilidade: Calibrações e conversões centralizadas no ESP2
 * 
 * =================================================================================
 * SENSORES SUPORTADOS
 * =================================================================================
 * 
 *   • IMU (MPU6050, ICM42688, etc.)     → Aceleração + Giroscópio
 *   • Barómetro (BMP280, MS5611, etc.)  → Pressão + Temperatura
 *   • Bateria (ADC)                     → Tensão + Corrente
 *   • Termopares (MAX31855, MAX6675)    → Temperatura (4 canais)
 * 
 * =================================================================================
 * SISTEMA DE DRIVERS (EXTENSIBILIDADE)
 * =================================================================================
 * 
 * Para adicionar um novo sensor, crie uma classe que herde de SensorDriver
 * e registe-a com addDriver(). Exemplo:
 * 
 *   class MeuSensor : public SensorDriver {
 *       bool begin() override;
 *       void update() override;
 *       const char* name() const override { return "MeuSensor"; }
 *   };
 *   
 *   Sensors sensors;
 *   sensors.addDriver(new MeuSensor());
 * 
 * =================================================================================
 */

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

/* =================================================================================
 * CONSTANTES DE HARDWARE
 * =================================================================================
 */

/**
 * THERMOCOUPLE_COUNT — Número de termopares ligados ao ESP1
 * 
 * BeaconFly v1.0 suporta até 4 termopares (um por ESC/motor)
 */
#define THERMOCOUPLE_COUNT  4

/**
 * MAX_DRIVERS — Número máximo de drivers de sensores externos
 * 
 * Suficiente para sensores adicionais (GPS externo, lidar, etc.)
 */
#define MAX_DRIVERS         8

/**
 * BATT_PIN_VOLTAGE — Pino ADC para leitura de tensão da bateria
 * 
 * GPIO34 é ADC1_CH6, entrada analógica pura (sem pull-up)
 */
#define BATT_PIN_VOLTAGE    34

/**
 * BATT_PIN_CURRENT — Pino ADC para leitura de corrente da bateria
 * 
 * GPIO35 é ADC1_CH7, entrada analógica pura
 */
#define BATT_PIN_CURRENT    35

/**
 * I2C_SDA — Pino I2C para dados (data)
 * 
 * GPIO21 é SDA padrão no ESP32 DevKit
 */
#define I2C_SDA             21

/**
 * I2C_SCL — Pino I2C para clock
 * 
 * GPIO22 é SCL padrão no ESP32 DevKit
 */
#define I2C_SCL             22

/**
 * I2C_FREQUENCY_HZ — Frequência do barramento I2C
 * 
 * 400 kHz é o máximo suportado pela maioria dos sensores
 */
#define I2C_FREQUENCY_HZ    400000

/* =================================================================================
 * ESTRUTURAS DE DADOS RAW
 * =================================================================================
 */

/**
 * IMURaw — Dados crus do sensor inercial (IMU)
 * 
 * Valores em counts/LSB (depende do range configurado)
 * Exemplo: MPU6050 com range ±2g → 16384 LSB/g
 */
struct IMURaw {
    int16_t accelX;         /* Aceleração X (counts) */
    int16_t accelY;         /* Aceleração Y (counts) */
    int16_t accelZ;         /* Aceleração Z (counts) */
    int16_t gyroX;          /* Velocidade angular X (counts) */
    int16_t gyroY;          /* Velocidade angular Y (counts) */
    int16_t gyroZ;          /* Velocidade angular Z (counts) */
    int16_t temperature;    /* Temperatura do sensor (counts) */
    bool    valid;          /* Dados válidos? */
    uint32_t timestamp;     /* Timestamp da leitura (millis) */
};

/**
 * BaroRaw — Dados crus do barómetro
 * 
 * Pressão: counts (depende do sensor)
 * Temperatura: counts (depende do sensor)
 */
struct BaroRaw {
    int32_t pressure;       /* Pressão atmosférica (counts) */
    int32_t temperature;    /* Temperatura (counts) */
    bool    valid;          /* Dados válidos? */
    uint32_t timestamp;     /* Timestamp da leitura (millis) */
};

/**
 * ThermocoupleRaw — Dados crus de um termopar
 * 
 * rawValue: temperatura em counts (ex: MAX31855 → 0.25°C por LSB)
 * fault:    código de erro do termopar (0 = sem erro)
 */
struct ThermocoupleRaw {
    int32_t rawValue;       /* Temperatura (counts) */
    uint8_t fault;          /* Código de falha (0 = OK) */
    bool    valid;          /* Dados válidos? */
    uint32_t timestamp;     /* Timestamp da leitura (millis) */
};

/**
 * BatteryRaw — Dados crus da bateria (ADC)
 * 
 * adcVoltage: valor ADC (0-4095) — corresponde à tensão da bateria
 * adcCurrent: valor ADC (0-4095) — corresponde à corrente
 */
struct BatteryRaw {
    uint16_t adcVoltage;    /* ADC da tensão (0-4095) */
    uint16_t adcCurrent;    /* ADC da corrente (0-4095) */
    bool    valid;          /* Dados válidos? */
    uint32_t timestamp;     /* Timestamp da leitura (millis) */
};

/**
 * SensorsRaw — Agregação de todos os dados crus
 * 
 * Utilizado para envio via túnel UART para o ESP2
 */
struct SensorsRaw {
    IMURaw      imu;
    BaroRaw     baro;
    BatteryRaw  battery;
    ThermocoupleRaw thermocouples[THERMOCOUPLE_COUNT];
};

/**
 * SensorsPayload — Estrutura compacta para transmissão UART
 * 
 * #pragma pack(push,1) garante que não há padding entre campos,
 * permitindo envio direto via Serial.write()
 */
#pragma pack(push, 1)
struct SensorsPayload {
    IMURaw          imu;
    BaroRaw         baro;
    BatteryRaw      battery;
    int32_t         thermocouples[THERMOCOUPLE_COUNT];  /* Apenas rawValue */
};
#pragma pack(pop)

/* =================================================================================
 * CLASSE BASE SENSOR DRIVER (EXTENSIBILIDADE)
 * =================================================================================
 * 
 * Para adicionar um novo sensor, crie uma classe que herde de SensorDriver
 * e implemente begin(), update() e name().
 */

class SensorDriver {
public:
    virtual ~SensorDriver() {}
    
    /**
     * Inicializa o sensor (configuração de hardware)
     * @return true se inicializado com sucesso
     */
    virtual bool begin() = 0;
    
    /**
     * Atualiza os dados do sensor (chamado a cada ciclo)
     */
    virtual void update() = 0;
    
    /**
     * Retorna o nome do driver (para debug)
     */
    virtual const char* name() const = 0;
};

/* =================================================================================
 * CLASSE PRINCIPAL SENSORS
 * =================================================================================
 */

class Sensors {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    /**
     * Construtor — inicializa todas as estruturas com valores seguros
     */
    Sensors();
    
    /**
     * Inicializa todos os sensores internos e drivers registados
     * 
     * Deve ser chamada uma vez no setup().
     */
    void begin();
    
    /**
     * Atualiza todos os sensores (chamar a cada ciclo, ~100Hz)
     * 
     * Lê IMU, barómetro, termopares, bateria e drivers externos.
     */
    void update();

    /* =========================================================================
     * SISTEMA DE DRIVERS (EXTENSIBILIDADE)
     * ========================================================================= */
    
    /**
     * Regista um driver de sensor externo
     * 
     * @param driver Ponteiro para o driver (o Sensors torna-se proprietário)
     * @return true se registado com sucesso
     */
    bool addDriver(SensorDriver* driver);
    
    /**
     * Retorna o número de drivers registados
     */
    uint8_t getDriverCount() const;

    /* =========================================================================
     * GETTERS — DADOS INDIVIDUAIS
     * ========================================================================= */
    
    /**
     * Retorna os dados crus da IMU
     */
    const IMURaw& getIMU() const;
    
    /**
     * Retorna os dados crus do barómetro
     */
    const BaroRaw& getBaro() const;
    
    /**
     * Retorna os dados crus de um termopar específico
     * 
     * @param idx Índice do termopar (0 a THERMOCOUPLE_COUNT-1)
     */
    const ThermocoupleRaw& getThermocouple(uint8_t idx) const;
    
    /**
     * Retorna os dados crus da bateria
     */
    const BatteryRaw& getBattery() const;
    
    /**
     * Retorna todos os dados crus agregados
     */
    SensorsRaw getAllRaw() const;
    
    /**
     * Retorna o payload compacto para envio via UART
     * 
     * Utilizado pelo ESP1 para enviar dados ao ESP2.
     * A estrutura é compacta (#pragma pack) para transmissão eficiente.
     */
    SensorsPayload getPayload() const;

    /* =========================================================================
     * ESTADO E DIAGNÓSTICO
     * ========================================================================= */
    
    /**
     * Verifica se os sensores críticos (IMU + Barómetro) estão válidos
     * 
     * @return true se IMU e barómetro têm dados válidos
     */
    bool areCriticalValid() const;
    
    /**
     * Retorna o timestamp da última atualização
     */
    uint32_t getLastUpdateTime() const;
    
    /**
     * Ativa/desativa modo debug
     * 
     * @param enable true = ativar mensagens de diagnóstico
     */
    void setDebug(bool enable);

private:
    /* =========================================================================
     * DADOS DOS SENSORES INTERNOS
     * ========================================================================= */
    
    IMURaw          _imu;           /* Dados da IMU */
    BaroRaw         _baro;          /* Dados do barómetro */
    BatteryRaw      _battery;       /* Dados da bateria */
    ThermocoupleRaw _thermocouples[THERMOCOUPLE_COUNT]; /* Termopares */
    
    /* =========================================================================
     * DRIVERS EXTERNOS
     * ========================================================================= */
    
    SensorDriver*   _drivers[MAX_DRIVERS];  /* Array de drivers registados */
    uint8_t         _driverCount;           /* Número de drivers ativos */
    
    /* =========================================================================
     * ESTADO DO SISTEMA
     * ========================================================================= */
    
    uint32_t        _lastUpdateTime;   /* Timestamp da última atualização (ms) */
    bool            _debugEnabled;     /* Modo debug ativo? */
    bool            _i2cInitialized;   /* I2C já foi inicializado? */
    
    /* =========================================================================
     * MÉTODOS PRIVADOS DE LEITURA (SENSORES INTERNOS)
     * ========================================================================= */
    
    /**
     * Lê a IMU via I2C (MPU6050)
     * 
     * Configuração: Acel ±2g, Giro ±250°/s
     */
    void _readIMU();
    
    /**
     * Lê o barómetro via I2C (BMP280)
     * 
     * Configuração: Modo normal, oversampling ×16
     */
    void _readBaro();
    
    /**
     * Lê os termopares via SPI (MAX31855)
     * 
     * 4 termopares, cada um com pino CS dedicado
     */
    void _readThermocouples();
    
    /**
     * Lê a bateria via ADC com média móvel
     * 
     * 8 amostras para reduzir ruído
     */
    void _readBattery();
    
    /* =========================================================================
     * MÉTODOS AUXILIARES
     * ========================================================================= */
    
    /**
     * Inicializa o barramento I2C
     */
    void _initI2C();
    
    /**
     * Verifica se um dispositivo I2C responde no endereço especificado
     * 
     * @param addr Endereço I2C (7 bits)
     * @return true se o dispositivo respondeu
     */
    bool _i2cProbe(uint8_t addr);
};

#endif /* SENSORS_H */
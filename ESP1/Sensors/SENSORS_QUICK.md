# Guia Rápido – Sensors BeaconFly

> **Versão:** 2.0.0  
> **Uso:** Referência rápida para debug, manutenção e desenvolvimento

---

## 1. Sensores e Interfaces

| Sensor                | Interface | Pinos/Endereço                    |
| ----------------------|-----------|-----------------------------------|
| IMU (MPU6050)         | I2C       | 0x68                              |
| Barómetro (BMP280)    | I2C       | 0x76 ou 0x77                      |
| Termopares (MAX31855) | SPI       | CS:15,16,17,18; MISO:19; SCK:23   |
| Bateria               | ADC       | V:34, A:35                        |

---

## 2. Estruturas Rápidas

```cpp
struct IMURaw {
    int16_t accelX, accelY, accelZ;  // counts (±2g → 16384 LSB/g)
    int16_t gyroX, gyroY, gyroZ;     // counts (±250°/s → 131 LSB/°/s)
    int16_t temperature;             // counts (340 LSB/°C)
    bool valid;
    uint32_t timestamp;
};

struct BaroRaw {
    int32_t pressure;    // counts
    int32_t temperature; // counts
    bool valid;
    uint32_t timestamp;
};

struct ThermocoupleRaw {
    int32_t rawValue;    // counts (0.25°C/LSB)
    uint8_t fault;       // 0 = OK
    bool valid;
    uint32_t timestamp;
};

struct BatteryRaw {
    uint16_t adcVoltage; // 0-4095
    uint16_t adcCurrent; // 0-4095
    bool valid;
    uint32_t timestamp;
};
´´´

3. Inicialização

```cpp
Sensors sensors;

void setup() {
    sensors.begin();
    sensors.setDebug(true);  // Opcional
}
´´´

4. Loop Principal (100Hz)

```cpp
void loop() {
    sensors.update();
    
    if (sensors.areCriticalValid()) {
        SensorsPayload payload = sensors.getPayload();
        Serial2.write((uint8_t*)&payload, sizeof(payload));
    }
    
    delay(10);
}
´´´

5. Acesso a Dados

```cpp
// IMU
const IMURaw& imu = sensors.getIMU();
int16_t ax = imu.accelX;
int16_t gz = imu.gyroZ;

// Barómetro
const BaroRaw& baro = sensors.getBaro();
int32_t pressure = baro.pressure;

// Termopar 0 (motor 1)
const ThermocoupleRaw& tc0 = sensors.getThermocouple(0);
float tempC = tc0.rawValue * 0.25f;

// Bateria
const BatteryRaw& batt = sensors.getBattery();
float voltageADC = batt.adcVoltage * 3.3f / 4095.0f;
´´´

6. Payload UART (para ESP2)

```cpp
// Tamanho: ~57 bytes
SensorsPayload payload = sensors.getPayload();
Serial2.write((uint8_t*)&payload, sizeof(payload));
´´´
7. Conversão RAW → SI (ESP2)

Sensor                  Fórmula
Aceleração (g)          accel_g = raw / 16384.0f
Aceleração (m/s²)       accel_ms2 = accel_g * 9.80665f
Giro (°/s)              gyro_dps = raw / 131.0f
Temperatura IMU (°C)    temp_c = (raw / 340.0f) + 36.53f
Temperatura Baro (°C)   temp_c = raw / 512.0f (aprox.)
Termopar (°C)           temp_c = raw * 0.25f
Bateria (V)             volt_v = adc / 4095.0f * 3.3f * divisor

8. Constantes

Constante           Valor   Descrição
I2C_SDA             21      I2C Data
I2C_SCL             22      I2C Clock
BATT_PIN_VOLTAGE    34      ADC tensão
BATT_PIN_CURRENT    35      ADC corrente
THERMOCOUPLE_COUNT  4       Número de termopares
MAX_DRIVERS         8       Máx. drivers externos
ADC_SAMPLES         8       Média móvel bateria

9. Debug

```cpp
sensors.setDebug(true);
´´´

Mensagens típicas:

[Sensors] _readIMU() — Accel(164,23,16384) Gyro(12,-5,3)
[Sensors] _readBaro() — PressRaw=101325, TempRaw=2500
[Sensors] _readThermocouples() — TC0=2500, TC1=2512
[Sensors] _readBattery() — V_ADC=2048, A_ADC=512

10. Verificação Rápida

```cpp
void testSensors() {
    Sensors sensors;
    sensors.begin();
    sensors.setDebug(true);
    
    for (int i = 0; i < 100; i++) {
        sensors.update();
        delay(10);
        
        if (sensors.areCriticalValid()) {
            Serial.println("✅ IMU e Barómetro OK");
        } else {
            Serial.println("❌ Sensores críticos inválidos");
        }
        
        const ThermocoupleRaw& tc0 = sensors.getThermocouple(0);
        if (tc0.valid) {
            Serial.printf("TC0: %.2f°C\n", tc0.rawValue * 0.25f);
        } else {
            Serial.printf("TC0 falha: %d\n", tc0.fault);
        }
    }
}
´´´

11. Erros Comuns

Erro                    Causa                   Solução
MPU6050 não detectado   I2C falhou              Verificar fiação SDA/SCL
BMP280 não detectado    Endereço errado         Tentar 0x76 e 0x77
TC falha: 0x0001        Termopar curto VCC      Verificar termopar
TC falha: 0x0002        Termopar curto GND      Verificar termopar
TC falha: 0x0004        Termopar desconectado   Verificar conector

12. Driver Personalizado (Exemplo)

```cpp
class MeuSensor : public SensorDriver {
    bool begin() override { /* inicializar */ return true; }
    void update() override { /* ler sensor */ }
    const char* name() const override { return "MeuSensor"; }
};

sensors.addDriver(new MeuSensor());
´´´

Fim do Guia Rápido – Sensors BeaconFly v2.0.0

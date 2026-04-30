# Sensors – Pasta `Sensors/`

> **Versão:** 2.0.0  
> **Data:** 2026-04-17  
> **Autor:** BeaconFly UAS Team

Esta documentação detalha o **módulo de aquisição de sensores** do ESP1. Abrange os 2 arquivos da pasta: `Sensors.h` e `Sensors.cpp`, explicando a arquitetura, sensores suportados, sistema de drivers e integração com o ESP2.

---

## 1. Objetivo do Módulo

O módulo `Sensors` é responsável por **adquirir dados CRUS (raw)** de todos os sensores ligados ao ESP1.

**Princípio fundamental — Separação de responsabilidades:**

| Componente         | Responsabilidade                      |
| -------------------|---------------------------------------|
| **Sensors (ESP1)** | Adquire dados RAW (counts, ADC, etc.) |
| **ESP2**           | Converte RAW → SI (unidades físicas)  |
| **FlightControl**  | Usa dados SI para controlo de voo     |

**Porquê esta separação?**

| Razão                 | Explicação                                        |
| ----------------------|---------------------------------------------------|
| **Performance**       | ESP1 foca em controlo de voo (tempo real a 100Hz) |
| **Flexibilidade**     | Mudar sensores não requer alterar lógica de voo   |
| **Manutenibilidade**  | Calibrações e conversões centralizadas no ESP2    |
| **Portabilidade**     | O mesmo ESP1 pode usar diferentes sensores        |

---

## 2. Arquitetura do Módulo

### 2.1 Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ SENSORES (ESP1)                                                                 │
│ │                                                                               │
│ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐                 │
│ │ IMU         │ │ Barómetro   │ │ Termopares  │ │ Bateria     │                 │
│ │ (MPU6050)   │ │ (BMP280)    │ │ (MAX31855)  │ │ (ADC)       │                 │
│ └──────┬──────┘ └──────┬──────┘ └──────┬──────┘ └──────┬──────┘                 │
│        │               │               │               │                        │
│        ▼               ▼               ▼               ▼                        │
│ ┌─────────────────────────────────────────────────────────────────────────────┐ │
│ │ Sensors::update()                                                           │ │
│ │ •_readIMU() → IMURaw (accel, gyro, temp)                                   │ │
│ │ •_readBaro() → BaroRaw (pressure, temp)                                    │ │
│ │ •_readThermocouples() → ThermocoupleRaw[4] (temp, fault)                   │ │
│ │ •_readBattery() → BatteryRaw (adcVoltage, adcCurrent)                      │ │
│ └─────────────────────────────────────────────────────────────────────────────┘ │
│ │                                                                               │
│ ▼                                                                               │
│ ┌─────────────────────────────────────────────────────────────────────────────┐ │
│ │ Sensors::getPayload()                                                       │ │
│ │ Estrutura compacta (#pragma pack) para transmissão UART                     │ │
│ └─────────────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────────┘
│
▼ (UART — dados RAW)
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP2                                                                            │
│ │                                                                               │
│ ┌─────────────────────────────────────────────────────────────────────────────┐ │
│ │ Conversão RAW → SI                                                          │ │
│ │ • Aceleração: counts → m/s²                                                 │ │
│ │ • Giro: counts → °/s                                                        │ │
│ │ • Pressão: counts → Pa                                                      │ │
│ │ • Temperatura: counts → °C                                                  │ │
│ │ • Bateria: ADC → V, A                                                       │ │
│ └─────────────────────────────────────────────────────────────────────────────┘ │
│ │                                                                               │
│ ▼                                                                               │
│ ┌─────────────────────────────────────────────────────────────────────────────┐ │
│ │ FlightControl (SI)                                                          │ │
│ └─────────────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────────┘

### 2.2 Estruturas de Dados

```cpp
// Dados crus da IMU (16 bits cada)
struct IMURaw {
    int16_t accelX, accelY, accelZ;  // Aceleração (counts)
    int16_t gyroX, gyroY, gyroZ;      // Velocidade angular (counts)
    int16_t temperature;              // Temperatura (counts)
    bool    valid;                    // Dados válidos?
    uint32_t timestamp;               // Timestamp (ms)
};

// Dados crus do barómetro
struct BaroRaw {
    int32_t pressure;      // Pressão (counts)
    int32_t temperature;   // Temperatura (counts)
    bool    valid;
    uint32_t timestamp;
};

// Dados crus de termopar
struct ThermocoupleRaw {
    int32_t rawValue;      // Temperatura (counts, 0.25°C/LSB)
    uint8_t fault;         // Código de falha (0 = OK)
    bool    valid;
    uint32_t timestamp;
};

// Dados crus da bateria
struct BatteryRaw {
    uint16_t adcVoltage;   // ADC da tensão (0-4095)
    uint16_t adcCurrent;   // ADC da corrente (0-4095)
    bool    valid;
    uint32_t timestamp;
};
´´´

2.3 Payload Compacto (UART)

```cpp
#pragma pack(push, 1)
struct SensorsPayload {
    IMURaw          imu;              // 6×2 + 2 + 1 + 4 = ~19 bytes
    BaroRaw         baro;             // 2×4 + 1 + 4 = ~13 bytes
    BatteryRaw      battery;          // 2×2 + 1 + 4 = ~9 bytes
    int32_t         thermocouples[4]; // 4×4 = 16 bytes
};  // Total: ~57 bytes
#pragma pack(pop)
Porquê #pragma pack(push, 1)?
Garante que não há padding entre campos, permitindo enviar a estrutura diretamente via Serial.write() sem necessidade de serialização adicional.
´´´

3. Sensores Suportados

3.1 IMU – MPU6050

Parâmetro               Valor
Interface               I2C (endereço 0x68)
Aceleração              Range ±2g → 16,384 LSB/g
Giroscópio              Range ±250°/s → 131 LSB/°/s
Temperatura             -40 a +85°C, 340 LSB/°C
Taxa de atualização     1 kHz (filtrado internamente)

Registradores principais:

Registador              Endereço    Descrição
MPU6050_ACCEL_XOUT_H    0x3B        Aceleração X (MSB)
MPU6050_GYRO_XOUT_H     0x43        Giroscópio X (MSB)
MPU6050_TEMP_OUT_H      0x41        Temperatura (MSB)
MPU6050_PWR_MGMT_1      0x6B        Gestão de energia

3.2 Barómetro – BMP280

Parâmetro       Valor
Interface       I2C (0x76 ou 0x77)
Pressão         300-1100 hPa, ±1 hPa
Temperatura     -40 a +85°C, ±1°C
Oversampling    ×16 (alta resolução)
Filtro          ×16 (suavização)

Configuração recomendada:

Registador  Valor   Descrição
ctrl_meas   0x57    Temp ×16, Press ×16, modo normal
config      0xD0    Filtro ×16, tempo standby 500ms

3.3 Termopares – MAX31855

Parâmetro   Valor
Interface   SPI
Resolução   0.25°C por LSB
Range       -200 a +1350°C (tipo K)
Canais      4 (um por ESC/motor)

Pinos SPI:

Pino    GPIO    Função
TC_CS1  15      Chip Select Termopar 1
TC_CS2  16      Chip Select Termopar 2
TC_CS3  17      Chip Select Termopar 3
TC_CS4  18      Chip Select Termopar 4
TC_MISO 19      Dados (Master In Slave Out)
TC_SCK  23      Clock SPI

Códigos de falha do MAX31855:

Bit     Significado
0       Termopar em curto com VCC
1       Termopar em curto com GND
2       Termopar desconectado
3       Erro de referência interna

3.4 Bateria – ADC

Parâmetro       Valor
Pino tensão     GPIO34 (ADC1_CH6)
Pino corrente   GPIO35 (ADC1_CH7)
Resolução       12 bits (0-4095)
Referência      3.3V
Amostras        8 (média móvel)

Circuito recomendado:

Bateria (2S-6S) → Divisor resistivo → GPIO34
                         |
                         +-- 10kΩ
                         |
                        GND

Shunt de corrente → Amplificador → GPIO35

4. Sistema de Drivers (Extensibilidade)

4.1 Classe Base SensorDriver

```cpp
class SensorDriver {
public:
    virtual ~SensorDriver() {}
    virtual bool begin() = 0;           // Inicialização
    virtual void update() = 0;          // Leitura periódica
    virtual const char* name() const = 0; // Nome (debug)
};
´´´

4.2 Exemplo de Driver Personalizado

```cpp
// Exemplo: Sensor de distância Lidar
class LidarDriver : public SensorDriver {
private:
    float _distance;
    bool _valid;
    
public:
    bool begin() override {
        Serial2.begin(115200);
        return true;
    }
    
    void update() override {
        if (Serial2.available() >= 9) {
            // Formato: 0x59 0x59 0x00 0x00 0xXX 0xXX 0xXX 0xXX 0xXX
            _distance = readLidar();
            _valid = true;
        }
    }
    
    const char* name() const override {
        return "Lidar";
    }
    
    float getDistance() const { return _distance; }
    bool isValid() const { return _valid; }
};

// Registo
Sensors sensors;
sensors.addDriver(new LidarDriver());
´´´
4.3 Limitações

Constante       Valor   Descrição
MAX_DRIVERS     8       Número máximo de drivers externos

5. API Pública

5.1 Inicialização

```cpp
Sensors sensors;

void setup() {
    sensors.begin();
    sensors.setDebug(true);
}
´´´

5.2 Ciclo Principal (100Hz)

```cpp
void loop() {
    sensors.update();           // Ler todos os sensores
    
    if (sensors.areCriticalValid()) {
        SensorsPayload payload = sensors.getPayload();
        Serial2.write((uint8_t*)&payload, sizeof(payload));  // Enviar para ESP2
    }
    
    delay(10);  // 100Hz
}
´´´

5.3 Acesso a Dados Individuais

```cpp
// IMU
const IMURaw& imu = sensors.getIMU();
if (imu.valid) {
    Serial.printf("Accel: X=%d, Y=%d, Z=%d\n", imu.accelX, imu.accelY, imu.accelZ);
}

// Barómetro
const BaroRaw& baro = sensors.getBaro();
if (baro.valid) {
    Serial.printf("Pressure: %ld counts\n", baro.pressure);
}

// Termopar 0 (motor 1)
const ThermocoupleRaw& tc0 = sensors.getThermocouple(0);
if (tc0.valid) {
    Serial.printf("TC0: %.2f°C\n", tc0.rawValue * 0.25f);
}

// Bateria
const BatteryRaw& batt = sensors.getBattery();
if (batt.valid) {
    float voltage = batt.adcVoltage * 3.3f / 4095.0f;  // Raw → Volts (após divisor)
    Serial.printf("Battery: %.2fV\n", voltage);
}
´´´

5.4 Payload Compacto (UART)

```cpp
// Envio para ESP2
SensorsPayload payload = sensors.getPayload();
Serial2.write((uint8_t*)&payload, sizeof(payload));

// Receção no ESP2 (exemplo)
SensorsPayload received;
if (Serial2.available() >= sizeof(SensorsPayload)) {
    Serial2.readBytes((uint8_t*)&received, sizeof(SensorsPayload));
    // Converter RAW → SI...
}
´´´

6. Constantes e Configuração

6.1 Pinos (Ajustar conforme PCB)

Constante           Valor   Descrição
I2C_SDA             21      I2C Data
I2C_SCL             22      I2C Clock
BATT_PIN_VOLTAGE    34      ADC tensão
BATT_PIN_CURRENT    35      ADC corrente
TC_CS1_PIN          15      Termopar 1 CS
TC_CS2_PIN          16      Termopar 2 CS
TC_CS3_PIN          17      Termopar 3 CS
TC_CS4_PIN          18      Termopar 4 CS
TC_MISO_PIN         19      SPI MISO
TC_SCK_PIN          23      SPI SCK

6.2 Parâmetros de Leitura

Constante           Valor       Descrição
I2C_FREQUENCY_HZ    400000      Frequência I2C (400 kHz)
I2C_TIMEOUT_MS      10          Timeout I2C (ms)
ADC_SAMPLES         8           Amostras para média móvel
SENSOR_RETRIES      3           Tentativas antes de marcar inválido

7. Debug e Diagnóstico

7.1 Ativação

```cpp
sensors.setDebug(true);
´´´

7.2 Mensagens Típicas

[Sensors] begin() — Inicializando sensores...
[Sensors] _initI2C() — I2C inicializado: SDA=21, SCL=22, freq=400 kHz
[Sensors] _readIMU() — MPU6050 configurado
[Sensors] _readIMU() — Accel(164,23,16384) Gyro(12,-5,3) Temp=2500
[Sensors] _readBaro() — BMP280 detectado no endereço 0x76
[Sensors] _readBaro() — BMP280 configurado
[Sensors] _readThermocouples() — TC0=2500, TC1=2512, TC2=2498, TC3=2505
[Sensors] _readBattery() — V_ADC=2048 (1.65V), A_ADC=512
[Sensors] begin() — Concluído. 0 drivers externos registados.

7.3 Erros Comuns

Mensagem                            Causa                       Solução
MPU6050 não detectado               IMU não conectada           Verificar fiação I2C
BMP280 não detectado                Barómetro ausente           Verificar endereço (0x76 ou 0x77)
TC%d falha: 0x%04X                  Termopar desconectado       Verificar ligação do termopar
Driver X falhou na inicialização    Driver externo com erro     Verificar driver específico

8. Performance

Métrica                             Valor
Tempo update() (todos sensores)     ~5-10 ms
Tamanho do payload UART             ~57 bytes
Taxa de dados UART (100Hz)          ~5.7 KB/s
Memória RAM (estruturas)            ~200 bytes
Memória Flash (código)              ~8 KB

9. Exemplo Completo

```cpp
#include "Sensors.h"

Sensors sensors;

void setup() {
    Serial.begin(115200);
    Serial2.begin(921600);  // UART para ESP2 (alta velocidade)
    
    sensors.begin();
    sensors.setDebug(true);
    
    Serial.println("Sistema pronto");
}

void loop() {
    static uint32_t lastSend = 0;
    uint32_t now = millis();
    
    // Atualizar sensores a 100Hz
    sensors.update();
    
    // Enviar para ESP2 a cada 10ms (100Hz)
    if (now - lastSend >= 10) {
        if (sensors.areCriticalValid()) {
            SensorsPayload payload = sensors.getPayload();
            Serial2.write((uint8_t*)&payload, sizeof(payload));
        }
        lastSend = now;
    }
    
    // Debug: imprimir a cada 1 segundo
    static uint32_t lastPrint = 0;
    if (now - lastPrint >= 1000) {
        const IMURaw& imu = sensors.getIMU();
        const BaroRaw& baro = sensors.getBaro();
        const BatteryRaw& batt = sensors.getBattery();
        
        Serial.printf("IMU: A(%d,%d,%d) G(%d,%d,%d)\n",
                      imu.accelX, imu.accelY, imu.accelZ,
                      imu.gyroX, imu.gyroY, imu.gyroZ);
        Serial.printf("Baro: P=%ld, T=%ld\n", baro.pressure, baro.temperature);
        Serial.printf("Batt: V_ADC=%d, A_ADC=%d\n", batt.adcVoltage, batt.adcCurrent);
        
        lastPrint = now;
    }
}
´´´

10. Conversão RAW → SI (ESP2)

No ESP2, os dados crus devem ser convertidos para unidades SI:

Sensor              Raw    → SI         Fórmula
Aceleração          counts → m/s²       accel_ms2 = accel_raw / 16384.0f * 9.80665f
Giroscópio          counts → °/s        gyro_dps = gyro_raw / 131.0f
Temperatura IMU     counts → °C         temp_c = (temp_raw / 340.0f) + 36.53f
Pressão             counts → Pa         pressure_pa = (press_raw / 4096.0f) * 100.0f (aproximado)
Temperatura Baro    counts → °C         temp_c = temp_raw / 512.0f (aproximado)
Termopar            counts → °C         temp_c = rawValue * 0.25f
Bateria tensão      ADC    → V             voltage_v = adc / 4095.0f * 3.3f * divisor_factor

Fim da documentação – Sensors BeaconFly v2.0.0

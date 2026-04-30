# SIConverter – Pasta `SIConverter/`

> **Versão:** 1.0.0
> **Data:** 2026-04-27
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O módulo `SIConverter` é responsável por **converter os dados RAW recebidos do ESP1** (através da UART) em **unidades do Sistema Internacional (SI)**.

Os dados convertidos são disponibilizados para:

| Destino                  | Finalidade                                             |
| -------------------------|--------------------------------------------------------|
| **ESP1 (via UART)**      | Controlo de voo (FlightControl precisa de valores SI)  |
| **Telemetria (para GS)** | Dados de voo em tempo real                             |
| **Failsafe**             | Análise de condições críticas (ângulos, bateria, etc.) |

---

## 2. Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP1                                                                            │
│                                                                                 │
│ Sensors (RAW) → UART (TX) →                                                     │
│ • Acelerómetro (counts)                                                         │
│ • Giroscópio (counts)                                                           │
│ • Temperatura IMU (counts)                                                      │
│ • Pressão (counts)                                                              │
│ • Temperatura Baro (counts)                                                     │
│ • Bateria (ADC 0-4095)                                                          │
│ • Termopares (counts)                                                           │
└─────────────────────────────────────────────────────────────────────────────────┘
│
▼ (UART)
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP2                                                                            │
│                                                                                 │
│ ┌─────────────────────────────────────────────────────────────────────────┐     │
│ │ SIConverter::update()                                                   │     │
│ │                                                                         │     │
│ │ RAW → SI Conversões:                                                    │     │
│ │                                                                         │     │
│ │ • Aceleração: counts → m/s²                                             │     │
│ │ • Giro: counts → °/s                                                    │     │
│ │ • Temperatura IMU: counts → °C                                          │     │
│ │ • Pressão: counts → Pa                                                  │     │
│ │ • Temperatura Baro: counts → °C                                         │     │
│ │ • Tensão: ADC → V                                                       │     │
│ │ • Corrente: ADC → A                                                     │     │
│ │ • Termopares: counts → °C                                               │     │
│ └─────────────────────────────────────────────────────────────────────────┘     │
│                       │                                                         │
│ ┌─────────────────────┼────────────────┐                                        │
│ │                     │                │                                        │
│ ▼                     ▼                ▼                                        │
│ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐                                 │
│ │ ESP1 (UART) │ │ Telemetry   │ │ Failsafe    │                                 │
│ │ (SI data)   │ │ (para GS)   │ │ (análise)   │                                 │
│ └─────────────┘ └─────────────┘ └─────────────┘                                 │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘

---

## 3. Conversões Realizadas

| Sensor | RAW (do ESP1) | SI (saída) | Fórmula | Observações |
|--------|---------------|------------|---------|-------------|
| **Acelerómetro X** | int16_t (counts) | float (m/s²) | `raw / LSB_per_G * 9.80665` | Range ±2g padrão |
| **Acelerómetro Y** | int16_t (counts) | float (m/s²) | `raw / LSB_per_G * 9.80665` | |
| **Acelerómetro Z** | int16_t (counts) | float (m/s²) | `raw / LSB_per_G * 9.80665` | |
| **Giroscópio X** | int16_t (counts) | float (°/s) | `raw / LSB_per_DPS` | Range ±250°/s padrão |
| **Giroscópio Y** | int16_t (counts) | float (°/s) | `raw / LSB_per_DPS` | |
| **Giroscópio Z** | int16_t (counts) | float (°/s) | `raw / LSB_per_DPS` | |
| **Temperatura IMU** | int16_t (counts) | float (°C) | `raw / 340.0 + 36.53` | MPU6050 específico |
| **Pressão** | int32_t (counts) | float (Pa) | `raw * 101325 / 5300000` | Simplificada |
| **Temperatura Baro** | int32_t (counts) | float (°C) | `(raw - 250000) / 10000` | Simplificada |
| **Tensão Bateria** | uint16_t (ADC) | float (V) | `(adc / 4095) * 3.3 * divisor` | Divisor configurável |
| **Corrente Bateria** | uint16_t (ADC) | float (A) | `(adc / 4095) * 3.3 / (shunt * ganho)` | Shunt + amp configurável |
| **Termopar** | int32_t (counts) | float (°C) | `raw * 0.25` | MAX31855 específico |

---

## 4. Estrutura de Dados SI (`SIData`)

```cpp
struct SIData {
    // IMU — Aceleração (m/s²)
    float accelX_ms2;
    float accelY_ms2;
    float accelZ_ms2;
    
    // IMU — Velocidade angular (°/s)
    float gyroX_dps;
    float gyroY_dps;
    float gyroZ_dps;
    
    // IMU — Temperatura (°C)
    float imuTemp_c;
    
    // Barómetro — Pressão (Pa)
    float pressure_pa;
    
    // Barómetro — Temperatura (°C)
    float baroTemp_c;
    
    // Bateria — Tensão (V)
    float batteryVoltage_v;
    
    // Bateria — Corrente (A)
    float batteryCurrent_a;
    
    // Bateria — Potência (W)
    float batteryPower_w;
    
    // Bateria — Carga consumida (Ah)
    float batteryCharge_ah;
    
    // Termopares — Temperatura (°C)
    float thermocoupleTemp_c[4];
    
    // Timestamp da aquisição (ms)
    uint32_t timestamp;
    
    // Flags de validade
    bool imuValid;
    bool baroValid;
    bool batteryValid;
    bool thermocoupleValid[4];
};
´´´

5. Constantes de Calibração (Ajustáveis)

Constante               Valor Padrão    Descrição
ACCEL_LSB_PER_G         16384.0         LSB por G (para MPU6050 ±2g)
GYRO_LSB_PER_DPS        131.0           LSB por °/s (para MPU6050 ±250°/s)
IMU_TEMP_LSB_PER_C      340.0           LSB por °C (MPU6050)
IMU_TEMP_OFFSET         36.53           Offset de temperatura (°C)
ADC_MAX_VALUE           4095.0          Resolução ADC (12 bits)
ADC_VOLTAGE_REF         3.3             Tensão de referência (V)
VOLTAGE_DIVIDER_FACTOR  2.0             Fator do divisor resistivo
CURRENT_SHUNT_OHMS      0.001           Resistência do shunt (Ω)
CURRENT_AMP_GAIN        50.0            Ganho do amplificador
THERMOCOUPLE_LSB_PER_C  0.25            LSB por °C (MAX31855)

6. Calibração para Hardware Específico

6.1 Divisor de Tensão da Bateria

Se usar um divisor resistivo 10kΩ + 10kΩ:

Vout = Vin * (10k / (10k + 10k)) = Vin / 2
Fator = 2.0

6.2 Medição de Corrente

Exemplo com shunt de 1mΩ (0.001Ω) e amplificador de ganho 50:

V_shunt = I * 0.001
V_adc = V_shunt * 50
I = V_adc / (0.001 * 50) = V_adc / 0.05

6.3 Range do Acelerómetro

Range (G)       LSB por G       Fórmula
±2              16384           raw / 16384 * 9.80665
±4              8192            raw / 8192 * 9.80665
±8              4096            raw / 4096 * 9.80665
±16             2048            raw / 2048 * 9.80665

6.4 Range do Giroscópio

Range (°/s)     LSB por °/s     Fórmula
±250            131             raw / 131
±500            65.5            raw / 65.5
±1000           32.75           raw / 32.75
±2000           16.375          raw / 16.375

7. API Pública

7.1 Inicialização

```cpp
#include "SIConverter.h"

SIConverter siConverter;

void setup() {
    siConverter.begin();
    siConverter.setDebug(true);
}
´´´

7.2 Atualização com Dados RAW

```cpp
void loop() {
    // Receber dados RAW do ESP1 via UART
    SensorsPayload raw;
    if (Serial2.readBytes((uint8_t*)&raw, sizeof(raw)) == sizeof(raw)) {
        siConverter.update(raw);
    }
}
´´´

7.3 Acesso a Dados Individuais

```cpp
// Aceleração
float ax = siConverter.getAccelX();  // m/s²
float ay = siConverter.getAccelY();
float az = siConverter.getAccelZ();

// Giroscópio
float gx = siConverter.getGyroX();   // °/s
float gy = siConverter.getGyroY();
float gz = siConverter.getGyroZ();

// Temperaturas
float imuTemp = siConverter.getIMUTemperature();  // °C
float baroTemp = siConverter.getBaroTemperature(); // °C

// Bateria
float voltage = siConverter.getBatteryVoltage();  // V
float current = siConverter.getBatteryCurrent();  // A
float power = siConverter.getBatteryPower();      // W
float charge = siConverter.getBatteryCharge();    // Ah

// Termopares
float tc0 = siConverter.getThermocoupleTemp(0);   // °C
float tc1 = siConverter.getThermocoupleTemp(1);
float tc2 = siConverter.getThermocoupleTemp(2);
float tc3 = siConverter.getThermocoupleTemp(3);
´´´

7.4 Acesso a Todos os Dados de uma Vez

```cpp
const SIData& data = siConverter.getData();

Serial.printf("Accel: %.2f, %.2f, %.2f m/s²\n", 
              data.accelX_ms2, data.accelY_ms2, data.accelZ_ms2);
Serial.printf("Bateria: %.2f V, %.2f A\n", 
              data.batteryVoltage_v, data.batteryCurrent_a);
´´´

7.5 Verificação de Validade

```cpp
if (siConverter.isIMUValid()) {
    // Dados IMU confiáveis
}

if (siConverter.areCriticalValid()) {
    // IMU e Barómetro OK — pode voar
}
´´´

7.6 Calibração em Runtime (Ajuste de Hardware)

```cpp
// Se usar divisor 10k+10k = fator 2.0
siConverter.setVoltageDividerFactor(2.0);

// Se usar shunt 0.5mΩ e ganho 100
siConverter.setCurrentShuntOhms(0.0005);
siConverter.setCurrentAmpGain(100.0);

// Se mudar range do acelerómetro para ±4g
siConverter.setAccelRangeG(4.0);

// Se mudar range do giroscópio para ±500°/s
siConverter.setGyroRangeDPS(500.0);
´´´

7.7 Reset da Carga da Bateria

```cpp
// Resetar acumulador após carregar a bateria
siConverter.resetBatteryCharge();
´´´

8. Exemplo Completo de Integração

```cpp
#include "SIConverter.h"
#include "Telemetry.h"
#include "Failsafe.h"

SIConverter siConverter;
Telemetry telemetry;
Failsafe failsafe;

void setup() {
    Serial.begin(115200);
    Serial2.begin(921600);  // UART para ESP1
    
    siConverter.begin();
    siConverter.setDebug(true);
    
    telemetry.begin();
    failsafe.begin();
}

void loop() {
    // 1. Receber dados RAW do ESP1
    SensorsPayload raw;
    if (Serial2.readBytes((uint8_t*)&raw, sizeof(raw)) == sizeof(raw)) {
        
        // 2. Converter RAW → SI
        siConverter.update(raw);
        const SIData& si = siConverter.getData();
        
        // 3. Construir telemetria para GS
        telemetry.updateFromSI(si);
        
        // 4. Alimentar Failsafe
        failsafe.updateInputs(
            si.accelX_ms2, si.accelY_ms2, si.accelZ_ms2,
            si.gyroX_dps, si.gyroY_dps, si.gyroZ_dps,
            si.batteryVoltage_v, si.batteryCurrent_a,
            si.thermocoupleTemp_c[0]  // temperatura do motor
        );
        
        // 5. Enviar dados SI de volta para ESP1
        uint8_t siBuffer[256];
        size_t len = buildSIPacket(si, siBuffer);
        Serial2.write(siBuffer, len);
    }
    
    // 6. Processar telemetria e failsafe
    telemetry.process();
    failsafe.process();
}
´´´

9. Debug

9.1 Ativação

```cpp
siConverter.setDebug(true);
´´´

9.2 Mensagens Típicas

[SIConverter] begin() — Inicializando conversor RAW → SI
[SIConverter]   Parâmetros de calibração:
[SIConverter]     Acelerómetro range: 2.0 G
[SIConverter]     Giroscópio range: 250 °/s
[SIConverter]     Divisor tensão: 2.00
[SIConverter]     Shunt corrente: 0.0010 Ω
[SIConverter]     Ganho corrente: 50.0
[SIConverter] begin() — Conversor inicializado

[SIConverter] convert() — Raw → SI concluído
[SIConverter]   Accel: 0.12, -0.05, 9.81 m/s²
[SIConverter]   Gyro: 0.02, -0.01, 0.03 °/s
[SIConverter]   Bateria: 12.34 V, 0.12 A, 1.48 W

10. Performance

Métrica             Valor
Tempo de conversão  ~50 µs
RAM utilizada       ~200 bytes (estrutura SIData)
Flash utilizada     ~2 KB
Atualização máxima  Limitada pelo UART (921600 baud)

11. Exemplo de Dados Convertidos

Entrada (RAW do ESP1)

IMU: Accel(1640, -820, 16800), Gyro(25, -15, 40), Temp(8500)
Baro: Pressure(5300000), Temp(250000)
Battery: ADC_V(2000), ADC_A(200)
Thermocouples: [2500, 2512, 2498, 2505]

Saída (SI)

Accel: 0.98, -0.49, 10.05 m/s²
Gyro: 0.19, -0.11, 0.31 °/s
IMU Temp: 25.53 °C
Pressão: 101325 Pa
Baro Temp: 25.00 °C
Bateria: 12.34 V, 0.12 A, 1.48 W, 0.034 Ah
Termopares: 25.0, 25.1, 24.9, 25.1 °C

Fim da documentação – SIConverter v1.0.0

# Guia Rápido – SIConverter

> **Versão:** 1.0.0
> **Data:** 2026-04-27
> **Autor:** BeaconFly UAS Team

---

## 1. O que este módulo faz

Converte dados RAW do ESP1 para **unidades SI** (metros, graus, volts, amperes, etc.).

| De (RAW)                  | Para (SI) |
| --------------------------|-----------|
| Aceleração (counts)       | m/s²      |
| Giro (counts)             | °/s       |
| Temperatura IMU (counts)  | °C        |
| Pressão (counts)          | Pa        |
| Temperatura Baro (counts) | °C        |
| ADC (0-4095)              | V, A      |
| Termopar (counts)         | °C        |

---

## 2. Código Rápido

```cpp
#include "SIConverter.h"

SIConverter si;

void setup() {
    si.begin();
    si.setDebug(true);
}

void loop() {
    // Receber RAW do ESP1
    SensorsPayload raw;
    if (getRawFromESP1(raw)) {
        si.update(raw);
        
        // Aceder a dados SI
        float voltage = si.getBatteryVoltage();
        float current = si.getBatteryCurrent();
        float roll = si.getGyroX();  // se integrado
        
        // Enviar SI de volta para ESP1
        sendSIToESP1(si.getData());
    }
}
´´´

3. Getters Disponíveis

Método                  Retorno                 Unidade
getAccelX()             Aceleração X            m/s²
getAccelY()             Aceleração Y            m/s²
getAccelZ()             Aceleração Z            m/s²
getGyroX()              Velocidade angular X    °/s
getGyroY()              Velocidade angular Y    °/s
getGyroZ()              Velocidade angular Z    °/s
getIMUTemperature()     Temperatura IMU         °C
getPressure()           Pressão                 Pa
getBaroTemperature()    Temperatura Baro        °C
getBatteryVoltage()     Tensão                  V
getBatteryCurrent()     Corrente                A
getBatteryPower()       Potência                W
getBatteryCharge()      Carga consumida         Ah
getThermocoupleTemp(n)  Temperatura termopar    °C

4. Verificação de Validade

```cpp
if (si.isIMUValid()) { /* dados IMU confiáveis */ }
if (si.isBaroValid()) { /* dados baro confiáveis */ }
if (si.areCriticalValid()) { /* IMU + Baro OK — pode voar */ }
´´´

5. Calibração (Ajustar conforme Hardware)

```cpp
// Divisor de tensão (ex: 10k+10k = fator 2)
si.setVoltageDividerFactor(2.0);

// Shunt de corrente (ex: 1mΩ)
si.setCurrentShuntOhms(0.001);

// Ganho do amplificador (ex: 50x)
si.setCurrentAmpGain(50.0);

// Range do acelerómetro (ex: ±4g)
si.setAccelRangeG(4.0);

// Range do giroscópio (ex: ±500°/s)
si.setGyroRangeDPS(500.0);
´´´

6. Reset da Carga da Bateria

```cpp
// Após carregar a bateria
si.resetBatteryCharge();
´´´

7. Debug

```cpp
si.setDebug(true);
´´´

Mensagens:

[SIConverter] convert() — Raw → SI concluído
[SIConverter]   Accel: 0.12, -0.05, 9.81 m/s²
[SIConverter]   Bateria: 12.34 V, 0.12 A

8. Dados RAW (Entrada) — Estrutura

```cpp
struct SensorsPayload {
    IMURaw      imu;       // accelX, accelY, accelZ, gyroX, gyroY, gyroZ, temp
    BaroRaw     baro;      // pressure, temperature
    BatteryRaw  battery;   // adcVoltage, adcCurrent
    int32_t     thermocouples[4];
};
´´´

9. Dados SI (Saída) — Estrutura

```cpp
struct SIData {
    float accelX_ms2, accelY_ms2, accelZ_ms2;
    float gyroX_dps, gyroY_dps, gyroZ_dps;
    float imuTemp_c;
    float pressure_pa;
    float baroTemp_c;
    float batteryVoltage_v;
    float batteryCurrent_a;
    float batteryPower_w;
    float batteryCharge_ah;
    float thermocoupleTemp_c[4];
    uint32_t timestamp;
    bool imuValid, baroValid, batteryValid;
    bool thermocoupleValid[4];
};
´´´

10. Fórmulas de Conversão (Resumo)

Sensor          Fórmula
Aceleração      raw / 16384 * 9.80665
Giro            raw / 131
Temp IMU        raw / 340 + 36.53
Pressão         raw * 101325 / 5300000
Temp Baro       (raw - 250000) / 10000
Tensão          (adc / 4095) * 3.3 * divisor
Corrente        (adc / 4095) * 3.3 / (shunt * ganho)
Termopar        raw * 0.25

Fim do Guia Rápido – SIConverter v1.0.0

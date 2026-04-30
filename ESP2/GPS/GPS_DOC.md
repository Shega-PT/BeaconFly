# GPS – Pasta `GPS/` (ESP2)

> **Versão:** 1.0.0
> **Data:** 2026-04-29
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O módulo `GPS` é responsável por **processar dados de posicionamento global** de uma grande variedade de módulos GPS suportados, fornecendo uma interface unificada para acesso a dados de posição, velocidade, tempo e qualidade do sinal.

**Princípio fundamental:** Suportar qualquer hardware GPS que o utilizador possa acoplar, desde módulos baratos NMEA até módulos RTK de alta precisão.

---

## 2. Hardware Suportado

### 2.1 Protocolos de Comunicação

| Protocolo     | Módulos/Modelos                                           |
| --------------|-----------------------------------------------------------|
| **NMEA 0183** | Ublox NEO-6M, NEO-7M, NEO-8M, NEO-M8N, NEO-M9N, ZED-F9P   |
|               | Quectel L80, L86, L96                                     |
|               | SIMCom SIM28, SIM68, SIM808                               |
|               | MediaTek MT3333, MT3339                                   |
|               | Generic NMEA GPS modules                                  |
| **UBX**       | Ublox NEO-6M, NEO-7M, NEO-8M, NEO-M8N (protocolo binário) |
|               | Ublox NEO-M9N, ZED-F9P (RTK)                              |
| **SiRF**      | SiRFstarIII, SiRFstarIV                                   |
| **MTK**       | MediaTek MT3333, MT3339 (protocolo binário)               |

### 2.2 Sistemas de Satélites

| Sistema   | Região | Suporte         |
| ----------|--------|-----------------|
| GPS       | EUA    | ✅ Completo     |
| GLONASS   | Rússia | ✅ Configurável |
| Galileo   | Europa | ✅ Configurável |
| BeiDou    | China  | ✅ Configurável |
| QZSS      | Japão  | ✅ Detetado     |
| NavIC     | Índia  | ✅ Detetado     |

---

## 3. Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ MÓDULO GPS                                                                      │
│                                                                                 │
│ ┌─────────────┐   ┌─────────────┐   ┌─────────────┐                             │
│ │ NMEA        │   │ UBX         │   │ SiRF        │                             │
│ │ (texto)     │   │ (binário)   │   │ (binário)   │                             │
│ └──────┬──────┘   └──────┬──────┘   └──────┬──────┘                             │
│        │                 │                 │                                    │
│        └─────────────────┼─────────────────┘                                    │
│                          │                                                      │
│                          ▼ (UART)                                               │
└────────────────────────────┼────────────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP2                                                                            │
│                                                                                 │
│ ┌─────────────────────────────────────────────────────────────────────────┐     │
│ │ GPS::update()                                                           │     │
│ │                                                                         │     │
│ │ • Leitura da UART (Serial2)                                             │     │
│ │ • Deteção automática de protocolo                                       │     │
│ │ • Parsing de sentenças/pacotes                                          │     │
│ │ • Atualização da estrutura GPSData                                      │     │
│ └─────────────────────────────────────────────────────────────────────────┘     │
│ │                                                                               │
│ ▼                                                                               │
│ ┌─────────────────────────────────────┐                                         │
│ │ GPSData (estrutura)                 │                                         │
│ │ • Posição (lat, lon, alt)           │                                         │
│ │ • Velocidade (km/h, m/s, nós)       │                                         │
│ │ • Curso (heading)                   │                                         │
│ │ • Tempo UTC (hora, dia, mês, ano)   │                                         │
│ │ • Precisão (HDOP, VDOP, PDOP)       │                                         │
│ │ • Satélites (visíveis, usados)      │                                         │
│ │ • Qualidade do fix                  │                                         │
│ └─────────────────────────────────────┘                                         │
│                               │                                                 │
│ ┌─────────────────────────────┼─────────────────────────────┐                   │
│ │                             │                             │                   │
│ ▼                             ▼                             ▼                   │
│ ┌─────────────┐         ┌─────────────┐              ┌─────────────┐            │
│ │ Telemetry   │         │ Shell       │              │ SIConverter │            │
│ │ (para GS)   │         │ (comandos)  │              │ (dados)     │            │
│ └─────────────┘         └─────────────┘              └─────────────┘            │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘

---

## 4. Estrutura de Dados (`GPSData`)

### 4.1 Posição

| Campo             | Tipo   | Unidade | Descrição                        |
| ------------------|--------|---------|----------------------------------|
| `latitude`        | double | graus   | Latitude (-90 a 90)              |
| `longitude`       | double | graus   | Longitude (-180 a 180)           |
| `altitude`        | float  | metros  | Altitude (MSL, Mean Sea Level)   |
| `geoidSeparation` | float  | metros  | Separação geoidal (WGS84 vs MSL) |

### 4.2 Velocidade e Direção

| Campo         | Tipo  | Unidade | Descrição                           |
| --------------|-------|---------|-------------------------------------|
| `speedKmh`    | float | km/h    | Velocidade em quilómetros por hora  |
| `speedKnots`  | float | nós     | Velocidade em nós (náutico)         |
| `speedMs`     | float | m/s     | Velocidade em metros por segundo    |
| `heading`     | float | graus   | Curso verdadeiro (0-360)            |
| `headingMag`  | float | graus   | Curso magnético (0-360)             |

### 4.3 Tempo UTC

| Campo         | Tipo     | Descrição            |
| --------------|----------|----------------------|
| `hour`        | uint8_t  | Hora (0-23)          |
| `minute`      | uint8_t  | Minuto (0-59)        |
| `second`      | uint8_t  | Segundo (0-59)       |
| `millisecond` | uint16_t | Milissegundo (0-999) |
| `day`         | uint8_t  | Dia (1-31)           |
| `month`       | uint8_t  | Mês (1-12)           |
| `year`        | uint16_t | Ano (2000-2099)      |

### 4.4 Precisão

| Campo  | Tipo  | Unidade | Descrição                                        |
| -------|-------|---------|--------------------------------------------------|
| `hdop` | float | -       | Diluição horizontal da precisão (<1 é excelente) |
| `vdop` | float | -       | Diluição vertical da precisão                    |
| `pdop` | float | -       | Diluição posicional da precisão                  |

### 4.5 Satélites

| Campo               | Tipo    | Descrição                 |
| --------------------|---------|---------------------------|
| `satellitesVisible` | uint8_t | Satélites visíveis no céu |
| `satellitesUsed`    | uint8_t | Satélites usados no fix   |
| `gpsSats`           | uint8_t | Satélites GPS usados      |
| `glonassSats`       | uint8_t | Satélites GLONASS usados  |
| `galileoSats`       | uint8_t | Satélites Galileo usados  |
| `beidouSats`        | uint8_t | Satélites BeiDou usados   |

### 4.6 Qualidade

| Campo             | Tipo         | Descrição                          |
| ------------------|--------------|------------------------------------|
| `fixType`         | `GPSFixType` | Tipo de fix (NONE, 2D, 3D, RTK)    |
| `fixQuality`      | float        | Qualidade do fix (0-1, 1=excelente)|
| `hasFix`          | bool         | Tem fix? (2D ou 3D)                |
| `has3DFix`        | bool         | Tem fix 3D?                        |
| `isValid`         | bool         | Dados válidos?                     |
| `timeToFirstFixMs`| uint32_t     | Tempo para primeiro fix (ms)       |

---

## 5. Tipos de Fix (`GPSFixType`)

| Valor | Constante           | Descrição                                  |
| ------|---------------------|--------------------------------------------|
| 0     | `GPS_FIX_NONE`      | Sem fix                                    |
| 1     | `GPS_FIX_2D`        | Fix 2D (latitude/longitude apenas)         |
| 2     | `GPS_FIX_3D`        | Fix 3D (latitude/longitude/altitude)       |
| 3     | `GPS_FIX_RTK_FLOAT` | RTK float (precisão centimétrica, ZED-F9P) |
| 4     | `GPS_FIX_RTK_FIXED` | RTK fixed (precisão centimétrica fixa)     |

---

## 6. Qualidade do Fix (`fixQuality`)

| Valor   | Significado | Aplicável quando               |
| --------|-------------|--------------------------------|
| 0.9-1.0 | Excelente   | HDOP < 1, 3D fix, >6 satélites |
| 0.7-0.9 | Bom         | HDOP < 2, 3D fix               |
| 0.5-0.7 | Razoável    | HDOP < 5, 2D fix               |
| 0.3-0.5 | Fraco       | HDOP > 5, poucos satélites     |
| 0.0-0.3 | Inválido    | Sem fix ou dados antigos (>2s) |

---

## 7. API Pública

### 7.1 Inicialização (Padrão)

```cpp
#include "GPS.h"

GPS gps;

void setup() {
    gps.begin();
    gps.setDebug(true);
}
´´´

7.2 Inicialização com Configuração Personalizada

```cpp
GPSConfig config;
config.protocol = GPS_PROTOCOL_NMEA;
config.baudRate = 9600;
config.rxPin = 16;
config.txPin = 17;
config.enableGLONASS = true;
config.enableGalileo = true;
config.enableBeiDou = false;
config.updateRateHz = 5;

gps.begin(config);
´´´

7.3 Loop Principal

```cpp
void loop() {
    gps.update();
    
    if (gps.hasFix()) {
        const GPSData& data = gps.getData();
        Serial.printf("Lat: %.6f, Lon: %.6f, Alt: %.1fm\n",
                      data.latitude, data.longitude, data.altitude);
        Serial.printf("Speed: %.1f km/h, Heading: %.1f°\n",
                      data.speedKmh, data.heading);
        Serial.printf("Satellites: %d, HDOP: %.1f, Quality: %.2f\n",
                      data.satellitesUsed, data.hdop, data.fixQuality);
    }
    
    delay(100);
}
´´´

7.4 Configuração Dinâmica

```cpp
// Mudar taxa de atualização
gps.setUpdateRate(10);  // 10 Hz

// Ativar/desativar sistemas de satélite
gps.setSatelliteSystems(true, true, false);  // GPS + Galileo

// Forçar protocolo (se auto-deteção falhar)
gps.setProtocol(GPS_PROTOCOL_UBX);

// Resetar estatísticas (TTFF)
gps.resetStats();
´´´

7.5 Acesso a Dados

```cpp
const GPSData& data = gps.getData();

if (data.hasFix) {
    double lat = data.latitude;
    double lon = data.longitude;
    float alt = data.altitude;
    float speed = data.speedKmh;
    float heading = data.heading;
    uint8_t sats = data.satellitesUsed;
}
´´´

7.6 Utilitários Estáticos

```cpp
// Distância entre duas coordenadas (metros)
double dist = GPS::distanceBetween(lat1, lon1, lat2, lon2);

// Bearing entre duas coordenadas (graus)
double bearing = GPS::bearingBetween(lat1, lon1, lat2, lon2);
´´´

8. Exemplo de Sessão de Debug

Ativação

```cpp
gps.setDebug(true);
gps.setRawLogging(true);  // Imprime dados brutos do GPS
´´´

Saída Típica (NMEA)

[GPS] begin() — Inicializando GPS...
[GPS]   Serial2: RX=16, TX=17, baud=9600
[GPS]   Detetando protocolo...
[GPS]   Protocolo detetado: NMEA
[GPS] begin() — GPS inicializado

[GPS RAW] $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
[GPS RAW] $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
[GPS RAW] $GPGSA,A,3,04,05,09,12,24,25,31,32,1.2,0.9,0.8*3A
[GPS RAW] $GPGSV,3,1,12,04,23,162,33,05,12,287,36,09,47,067,42,12,01,088,32*7B
[GPS RAW] $GPVTG,084.4,T,081.3,M,022.4,N,041.5,K*5F

[GPS] TTFF: 3421 ms

9. Interpretação de Qualidade

HDOP        Significado     Ação recomendada
< 1.0       Excelente       Ideal para navegação precisa
1.0-2.0     Bom             Adequado para maioria das aplicações
2.0-5.0     Razoável        Aceitável, mas com cautela
5.0-10.0    Fraco           Precisão reduzida
> 10.0      Muito fraco     Dados não confiáveis

10. Resolução de Problemas

Problema                Possível causa                  Solução
Sem fix                 GPS ao ar livre?                Aguardar ou mover para área aberta
TTFF muito alto         Primeira vez ou sinal fraco     Aguardar até 60 segundos
Protocolo não detetado  Hardware incompatível           Forçar protocolo com setProtocol()
Dados inválidos         Checksum errado                 Verificar ligações do módulo
Sem dados na UART       Pinos RX/TX trocados            Verificar fiação

Fim da documentação – GPS v1.0.0

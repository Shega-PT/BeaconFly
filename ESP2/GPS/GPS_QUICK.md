# Guia Rápido – GPS

> **Versão:** 1.0.0
> **Data:** 2026-04-29
> **Autor:** BeaconFly UAS Team

---

## 1. Código Rápido

```cpp
#include "GPS.h"

GPS gps;

void setup() {
    gps.begin();
    gps.setDebug(true);
}

void loop() {
    gps.update();
    
    if (gps.hasFix()) {
        const GPSData& data = gps.getData();
        Serial.printf("Lat: %.6f, Lon: %.6f, Alt: %.1fm\n",
                      data.latitude, data.longitude, data.altitude);
    }
    
    delay(100);
}
´´´

2. Configuração Padrão (Pinos)

Parâmetro   Valor   GPIO
RX          16      Recebe dados do GPS
TX          17      Envia comandos para GPS
Baud rate   9600    Velocidade padrão NMEA

3. Comandos Úteis

```cpp
// Inicialização padrão
gps.begin();

// Inicialização com configuração personalizada
GPSConfig config = GPSConfig::defaultConfig();
config.enableGLONASS = true;
gps.begin(config);

// Verificar fix
if (gps.hasFix()) { ... }

// Aceder a dados
const GPSData& d = gps.getData();
double lat = d.latitude;
double lon = d.longitude;
float alt = d.altitude;
float speed = d.speedKmh;
float heading = d.heading;

// Configurar taxa (UBX apenas)
gps.setUpdateRate(5);  // 5 Hz

// Ativar sistemas de satélite (UBX apenas)
gps.setSatelliteSystems(true, true, false);  // GPS + Galileo

// Forçar protocolo
gps.setProtocol(GPS_PROTOCOL_NMEA);
´´´

4. Tipos de Fix

Valor   Constante           Descrição
0       GPS_FIX_NONE        Sem fix
1       GPS_FIX_2D          Fix 2D (lat/lon)
2       GPS_FIX_3D          Fix 3D (+ altitude)
3       GPS_FIX_RTK_FLOAT   RTK float
4       GPS_FIX_RTK_FIXED   RTK fixed

5. Interpretação Rápida

HDOP        Qualidade
< 1.0       ⭐⭐⭐⭐⭐ Excelente
1.0-2.0     ⭐⭐⭐⭐ Bom
2.0-5.0     ⭐⭐⭐ Razoável
5.0-10.0    ⭐⭐ Fraco
> 10.0      ⭐ Muito fraco

6. Debug

```cpp
gps.setDebug(true);
´´´

Mensagens:

[GPS] begin() — GPS inicializado
[GPS] TTFF: 3421 ms
[GPS] Protocolo detetado: NMEA

7. Utilitários

```cpp
// Distância entre coordenadas (metros)
double dist = GPS::distanceBetween(lat1, lon1, lat2, lon2);

// Bearing entre coordenadas (graus)
double bearing = GPS::bearingBetween(lat1, lon1, lat2, lon2);
´´´

Fim do Guia Rápido – GPS v1.0.0

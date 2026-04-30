/**
 * =================================================================================
 * GPS.H — PROCESSAMENTO DE DADOS GPS (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-29
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * Este módulo é responsável por processar dados de GPS de uma grande variedade
 * de hardware suportado. Fornece uma interface unificada para acesso a dados
 * de posição, velocidade, tempo e qualidade do sinal.
 * 
 * =================================================================================
 * HARDWARE SUPORTADO
 * =================================================================================
 * 
 *   ┌─────────────┬─────────────────────────────────────────────────────────────┐
 *   │ Protocolo   │ Módulos/Modelos                                             │
 *   ├─────────────┼─────────────────────────────────────────────────────────────┤
 *   │ NMEA 0183   │ Ublox NEO-6M, NEO-7M, NEO-8M, NEO-M8N                       │
 *   │             │ Ublox NEO-M9N, ZED-F9P                                       │
 *   │             │ Quectel L80, L86, L96                                        │
 *   │             │ SIMCom SIM28, SIM68, SIM808                                  │
 *   │             │ MediaTek MT3333, MT3339                                      │
 *   │             │ Generic NMEA GPS modules                                     │
 *   ├─────────────┼─────────────────────────────────────────────────────────────┤
 *   │ UBX         │ Ublox NEO-6M, NEO-7M, NEO-8M, NEO-M8N (protocolo binário)  │
 *   │             │ Ublox NEO-M9N, ZED-F9P (RTK)                                 │
 *   ├─────────────┼─────────────────────────────────────────────────────────────┤
 *   │ SiRF        │ SiRFstarIII, SiRFstarIV                                      │
 *   ├─────────────┼─────────────────────────────────────────────────────────────┤
 *   │ MTK         │ MediaTek MT3333, MT3339 (protocolo binário)                 │
 *   └─────────────┴─────────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* =================================================================================
 * CONSTANTES DE CONFIGURAÇÃO
 * =================================================================================
 */

#define GPS_BUFFER_SIZE            256
#define GPS_NMEA_MAX_SENTENCE      120
#define GPS_UBX_MAX_PACKET         256
#define GPS_HISTORY_SIZE           10

/* =================================================================================
 * TIMEOUTS
 * =================================================================================
 */

#define GPS_TIMEOUT_NO_DATA_MS     2000
#define GPS_TIMEOUT_NO_FIX_MS      10000

/* =================================================================================
 * PINOS PADRÃO
 * =================================================================================
 */

#define GPS_DEFAULT_RX_PIN         16
#define GPS_DEFAULT_TX_PIN         17
#define GPS_DEFAULT_BAUD           9600

/* =================================================================================
 * PROTOCOLOS DE COMUNICAÇÃO
 * =================================================================================
 */

enum GPSProtocol : uint8_t {
    GPS_PROTOCOL_AUTO   = 0,
    GPS_PROTOCOL_NMEA   = 1,
    GPS_PROTOCOL_UBX    = 2,
    GPS_PROTOCOL_SIRF   = 3,
    GPS_PROTOCOL_MTK    = 4
};

/* =================================================================================
 * TIPO DE FIX
 * =================================================================================
 */

enum GPSFixType : uint8_t {
    GPS_FIX_NONE        = 0,
    GPS_FIX_2D          = 1,
    GPS_FIX_3D          = 2,
    GPS_FIX_RTK_FLOAT   = 3,
    GPS_FIX_RTK_FIXED   = 4
};

/* =================================================================================
 * SISTEMAS DE SATÉLITES
 * =================================================================================
 */

enum GPSSatelliteSystem : uint8_t {
    GPS_SYS_GPS         = 0,
    GPS_SYS_GLONASS     = 1,
    GPS_SYS_GALILEO     = 2,
    GPS_SYS_BEIDOU      = 3,
    GPS_SYS_QZSS        = 4,
    GPS_SYS_NAVIC       = 5
};

/* =================================================================================
 * ESTRUTURA DE DADOS GPS
 * =================================================================================
 */

struct GPSData {
    double   latitude;
    double   longitude;
    float    altitude;
    float    geoidSeparation;
    
    float    speedKmh;
    float    speedKnots;
    float    speedMs;
    float    heading;
    float    headingMag;
    
    double   lastLatitude;
    double   lastLongitude;
    float    lastAltitude;
    uint32_t lastTimeMs;
    
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint16_t millisecond;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
    
    float    hdop;
    float    vdop;
    float    pdop;
    
    uint8_t  satellitesVisible;
    uint8_t  satellitesUsed;
    uint8_t  gpsSats;
    uint8_t  glonassSats;
    uint8_t  galileoSats;
    uint8_t  beidouSats;
    
    GPSFixType fixType;
    float    fixQuality;
    
    bool     isValid;
    bool     hasFix;
    bool     has3DFix;
    bool     hasTime;
    
    uint32_t lastUpdateMs;
    uint32_t lastValidMs;
    uint32_t timeToFirstFixMs;
};

/* =================================================================================
 * ESTRUTURA DE CONFIGURAÇÃO
 * =================================================================================
 */

struct GPSConfig {
    GPSProtocol protocol;
    uint32_t    baudRate;
    uint8_t     rxPin;
    uint8_t     txPin;
    uint8_t     enPin;
    uint8_t     ppsPin;
    bool        enableGLONASS;
    bool        enableGalileo;
    bool        enableBeiDou;
    uint8_t     updateRateHz;
    
    static GPSConfig defaultConfig() {
        GPSConfig cfg;
        cfg.protocol = GPS_PROTOCOL_AUTO;
        cfg.baudRate = GPS_DEFAULT_BAUD;
        cfg.rxPin = GPS_DEFAULT_RX_PIN;
        cfg.txPin = GPS_DEFAULT_TX_PIN;
        cfg.enPin = 0;
        cfg.ppsPin = 0;
        cfg.enableGLONASS = true;
        cfg.enableGalileo = false;
        cfg.enableBeiDou = false;
        cfg.updateRateHz = 5;
        return cfg;
    }
};

/* =================================================================================
 * CLASSE GPS
 * =================================================================================
 */

class GPS {
public:
    GPS();
    
    bool begin();
    bool begin(const GPSConfig& config);
    void end();
    void update();
    
    bool setUpdateRate(uint8_t rateHz);
    bool setSatelliteSystems(bool enableGLONASS, bool enableGalileo, bool enableBeiDou);
    void setProtocol(GPSProtocol protocol);
    
    const GPSData& getData() const;
    bool hasFix() const;
    bool has3DFix() const;
    bool isActive() const;
    uint32_t getLastUpdateTime() const;
    GPSProtocol getProtocol() const;
    
    static double distanceBetween(double lat1, double lon1, double lat2, double lon2);
    static double bearingBetween(double lat1, double lon1, double lat2, double lon2);
    
    void resetStats();
    void setDebug(bool enable);
    void setRawLogging(bool enable);

private:
    GPSData     _data;
    GPSConfig   _config;
    GPSProtocol _detectedProtocol;
    bool        _initialized;
    bool        _active;
    bool        _debugEnabled;
    bool        _rawLogging;
    uint32_t    _startTimeMs;
    uint32_t    _lastCharMs;
    
    char        _nmeaBuffer[GPS_NMEA_MAX_SENTENCE];
    uint8_t     _nmeaIndex;
    uint8_t     _ubxBuffer[GPS_UBX_MAX_PACKET];
    uint8_t     _ubxIndex;
    uint8_t     _ubxExpectedLen;
    uint8_t     _ubxClass;
    uint8_t     _ubxId;
    
    double      _historyLat[GPS_HISTORY_SIZE];
    double      _historyLon[GPS_HISTORY_SIZE];
    uint32_t    _historyTime[GPS_HISTORY_SIZE];
    uint8_t     _historyIndex;
    uint8_t     _historyCount;
    
    bool _detectProtocol();
    bool _isNMEA(const uint8_t* data, size_t len);
    bool _isUBX(const uint8_t* data, size_t len);
    bool _isSiRF(const uint8_t* data, size_t len);
    bool _isMTK(const uint8_t* data, size_t len);
    
    void _processNMEA(char* sentence);
    void _parseGGA(char* fields[]);
    void _parseRMC(char* fields[]);
    void _parseGSA(char* fields[]);
    void _parseGSV(char* fields[]);
    void _parseVTG(char* fields[]);
    void _parseGLL(char* fields[]);
    
    void _processUBX(const uint8_t* packet, size_t len);
    void _parseUBXNAVPVT(const uint8_t* data);
    void _parseUBXNAVSAT(const uint8_t* data);
    
    double _nmeaToDouble(const char* str);
    double _nmeaToLatitude(const char* lat, const char* ns);
    double _nmeaToLongitude(const char* lon, const char* ew);
    uint8_t _nmeaToFixType(char quality);
    
    void _updateSpeedAndHeading();
    void _updateFixQuality();
    uint8_t _nmeaChecksum(const char* sentence);
    void _resetData();
    void _sendConfigCommand(const uint8_t* cmd, size_t len);
    
    bool _configureUBX();
    bool _configureNMEA();
    
    static GPS* _instance;
};

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* gpsProtocolToString(GPSProtocol protocol);
const char* gpsFixTypeToString(GPSFixType fixType);
const char* gpsSatSystemToString(GPSSatelliteSystem system);

#endif /* GPS_H */
/**
 * =================================================================================
 * GPS.CPP — IMPLEMENTAÇÃO DO PROCESSAMENTO DE GPS
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-29
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 */

#include "GPS.h"
#include <HardwareSerial.h>
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define DEG_TO_RAD (PI / 180.0f)
#define RAD_TO_DEG (180.0f / PI)

#define EARTH_RADIUS_M 6371000.0

/* =================================================================================
 * DEBUG
 * =================================================================================
 */

#define GPS_DEBUG

#ifdef GPS_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[GPS] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * CONFIGURAÇÃO UBX (mensagens para módulos Ublox)
 * =================================================================================
 */

static const uint8_t UBX_CFG_RATE[] = {0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xE8, 0x03, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00};
static const uint8_t UBX_CFG_MSG_GGA[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x23};
static const uint8_t UBX_CFG_MSG_RMC[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x24};
static const uint8_t UBX_CFG_MSG_GSA[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x25};
static const uint8_t UBX_CFG_MSG_GSV[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x26};
static const uint8_t UBX_CFG_MSG_VTG[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x28};
static const uint8_t UBX_CFG_GNSS[] = {0xB5, 0x62, 0x06, 0x3E, 0x00, 0x00};

/* =================================================================================
 * INSTÂNCIA GLOBAL
 * =================================================================================
 */

GPS* GPS::_instance = nullptr;

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

GPS::GPS()
    : _detectedProtocol(GPS_PROTOCOL_AUTO)
    , _initialized(false)
    , _active(false)
    , _debugEnabled(false)
    , _rawLogging(false)
    , _startTimeMs(0)
    , _lastCharMs(0)
    , _nmeaIndex(0)
    , _ubxIndex(0)
    , _ubxExpectedLen(0)
    , _ubxClass(0)
    , _ubxId(0)
    , _historyIndex(0)
    , _historyCount(0)
{
    memset(&_data, 0, sizeof(GPSData));
    memset(&_config, 0, sizeof(GPSConfig));
    memset(_nmeaBuffer, 0, sizeof(_nmeaBuffer));
    memset(_ubxBuffer, 0, sizeof(_ubxBuffer));
    memset(_historyLat, 0, sizeof(_historyLat));
    memset(_historyLon, 0, sizeof(_historyLon));
    memset(_historyTime, 0, sizeof(_historyTime));
    
    _instance = this;
    
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * RESET DATA — RESETA ESTRUTURA DE DADOS
 * =================================================================================
 */

void GPS::_resetData() {
    _data.latitude = 0.0;
    _data.longitude = 0.0;
    _data.altitude = 0.0f;
    _data.geoidSeparation = 0.0f;
    _data.speedKmh = 0.0f;
    _data.speedKnots = 0.0f;
    _data.speedMs = 0.0f;
    _data.heading = 0.0f;
    _data.headingMag = 0.0f;
    _data.hdop = 99.9f;
    _data.vdop = 99.9f;
    _data.pdop = 99.9f;
    _data.satellitesVisible = 0;
    _data.satellitesUsed = 0;
    _data.gpsSats = 0;
    _data.glonassSats = 0;
    _data.galileoSats = 0;
    _data.beidouSats = 0;
    _data.fixType = GPS_FIX_NONE;
    _data.fixQuality = 0.0f;
    _data.isValid = false;
    _data.hasFix = false;
    _data.has3DFix = false;
    _data.hasTime = false;
    _data.lastUpdateMs = millis();
}

/* =================================================================================
 * BEGIN (DEFAULT)
 * =================================================================================
 */

bool GPS::begin() {
    GPSConfig config = GPSConfig::defaultConfig();
    return begin(config);
}

/* =================================================================================
 * BEGIN (COM CONFIGURAÇÃO)
 * =================================================================================
 */

bool GPS::begin(const GPSConfig& config) {
    DEBUG_PRINT("begin() — Inicializando GPS...\n");
    
    _config = config;
    _resetData();
    _startTimeMs = millis();
    _lastCharMs = millis();
    _nmeaIndex = 0;
    _ubxIndex = 0;
    _historyIndex = 0;
    _historyCount = 0;
    
    /* Inicializar Serial para GPS */
    if (_config.rxPin > 0 && _config.txPin > 0) {
        Serial2.begin(_config.baudRate, SERIAL_8N1, _config.rxPin, _config.txPin);
        DEBUG_PRINT("  Serial2: RX=%d, TX=%d, baud=%lu\n", _config.rxPin, _config.txPin, _config.baudRate);
    } else {
        Serial2.begin(_config.baudRate);
        DEBUG_PRINT("  Serial2: baud=%lu (pinos padrão)\n", _config.baudRate);
    }
    
    /* Habilitar pino de enable se configurado */
    if (_config.enPin > 0) {
        pinMode(_config.enPin, OUTPUT);
        digitalWrite(_config.enPin, HIGH);
        DEBUG_PRINT("  Enable pin: %d\n", _config.enPin);
    }
    
    /* Detetar protocolo */
    if (_config.protocol == GPS_PROTOCOL_AUTO) {
        DEBUG_PRINT("  Detetando protocolo...\n");
        if (!_detectProtocol()) {
            DEBUG_PRINT("  Falha na deteção. Usando NMEA como fallback.\n");
            _detectedProtocol = GPS_PROTOCOL_NMEA;
        } else {
            DEBUG_PRINT("  Protocolo detetado: %s\n", gpsProtocolToString(_detectedProtocol));
        }
    } else {
        _detectedProtocol = _config.protocol;
        DEBUG_PRINT("  Protocolo forçado: %s\n", gpsProtocolToString(_detectedProtocol));
    }
    
    /* Configurar módulo conforme protocolo */
    if (_detectedProtocol == GPS_PROTOCOL_UBX) {
        _configureUBX();
    } else if (_detectedProtocol == GPS_PROTOCOL_NMEA) {
        _configureNMEA();
    }
    
    _initialized = true;
    _active = true;
    
    DEBUG_PRINT("begin() — GPS inicializado\n");
    return true;
}

/* =================================================================================
 * END
 * =================================================================================
 */

void GPS::end() {
    DEBUG_PRINT("end() — Encerrando GPS\n");
    
    if (_config.enPin > 0) {
        digitalWrite(_config.enPin, LOW);
    }
    
    Serial2.end();
    _initialized = false;
    _active = false;
}

/* =================================================================================
 * DETECT PROTOCOL — DETEÇÃO AUTOMÁTICA DE PROTOCOLO
 * =================================================================================
 */

bool GPS::_detectProtocol() {
    uint32_t start = millis();
    uint8_t buffer[64];
    
    while (millis() - start < 3000) {
        while (Serial2.available()) {
            size_t len = Serial2.readBytes(buffer, sizeof(buffer));
            _lastCharMs = millis();
            
            if (_isNMEA(buffer, len)) {
                _detectedProtocol = GPS_PROTOCOL_NMEA;
                return true;
            }
            if (_isUBX(buffer, len)) {
                _detectedProtocol = GPS_PROTOCOL_UBX;
                return true;
            }
            if (_isSiRF(buffer, len)) {
                _detectedProtocol = GPS_PROTOCOL_SIRF;
                return true;
            }
            if (_isMTK(buffer, len)) {
                _detectedProtocol = GPS_PROTOCOL_MTK;
                return true;
            }
        }
        delay(10);
    }
    
    return false;
}

/* =================================================================================
 * IS NMEA — VERIFICA SE OS DADOS SÃO NMEA
 * =================================================================================
 */

bool GPS::_isNMEA(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len && i < 20; i++) {
        if (data[i] == '$') {
            return true;
        }
    }
    return false;
}

/* =================================================================================
 * IS UBX — VERIFICA SE OS DADOS SÃO UBX
 * =================================================================================
 */

bool GPS::_isUBX(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len - 1 && i < 20; i++) {
        if (data[i] == 0xB5 && data[i + 1] == 0x62) {
            return true;
        }
    }
    return false;
}

/* =================================================================================
 * IS SIRF — VERIFICA SE OS DADOS SÃO SIRF
 * =================================================================================
 */

bool GPS::_isSiRF(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len - 1 && i < 20; i++) {
        if (data[i] == 0xA0 && data[i + 1] == 0xA2) {
            return true;
        }
    }
    return false;
}

/* =================================================================================
 * IS MTK — VERIFICA SE OS DADOS SÃO MTK
 * =================================================================================
 */

bool GPS::_isMTK(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len - 1 && i < 20; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            /* MTK header simplificado */
            return true;
        }
    }
    return false;
}

/* =================================================================================
 * PROCESS NMEA — PROCESSAMENTO DE SENTENÇAS NMEA
 * =================================================================================
 */

void GPS::_processNMEA(char* sentence) {
    if (_rawLogging) {
        Serial.printf("[GPS RAW] %s\n", sentence);
    }
    
    /* Verificar checksum */
    uint8_t computed = _nmeaChecksum(sentence);
    char* asterisk = strchr(sentence, '*');
    if (!asterisk) return;
    
    uint8_t received = (uint8_t)strtol(asterisk + 1, NULL, 16);
    if (computed != received) {
        DEBUG_PRINT("Checksum inválido: calc=0x%02X, recv=0x%02X\n", computed, received);
        return;
    }
    
    *asterisk = '\0';
    
    /* Parsear campos */
    char* fields[20];
    uint8_t fieldCount = 0;
    char* token = strtok(sentence, ",");
    
    while (token && fieldCount < 20) {
        fields[fieldCount++] = token;
        token = strtok(NULL, ",");
    }
    
    if (fieldCount < 1) return;
    
    /* Identificar tipo de sentença */
    if (strncmp(fields[0] + 3, "GGA", 3) == 0) {
        _parseGGA(fields);
    } else if (strncmp(fields[0] + 3, "RMC", 3) == 0) {
        _parseRMC(fields);
    } else if (strncmp(fields[0] + 3, "GSA", 3) == 0) {
        _parseGSA(fields);
    } else if (strncmp(fields[0] + 3, "GSV", 3) == 0) {
        _parseGSV(fields);
    } else if (strncmp(fields[0] + 3, "VTG", 3) == 0) {
        _parseVTG(fields);
    } else if (strncmp(fields[0] + 3, "GLL", 3) == 0) {
        _parseGLL(fields);
    }
}

/* =================================================================================
 * PARSE GGA — $GPGGA
 * =================================================================================
 */

void GPS::_parseGGA(char* fields[]) {
    if (fieldCount < 15) return;
    
    /* Hora */
    if (strlen(fields[1]) >= 6) {
        _data.hour = (fields[1][0] - '0') * 10 + (fields[1][1] - '0');
        _data.minute = (fields[1][2] - '0') * 10 + (fields[1][3] - '0');
        _data.second = (fields[1][4] - '0') * 10 + (fields[1][5] - '0');
        _data.hasTime = true;
    }
    
    /* Latitude */
    if (strlen(fields[2]) > 0 && strlen(fields[3]) > 0) {
        _data.latitude = _nmeaToLatitude(fields[2], fields[3]);
    }
    
    /* Longitude */
    if (strlen(fields[4]) > 0 && strlen(fields[5]) > 0) {
        _data.longitude = _nmeaToLongitude(fields[4], fields[5]);
    }
    
    /* Fix quality */
    if (strlen(fields[6]) > 0) {
        char quality = fields[6][0];
        _data.fixType = _nmeaToFixType(quality);
        _data.hasFix = (_data.fixType >= GPS_FIX_2D);
        _data.has3DFix = (_data.fixType >= GPS_FIX_3D);
    }
    
    /* Satélites */
    if (strlen(fields[7]) > 0) {
        _data.satellitesUsed = atoi(fields[7]);
    }
    
    /* HDOP */
    if (strlen(fields[8]) > 0) {
        _data.hdop = atof(fields[8]);
    }
    
    /* Altitude */
    if (strlen(fields[9]) > 0) {
        _data.altitude = atof(fields[9]);
    }
    
    /* Separação geoidal */
    if (strlen(fields[11]) > 0) {
        _data.geoidSeparation = atof(fields[11]);
    }
    
    _data.lastUpdateMs = millis();
    _data.isValid = true;
    
    if (_data.hasFix) {
        _data.lastValidMs = _data.lastUpdateMs;
        if (_data.timeToFirstFixMs == 0 && _data.has3DFix) {
            _data.timeToFirstFixMs = _data.lastUpdateMs - _startTimeMs;
            DEBUG_PRINT("TTFF: %lu ms\n", _data.timeToFirstFixMs);
        }
    }
}

/* =================================================================================
 * PARSE RMC — $GPRMC
 * =================================================================================
 */

void GPS::_parseRMC(char* fields[]) {
    if (fieldCount < 12) return;
    
    /* Status (A=Válido, V=Inválido) */
    if (strlen(fields[2]) > 0) {
        _data.isValid = (fields[2][0] == 'A');
    }
    
    /* Latitude */
    if (_data.isValid && strlen(fields[3]) > 0 && strlen(fields[4]) > 0) {
        _data.latitude = _nmeaToLatitude(fields[3], fields[4]);
    }
    
    /* Longitude */
    if (_data.isValid && strlen(fields[5]) > 0 && strlen(fields[6]) > 0) {
        _data.longitude = _nmeaToLongitude(fields[5], fields[6]);
    }
    
    /* Velocidade (nós) */
    if (strlen(fields[7]) > 0) {
        _data.speedKnots = atof(fields[7]);
        _data.speedKmh = _data.speedKnots * 1.852f;
        _data.speedMs = _data.speedKmh / 3.6f;
    }
    
    /* Curso */
    if (strlen(fields[8]) > 0) {
        _data.heading = atof(fields[8]);
    }
    
    /* Data */
    if (strlen(fields[9]) > 0 && strlen(fields[9]) >= 6) {
        _data.day = (fields[9][0] - '0') * 10 + (fields[9][1] - '0');
        _data.month = (fields[9][2] - '0') * 10 + (fields[9][3] - '0');
        _data.year = 2000 + ((fields[9][4] - '0') * 10 + (fields[9][5] - '0'));
    }
    
    _data.lastUpdateMs = millis();
    
    if (_data.isValid) {
        _data.lastValidMs = _data.lastUpdateMs;
        _data.hasFix = true;
    }
}

/* =================================================================================
 * PARSE GSA — $GPGSA
 * =================================================================================
 */

void GPS::_parseGSA(char* fields[]) {
    if (fieldCount < 18) return;
    
    /* Fix type */
    if (strlen(fields[2]) > 0) {
        int fix = atoi(fields[2]);
        _data.hasFix = (fix >= 2);
        _data.has3DFix = (fix == 3);
    }
    
    /* PDOP */
    if (strlen(fields[15]) > 0) {
        _data.pdop = atof(fields[15]);
    }
    
    /* HDOP */
    if (strlen(fields[16]) > 0) {
        _data.hdop = atof(fields[16]);
    }
    
    /* VDOP */
    if (strlen(fields[17]) > 0) {
        _data.vdop = atof(fields[17]);
    }
}

/* =================================================================================
 * PARSE GSV — $GPGSV
 * =================================================================================
 */

void GPS::_parseGSV(char* fields[]) {
    if (fieldCount < 4) return;
    
    /* Número total de satélites visíveis */
    if (strlen(fields[3]) > 0) {
        _data.satellitesVisible = atoi(fields[3]);
    }
}

/* =================================================================================
 * PARSE VTG — $GPVTG
 * =================================================================================
 */

void GPS::_parseVTG(char* fields[]) {
    if (fieldCount < 9) return;
    
    /* Curso verdadeiro */
    if (strlen(fields[1]) > 0) {
        _data.heading = atof(fields[1]);
    }
    
    /* Curso magnético */
    if (strlen(fields[3]) > 0) {
        _data.headingMag = atof(fields[3]);
    }
    
    /* Velocidade (nós) */
    if (strlen(fields[5]) > 0) {
        _data.speedKnots = atof(fields[5]);
        _data.speedKmh = _data.speedKnots * 1.852f;
        _data.speedMs = _data.speedKmh / 3.6f;
    }
}

/* =================================================================================
 * PARSE GLL — $GPGLL
 * =================================================================================
 */

void GPS::_parseGLL(char* fields[]) {
    if (fieldCount < 7) return;
    
    /* Latitude */
    if (strlen(fields[1]) > 0 && strlen(fields[2]) > 0) {
        _data.latitude = _nmeaToLatitude(fields[1], fields[2]);
    }
    
    /* Longitude */
    if (strlen(fields[3]) > 0 && strlen(fields[4]) > 0) {
        _data.longitude = _nmeaToLongitude(fields[3], fields[4]);
    }
    
    /* Hora */
    if (strlen(fields[5]) >= 6) {
        _data.hour = (fields[5][0] - '0') * 10 + (fields[5][1] - '0');
        _data.minute = (fields[5][2] - '0') * 10 + (fields[5][3] - '0');
        _data.second = (fields[5][4] - '0') * 10 + (fields[5][5] - '0');
        _data.hasTime = true;
    }
    
    /* Status */
    if (strlen(fields[6]) > 0) {
        _data.isValid = (fields[6][0] == 'A');
    }
}

/* =================================================================================
 * PROCESS UBX — PROCESSAMENTO DE PACOTES UBX
 * =================================================================================
 */

void GPS::_processUBX(const uint8_t* packet, size_t len) {
    if (len < 8) return;
    
    uint8_t msgClass = packet[2];
    uint8_t msgId = packet[3];
    uint16_t msgLen = packet[4] | (packet[5] << 8);
    
    if (len < (size_t)(8 + msgLen + 2)) return;
    
    /* Verificar checksum */
    uint8_t ck_a = 0, ck_b = 0;
    for (size_t i = 2; i < 6 + msgLen; i++) {
        ck_a += packet[i];
        ck_b += ck_a;
    }
    
    if (ck_a != packet[6 + msgLen] || ck_b != packet[7 + msgLen]) {
        DEBUG_PRINT("UBX checksum inválido\n");
        return;
    }
    
    const uint8_t* payload = packet + 8;
    
    if (msgClass == 0x01 && msgId == 0x07) {
        _parseUBXNAVPVT(payload);
    } else if (msgClass == 0x01 && msgId == 0x35) {
        _parseUBXNAVSAT(payload);
    }
}

/* =================================================================================
 * PARSE UBX NAV-PVT
 * =================================================================================
 */

void GPS::_parseUBXNAVPVT(const uint8_t* data) {
    uint32_t iTOW = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    
    _data.year = data[4] | (data[5] << 8);
    _data.month = data[6];
    _data.day = data[7];
    _data.hour = data[8];
    _data.minute = data[9];
    _data.second = data[10];
    _data.hasTime = true;
    
    uint8_t fixType = data[20];
    _data.hasFix = (fixType >= 2);
    _data.has3DFix = (fixType >= 3);
    if (fixType == 3) _data.fixType = GPS_FIX_3D;
    else if (fixType == 2) _data.fixType = GPS_FIX_2D;
    else _data.fixType = GPS_FIX_NONE;
    
    double lat = data[28] | (data[29] << 8) | (data[30] << 16) | (data[31] << 24);
    double lon = data[32] | (data[33] << 8) | (data[34] << 16) | (data[35] << 24);
    _data.latitude = lat / 10000000.0;
    _data.longitude = lon / 10000000.0;
    
    int32_t alt = data[36] | (data[37] << 8) | (data[38] << 16) | (data[39] << 24);
    _data.altitude = alt / 1000.0f;
    
    int32_t hMSL = data[40] | (data[41] << 8) | (data[42] << 16) | (data[43] << 24);
    _data.altitude = hMSL / 1000.0f;
    
    uint32_t speed = data[60] | (data[61] << 8) | (data[62] << 16) | (data[63] << 24);
    _data.speedMs = speed / 1000.0f;
    _data.speedKmh = _data.speedMs * 3.6f;
    _data.speedKnots = _data.speedMs * 1.94384f;
    
    uint32_t heading = data[76] | (data[77] << 8) | (data[78] << 16) | (data[79] << 24);
    _data.heading = heading / 100000.0f;
    
    uint16_t hdop = data[84] | (data[85] << 8);
    _data.hdop = hdop / 100.0f;
    
    _data.isValid = true;
    _data.lastUpdateMs = millis();
    _data.lastValidMs = _data.lastUpdateMs;
    
    if (_data.timeToFirstFixMs == 0 && _data.has3DFix) {
        _data.timeToFirstFixMs = _data.lastUpdateMs - _startTimeMs;
        DEBUG_PRINT("TTFF: %lu ms\n", _data.timeToFirstFixMs);
    }
}

/* =================================================================================
 * PARSE UBX NAV-SAT
 * =================================================================================
 */

void GPS::_parseUBXNAVSAT(const uint8_t* data) {
    _data.satellitesUsed = data[4];
}

/* =================================================================================
 * UPDATE — ATUALIZAÇÃO PRINCIPAL (CHAMAR NO LOOP)
 * =================================================================================
 */

void GPS::update() {
    if (!_initialized) return;
    
    while (Serial2.available()) {
        uint8_t c = Serial2.read();
        _lastCharMs = millis();
        
        if (_rawLogging) {
            Serial.printf("%02X ", c);
        }
        
        if (_detectedProtocol == GPS_PROTOCOL_NMEA) {
            if (c == '$') {
                _nmeaIndex = 0;
                _nmeaBuffer[_nmeaIndex++] = c;
            } else if (c == '\n' && _nmeaIndex > 0) {
                _nmeaBuffer[_nmeaIndex] = '\0';
                _processNMEA(_nmeaBuffer);
                _nmeaIndex = 0;
            } else if (_nmeaIndex < GPS_NMEA_MAX_SENTENCE - 1 && _nmeaIndex > 0) {
                _nmeaBuffer[_nmeaIndex++] = c;
            }
        } else if (_detectedProtocol == GPS_PROTOCOL_UBX) {
            if (_ubxIndex == 0 && c == 0xB5) {
                _ubxBuffer[_ubxIndex++] = c;
            } else if (_ubxIndex == 1 && c == 0x62) {
                _ubxBuffer[_ubxIndex++] = c;
            } else if (_ubxIndex >= 2) {
                _ubxBuffer[_ubxIndex++] = c;
                
                if (_ubxIndex == 6) {
                    _ubxExpectedLen = _ubxBuffer[4] | (_ubxBuffer[5] << 8);
                }
                
                if (_ubxIndex >= (size_t)(8 + _ubxExpectedLen + 2)) {
                    _processUBX(_ubxBuffer, _ubxIndex);
                    _ubxIndex = 0;
                    _ubxExpectedLen = 0;
                } else if (_ubxIndex >= GPS_UBX_MAX_PACKET) {
                    _ubxIndex = 0;
                }
            } else {
                _ubxIndex = 0;
            }
        }
    }
    
    /* Verificar timeout */
    if (millis() - _lastCharMs > GPS_TIMEOUT_NO_DATA_MS) {
        _active = false;
    } else {
        _active = true;
    }
    
    _updateSpeedAndHeading();
    _updateFixQuality();
}

/* =================================================================================
 * UPDATE SPEED AND HEADING — ATUALIZA VELOCIDADE E RUMO POR POSIÇÃO ANTERIOR
 * =================================================================================
 */

void GPS::_updateSpeedAndHeading() {
    if (!_data.hasFix) return;
    
    _historyLat[_historyIndex] = _data.latitude;
    _historyLon[_historyIndex] = _data.longitude;
    _historyTime[_historyIndex] = _data.lastUpdateMs;
    _historyIndex = (_historyIndex + 1) % GPS_HISTORY_SIZE;
    if (_historyCount < GPS_HISTORY_SIZE) _historyCount++;
    
    if (_historyCount < 2) return;
    
    uint8_t prev = (_historyIndex - 1 + GPS_HISTORY_SIZE) % GPS_HISTORY_SIZE;
    double dist = distanceBetween(_historyLat[prev], _historyLon[prev], 
                                   _data.latitude, _data.longitude);
    uint32_t dt = _data.lastUpdateMs - _historyTime[prev];
    
    if (dt > 0 && dt < 2000) {
        float speedMsCalc = dist / (dt / 1000.0f);
        
        /* Se o GPS não forneceu velocidade, usa a calculada */
        if (_data.speedMs == 0 && speedMsCalc > 0.5f) {
            _data.speedMs = speedMsCalc;
            _data.speedKmh = speedMsCalc * 3.6f;
            _data.speedKnots = speedMsCalc * 1.94384f;
        }
    }
    
    /* Calcular bearing se não fornecido */
    if (_data.heading == 0 && _historyCount > 1) {
        _data.heading = bearingBetween(_historyLat[prev], _historyLon[prev],
                                        _data.latitude, _data.longitude);
    }
}

/* =================================================================================
 * UPDATE FIX QUALITY — ATUALIZA QUALIDADE DO FIX
 * =================================================================================
 */

void GPS::_updateFixQuality() {
    if (!_data.hasFix) {
        _data.fixQuality = 0.0f;
        return;
    }
    
    float quality = 1.0f;
    
    if (_data.hdop > 0) {
        if (_data.hdop < 1.0f) quality = 1.0f;
        else if (_data.hdop < 2.0f) quality = 0.9f;
        else if (_data.hdop < 5.0f) quality = 0.7f;
        else if (_data.hdop < 10.0f) quality = 0.5f;
        else quality = 0.3f;
    }
    
    if (!_data.has3DFix) quality *= 0.7f;
    if (_data.satellitesUsed < 6) quality *= 0.8f;
    if (millis() - _data.lastValidMs > 2000) quality *= 0.5f;
    
    _data.fixQuality = quality;
}

/* =================================================================================
 * NMEA TO DOUBLE
 * =================================================================================
 */

double GPS::_nmeaToDouble(const char* str) {
    if (!str || strlen(str) == 0) return 0.0;
    
    char* dot = strchr(str, '.');
    if (!dot) return atof(str);
    
    int intPart = atoi(str);
    double fracPart = atof(dot);
    
    return intPart + fracPart;
}

/* =================================================================================
 * NMEA TO LATITUDE
 * =================================================================================
 */

double GPS::_nmeaToLatitude(const char* lat, const char* ns) {
    if (!lat || strlen(lat) == 0) return 0.0;
    
    double raw = atof(lat);
    int degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100);
    double result = degrees + (minutes / 60.0);
    
    if (ns[0] == 'S') result = -result;
    
    return result;
}

/* =================================================================================
 * NMEA TO LONGITUDE
 * =================================================================================
 */

double GPS::_nmeaToLongitude(const char* lon, const char* ew) {
    if (!lon || strlen(lon) == 0) return 0.0;
    
    double raw = atof(lon);
    int degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100);
    double result = degrees + (minutes / 60.0);
    
    if (ew[0] == 'W') result = -result;
    
    return result;
}

/* =================================================================================
 * NMEA TO FIX TYPE
 * =================================================================================
 */

uint8_t GPS::_nmeaToFixType(char quality) {
    switch (quality) {
        case '0': return GPS_FIX_NONE;
        case '1': return GPS_FIX_2D;
        case '2': return GPS_FIX_3D;
        case '4': return GPS_FIX_RTK_FIXED;
        case '5': return GPS_FIX_RTK_FLOAT;
        default:  return GPS_FIX_NONE;
    }
}

/* =================================================================================
 * NMEA CHECKSUM
 * =================================================================================
 */

uint8_t GPS::_nmeaChecksum(const char* sentence) {
    if (!sentence || sentence[0] != '$') return 0;
    
    uint8_t crc = 0;
    const char* p = sentence + 1;
    
    while (*p && *p != '*') {
        crc ^= (uint8_t)*p;
        p++;
    }
    
    return crc;
}

/* =================================================================================
 * CONFIGURE UBX
 * =================================================================================
 */

bool GPS::_configureUBX() {
    DEBUG_PRINT("_configureUBX() — Configurando módulo UBX\n");
    
    _sendConfigCommand(UBX_CFG_RATE, sizeof(UBX_CFG_RATE));
    _sendConfigCommand(UBX_CFG_MSG_GGA, sizeof(UBX_CFG_MSG_GGA));
    _sendConfigCommand(UBX_CFG_MSG_RMC, sizeof(UBX_CFG_MSG_RMC));
    _sendConfigCommand(UBX_CFG_MSG_GSA, sizeof(UBX_CFG_MSG_GSA));
    _sendConfigCommand(UBX_CFG_MSG_GSV, sizeof(UBX_CFG_MSG_GSV));
    _sendConfigCommand(UBX_CFG_MSG_VTG, sizeof(UBX_CFG_MSG_VTG));
    
    return true;
}

/* =================================================================================
 * CONFIGURE NMEA
 * =================================================================================
 */

bool GPS::_configureNMEA() {
    DEBUG_PRINT("_configureNMEA() — Usando NMEA padrão (sem configuração adicional)\n");
    return true;
}

/* =================================================================================
 * SEND CONFIG COMMAND
 * =================================================================================
 */

void GPS::_sendConfigCommand(const uint8_t* cmd, size_t len) {
    Serial2.write(cmd, len);
    delay(50);
}

/* =================================================================================
 * SET UPDATE RATE
 * =================================================================================
 */

bool GPS::setUpdateRate(uint8_t rateHz) {
    _config.updateRateHz = rateHz;
    
    if (_detectedProtocol == GPS_PROTOCOL_UBX) {
        uint32_t rateMs = 1000 / rateHz;
        uint8_t cmd[] = {0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 
                         (uint8_t)(rateMs & 0xFF), (uint8_t)((rateMs >> 8) & 0xFF),
                         0x01, 0x00, 0x01, 0x00, 0x00, 0x00};
        
        uint8_t ck_a = 0, ck_b = 0;
        for (int i = 2; i < 12; i++) {
            ck_a += cmd[i];
            ck_b += ck_a;
        }
        cmd[12] = ck_a;
        cmd[13] = ck_b;
        
        _sendConfigCommand(cmd, sizeof(cmd));
        return true;
    }
    
    return false;
}

/* =================================================================================
 * SET SATELLITE SYSTEMS
 * =================================================================================
 */

bool GPS::setSatelliteSystems(bool enableGLONASS, bool enableGalileo, bool enableBeiDou) {
    _config.enableGLONASS = enableGLONASS;
    _config.enableGalileo = enableGalileo;
    _config.enableBeiDou = enableBeiDou;
    
    if (_detectedProtocol == GPS_PROTOCOL_UBX) {
        uint8_t gnssConfig[40];
        memset(gnssConfig, 0, sizeof(gnssConfig));
        
        gnssConfig[0] = 0xB5;
        gnssConfig[1] = 0x62;
        gnssConfig[2] = 0x06;
        gnssConfig[3] = 0x3E;
        gnssConfig[4] = 0x00;
        gnssConfig[5] = 0x00;
        
        _sendConfigCommand(gnssConfig, 8);
        return true;
    }
    
    return false;
}

/* =================================================================================
 * SET PROTOCOL
 * =================================================================================
 */

void GPS::setProtocol(GPSProtocol protocol) {
    _detectedProtocol = protocol;
    DEBUG_PRINT("setProtocol() — Protocolo alterado: %s\n", gpsProtocolToString(protocol));
}

/* =================================================================================
 * GET DATA
 * =================================================================================
 */

const GPSData& GPS::getData() const {
    return _data;
}

/* =================================================================================
 * HAS FIX
 * =================================================================================
 */

bool GPS::hasFix() const {
    return _data.hasFix && (millis() - _data.lastUpdateMs < 2000);
}

/* =================================================================================
 * HAS 3D FIX
 * =================================================================================
 */

bool GPS::has3DFix() const {
    return _data.has3DFix && (millis() - _data.lastUpdateMs < 2000);
}

/* =================================================================================
 * IS ACTIVE
 * =================================================================================
 */

bool GPS::isActive() const {
    return _active && (millis() - _lastCharMs < 3000);
}

/* =================================================================================
 * GET LAST UPDATE TIME
 * =================================================================================
 */

uint32_t GPS::getLastUpdateTime() const {
    return _data.lastUpdateMs;
}

/* =================================================================================
 * GET PROTOCOL
 * =================================================================================
 */

GPSProtocol GPS::getProtocol() const {
    return _detectedProtocol;
}

/* =================================================================================
 * RESET STATS
 * =================================================================================
 */

void GPS::resetStats() {
    _data.timeToFirstFixMs = 0;
    _startTimeMs = millis();
    DEBUG_PRINT("resetStats() — Estatísticas resetadas\n");
}

/* =================================================================================
 * SET DEBUG
 * =================================================================================
 */

void GPS::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}

/* =================================================================================
 * SET RAW LOGGING
 * =================================================================================
 */

void GPS::setRawLogging(bool enable) {
    _rawLogging = enable;
    DEBUG_PRINT("setRawLogging() — Raw logging %s\n", enable ? "ATIVADO" : "desativado");
}

/* =================================================================================
 * DISTANCE BETWEEN (Haversine)
 * =================================================================================
 */

double GPS::distanceBetween(double lat1, double lon1, double lat2, double lon2) {
    double lat1Rad = lat1 * DEG_TO_RAD;
    double lat2Rad = lat2 * DEG_TO_RAD;
    double deltaLat = (lat2 - lat1) * DEG_TO_RAD;
    double deltaLon = (lon2 - lon1) * DEG_TO_RAD;
    
    double a = sin(deltaLat / 2) * sin(deltaLat / 2) +
               cos(lat1Rad) * cos(lat2Rad) *
               sin(deltaLon / 2) * sin(deltaLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    
    return EARTH_RADIUS_M * c;
}

/* =================================================================================
 * BEARING BETWEEN
 * =================================================================================
 */

double GPS::bearingBetween(double lat1, double lon1, double lat2, double lon2) {
    double lat1Rad = lat1 * DEG_TO_RAD;
    double lat2Rad = lat2 * DEG_TO_RAD;
    double deltaLon = (lon2 - lon1) * DEG_TO_RAD;
    
    double y = sin(deltaLon) * cos(lat2Rad);
    double x = cos(lat1Rad) * sin(lat2Rad) - sin(lat1Rad) * cos(lat2Rad) * cos(deltaLon);
    
    double bearing = atan2(y, x) * RAD_TO_DEG;
    if (bearing < 0) bearing += 360;
    
    return bearing;
}

/* =================================================================================
 * GPS PROTOCOL TO STRING
 * =================================================================================
 */

const char* gpsProtocolToString(GPSProtocol protocol) {
    switch (protocol) {
        case GPS_PROTOCOL_AUTO: return "AUTO";
        case GPS_PROTOCOL_NMEA: return "NMEA";
        case GPS_PROTOCOL_UBX:  return "UBX";
        case GPS_PROTOCOL_SIRF: return "SiRF";
        case GPS_PROTOCOL_MTK:  return "MTK";
        default:                return "UNKNOWN";
    }
}

/* =================================================================================
 * GPS FIX TYPE TO STRING
 * =================================================================================
 */

const char* gpsFixTypeToString(GPSFixType fixType) {
    switch (fixType) {
        case GPS_FIX_NONE:      return "NO FIX";
        case GPS_FIX_2D:        return "2D FIX";
        case GPS_FIX_3D:        return "3D FIX";
        case GPS_FIX_RTK_FLOAT: return "RTK FLOAT";
        case GPS_FIX_RTK_FIXED: return "RTK FIXED";
        default:                return "UNKNOWN";
    }
}

/* =================================================================================
 * GPS SAT SYSTEM TO STRING
 * =================================================================================
 */

const char* gpsSatSystemToString(GPSSatelliteSystem system) {
    switch (system) {
        case GPS_SYS_GPS:     return "GPS";
        case GPS_SYS_GLONASS: return "GLONASS";
        case GPS_SYS_GALILEO: return "Galileo";
        case GPS_SYS_BEIDOU:  return "BeiDou";
        case GPS_SYS_QZSS:    return "QZSS";
        case GPS_SYS_NAVIC:   return "NavIC";
        default:              return "UNKNOWN";
    }
}
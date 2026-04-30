/**
 * =================================================================================
 * TELEMETRY.CPP — IMPLEMENTAÇÃO DA GESTÃO DE TELEMETRIA (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-29
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. Telemetria é enviada a 10Hz (normal) ou 20Hz (failsafe)
 * 2. Utiliza o módulo LoRa para envio (fila de prioridades)
 * 3. Dados SI são enviados de volta para ESP1 via UART
 * 4. Constrói mensagens TLV compatíveis com o protocolo BeaconFly
 * 
 * =================================================================================
 */

#include "Telemetry.h"
#include <Arduino.h>
#include <cmath>

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define TELEMETRY_DEBUG

#ifdef TELEMETRY_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[Telemetry] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * INSTÂNCIA GLOBAL
 * =================================================================================
 */

Telemetry* Telemetry::_instance = nullptr;

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* telemetryModeToString(TelemetryMode mode) {
    switch (mode) {
        case TELEMETRY_MODE_NORMAL: return "NORMAL";
        case TELEMETRY_MODE_FAST:   return "FAST";
        case TELEMETRY_MODE_DEBUG:  return "DEBUG";
        default:                    return "UNKNOWN";
    }
}

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

Telemetry::Telemetry()
    : _si(nullptr)
    , _gps(nullptr)
    , _flightControl(nullptr)
    , _failsafe(nullptr)
    , _lora(nullptr)
    , _mode(TELEMETRY_MODE_NORMAL)
    , _enabled(true)
    , _debugEnabled(false)
    , _lastSendMs(0)
    , _packetsSent(0)
    , _packetsFailed(0)
    , _uartCallback(nullptr)
{
    memset(_buffer, 0, sizeof(_buffer));
    _instance = this;
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO
 * =================================================================================
 */

bool Telemetry::begin(SIConverter* si, GPS* gps, FlightControl* fc, 
                      Failsafe* failsafe, LoRa* lora) {
    if (!si || !gps || !fc || !failsafe || !lora) {
        DEBUG_PRINT("begin() falhou: ponteiros nulos\n");
        return false;
    }
    
    _si = si;
    _gps = gps;
    _flightControl = fc;
    _failsafe = failsafe;
    _lora = lora;
    
    _mode = TELEMETRY_MODE_NORMAL;
    _enabled = true;
    _lastSendMs = millis();
    _packetsSent = 0;
    _packetsFailed = 0;
    
    DEBUG_PRINT("begin() — Telemetria inicializada (modo: NORMAL)\n");
    return true;
}

/* =================================================================================
 * SET MODE — DEFINE O MODO DE TELEMETRIA
 * =================================================================================
 */

void Telemetry::setMode(TelemetryMode mode) {
    if (_mode != mode) {
        _mode = mode;
        DEBUG_PRINT("setMode() — Modo alterado: %s\n", telemetryModeToString(mode));
    }
}

/* =================================================================================
 * SET FAILSAFE MODE — DEFINE MODO COM BASE NO FAILSAFE
 * =================================================================================
 */

void Telemetry::setFailsafeMode(bool failsafeActive) {
    if (failsafeActive) {
        setMode(TELEMETRY_MODE_FAST);
    } else {
        setMode(TELEMETRY_MODE_NORMAL);
    }
}

/* =================================================================================
 * GET SEND INTERVAL — OBTÉM INTERVALO DE ENVIO CONFORME MODO
 * =================================================================================
 */

uint32_t Telemetry::_getSendInterval() const {
    switch (_mode) {
        case TELEMETRY_MODE_FAST:
            return TELEMETRY_FAST_INTERVAL_MS;
        case TELEMETRY_MODE_DEBUG:
            return TELEMETRY_FAST_INTERVAL_MS;
        case TELEMETRY_MODE_NORMAL:
        default:
            return TELEMETRY_SEND_INTERVAL_MS;
    }
}

/* =================================================================================
 * ADD FIELD — ADICIONA CAMPO TLV (FLOAT)
 * =================================================================================
 */

void Telemetry::_addField(TLVMessage& msg, uint8_t id, float value) {
    if (msg.tlvCount >= TELEMETRY_MAX_TLVS) return;
    addTLVFloat(&msg, id, value);
}

/* =================================================================================
 * ADD FIELD — ADICIONA CAMPO TLV (INT32)
 * =================================================================================
 */

void Telemetry::_addField(TLVMessage& msg, uint8_t id, int32_t value) {
    if (msg.tlvCount >= TELEMETRY_MAX_TLVS) return;
    addTLVInt32(&msg, id, value);
}

/* =================================================================================
 * ADD FIELD — ADICIONA CAMPO TLV (UINT32)
 * =================================================================================
 */

void Telemetry::_addField(TLVMessage& msg, uint8_t id, uint32_t value) {
    if (msg.tlvCount >= TELEMETRY_MAX_TLVS) return;
    addTLVUint32(&msg, id, value);
}

/* =================================================================================
 * ADD FIELD — ADICIONA CAMPO TLV (UINT16)
 * =================================================================================
 */

void Telemetry::_addField(TLVMessage& msg, uint8_t id, uint16_t value) {
    if (msg.tlvCount >= TELEMETRY_MAX_TLVS) return;
    addTLVUint16(&msg, id, value);
}

/* =================================================================================
 * ADD FIELD — ADICIONA CAMPO TLV (UINT8)
 * =================================================================================
 */

void Telemetry::_addField(TLVMessage& msg, uint8_t id, uint8_t value) {
    if (msg.tlvCount >= TELEMETRY_MAX_TLVS) return;
    addTLVUint8(&msg, id, value);
}

/* =================================================================================
 * ADD ATTITUDE FIELDS — ADICIONA CAMPOS DE ATITUDE
 * =================================================================================
 */

void Telemetry::_addAttitudeFields(TLVMessage& msg) {
    const SIData& si = _si->getData();
    _addField(msg, FLD_ROLL, si.gyroX_dps);
    _addField(msg, FLD_PITCH, si.gyroY_dps);
    _addField(msg, FLD_YAW, si.gyroZ_dps);
    
    if (_gps && _gps->isValid()) {
        _addField(msg, FLD_HEADING, _gps->getHeading());
    }
}

/* =================================================================================
 * ADD ACCEL FIELDS — ADICIONA CAMPOS DE ACELERAÇÃO
 * =================================================================================
 */

void Telemetry::_addAccelFields(TLVMessage& msg) {
    const SIData& si = _si->getData();
    _addField(msg, FLD_VX, si.accelX_ms2);
    _addField(msg, FLD_VY, si.accelY_ms2);
    _addField(msg, FLD_VZ, si.accelZ_ms2);
    
    /* Velocidades angulares */
    _addField(msg, FLD_ROLL_RATE, si.gyroX_dps);
    _addField(msg, FLD_PITCH_RATE, si.gyroY_dps);
    _addField(msg, FLD_YAW_RATE, si.gyroZ_dps);
}

/* =================================================================================
 * ADD GYRO FIELDS — ADICIONA CAMPOS DE GIROSCÓPIO
 * =================================================================================
 */

void Telemetry::_addGyroFields(TLVMessage& msg) {
    const SIData& si = _si->getData();
    _addField(msg, FLD_ROLL_RATE, si.gyroX_dps);
    _addField(msg, FLD_PITCH_RATE, si.gyroY_dps);
    _addField(msg, FLD_YAW_RATE, si.gyroZ_dps);
}

/* =================================================================================
 * ADD ALTITUDE FIELDS — ADICIONA CAMPOS DE ALTITUDE E PRESSÃO
 * =================================================================================
 */

void Telemetry::_addAltitudeFields(TLVMessage& msg) {
    const SIData& si = _si->getData();
    _addField(msg, FLD_ALT_FUSED, si.altitude_m);
    _addField(msg, FLD_ALT_BARO, si.pressure_pa / 100.0f); /* Pa → hPa */
}

/* =================================================================================
 * ADD BATTERY FIELDS — ADICIONA CAMPOS DE BATERIA
 * =================================================================================
 */

void Telemetry::_addBatteryFields(TLVMessage& msg) {
    const SIData& si = _si->getData();
    _addField(msg, FLD_BATT_V, si.batteryVoltage_v);
    _addField(msg, FLD_BATT_A, si.batteryCurrent_a);
    _addField(msg, FLD_BATT_W, si.batteryPower_w);
    _addField(msg, FLD_BATT_SOC, si.batteryCharge_ah);
}

/* =================================================================================
 * ADD TEMPERATURE FIELDS — ADICIONA CAMPOS DE TEMPERATURA
 * =================================================================================
 */

void Telemetry::_addTemperatureFields(TLVMessage& msg) {
    const SIData& si = _si->getData();
    _addField(msg, FLD_ESP2_TEMP, si.imuTemp_c);
    
    /* Termopares */
    _addField(msg, FLD_TEMP1, si.thermocoupleTemp_c[0]);
    _addField(msg, FLD_TEMP2, si.thermocoupleTemp_c[1]);
    _addField(msg, FLD_TEMP3, si.thermocoupleTemp_c[2]);
    _addField(msg, FLD_TEMP4, si.thermocoupleTemp_c[3]);
}

/* =================================================================================
 * ADD GPS FIELDS — ADICIONA CAMPOS DE GPS
 * =================================================================================
 */

void Telemetry::_addGPSFields(TLVMessage& msg) {
    if (!_gps || !_gps->isValid()) return;
    
    _addField(msg, FLD_GPS_LAT, (int32_t)(_gps->getLatitude() * 10000000));
    _addField(msg, FLD_GPS_LON, (int32_t)(_gps->getLongitude() * 10000000));
    _addField(msg, FLD_GPS_ALT, (int32_t)(_gps->getAltitude() * 1000));
    _addField(msg, FLD_GPS_SPEED, (uint16_t)(_gps->getSpeed() * 100));
    _addField(msg, FLD_GPS_SATS, (uint8_t)_gps->getSatellites());
    _addField(msg, FLD_GPS_HDOP, (uint16_t)(_gps->getHdop() * 100));
}

/* =================================================================================
 * ADD SYSTEM FIELDS — ADICIONA CAMPOS DE ESTADO DO SISTEMA
 * =================================================================================
 */

void Telemetry::_addSystemFields(TLVMessage& msg) {
    if (_flightControl) {
        _addField(msg, FLD_MODE, (uint8_t)_flightControl->getMode());
    }
    
    if (_failsafe) {
        _addField(msg, FLD_FS_STATE, _failsafe->isActive() ? 1 : 0);
        _addField(msg, FLD_FS_REASON, (uint8_t)_failsafe->getReason());
    }
    
    _addField(msg, FLD_RX_LINK, (uint8_t)100);  /* Placeholder */
    _addField(msg, FLD_TX_LINK, (uint8_t)100);  /* Placeholder */
}

/* =================================================================================
 * ADD FAILSAFE FIELDS — ADICIONA CAMPOS DE FAILSAFE
 * =================================================================================
 */

void Telemetry::_addFailsafeFields(TLVMessage& msg) {
    if (!_failsafe) return;
    
    _addField(msg, FLD_FS_STATE, _failsafe->isActive() ? 1 : 0);
    _addField(msg, FLD_FS_REASON, (uint8_t)_failsafe->getReason());
    _addField(msg, FLD_FS_ACTION, (uint8_t)_failsafe->getLevel());
}

/* =================================================================================
 * BUILD TELEMETRY PACKET — CONSTRÓI PACOTE DE TELEMETRIA COMPLETA
 * =================================================================================
 */

size_t Telemetry::buildTelemetryPacket(uint8_t* buffer, size_t size) {
    if (!_si || !_si->areCriticalValid()) {
        DEBUG_PRINT("buildTelemetryPacket() — Dados SI inválidos\n");
        return 0;
    }
    
    TLVMessage msg;
    msg.startByte = START_BYTE;
    msg.msgID = MSG_TELEMETRY;
    msg.tlvCount = 0;
    msg.checksum = 0;
    
    /* Adicionar campos */
    _addAttitudeFields(msg);
    _addAccelFields(msg);
    _addBatteryFields(msg);
    _addTemperatureFields(msg);
    _addGPSFields(msg);
    _addSystemFields(msg);
    
    if (_mode == TELEMETRY_MODE_DEBUG) {
        _addFailsafeFields(msg);
    }
    
    /* Serializar mensagem */
    size_t len = buildMessage(&msg, MSG_TELEMETRY, buffer, size);
    
    DEBUG_PRINT("buildTelemetryPacket() — %d TLVs, %zu bytes\n", msg.tlvCount, len);
    return len;
}

/* =================================================================================
 * BUILD CRITICAL PACKET — CONSTRÓI PACOTE DE TELEMETRIA REDUZIDA
 * =================================================================================
 */

size_t Telemetry::buildCriticalPacket(uint8_t* buffer, size_t size) {
    if (!_si || !_si->areCriticalValid()) {
        return 0;
    }
    
    TLVMessage msg;
    msg.startByte = START_BYTE;
    msg.msgID = MSG_TELEMETRY;
    msg.tlvCount = 0;
    
    /* Apenas dados críticos */
    const SIData& si = _si->getData();
    _addField(msg, FLD_ROLL, si.gyroX_dps);
    _addField(msg, FLD_PITCH, si.gyroY_dps);
    _addField(msg, FLD_ALT_FUSED, si.altitude_m);
    _addField(msg, FLD_BATT_V, si.batteryVoltage_v);
    
    if (_failsafe) {
        _addField(msg, FLD_FS_STATE, _failsafe->isActive() ? 1 : 0);
    }
    
    return buildMessage(&msg, MSG_TELEMETRY, buffer, size);
}

/* =================================================================================
 * BUILD DEBUG PACKET — CONSTRÓI PACOTE DE TELEMETRIA DEBUG
 * =================================================================================
 */

size_t Telemetry::buildDebugPacket(uint8_t* buffer, size_t size) {
    if (!_si) return 0;
    
    TLVMessage msg;
    msg.startByte = START_BYTE;
    msg.msgID = MSG_DEBUG;
    msg.tlvCount = 0;
    
    /* Todos os dados disponíveis */
    _addAttitudeFields(msg);
    _addAccelFields(msg);
    _addGyroFields(msg);
    _addAltitudeFields(msg);
    _addBatteryFields(msg);
    _addTemperatureFields(msg);
    _addGPSFields(msg);
    _addSystemFields(msg);
    _addFailsafeFields(msg);
    
    /* Adicionar timestamp */
    _addField(msg, 0x90, (uint32_t)millis());
    
    return buildMessage(&msg, MSG_DEBUG, buffer, size);
}

/* =================================================================================
 * SEND — ENVIA TELEMETRIA (COM CONTROLO DE TAXA)
 * =================================================================================
 */

void Telemetry::send() {
    if (!_enabled) return;
    
    uint32_t now = millis();
    uint32_t interval = _getSendInterval();
    
    if (now - _lastSendMs < interval) return;
    _lastSendMs = now;
    
    sendImmediate();
}

/* =================================================================================
 * SEND IMMEDIATE — ENVIA TELEMETRIA IMEDIATAMENTE
 * =================================================================================
 */

void Telemetry::sendImmediate() {
    if (!_enabled || !_lora) return;
    
    size_t len = 0;
    
    switch (_mode) {
        case TELEMETRY_MODE_FAST:
            len = buildCriticalPacket(_buffer, sizeof(_buffer));
            break;
        case TELEMETRY_MODE_DEBUG:
            len = buildDebugPacket(_buffer, sizeof(_buffer));
            break;
        case TELEMETRY_MODE_NORMAL:
        default:
            len = buildTelemetryPacket(_buffer, sizeof(_buffer));
            break;
    }
    
    if (len > 0) {
        _sendToLoRa(_buffer, len);
        _packetsSent++;
        DEBUG_PRINT("sendImmediate() — Pacote enviado (%zu bytes, modo=%s)\n", 
                    len, telemetryModeToString(_mode));
    } else {
        _packetsFailed++;
        DEBUG_PRINT("sendImmediate() — Falha ao construir pacote\n");
    }
}

/* =================================================================================
 * SEND TO ESP1 — ENVIA DADOS SI PARA ESP1 VIA UART
 * =================================================================================
 */

void Telemetry::sendToESP1(const SIData& data) {
    TLVMessage msg;
    msg.startByte = START_BYTE;
    msg.msgID = MSG_SI_DATA;
    msg.tlvCount = 0;
    
    /* Adicionar campos SI */
    _addField(msg, FLD_ROLL, data.gyroX_dps);
    _addField(msg, FLD_PITCH, data.gyroY_dps);
    _addField(msg, FLD_YAW, data.gyroZ_dps);
    _addField(msg, FLD_ALT_FUSED, data.altitude_m);
    _addField(msg, FLD_BATT_V, data.batteryVoltage_v);
    _addField(msg, FLD_BATT_A, data.batteryCurrent_a);
    
    /* Velocidades angulares */
    _addField(msg, FLD_ROLL_RATE, data.gyroX_dps);
    _addField(msg, FLD_PITCH_RATE, data.gyroY_dps);
    _addField(msg, FLD_YAW_RATE, data.gyroZ_dps);
    
    /* Serializar e enviar */
    size_t len = buildMessage(&msg, MSG_SI_DATA, _buffer, sizeof(_buffer));
    if (len > 0) {
        _sendToUART(_buffer, len);
        DEBUG_PRINT("sendToESP1() — Dados SI enviados (%zu bytes)\n", len);
    }
}

/* =================================================================================
 * SEND COMMAND TO ESP1 — ENVIA COMANDO PARA ESP1 VIA UART
 * =================================================================================
 */

void Telemetry::sendCommandToESP1(uint8_t cmdID, float value) {
    TLVMessage msg;
    msg.startByte = START_BYTE;
    msg.msgID = MSG_COMMAND;
    msg.tlvCount = 0;
    
    _addField(msg, cmdID, value);
    
    size_t len = buildMessage(&msg, MSG_COMMAND, _buffer, sizeof(_buffer));
    if (len > 0) {
        _sendToUART(_buffer, len);
        DEBUG_PRINT("sendCommandToESP1() — Comando 0x%02X enviado\n", cmdID);
    }
}

/* =================================================================================
 * SEND TO LORA — ENVIA PACOTE VIA LORA PARA GS
 * =================================================================================
 */

void Telemetry::_sendToLoRa(const uint8_t* data, size_t len) {
    if (_lora) {
        _lora->send(data, len, LORA_PRIORITY_TELEMETRY);
    }
}

/* =================================================================================
 * SEND TO UART — ENVIA PACOTE VIA UART PARA ESP1
 * =================================================================================
 */

void Telemetry::_sendToUART(const uint8_t* data, size_t len) {
    if (_uartCallback) {
        _uartCallback(data, len);
    } else {
        /* Fallback para Serial2 se callback não definido */
        Serial2.write(data, len);
    }
}

/* =================================================================================
 * SET ENABLED — ATIVA/DESATIVA TELEMETRIA
 * =================================================================================
 */

void Telemetry::setEnabled(bool enable) {
    _enabled = enable;
    DEBUG_PRINT("setEnabled() — Telemetria %s\n", enable ? "ATIVADA" : "DESATIVADA");
}

/* =================================================================================
 * IS ENABLED — VERIFICA SE TELEMETRIA ESTÁ ATIVA
 * =================================================================================
 */

bool Telemetry::isEnabled() const {
    return _enabled;
}

/* =================================================================================
 * SET UART CALLBACK — DEFINE CALLBACK PARA ENVIO VIA UART
 * =================================================================================
 */

void Telemetry::setUARTCallback(void (*callback)(const uint8_t*, size_t)) {
    _uartCallback = callback;
    DEBUG_PRINT("setUARTCallback() — Callback registado\n");
}

/* =================================================================================
 * GET PACKETS SENT — RETORNA NÚMERO DE PACOTES ENVIADOS
 * =================================================================================
 */

uint32_t Telemetry::getPacketsSent() const {
    return _packetsSent;
}

/* =================================================================================
 * RESET STATS — RESETA ESTATÍSTICAS
 * =================================================================================
 */

void Telemetry::resetStats() {
    _packetsSent = 0;
    _packetsFailed = 0;
    DEBUG_PRINT("resetStats() — Estatísticas resetadas\n");
}

/* =================================================================================
 * GET STATUS STRING — OBTÉM STRING COM ESTADO DO MÓDULO
 * =================================================================================
 */

void Telemetry::getStatusString(char* buffer, size_t bufferSize) const {
    if (!buffer || bufferSize == 0) return;
    
    snprintf(buffer, bufferSize,
             "Telemetry: mode=%s enabled=%s sent=%lu failed=%lu",
             telemetryModeToString(_mode),
             _enabled ? "Y" : "N",
             _packetsSent, _packetsFailed);
}

/* =================================================================================
 * SET DEBUG — ATIVA/DESATIVA DEBUG
 * =================================================================================
 */

void Telemetry::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}
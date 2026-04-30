/**
 * =================================================================================
 * BEACON.CPP — IMPLEMENTAÇÃO DO SINAL DE IDENTIFICAÇÃO UAS
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-27
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. Wi-Fi em modo Access Point (AP) — rede aberta, sem password
 * 2. Transmissão via UDP broadcast na porta 14550
 * 3. Formato JSON legível por humanos
 * 4. Frequência de 1 Hz conforme requisitos legais
 * 
 * =================================================================================
 */

#include "Beacon.h"

/* =================================================================================
 * =================================================================================
 *                    CONFIGURAÇÃO MANUAL — ALTERE AQUI OS SEUS DADOS
 * =================================================================================
 * =================================================================================
 * 
 * ╔═══════════════════════════════════════════════════════════════════════════════╗
 * ║  ATENÇÃO: Estes são os ÚNICOS valores que precisas de alterar neste ficheiro. ║
 * ║  Altera conforme os teus dados de piloto e aeronave.                          ║
 * ║  Depois de alterar, compila e carrega novamente no ESP2.                      ║
 * ╚═══════════════════════════════════════════════════════════════════════════════╝
 * 
 * =================================================================================
 */

/* --------------------------------------------------------------------------------
 * PILOT_ID — Identificação do piloto
 * --------------------------------------------------------------------------------
 * Deve corresponder ao número de registo/licença emitido pela autoridade nacional.
 * Exemplos: "PT-12345" (Portugal), "FR-67890" (França), "ES-ABCDE" (Espanha)
 */
static const char* PILOT_ID = "FRA-RP-000000119892";

/* --------------------------------------------------------------------------------
 * UAS_SERIAL — Número de série da aeronave
 * --------------------------------------------------------------------------------
 * Número único que identifica a aeronave. Deve estar gravado no exterior do drone.
 * Exemplo: "BFLY001"
 */
static const char* UAS_SERIAL = "BEACONFLY";

/* --------------------------------------------------------------------------------
 * UAS_MODEL — Modelo da aeronave
 * --------------------------------------------------------------------------------
 * Nome comercial ou modelo da aeronave.
 * Exemplo: "BeaconFly v1.0"
 */
static const char* UAS_MODEL = "BeaconFly";

/* --------------------------------------------------------------------------------
 * SESSION_ID — ID da sessão de voo
 * --------------------------------------------------------------------------------
 * Número que identifica a sessão de voo atual. Incrementa manualmente a cada voo.
 * Range: 1 a 999
 */
static const uint16_t SESSION_ID = 1;

/* =================================================================================
 * FIM DA CONFIGURAÇÃO MANUAL — NÃO ALTERAR NADA ABAIXO DESTA LINHA
 * =================================================================================
 */

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

Beacon::Beacon()
    : _sessionId(SESSION_ID)
    , _latitude(0.0)
    , _longitude(0.0)
    , _altitude(0.0f)
    , _speedHorizontal(0.0f)
    , _speedVertical(0.0f)
    , _heading(0.0f)
    , _timestamp(0)
    , _active(false)
    , _lastBroadcastMs(0)
{
    memset(_pilotId, 0, sizeof(_pilotId));
    memset(_uasSerial, 0, sizeof(_uasSerial));
    memset(_uasModel, 0, sizeof(_uasModel));
    memset(_ssid, 0, sizeof(_ssid));
    
    strncpy(_pilotId, PILOT_ID, sizeof(_pilotId) - 1);
    strncpy(_uasSerial, UAS_SERIAL, sizeof(_uasSerial) - 1);
    strncpy(_uasModel, UAS_MODEL, sizeof(_uasModel) - 1);
}

/* =================================================================================
 * GENERATE SSID
 * =================================================================================
 */

void Beacon::_generateSSID() {
    snprintf(_ssid, sizeof(_ssid), "%s%s_%03d", BEACON_SSID_PREFIX, _uasSerial, _sessionId);
}

/* =================================================================================
 * UPDATE TIMESTAMP
 * =================================================================================
 */

void Beacon::_updateTimestamp() {
    /* TODO: Integrar com NTP quando disponível */
    _timestamp = millis() / 1000;
}

/* =================================================================================
 * BUILD JSON PACKET
 * =================================================================================
 */

void Beacon::_buildJsonPacket(char* buffer, size_t size) {
    _updateTimestamp();
    
    snprintf(buffer, size,
        "{\"pilot\":\"%s\",\"serial\":\"%s\",\"model\":\"%s\",\"session\":%d,"
        "\"timestamp\":%lu,"
        "\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,"
        "\"spd_h\":%.1f,\"spd_v\":%.1f,\"hdg\":%.0f}",
        _pilotId, _uasSerial, _uasModel, _sessionId,
        (unsigned long)_timestamp,
        _latitude, _longitude, _altitude,
        _speedHorizontal, _speedVertical, _heading);
}

/* =================================================================================
 * SEND UDP
 * =================================================================================
 */

void Beacon::_sendUDP(const char* packet) {
    IPAddress broadcastIp = ~WiFi.subnetMask() | WiFi.gatewayIP();
    _udp.beginPacket(broadcastIp, BEACON_UDP_PORT);
    _udp.write((const uint8_t*)packet, strlen(packet));
    _udp.endPacket();
}

/* =================================================================================
 * BEGIN
 * =================================================================================
 */

bool Beacon::begin() {
    _generateSSID();
    
    if (!WiFi.softAP(_ssid, NULL)) {
        return false;
    }
    
    _udp.begin(BEACON_UDP_PORT);
    _active = true;
    
    return true;
}

/* =================================================================================
 * UPDATE
 * =================================================================================
 */

void Beacon::update(double lat, double lon, float alt, float speedH, float speedV, float heading) {
    _latitude = lat;
    _longitude = lon;
    _altitude = alt;
    _speedHorizontal = speedH;
    _speedVertical = speedV;
    _heading = heading;
}

/* =================================================================================
 * LOOP
 * =================================================================================
 */

void Beacon::loop() {
    if (!_active) return;
    
    uint32_t now = millis();
    if (now - _lastBroadcastMs < BEACON_INTERVAL_MS) return;
    _lastBroadcastMs = now;
    
    char packet[BEACON_BUFFER_SIZE];
    _buildJsonPacket(packet, sizeof(packet));
    _sendUDP(packet);
}

/* =================================================================================
 * GETTERS
 * =================================================================================
 */

bool Beacon::isActive() const {
    return _active;
}

const char* Beacon::getSSID() const {
    return _ssid;
}

const char* Beacon::getPilotId() const {
    return _pilotId;
}

const char* Beacon::getSerial() const {
    return _uasSerial;
}

const char* Beacon::getModel() const {
    return _uasModel;
}

uint16_t Beacon::getSessionId() const {
    return _sessionId;
}
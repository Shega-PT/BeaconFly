/**
 * =================================================================================
 * BEACON.H — SINAL DE IDENTIFICAÇÃO UAS (BEACON Wi-Fi)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-27
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * REQUISITOS LEGAIS (EASA / DGAC / ANAC)
 * =================================================================================
 * 
 * Este módulo implementa o sistema de identificação remota (Remote ID) obrigatório
 * para operações de UAS na União Europeia, conforme regulamentos:
 * 
 *   • EASA (European Union Aviation Safety Agency) — Implementing Regulation (EU)
 *     2019/945 e 2019/947
 *   • DGAC (France) — Arrêté du 17 décembre 2015
 *   • ANAC (Portugal) — Regulamento da Autoridade Nacional de Aviação Civil
 * 
 * =================================================================================
 * DADOS TRANSMITIDOS (OBRIGATÓRIOS POR LEI)
 * =================================================================================
 * 
 *   • ID do piloto (número de registo/licença)
 *   • Número de série da aeronave
 *   • Modelo da aeronave
 *   • ID da sessão de voo
 *   • Posição atual (latitude, longitude, altitude)
 *   • Velocidade (horizontal e vertical)
 *   • Rumo (heading)
 * 
 * =================================================================================
 * TECNOLOGIA DE TRANSMISSÃO
 * =================================================================================
 * 
 *   • Wi-Fi (2.4 GHz) — modo Access Point
 *   • Rede ABERTA (sem password) para inspeção
 *   • Formato JSON
 *   • UDP broadcast na porta 14550
 * 
 * =================================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

/* =================================================================================
 * CONSTANTES DE CONFIGURAÇÃO
 * =================================================================================
 */

#define BEACON_UDP_PORT         14550
#define BEACON_INTERVAL_MS      1000
#define BEACON_BUFFER_SIZE      512
#define BEACON_SSID_PREFIX      "UAS_"

/* =================================================================================
 * CLASSE BEACON
 * =================================================================================
 */

class Beacon {
public:
    Beacon();
    bool begin();
    void update(double lat, double lon, float alt, float speedH, float speedV, float heading);
    void loop();
    bool isActive() const;
    const char* getSSID() const;
    const char* getPilotId() const;
    const char* getSerial() const;
    const char* getModel() const;
    uint16_t getSessionId() const;

private:
    char _pilotId[32];
    char _uasSerial[16];
    char _uasModel[32];
    uint16_t _sessionId;
    char _ssid[48];
    
    double _latitude;
    double _longitude;
    float _altitude;
    float _speedHorizontal;
    float _speedVertical;
    float _heading;
    uint32_t _timestamp;
    
    bool _active;
    uint32_t _lastBroadcastMs;
    WiFiUDP _udp;
    
    void _generateSSID();
    void _buildJsonPacket(char* buffer, size_t size);
    void _sendUDP(const char* packet);
    void _updateTimestamp();
};
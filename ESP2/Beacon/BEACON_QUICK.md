# Guia Rápido – Beacon

> **Versão:** 1.0.0

---

## 1. Configurar os Teus Dados

No `Beacon.cpp`, altera estas 4 linhas:

```cpp
static const char* PILOT_ID   = "PT-12345";
static const char* UAS_SERIAL = "BFLY001";
static const char* UAS_MODEL  = "BeaconFly v1.0";
static const uint16_t SESSION_ID = 1;
´´´

2. Rede Wi-Fi

Parâmetro   Valor
SSID        UAS_BFLY001_001 (ou conforme configurado)
Password    (nenhuma - rede aberta)
Porta UDP   14550

3. Formato do Pacote

```json
{
  "pilot": "PT-12345",
  "serial": "BFLY001",
  "model": "BeaconFly v1.0",
  "session": 1,
  "timestamp": 1713987654,
  "lat": 41.149612,
  "lon": -8.610912,
  "alt": 50.2,
  "spd_h": 12.5,
  "spd_v": 0.5,
  "hdg": 145
}
´´´

Fim do Guia Rápido – Beacon v1.0.0

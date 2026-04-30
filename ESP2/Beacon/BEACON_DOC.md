# Beacon – Pasta `Beacon/`

> **Versão:** 1.0.0
> **Data:** 2026-04-27
> **Autor:** BeaconFly UAS Team

---

## 1. Objetivo do Módulo

O módulo `Beacon` implementa o sistema de identificação remota (Remote ID) obrigatório para operações de UAS na União Europeia.

**Princípio fundamental:** Qualquer inspector com um telemóvel pode ligar-se à rede Wi-Fi aberta do drone e ler a identificação do piloto e os dados de voo.

---

## 2. Como Configurar os Teus Dados

No ficheiro `Beacon.cpp`, localiza as seguintes linhas no início do ficheiro:

```cpp
static const char* PILOT_ID   = "PT-12345";       // ← Altera aqui
static const char* UAS_SERIAL = "BFLY001";        // ← Altera aqui
static const char* UAS_MODEL  = "BeaconFly v1.0"; // ← Altera aqui
static const uint16_t SESSION_ID = 1;             // ← Altera aqui
Altera os valores conforme os teus dados, compila e carrega no ESP2.
´´´

3. Interface Pública

Método                                          Descrição
begin()                                         Inicializa o beacon (Wi-Fi AP, UDP)
update(lat, lon, alt, speedH, speedV, heading)  Atualiza os dados de voo
loop()                                          Transmite o beacon (chamar no loop principal)
isActive()                                      Verifica se o beacon está ativo
getSSID()                                       Retorna o SSID da rede
getPilotId()                                    Retorna o ID do piloto
getSerial()                                     Retorna o número de série
getModel()                                      Retorna o modelo
getSessionId()                                  Retorna o ID da sessão

4. Exemplo de Integração

```cpp
#include "Beacon.h"

Beacon beacon;

void setup() {
    beacon.begin();
}

void loop() {
    beacon.update(latitude, longitude, altitude, speedH, speedV, heading);
    beacon.loop();
}
´´´

Fim da documentação – Beacon v1.0.0

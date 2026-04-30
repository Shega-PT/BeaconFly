# FlightControl – Pasta `FlightControl/`

> **Versão:** 2.0.0  
> **Data:** 2026-04-17  
> **Autor:** BeaconFly UAS Team

Esta documentação detalha a **implementação do controlador de voo** do sistema BeaconFly. Abrange os 2 arquivos da pasta: `FlightControl.h` e `FlightControl.cpp`, explicando a lógica de controlo, modos de voo, PIDs, navegação e integração com os restantes módulos.

---

## 1. Objetivo do Módulo

O módulo `FlightControl` é o **cérebro do sistema BeaconFly**. Responsável por:

- **Estabilização da aeronave** – PIDs para Roll, Pitch, Yaw e Altitude
- **Processamento de comandos** – Recebidos da Ground Station via ESP1
- **Fusão de sensores** – Combinação de GPS + Barómetro para altitude estável
- **Navegação autónoma** – Waypoints, RTL (Return To Launch)
- **Modo híbrido de testes** – `DEBUG_AUTO_MANUAL` com override manual e timeout
- **Geração de sinais** – Saída em microssegundos para o módulo `Actuators`

O FlightControl opera num loop **determinístico de 50-100Hz** e recebe dados em unidades SI do ESP2 via protocolo TLV.

---

## 2. Arquitetura do Controlador

### 2.1 Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ ESP1                                                                            │
│ (Sensores: IMU, Barómetro, GPS, Bateria, Termopares)                            │
│ │                                                                               │
│ ▼ (UART - RAW)                                                                  │
│ │                                                                               │
│ ESP2                                                                            │
│ │                                                                               │
│ ▼ (UART - Dados SI)                                                             │
│ updateState(TLVMessage)                                                         │
│ │                                                                               │
│ ▼                                                                               │
│ ┌─────────────────────────────────────────────────────────────────────────────┐ │
│ │ FLIGHTCONTROL                                                               │ │
│ │ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐             │ │
│ │ │ PID         │ │ MIXER       │ │ SLEW        │ │ TRIM        │             │ │
│ │ │ Roll        │ │ Elevons     │ │ RATE        │ │ (Offsets)   │             │ │
│ │ │ Pitch       │ │ (Asa Delta) │ │ Servo/Motor │ │             │             │ │
│ │ │ Yaw         │ │             │ │             │ │             │             │ │
│ │ │ Altitude    │ │             │ │             │ │             │             │ │
│ │ └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘             │ │
│ └─────────────────────────────────────────────────────────────────────────────┘ │
│ │                                                                               │
│ ▼ (ActuatorSignals em µs)                                                       │
│ Actuators::update()                                                             │
│ │                                                                               │
│ ▼                                                                               │
│ ┌─────────────────────────────────────────────────────────────────────────────┐ │
│ │ ATUADORES                                                                   │ │
│ │ • 5 Servos (wingR, wingL, rudder, elevonR, elevonL)                         │ │
│ │ • 1 Motor (ESC)                                                             │ │
│ └─────────────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────────┘

### 2.2 Ciclo de Controlo (update())

```cpp
void FlightControl::update() {
    // 1. Calcular dt (tempo desde o último ciclo)
    float dt = (now - _lastUpdateMs) / 1000.0f;
    
    // 2. Verificar se dados do ESP2 estão atualizados (<500ms)
    if ((now - _state.timestamp) > FC_DATA_STALE_MS) return;
    
    // 3. Executar lógica conforme o modo de voo atual
    switch (_mode) {
        case MODE_MANUAL:             _updateManual();           break;
        case MODE_STABILIZE:          _updateStabilize(dt);      break;
        case MODE_ALT_HOLD:           _updateAltHold(dt);        break;
        case MODE_POSHOLD:            _updatePosHold(dt);        break;
        case MODE_AUTO:               _updateAuto(dt);           break;
        case MODE_RTL:                _updateRTL(dt);            break;
        case MODE_DEBUG_AUTO_MANUAL:  _updateDebugAutoManual(dt); break;
    }
}
´´´

3. Modos de Voo

3.1 Tabela de Modos

Modo                    Valor   Descrição                               Utilização
MODE_MANUAL             0       Passthrough direto dos comandos da GS   Testes manuais, calibração
MODE_STABILIZE          1       Estabilização angular (Roll/Pitch/Yaw)  Voo normal assistido
MODE_ALT_HOLD           2       Estabilização + manutenção de altitude  Voo com altitude constante
MODE_POSHOLD            3       Manutenção de posição GPS               Voo circular
MODE_AUTO               4       Navegação autónoma por waypoints        Missões pré-programadas
MODE_RTL                5       Return To Launch                        Emergência / fim de missão
MODE_DEBUG_AUTO_MANUAL  6       Híbrido para testes                     Desenvolvimento e validação

3.2 Descrição Detalhada dos Modos

MODE_MANUAL
- Comportamento: Os comandos da GS vão diretamente para os atuadores
- Sem PID: Nenhuma estabilização aplicada
- Útil para: Testes de hardware, calibração de servos, voo acrobático

MODE_STABILIZE
- Comportamento: PID sobre ângulos de Roll, Pitch e Yaw
- Setpoints: Ângulos desejados em graus
- Saída: Correções aplicadas aos elevons e leme

MODE_ALT_HOLD
- Comportamento: STABILIZE + PID de altitude
- Fusão: GPS + Barómetro (filtro complementar)
- Throttle: Base de 30% + correção do PID

MODE_POSHOLD
- Comportamento: ALT_HOLD + controlo de posição GPS
- Requisitos: GPS válido e velocidade válida
- Estado: Em desenvolvimento (atualmente fallback para ALT_HOLD)

MODE_AUTO
- Comportamento: Navegação autónoma por waypoints
- Navegação: Bearing → Roll error → PID Roll
- Transição: Avança para próximo waypoint quando distância < tolerância

MODE_RTL
- Comportamento: Regressar ao ponto de descolagem (0,0)
- Aterragem: Quando altitude < 2m, reduz throttle e desarma
- Segurança: Fallback para ALT_HOLD se GPS inválido

MODE_DEBUG_AUTO_MANUAL (HÍBRIDO)
- Comportamento base: Autónomo (segue trajeto pré-definido)
- Override manual: Ao receber comando, entra em modo STABILIZE
- Timeout: Após X segundos sem comandos, retorna ao waypoint mais próximo
- Shell expandido: Quase todos os comandos permitidos (exceto críticos em voo)

4. PID Controller

4.1 Estrutura PIDController

```cpp
struct PIDController {
    float Kp, Ki, Kd;           // Ganhos
    float integral;             // Termo integral acumulado
    float lastError;            // Erro anterior (para D)
    float filteredD;            // Termo D filtrado (LPF)
    float iMax, iMin;           // Anti-windup
    float lpfAlpha;             // Coeficiente do filtro (0=mais filtrado)
};
´´´

4.2 Implementação do PID

```cpp
float _runPID(PIDController& pid, float error, float dt, float limit) {
    float P = pid.Kp * error;                         // Proporcional
    
    pid.integral += pid.Ki * error * dt;              // Integral
    pid.integral = constrain(pid.integral, pid.iMin, pid.iMax);
    float I = pid.integral;
    
    float rawD = (error - pid.lastError) / dt;        // Derivativo
    pid.filteredD = pid.lpfAlpha * rawD + (1 - pid.lpfAlpha) * pid.filteredD;
    float D = pid.Kd * pid.filteredD;
    
    pid.lastError = error;
    
    return constrain(P + I + D, -limit, limit);
}
´´´

4.3 Ganhos Padrão

Eixo        Kp      Ki      Kd      Limite (µs)     lpfAlpha
Roll        1.25    0.06    0.28    400             0.72
Pitch       1.10    0.05    0.25    400             0.72
Yaw         0.85    0.07    0.12    350             0.55
Altitude    1.80    0.025   0.45    600             0.35

5. Fusão de Altitude

5.1 Problema

Barómetro: Estável a curto prazo, mas deriva com temperatura/pressão
GPS: Sem deriva, mas tem ruído e pode perder sinal

5.2 Solução – Filtro Complementar
```cpp
float _fuseAltitude(float gps, float baro) {
    static float fused = 0.0f;
    static bool initialized = false;
    
    if (!initialized) {
        fused = baro;
        initialized = true;
    }
    
    const float alpha = 0.02f;  // 98% baro, 2% GPS
    fused = (1.0f - alpha) * fused + alpha * gps;
    
    return fused;
}
Efeito: Altitude estável, sem deriva, com correção lenta do GPS.
´´´

6. Mixer de Elevons (Asa Delta)

6.1 Fórmula

Para uma asa delta (flying wing), os elevons combinam pitch e roll:

elevonR = neutro + pitchCorr + rollCorr
elevonL = neutro + pitchCorr - rollCorr

6.2 Implementação

```cpp
void _mixElevons(float pitchCorr, float rollCorr, uint16_t& elevonR, uint16_t& elevonL) {
    float r = FC_SERVO_NEUTRAL_US + pitchCorr + rollCorr;
    float l = FC_SERVO_NEUTRAL_US + pitchCorr - rollCorr;
    
    elevonR = constrain(r, FC_SERVO_MIN_US, FC_SERVO_MAX_US);
    elevonL = constrain(l, FC_SERVO_MIN_US, FC_SERVO_MAX_US);
}
´´´

6.3 Exemplo

Pitch   Roll    Elevon R    Elevon L    Efeito
+100    0       1600        1600        Subir (ambos sobem)
0       +100    1600        1400        Virar à direita
+100    +100    1700        1500        Subir e virar à direita

7. Navegação Autónoma

7.1 Waypoints

```cpp
struct Waypoint {
    float latitude;      // Graus (WGS84)
    float longitude;     // Graus (WGS84)
    float altitude;      // Metros
    float heading;       // Graus (0-360)
    float tolerance;     // Metros — distância para considerar "chegou"
};
´´´

7.2 Algoritmo de Navegação

```cpp
bool _navigateToWaypoint(const Waypoint& target, float dt) {
    // 1. Calcular distância e bearing
    float distance = _calculateDistance(lat, lon, target.lat, target.lon);
    float bearing = _calculateBearing(lat, lon, target.lat, target.lon);
    
    // 2. Verificar se chegou
    if (distance < target.tolerance) return true;
    
    // 3. Converter bearing para erro de roll
    float rollError = _bearingToRollError(bearing, heading);
    
    // 4. Aplicar setpoints
    _setpoints.roll = rollError;
    _setpoints.pitch = 5.0f;
    _setpoints.altitude = target.altitude;
    
    return false;
}
´´´

7.3 Fórmulas de Navegação

Distância (Haversine):

a = sin²(Δlat/2) + cos(lat1) · cos(lat2) · sin²(Δlon/2)
c = 2 · atan2(√a, √(1-a))
distância = R · c  (R = 6371000 m)

Bearing:

θ = atan2(sin(Δlon) · cos(lat2),
          cos(lat1) · sin(lat2) - sin(lat1) · cos(lat2) · cos(Δlon))

8. Modo DEBUG_AUTO_MANUAL (Detalhado)

8.1 Conceito

Este modo foi concebido especificamente para testes em campo, permitindo:
- Comportamento autónomo – Segue um trajeto pré-definido
- Override manual – Ao receber comandos (joystick/RC), sobrepõe-se
- Timeout manual – Após X segundos sem comandos, regressa ao modo autónomo
- Retoma do waypoint mais próximo – Não volta ao início da trajetória

8.2 Fluxo de Estados

                    ┌─────────────────────────────────────┐
                    │      MODO AUTÓNOMO ATIVO            │
                    │  (segue trajeto, waypoint mais      │
                    │   próximo como referência)          │
                    └─────────────────────────────────────┘
                                      │
                                      ▼ (comando manual)
                    ┌─────────────────────────────────────┐
                    │      OVERRIDE MANUAL ATIVO          │
                    │  (responde a joystick/RC,           │
                    │   estabiliza como STABILIZE)        │
                    │   Timer de timeout a correr         │
                    └─────────────────────────────────────┘
                                      │
                                      ▼ (timeout expirado)
                    ┌─────────────────────────────────────┐
                    │  CALCULAR WAYPOINT MAIS PRÓXIMO     │
                    │  Retomar navegação a partir desse   │
                    │  ponto (não volta ao início)        │
                    └─────────────────────────────────────┘
                                      │
                                      ▼
                        (volta ao estado inicial)

8.3 Implementação

```cpp
void _updateDebugAutoManual(float dt) {
    // Verificar timeout do override manual
    if (_manualOverrideActive && timeoutExpirado()) {
        _manualOverrideActive = false;
        
        // Encontrar waypoint mais próximo da posição atual
        uint8_t nearest = _findNearestWaypoint(_debugTrajectory, _debugTrajectoryCount);
        _debugCurrentPoint = nearest;
    }
    
    if (_manualOverrideActive) {
        // Comportamento STABILIZE (manual)
        _updateStabilize(dt);
    } else {
        // Comportamento autónomo
        _navigateToWaypoint(_debugTrajectory[_debugCurrentPoint], dt);
    }
}
´´´

8.4 Funções Específicas

Função                                  Descrição
setDebugTrajectory(waypoints, count)    Define o trajeto para testes
registerManualCommand()                 Regista comando manual (reset do timeout)
getDebugManualTimeoutRemaining()        Tempo restante no override
isManualOverrideActive()                Verifica se override está ativo
setDebugManualTimeout(ms)               Configura timeout (padrão: 3000ms)

9. Segurança

9.1 Proteções Implementadas

Mecanismo               Descrição
Data stale              Se dados do ESP2 >500ms, mantém outputs anteriores
Constrain               Todos os setpoints são limitados a valores seguros
Anti-windup             Integradores limitados (iMax/iMin)
Slew rate               Evita movimentos bruscos dos servos
Timeout manual          Retorna a autónomo após X segundos sem comandos
Verificação em solo     ARM/DISARM só permitido em solo

9.2 Comandos Bloqueados em Voo

Comando                 Permitido em solo      Permitido em voo
CMD_ARM                 ✅                      ❌
CMD_DISARM              ✅                      ❌
CMD_REBOOT              ✅                      ❌
CMD_SHUTDOWN            ✅                      ❌
CMD_SET_PARAM (crítico) ✅                      ❌
CMD_SET_MODE            ✅                      ✅
CMD_SET_ROLL/PITCH/YAW  ✅                      ✅

9.3 Função de Segurança

```cpp
bool _isShellCommandSafe(uint8_t commandID) const {
    bool inFlight = !_isOnGround();
    
    switch (commandID) {
        case CMD_ARM:
        case CMD_DISARM:
        case CMD_REBOOT:
        case CMD_SHUTDOWN:
        case CMD_SET_PARAM:
            return !inFlight;  // Apenas em solo
        default:
            return true;       // Permitido sempre
    }
}
´´´

10. Slew Rate (Suavização)

10.1 Conceito

O slew rate limita a variação máxima de um sinal por ciclo de atualização, evitando movimentos bruscos que podem danificar servos ou destabilizar a aeronave.

10.2 Constantes
Atuador     Slew Rate (µs/ciclo)    Efeito
Servos      30                      ~1500 µs/s (a 50Hz)
Motor       20                      ~1000 µs/s

10.3 Implementação

```cpp
uint16_t _applySlewRate(uint16_t current, uint16_t target, uint16_t maxDelta) {
    if (target > current) {
        return (target - current <= maxDelta) ? target : current + maxDelta;
    }
    if (target < current) {
        return (current - target <= maxDelta) ? target : current - maxDelta;
    }
    return current;
}
´´´

11. Trim (Offsets)

11.1 Estrutura

```cpp
struct TrimValues {
    int16_t wingR   = 0;    // Asa direita
    int16_t wingL   = 0;    // Asa esquerda
    int16_t rudder  = 0;    // Leme
    int16_t elevonR = 0;    // Elevon direito
    int16_t elevonL = 0;    // Elevon esquerdo
    int16_t motor   = 0;    // Motor
};
´´´

11.2 Aplicação

```cpp
uint16_t _applyTrim(uint16_t value, int16_t trim, uint16_t minUs, uint16_t maxUs) {
    int32_t trimmed = (int32_t)value + trim;
    return (uint16_t)constrain(trimmed, (int32_t)minUs, (int32_t)maxUs);
}
´´´

Utilização: Compensar desvios mecânicos (ex: asa ligeiramente desalinhada).

12. API Pública

12.1 Inicialização

```cpp
FlightControl fc;

void setup() {
    fc.begin();
}
´´´

12.2 Ciclo Principal (100Hz)

```cpp
void loop() {
    fc.update();  // Chamar a 100Hz
    delay(10);
}
´´´

12.3 Receção de Dados do ESP2

```cpp
void onDataFromESP2(const TLVMessage& msg) {
    fc.updateState(msg);
}
´´´

12.4 Processamento de Comandos

```cpp
void onCommandFromGS(const TLVMessage& msg) {
    fc.processCommand(msg);
}
´´´

12.5 Configuração de Waypoints

```cpp
Waypoint waypoints[] = {
    {41.1496, -8.6109, 100, 0, 10},  // Porto
    {41.1578, -8.6291, 120, 90, 10}, // Matosinhos
    {41.1450, -8.6500, 80, 180, 10}  // Leça
};

fc.setWaypoints(waypoints, 3);
fc.setMode(MODE_AUTO);
´´´

12.6 Modo DEBUG_AUTO_MANUAL

```cpp
// Definir trajeto para testes
Waypoint testTraj[] = {
    {41.1496, -8.6109, 50, 0, 10},
    {41.1520, -8.6150, 60, 45, 10},
    {41.1550, -8.6120, 55, 90, 10}
};

fc.setDebugTrajectory(testTraj, 3);
fc.setDebugManualTimeout(5000);  // 5 segundos de timeout
fc.setMode(MODE_DEBUG_AUTO_MANUAL);

// No loop, quando receber comando manual:
fc.registerManualCommand();  // Reinicia timeout
´´´

12.7 Ajuste de PIDs

```cpp
fc.setPIDGains(0, 1.5f, 0.08f, 0.35f);  // Roll
fc.setPIDGains(1, 1.3f, 0.06f, 0.30f);  // Pitch
fc.setPIDGains(2, 1.0f, 0.10f, 0.15f);  // Yaw
fc.setPIDGains(3, 2.0f, 0.03f, 0.50f);  // Altitude
´´´

12.8 Consultas de Estado

```cpp
FlightMode mode = fc.getMode();
const FlightState& state = fc.getState();
const ActuatorSignals& outputs = fc.getOutputs();
bool overrideActive = fc.isManualOverrideActive();
uint32_t timeoutRemaining = fc.getDebugManualTimeoutRemaining();
´´´

13. Constantes e Limites

Constante                       Valor   Descrição
FC_SERVO_NEUTRAL_US             1500    Neutro dos servos (µs)
FC_SERVO_MIN_US                 1000    Mínimo seguro
FC_SERVO_MAX_US                 2000    Máximo seguro
FC_MOTOR_MIN_US                 1100    Mínimo motor (potência zero)
FC_MOTOR_MAX_US                 1900    Máximo motor
MAX_ROLL_ANGLE                  45°     Ângulo máximo de rolamento
MAX_PITCH_ANGLE                 35°     Ângulo máximo de inclinação
FC_DATA_STALE_MS                500ms   Timeout dados ESP2
FC_DEBUG_MANUAL_TIMEOUT_MS      3000ms  Timeout override manual
FC_GROUND_ALTITUDE_THRESHOLD    0.5m    Altitude para considerar "em solo"

14. Debug

14.1 Ativação

```cpp
fc.setDebug(true);
´´´

14.2 Mensagens Típicas

[FlightControl] begin() — Configurando PID e estado inicial...
[FlightControl] FlightControl inicializado com sucesso (Modo: STABILIZE)
[FlightControl] Mudança de modo: 1 → 6
[FlightControl] setDebugTrajectory: 3 waypoints carregados
[FlightControl] registerManualCommand: OVERRIDE MANUAL ATIVADO
[FlightControl] _updateDebugAutoManual: OVERRIDE MANUAL — timeout em 2500 ms
[FlightControl] _updateDebugAutoManual: Timeout expirado → retomando no waypoint 1 (mais próximo)
[FlightControl] _updateAuto: Chegou ao waypoint 0, próximo: 1
[FlightControl] _updateAuto: MISSÃO COMPLETA!

15. Exemplo Completo

```cpp
#include "FlightControl.h"
#include "Actuators.h"

FlightControl fc;
Actuators actuators;

void setup() {
    Serial.begin(115200);
    
    actuators.begin();
    fc.begin();
    fc.setDebug(true);
    
    // Definir trajeto para modo DEBUG_AUTO_MANUAL
    Waypoint testTraj[] = {
        {41.1496, -8.6109, 50, 0, 10},
        {41.1520, -8.6150, 60, 45, 10},
        {41.1550, -8.6120, 55, 90, 10}
    };
    fc.setDebugTrajectory(testTraj, 3);
    fc.setMode(MODE_DEBUG_AUTO_MANUAL);
    
    // Armar atuadores
    actuators.arm();
}

void loop() {
    static uint32_t lastUpdate = 0;
    uint32_t now = millis();
    
    if (now - lastUpdate >= 10) {  // 100Hz
        fc.update();
        actuators.update(fc.getOutputs());
        lastUpdate = now;
    }
    
    // Simular receção de dados do ESP2
    if (Serial.available()) {
        // Receber TLVMessage e chamar fc.updateState()
    }
    
    // Simular receção de comandos da GS
    if (/* comando recebido */) {
        fc.registerManualCommand();  // Para modo DEBUG_AUTO_MANUAL
        // fc.processCommand(msg);
    }
}
´´´

16. Observações Finais

- O FlightControl é o módulo central do sistema – nenhum outro módulo deve calcular PIDs ou gerar sinais para atuadores.
- O update() deve ser chamado a 50-100Hz para garantir estabilidade.
- O updateState() deve ser chamado sempre que novos dados do ESP2 chegam.
- O processCommand() processa comandos da Ground Station.
- O modo DEBUG_AUTO_MANUAL é ideal para testes em campo, permitindo alternar entre autónomo e manual sem perder a referência da missão.

Fim da documentação – FlightControl BeaconFly v2.0.0

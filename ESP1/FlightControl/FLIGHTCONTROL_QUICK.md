# Guia Rápido – FlightControl BeaconFly

> **Versão:** 2.0.0  
> **Uso:** Referência rápida para debug, manutenção e desenvolvimento

---

## 1. Modos de Voo

| Modo                      | Valor | Descrição                 |
| --------------------------|-------|---------------------------|
| `MODE_MANUAL`             | 0     | Passthrough direto        |
| `MODE_STABILIZE`          | 1     | Estabilização angular     |
| `MODE_ALT_HOLD`           | 2     | Estabilização + altitude  |
| `MODE_POSHOLD`            | 3     | Manutenção de posição GPS |
| `MODE_AUTO`               | 4     | Navegação por waypoints   |
| `MODE_RTL`                | 5     | Return To Launch          |
| `MODE_DEBUG_AUTO_MANUAL`  | 6     | Híbrido para testes       |

---

## 2. Constantes Rápidas

| Constante                     | Valor  | Descrição               |
| ------------------------------|--------|-------------------------|
| `FC_SERVO_NEUTRAL_US`         | 1500   | Neutro dos servos       |
| `FC_SERVO_MIN_US`             | 1000   | Mínimo servo            |
| `FC_SERVO_MAX_US`             | 2000   | Máximo servo            |
| `FC_MOTOR_MIN_US`             | 1100   | Mínimo motor            |
| `FC_MOTOR_MAX_US`             | 1900   | Máximo motor            |
| `MAX_ROLL_ANGLE`              | 45°    | Máximo roll             |
| `MAX_PITCH_ANGLE`             | 35°    | Máximo pitch            |
| `FC_DATA_STALE_MS`            | 500ms  | Timeout dados ESP2      |
| `FC_DEBUG_MANUAL_TIMEOUT_MS`  | 3000ms | Timeout override manual |

---

## 3. PID Gains Padrão

| Eixo      | Kp   | Ki    | Kd   | Limite |
| ----------|------|-------|------|--------|
| Roll      | 1.25 | 0.06  | 0.28 | 400µs  |
| Pitch     | 1.10 | 0.05  | 0.25 | 400µs  |
| Yaw       | 0.85 | 0.07  | 0.12 | 350µs  |
| Altitude  | 1.80 | 0.025 | 0.45 | 600µs  |

---

## 4. Estruturas Principais

### FlightState (dados SI do ESP2)

```cpp
struct FlightState {
    float roll, pitch, yaw, heading;     // Atitude (graus)
    float rollRate, pitchRate, yawRate;  // °/s
    float latitude, longitude;            // GPS (graus)
    float altGPS, altBaro, altFused;     // Altitude (m)
    float vx, vy, vz;                    // Velocidade (m/s)
    float battV, battA;                  // Bateria
    bool attitudeValid, gpsValid;        // Flags
};
´´´

ActuatorSignals (saída para atuadores)

```cpp
struct ActuatorSignals {
    uint16_t wingR, wingL;      // Asas (µs)
    uint16_t rudder;            // Leme (µs)
    uint16_t elevonR, elevonL;  // Elevons (µs)
    uint16_t motor;             // Motor (µs)
};
´´´

Waypoint (navegação)

```cpp
struct Waypoint {
    float latitude, longitude;   // GPS (graus)
    float altitude;              // Metros
    float heading;               // Graus (0-360)
    float tolerance;             // Metros (distância para "chegou")
};
´´´

5. Comandos Rápidos (GS → ESP1)

Comando             ID      Parâmetro   Descrição
CMD_SET_MODE        0xC2    uint8_t     Mudar modo de voo
CMD_SET_ROLL        0xD2    float       Ângulo de roll desejado
CMD_SET_PITCH       0xD3    float       Ângulo de pitch desejado
CMD_SET_YAW         0xD4    float       Heading desejado
CMD_SET_ALT_TARGET  0xD0    float       Altitude desejada (m)
CMD_SET_THROTTLE    0xD1    float       Potência (0-1)
CMD_ARM             0xC0    nenhum      Armar motor
CMD_DISARM          0xC1    nenhum      Desarmar
CMD_RTL             0xF1    nenhum      Return To Launch

6. Exemplos de Código

Inicialização básica

```cpp
FlightControl fc;

void setup() {
    fc.begin();
    fc.setMode(MODE_STABILIZE);
}

void loop() {
    fc.update();  // 100Hz
    delay(10);
}
´´´

Configurar waypoints (modo AUTO)

```cpp
Waypoint waypoints[] = {
    {41.1496, -8.6109, 100, 0, 10},
    {41.1578, -8.6291, 120, 90, 10}
};
fc.setWaypoints(waypoints, 2);
fc.setMode(MODE_AUTO);
´´´

Modo DEBUG_AUTO_MANUAL (testes)

```cpp
Waypoint testTraj[] = {
    {41.1496, -8.6109, 50, 0, 10},
    {41.1520, -8.6150, 60, 45, 10}
};
fc.setDebugTrajectory(testTraj, 2);
fc.setDebugManualTimeout(5000);  // 5 segundos
fc.setMode(MODE_DEBUG_AUTO_MANUAL);

// Quando receber comando manual:
fc.registerManualCommand();  // Reinicia timeout
´´´

Ajustar PIDs em runtime

```cpp
fc.setPIDGains(0, 1.5f, 0.08f, 0.35f);  // Roll (axisID=0)
fc.setPIDGains(1, 1.3f, 0.06f, 0.30f);  // Pitch (1)
fc.setPIDGains(2, 1.0f, 0.10f, 0.15f);  // Yaw (2)
fc.setPIDGains(3, 2.0f, 0.03f, 0.50f);  // Altitude (3)
´´´

Consultar estado

```cpp
FlightMode mode = fc.getMode();
const FlightState& state = fc.getState();
bool armed = actuators.isArmed();
bool overrideActive = fc.isManualOverrideActive();
uint32_t timeout = fc.getDebugManualTimeoutRemaining();
´´´

7. Mixer de Elevons (Asa Delta)

Fórmula:

elevonR = 1500 + pitchCorr + rollCorr
elevonL = 1500 + pitchCorr - rollCorr

Exemplo:

Pitch Corr  Roll Corr   Elevon R    Elevon L    Efeito
+100        0           1600        1600        Subir
0           +100        1600        1400        Virar direita
+100        +100        1700        1500        Subir + virar direita

8. Fusão de Altitude

Filtro complementar:
- altFused = 0.98 * altFused + 0.02 * altGPS
- Barómetro: Estável a curto prazo (98% peso)

GPS: Corrige deriva a longo prazo (2% peso)

9. Slew Rate (Suavização)

Atuador     Slew Rate       Tempo para 1000µs
Servos      30 µs/ciclo     ~33 ciclos (0.66s a 50Hz)
Motor       20 µs/ciclo     ~50 ciclos (1.0s a 50Hz)

10. Segurança – Comandos Bloqueados em Voo

Comando                 Solo   Voo
CMD_ARM                 ✅      ❌
CMD_DISARM              ✅      ❌
CMD_REBOOT              ✅      ❌
CMD_SHUTDOWN            ✅      ❌
CMD_SET_PARAM (crítico) ✅      ❌
CMD_SET_MODE            ✅      ✅
CMD_SET_ROLL/PITCH/YAW  ✅      ✅

11. Debug – Ativação

```cpp
fc.setDebug(true);
´´´

Mensagens típicas:

[FlightControl] begin() — Configurando PID e estado inicial...
[FlightControl] Mudança de modo: 1 → 6
[FlightControl] registerManualCommand: OVERRIDE MANUAL ATIVADO
[FlightControl] _updateDebugAutoManual: Timeout expirado → retomando no waypoint 1

12. Erros Comuns e Soluções

Erro                            Causa                       Solução
Dados SI stale                  ESP2 não envia dados        Verificar UART e ligações
GPS inválido                    Sem sinal                   Aguardar fixo GPS
Modo AUTO não navega            _missionActive = false      Chamar setWaypoints()
Override manual não termina     Timeout muito longo         Ajustar setDebugManualTimeout()
Atuadores não respondem         Sistema desarmado           Chamar actuators.arm()

13. Sequência de Voo Típica

```cpp
// 1. Inicialização
fc.begin();
actuators.begin();

// 2. Aguardar GPS e sensores
while (!fc.getState().gpsValid) delay(100);

// 3. Armamento
actuators.arm();

// 4. Descolagem (modo STABILIZE)
fc.setMode(MODE_STABILIZE);
fc.setSetpoints({0, 5, 0, 50, 0.7f});  // pitch 5°, throttle 70%

// 5. Modo ALT_HOLD após atingir altitude
if (fc.getState().altFused > 10) {
    fc.setMode(MODE_ALT_HOLD);
    fc.setSetpoints({0, 0, 0, 50, 0.5f});
}

// 6. Navegação autónoma
fc.setWaypoints(waypoints, 3);
fc.setMode(MODE_AUTO);

// 7. Return To Launch
fc.setMode(MODE_RTL);

// 8. Aterragem (automática no RTL)
// Quando altFused < 2m, o sistema reduz throttle
´´´

14. Verificação Rápida em Solo

```cpp
// Teste sem hélices (segurança!)
void testGround() {
    fc.setMode(MODE_MANUAL);
    fc.setSetpoints({0, 0, 0, 0, 0.3f});  // throttle 30%
    
    // Verificar movimento dos servos
    fc.setSetpoints({30, 0, 0, 0, 0});    // roll 30°
    delay(1000);
    fc.setSetpoints({0, 20, 0, 0, 0});    // pitch 20°
    delay(1000);
    fc.setSetpoints({0, 0, 30, 0, 0});    // yaw 30°
    delay(1000);
}
´´´

Fim do Guia Rápido – FlightControl BeaconFly v2.0.0

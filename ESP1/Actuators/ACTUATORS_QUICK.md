# Guia Rápido – Actuators BeaconFly

> **Versão:** 2.0.0  
> **Uso:** Referência rápida para debug, manutenção e desenvolvimento

---

## 1. Constantes Fundamentais

| Constante             | Valor    | Descrição         |
| ----------------------|----------|-------------------|
| `PWM_SERVO_MIN`       | 1000 µs  | Mínimo servo      |
| `PWM_SERVO_NEUTRAL`   | 1500 µs  | Neutro servo      |
| `PWM_SERVO_MAX`       | 2000 µs  | Máximo servo      |
| `PWM_MOTOR_STOP`      | 1000 µs  | Motor parado      |
| `PWM_MOTOR_MIN`       | 1050 µs  | Potência mínima   |
| `PWM_MOTOR_MAX`       | 1900 µs  | Potência máxima   |
| `PWM_FREQUENCY_SERVO` | 50 Hz    | Frequência servos |
| `PWM_PERIOD_US`       | 20000 µs | Período (20ms)    |

---

## 2. Mapeamento de Pinos

| Atuador  | Pino | Canal LEDC |
| ---------|------|------------|
| Wing R   | 13   | 0          |
| Wing L   | 14   | 1          |
| Rudder   | 27   | 2          |
| Elevon R | 26   | 3          |
| Elevon L | 25   | 4          |
| Motor    | 33   | 5          |

---

## 3. Estados do Sistema

| Estado    | `isArmed()` | `isFailsafeActive()` | `update()` | `arm()`    |
| ----------|-------------|----------------------|------------|------------|
| Desarmado | false       | false                | Ignorado   | ✅         |
| Armado    | true        | false                | ✅         | Ignorado   |
| Failsafe  | false       | true                 | Bloqueado  | Bloqueado  |

---

## 4. Sequência de Operação

### 4.1 Inicialização Normal

```cpp
Actuators act;

void setup() {
    act.begin();           // 1. Inicializar hardware
    act.arm();             // 2. Armar (2 segundos)
}

void loop() {
    ActuatorSignals sig;
    sig.wingR = 1500;      // neutro
    sig.motor = 1200;      // potência baixa
    act.update(sig);       // 3. Atualizar (100Hz)
}
´´´

4.2 Emergência (Failsafe)

```cpp
// Ativar failsafe (perda de link, bateria baixa, etc.)
act.failsafeBlock();       // Bloqueia tudo, posição segura

// Após resolver o problema
act.failsafeClear();       // Remove bloqueio (volta a desarmado)
act.arm();                 // Re-armar
´´´

5. Valores Típicos

Servos

Situação                 Wing R      Wing L      Rudder      Elevon R    Elevon L
Neutro                   1500        1500        1500        1500        1500
Máximo roll (direita)    2000        1000        1500        1500        1500
Máximo pitch (frente)    1500        1500        1500        2000        2000
Máximo yaw (direita)     1500        1500        2000        1500        1500

Motor

Situação        PWM (µs)    Potência (%)
STOP            1000        0%
Mínimo útil     1050        ~5%
Cruzeiro        1400        ~40%
Máximo          1900        100%

6. Comandos Úteis (Shell/Serial)

Ativar debug

```cpp
act.setDebug(true);
Consultar estado
cpp
Serial.printf("Armed: %d, Failsafe: %d\n", act.isArmed(), act.isFailsafeActive());
Forçar posição segura
cpp
act.safePosition();
´´´

7. Erros Comuns e Soluções

Erro                    Causa                           Solução
Motor não arma          ESC não recebeu pulso de 2s     Verificar arm() é chamado
Servos não respondem    Sistema desarmado               Chamar arm() antes de update()
PWM bloqueado           Failsafe ativo                  Chamar failsafeClear()
Valores ignorados       _canSendSignals() false         Verificar isArmed() e isFailsafeActive()

8. Debug – Mensagens típicas

[Actuators] begin() — Inicializando sistema de atuadores...
[Actuators] _setupPWM() — Configurando LEDC (ESP32)
[Actuators]   Canal 0: pino=13, freq=50 Hz
[Actuators]   Canal 1: pino=14, freq=50 Hz
[Actuators]   Canal 2: pino=27, freq=50 Hz
[Actuators]   Canal 3: pino=26, freq=50 Hz
[Actuators]   Canal 4: pino=25, freq=50 Hz
[Actuators]   Canal 5: pino=33, freq=50 Hz
[Actuators] begin() — concluído. Sistema DESARMADO.

[Actuators] arm() — Iniciando sequência de armamento...
[Actuators] arm() — SISTEMA ARMADO com sucesso.

[Actuators] update → WR:1500 WL:1500 Rud:1500 ER:1500 EL:1500 Mot:1200

[Actuators] ⚠️⚠️⚠️ FAILSAFE BLOCK ATIVADO ⚠️⚠️⚠️
[Actuators] failsafeBlock() — Canais LEDC detachados.

9. Performance (ESP32)

Métrica                 Valor
Tempo update()          ~2 µs
Tempo arm()             2000 ms (bloqueante)
Tempo failsafeBlock()   ~100 µs
Jitter PWM              < 1 µs

10. Verificação Rápida

Teste de hardware (sem FlightControl)

```cpp
Actuators act;

void setup() {
    Serial.begin(115200);
    act.begin();
    act.setDebug(true);
    act.arm();
}

void loop() {
    ActuatorSignals sig;
    sig.wingR = 1500;
    sig.wingL = 1500;
    sig.rudder = 1500;
    sig.elevonR = 1500;
    sig.elevonL = 1500;
    sig.motor = 1200;   // motor a 20% de potência
    
    act.update(sig);
    delay(10);          // 100Hz
}
´´´

Fim do Guia Rápido – Actuators BeaconFly v2.0.0

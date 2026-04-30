# Actuators – Pasta `Actuators/`

> **Versão:** 2.0.0  
> **Data:** 2026-04-17  
> **Autor:** BeaconFly UAS Team

Esta documentação detalha a **implementação do controlo de atuadores** (servos e motor) do sistema BeaconFly. Abrange os 2 arquivos da pasta: `Actuators.h` e `Actuators.cpp`, explicando a lógica, segurança, modos de operação e integração com o FlightControl.

---

## 1. Objetivo do Módulo

O módulo `Actuators` é responsável por:

- Controlar **5 servos** (wingR, wingL, rudder, elevonR, elevonL)
- Controlar **1 motor** (ECU/ESC)
- Receber comandos do `FlightControl` em microssegundos (1000-2000µs)
- Gerar sinais PWM precisos para os atuadores
- Garantir segurança através de ARM/DISARM e FAILSAFE
- Ser **determinístico** e **não bloqueante** (prioridade máxima)

---

## 2. Arquitetura de PWM

### 2.1 ESP32 (Plataforma Principal)

Utiliza o **periférico LEDC** (LED Controller) do ESP32:

| Característica    | Valor                           |
| ------------------|---------------------------------|
| Método            | Hardware PWM (não bloqueante)   |
| Canais            | 6 (0 a 5)                       |
| Resolução         | 16 bits (65535 níveis)          |
| Frequência servos | 50 Hz (período 20ms)            |
| Frequência motor  | 50 Hz (configurável para 400Hz) |
| Precisão          | ~0.305 µs por tick              |

**Vantagens:**

- ✅ Não bloqueante — o hardware gera o sinal autonomamente
- ✅ Determinístico — não afeta o loop de 100Hz
- ✅ Alta precisão — ideal para controlo de voo

### 2.2 Outras Plataformas (Fallback)

Para garantir portabilidade (Arduino, RP2040, Jetson), existe uma implementação de **PWM por software**:

| Característica | Valor                                |
| ---------------|--------------------------------------|
| Método         | Software PWM com `delayMicroseconds` |
| Precisão       | Limitada pela precisão do timer      |
| Bloqueio       | **BLOQUEANTE** (afeta determinismo)  |

**Aviso:** Esta implementação é apenas para fallback. Em produção no ESP32, use sempre o modo LEDC.

---

## 3. Interface com FlightControl

### 3.1 Estrutura `ActuatorSignals`

```cpp
struct ActuatorSignals {
    uint16_t wingR;      // Asa direita — 1000 a 2000 µs
    uint16_t wingL;      // Asa esquerda — 1000 a 2000 µs
    uint16_t rudder;     // Leme — 1000 a 2000 µs
    uint16_t elevonR;    // Elevon direito — 1000 a 2000 µs
    uint16_t elevonL;    // Elevon esquerdo — 1000 a 2000 µs
    uint16_t motor;      // Motor — 1000 (STOP) a 1900 (MAX) µs
};
´´´

3.2 Fluxo de Dados

FlightControl (PID) → ActuatorSignals → Actuators::update() → PWM → Atuadores

4. Segurança e Estados

4.1 Máquina de Estados

                    ┌─────────────────────────────────────┐
                    │                                     │
                    ▼                                     │
    ┌──────────┐  arm()   ┌──────────┐  update()   ┌──────────┐
    │ DESARMADO │ ──────► │  ARMADO  │ ─────────►  │ ATUANDO  │
    └──────────┘          └──────────┘             └──────────┘
         ▲                     │                        │
         │                     │                        │
         │                     │                        │
    disarm()              failsafeBlock()          failsafeBlock()
         │                     │                        │
         │                     ▼                        ▼
         │              ┌──────────────────────────────────┐
         └──────────────│         FAILSAFE ATIVO           │
                        │    (bloqueio físico de saídas)   │
                        └──────────────────────────────────┘
                                      │
                                 failsafeClear()
                                      │
                                      ▼
                              (volta a DESARMADO)

4.2 Descrição dos Estados

Estado      Descrição                               update()    arm()
DESARMADO   Estado inicial após begin() ou disarm() Ignorado    Permitido
ARMADO      Após arm() bem-sucedido                 Permitido   Ignorado
ATUANDO     Sub-estado de ARMADO (escrevendo PWM)   Permitido   Ignorado
FAILSAFE    Após failsafeBlock()                    Bloqueado   Bloqueado

4.3 Transições de Segurança

Transição       Condição                    Ação
arm()           !_failsafeActive && !_armed Envia pulso de arm (2s), _armed = true
disarm()        Qualquer estado             safePosition(), _armed = false
failsafeBlock() !_failsafeActive            safePosition(), detacha canais, _failsafeActive = true
failsafeClear() _failsafeActive             Reconfigura canais, _failsafeActive = false, _armed = false

5. Limites Físicos

5.1 Servos

Constante           Valor (µs)  Descrição
PWM_SERVO_MIN       1000        Deflexão máxima negativa
PWM_SERVO_NEUTRAL   1500        Posição neutra (centro)
PWM_SERVO_MAX       2000        Deflexão máxima positiva

5.2 Motor

Constante       Valor (µs)  Descrição
PWM_MOTOR_STOP  1000        Motor parado (ESC armado)
PWM_MOTOR_MIN   1050        Potência mínima útil (zona morta)
PWM_MOTOR_MAX   1900        Potência máxima segura
PWM_MOTOR_ARM   1000        Pulso de armamento (2 segundos)

Nota: A zona entre 1000 e 1050 µs é uma zona morta (deadband) para evitar arranques involuntários.

6. Mapeamento de Pinos

Atuador         Pino GPIO   Canal LEDC  Constante
Asa direita     13          0           PIN_SERVO_WING_R
Asa esquerda    14          1           PIN_SERVO_WING_L
Leme            27          2           PIN_SERVO_RUDDER
Elevon direito  26          3           PIN_SERVO_ELEVON_R
Elevon esquerdo 25          4           PIN_SERVO_ELEVON_L
Motor           33          5           PIN_MOTOR_ECU

7. API Pública

7.1 Construtor e Inicialização

```cpp
Actuators actuators;

void setup() {
    actuators.begin();    // Inicializa hardware
}
´´´

7.2 Armamento e Controlo

```cpp
// Armar o sistema (enviar pulso ao ESC)
actuators.arm();

// Atualizar atuadores (chamado a 100Hz)
ActuatorSignals signals;
signals.wingR = 1500;   // neutro
signals.motor = 1200;   // potência baixa
actuators.update(signals);

// Desarmar
actuators.disarm();
´´´

7.3 Failsafe

```cpp
// Ativar failsafe (emergência)
actuators.failsafeBlock();

// Desativar failsafe (volta a desarmado)
actuators.failsafeClear();
´´´

7.4 Consultas de Estado

```cpp
bool armed = actuators.isArmed();
bool failsafe = actuators.isFailsafeActive();
ActuatorSignals last = actuators.getLastSignals();
´´´

7.5 Debug

```cpp
actuators.setDebug(true);   // Ativar mensagens de debug
´´´

8. Exemplo de Uso no FlightControl

```cpp
// No loop principal (100Hz)
void FlightControl::update() {
    // Calcular PID
    float roll_pid = pidRoll.update(desiredRoll, currentRoll);
    float pitch_pid = pidPitch.update(desiredPitch, currentPitch);
    
    // Converter para PWM (1000-2000µs)
    ActuatorSignals signals;
    signals.wingR = 1500 + (roll_pid * 500);
    signals.wingL = 1500 - (roll_pid * 500);
    signals.elevonR = 1500 + (pitch_pid * 500);
    signals.elevonL = 1500 - (pitch_pid * 500);
    signals.rudder = 1500;
    signals.motor = throttleToPWM(throttle);
    
    // Enviar para atuadores
    actuators.update(signals);
}
´´´

9. Segurança e Robustez

Mecanismo               Descrição
Constrain automático    Nenhum valor fora dos limites é escrito
ARM/DISARM explícito    Sistema só responde após armamento
Failsafe físico         Detacha canais LEDC no ESP32
Posição segura          Neutro + STOP em qualquer emergência
Debug condicional       Não afeta performance em produção
Portabilidade           Fallback para plataformas sem LEDC

10. Performance

Métrica         ESP32 (LEDC)    Outras (software)
Tempo de update ~2 µs           ~20 ms (bloqueante)
Jitter          < 1 µs          > 100 µs
Carga CPU       Mínima          Alta
Determinismo    ✅ Sim          ❌ Não

Recomendação: Utilizar sempre ESP32 com LEDC para produção.

11. Observações Finais

- O módulo Actuators é o único responsável por escrever nos pinos de saída.
- Nenhum outro módulo deve aceder diretamente aos pinos PWM.
- O FlightControl deve garantir que os valores estão dentro da gama esperada.
- O Security pode ativar failsafeBlock() em caso de anomalia.

Fim da documentação – Actuators BeaconFly v2.0.0

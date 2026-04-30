# Guia Rápido – Telemetry BeaconFly

> **Versão:** 2.0.0  
> **Uso:** Referência rápida para operação e análise de dados

---

## 1. Conceitos Fundamentais

| Conceito           | Descrição                                                           |
| -------------------|---------------------------------------------------------------------|
| **Caixa Negra**    | Regista TODOS os dados de voo para análise posterior                |
| **Não-bloqueante** | Buffer em RAM evita atrasos no loop de controlo                     |
| **Sessões**        | Cada boot gera novos ficheiros (`flight_XXX.csv`, `events_XXX.csv`) |
| **Timestamp**      | Em microssegundos (precisão máxima)                                 |

---

## 2. Backends de Armazenamento

| Ordem | Backend | Persistência |
| ------|---------|--------------|
| 1     | SD Card | ✅ Sim       |
| 2     | SPIFFS  | ✅ Sim       |
| 3     | RAM     | ❌ Não       |

---

## 3. Ficheiros Gerados

| Ficheiro         | Conteúdo            | Exemplo                  |
| -----------------|---------------------|--------------------------|
| `flight_001.csv` | Dados de voo (10Hz) | Roll, pitch, PWM, etc.   |
| `events_001.csv` | Eventos críticos    | ARM, FAILSAFE, BATT_LOW  |
| `session.bin`    | Última sessão       | 2 bytes                  |

---

## 4. Campos do Ficheiro de Voo (24 campos)

| Campo             | Unidade | Descrição               |
| ------------------|---------|-------------------------|
| `timestamp_us`    | µs      | Tempo desde boot        |
| `roll`            | graus   | Ângulo de rolamento     |
| `pitch`           | graus   | Ângulo de inclinação    |
| `yaw`             | graus   | Ângulo de guinada       |
| `alt_fused`       | m       | Altitude fundida        |
| `target_roll`     | graus   | Roll desejado           |
| `target_pitch`    | graus   | Pitch desejado          |
| `target_yaw`      | graus   | Yaw desejado            |
| `target_alt`      | m       | Altitude desejada       |
| `error_roll`      | graus   | Erro de roll            |
| `error_pitch`     | graus   | Erro de pitch           |
| `error_yaw`       | graus   | Erro de yaw             |
| `error_alt`       | m       | Erro de altitude        |
| `pwm_wingR`       | µs      | PWM asa direita         |
| `pwm_wingL`       | µs      | PWM asa esquerda        |
| `pwm_rudder`      | µs      | PWM leme                |
| `pwm_elevonR`     | µs      | PWM elevon direito      |
| `pwm_elevonL`     | µs      | PWM elevon esquerdo     |
| `pwm_motor`       | µs      | PWM motor               |
| `flight_mode`     | -       | Modo de voo (0-5)       |
| `battV`           | V       | Tensão da bateria       |
| `battA`           | A       | Corrente da bateria     |
| `loop_time_us`    | µs      | Tempo do último ciclo   |

---

## 5. Tipos de Eventos

| Evento            | Significado           |
| ------------------|-----------------------|
| `BOOT`            | Arranque              |
| `ARM`             | Motores armados       |
| `DISARM`          | Motores desarmados    |
| `MODE_CHANGE`     | Mudança de modo       |
| `FAILSAFE_ON`     | Failsafe ativado      |
| `FAILSAFE_OFF`    | Failsafe desativado   |
| `BATT_LOW`        | Bateria baixa         |
| `BATT_CRITICAL`   | Bateria crítica       |
| `SENSOR_FAIL`     | Falha de sensor       |
| `SECURITY`        | Violação de segurança |
| `CUSTOM`          | Evento personalizado  |

---

## 6. Modos de Voo (flight_mode)

| Valor | Modo      |
| ------|-----------|
| 0     | MANUAL    |
| 1     | STABILIZE |
| 2     | ALT_HOLD  |
| 3     | POSHOLD   |
| 4     | AUTO      |
| 5     | RTL       |

---

## 7. Exemplo de Linha CSV

```csv
12345678,2.34,-1.23,45.67,12.34,0.00,5.00,90.00,50.00,-2.34,6.23,44.33,37.66,1500,1500,1500,1520,1480,1200,1,12.34,0.12,9876
´´´

8. Código Rápido

Inicialização

```cpp
Telemetry telemetry;

void setup() {
    telemetry.begin();
}
´´´

Loop

```cpp
void loop() {
    telemetry.update(flightControl, actuators);
    // resto do loop...
}
´´´

Registrar evento

```cpp
telemetry.logEvent(EVT_ARM);
telemetry.logEvent(EVT_BATT_LOW, "10.5V");
´´´
Encerramento seguro

```cpp
telemetry.close();
´´´

9. Análise Rápida (Python)

```python
import pandas as pd

# Ler dados
df = pd.read_csv('flight_001.csv')

# Ver estatísticas
print(df[['roll', 'pitch', 'battV']].describe())

# Ver eventos
events = pd.read_csv('events_001.csv')
print(events['event_type'].value_counts())
´´´

10. Resolução de Problemas

Problema                Causa               Solução
Ficheiros não aparecem  SD falhou           Verificar SPIFFS (/events_XXX.csv)
Dados em falta          Buffer cheio        Reduzir TELEMETRY_FLUSH_INTERVAL_MS
Loop lento              SD lento            Usar SPIFFS ou aumentar buffer
Timestamp reinicia      Reset inesperado    Verificar alimentação

11. Parâmetros Configuráveis

Parâmetro                       Valor padrão    Descrição
TELEMETRY_SAMPLE_INTERVAL_MS    100             Taxa de amostragem (ms)
TELEMETRY_FLUSH_INTERVAL_MS     2000            Flush periódico (ms)
TELEMETRY_BUFFER_SIZE           200             Tamanho do buffer (entradas)

Fim do Guia Rápido – Telemetry BeaconFly v2.0.0

# Telemetry – Pasta `Telemetry/`

> **Versão:** 2.0.0  
> **Data:** 2026-04-19  
> **Autor:** BeaconFly UAS Team

Esta documentação detalha o **módulo de data logger interno (caixa negra)** do sistema BeaconFly. Abrange os 2 arquivos da pasta: `Telemetry.h` e `Telemetry.cpp`, explicando a arquitetura, formatos de dados, gestão de sessões e integração com os restantes módulos.

---

## 1. Objetivo do Módulo

O módulo `Telemetry` é a **caixa negra do BeaconFly**. Responsável por:

| Funcionalidade                 | Descrição                                               |
| -------------------------------|---------------------------------------------------------|
| **Registo de voo**             | Grava todos os dados de voo (atitude, setpoints, erros, PWM)|
| **Eventos críticos**           | Regista eventos importantes (ARM, FAILSAFE, BATERIA, etc.)|
| **Análise pós-voo**            | Fornece dados em CSV para análise em Excel/Python       |
| **Investigação de incidentes** | Permite reconstruir a sequência exata de eventos antes de uma falha|

**Princípio fundamental:** É um **data logger PURO** — não transmite nada via UART, LoRa ou qualquer outro canal. Apenas guarda informação em storage local.

---

## 2. Arquitetura do Logger

### 2.1 Fluxo de Dados

┌─────────────────────────────────────────────────────────────────────────────────┐
│ FLIGHTCONTROL                                                                   │
│ (Estado, Setpoints, Erros)                                                      │
└─────────────────────────────────────────────────────────────────────────────────┘
│
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ ACTUATORS                                                                       │
│ (Sinais PWM enviados)                                                           │
└─────────────────────────────────────────────────────────────────────────────────┘
│
▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│ TELEMETRY                                                                       │
│                                                                                 │
│ ┌─────────────────────────────────────────────────────────────────────────────┐ │
│ │ log() - Amostragem (10Hz)                                                   │ │
│ │ • Lê FlightControl e Actuators                                              │ │
│ │ • Cria TelemetryEntry com timestamp em MICROSSEGUNDOS                       │ │
│ │ • Adiciona ao buffer circular (200 entradas)                                │ │
│ └─────────────────────────────────────────────────────────────────────────────┘ │
│ │                                                                               │
│ ▼                                                                               │
│ ┌─────────────────────────────────────────────────────────────────────────────┐ │
│ │ BUFFER CIRCULAR (RAM)                                                       │ │
│ │ • 200 entradas × ~80 bytes = ~16 KB                                         │ │
│ │ • Permite logging não-bloqueante                                            │ │
│ └─────────────────────────────────────────────────────────────────────────────┘ │
│ │                                                                               │
│ ▼ (flush periódico a cada 2s)                                                   │
│ ┌─────────────────────────────────────────────────────────────────────────────┐ │
│ │ STORAGE (SD / SPIFFS / RAM)                                                 │ │
│ │ • SD Card (preferido) — maior capacidade                                    │ │
│ │ • SPIFFS (fallback) — flash interna                                         │ │
│ │ • RAM (último recurso) — dados perdem-se no reset                           │ │
│ └─────────────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────────┘

### 2.2 Backends de Armazenamento

| Backend          | Prioridade | Capacidade | Persistência | Velocidade |
| -----------------|------------|------------|--------------|------------|
| **SD Card**      | 1ª         | GB         | ✅ Sim       | Média      |
| **SPIFFS**       | 2ª         | ~1-2 MB    | ✅ Sim       | Rápida     |
| **RAM (buffer)** | 3ª         | ~16 KB     | ❌ Não       | Imediata   |

**Fallback automático:** Se o SD falhar, o sistema tenta SPIFFS. Se ambos falharem, usa apenas RAM (dados perdem-se no reset).

---

## 3. Formato dos Dados

### 3.1 Ficheiro de Voo (`/flight_XXX.csv`)

Cada linha representa uma amostra de dados (10Hz). Contém **24 campos**:

| Campo          | Unidade  | Descrição                                     |
| ---------------|----------|-----------------------------------------------|
| `timestamp_us` | µs       | Microssegundos desde o boot (PRECISÃO MÁXIMA) |
| `roll`         | graus    | Ângulo de rolamento atual                     |
| `pitch`        | graus    | Ângulo de inclinação atual                    |
| `yaw`          | graus    | Ângulo de guinada atual                       |
| `alt_fused`    | metros   | Altitude fundida (GPS + Baro)                 |
| `target_roll`  | graus    | Roll desejado (setpoint)                      |
| `target_pitch` | graus    | Pitch desejado (setpoint)                     |
| `target_yaw`   | graus    | Yaw desejado (setpoint)                       |
| `target_alt`   | metros   | Altitude desejada (setpoint)                  |
| `error_roll`   | graus    | Erro de roll (target - atual)                 |
| `error_pitch`  | graus    | Erro de pitch (target - atual)                |
| `error_yaw`    | graus    | Erro de yaw (target - atual)                  |
| `error_alt`    | metros   | Erro de altitude (target - atual)             |
| `pwm_wingR`    | µs       | Sinal PWM da asa direita                      |
| `pwm_wingL`    | µs       | Sinal PWM da asa esquerda                     |
| `pwm_rudder`   | µs       | Sinal PWM do leme                             |
| `pwm_elevonR`  | µs       | Sinal PWM do elevon direito                   |
| `pwm_elevonL`  | µs       | Sinal PWM do elevon esquerdo                  |
| `pwm_motor`    | µs       | Sinal PWM do motor                            |
| `flight_mode`  | -        | Modo de voo (0-5)                             |
| `battV`        | volts    | Tensão da bateria                             |
| `battA`        | amperes  | Corrente da bateria                           |
| `loop_time_us` | µs       | Tempo do último ciclo de controlo             |

### 3.2 Exemplo de Linha

```csv
timestamp_us,roll,pitch,yaw,alt_fused,target_roll,target_pitch,target_yaw,target_alt,error_roll,error_pitch,error_yaw,error_alt,pwm_wingR,pwm_wingL,pwm_rudder,pwm_elevonR,pwm_elevonL,pwm_motor,flight_mode,battV,battA,loop_time_us
12345678,2.34,-1.23,45.67,12.34,0.00,5.00,90.00,50.00,-2.34,6.23,44.33,37.66,1500,1500,1500,1520,1480,1200,1,12.34,0.12,9876
´´´

3.3 Ficheiro de Eventos (/events_XXX.csv)

Campo           Descrição
timestamp_us    Microssegundos desde o boot
event_type      Tipo de evento (BOOT, ARM, FAILSAFE_ON, etc.)
message         Mensagem descritiva (opcional)

3.4 Tipos de Eventos

Evento          Descrição
BOOT            Arranque do sistema (início de sessão)
ARM             Motores armados
DISARM          Motores desarmados
MODE_CHANGE     Mudança de modo de voo
FAILSAFE_ON     Ativação do modo de segurança
FAILSAFE_OFF    Desativação do failsafe
BATT_LOW        Bateria baixa (alerta)
BATT_CRITICAL   Bateria crítica (emergência)
SENSOR_FAIL     Falha de sensor (IMU, barómetro, etc.)
SECURITY        Violação de segurança (HMAC, replay, etc.)
CUSTOM          Evento definido pelo utilizador

4. Gestão de Sessões

4.1 Números de Sessão

A cada boot, o número da sessão é incrementado:
- Sessão #001 → flight_001.csv, events_001.csv
- Sessão #002 → flight_002.csv, events_002.csv
- Sessão #003 → flight_003.csv, events_003.csv
- ...
O número da última sessão é guardado no ficheiro /session.bin.

4.2 Ficheiros por Sessão

Ficheiro        Conteúdo
flight_XXX.csv  Dados periódicos de voo (10Hz)
events_XXX.csv  Eventos críticos (escrita imediata)
session.bin     Número da última sessão (2 bytes)

5. Timestamp em Microssegundos

Porquê microssegundos em vez de milissegundos?

Razão       Explicação
Precisão    Permite reconstruir a sequência exata de eventos (loop de 100Hz = 10ms)
Resolução   Microssegundos distinguem eventos que ocorrem no mesmo milissegundo
Análise     Facilita a correlação entre diferentes fontes de dados

Exemplo: Dois eventos podem ocorrer no mesmo millis() mas em momentos diferentes:
- millis() = 12345 (ambos os eventos)
- micros() = 12345678 (evento A)
- micros() = 12345999 (evento B) → 321µs depois

6. Buffer Circular (Não-Bloqueante)

6.1 Porquê um Buffer?

Problema                                                Solução
Escrever no SD pode demorar dezenas de milissegundos    Buffer em RAM acumula dados
O loop de controlo (100Hz) não pode ser bloqueado       Escrita assíncrona (flush periódico)
Falha do SD não deve interromper o voo                  Fallback para SPIFFS ou RAM

6.2 Parâmetros do Buffer

Parâmetro           Valor           Descrição
Tamanho             200 entradas    ~16 KB de RAM
Taxa de amostragem  10Hz            100ms entre amostras
Capacidade temporal 20 segundos     200 entradas × 100ms

6.3 Flush Periódico

A cada 2 segundos, o buffer é escrito no storage:
- [Log] → Buffer RAM → [Flush a cada 2s] → Storage (SD/SPIFFS)
- Em caso de falha catastrófica, perde-se no máximo 2 segundos de dados.

7. Eventos Críticos (Escrita Imediata)

Eventos críticos NÃO passam pelo buffer — são escritos imediatamente no storage.
Porquê? Se o sistema falhar segundos depois de um evento crítico (ex: failsafe ativado), queremos garantir que o evento fica registado.

Exemplos de eventos que merecem escrita imediata:

- FAILSAFE_ON — Ativação do modo de segurança
- BATT_CRITICAL — Bateria crítica
- SENSOR_FAIL — Falha de sensor
- SECURITY — Violação de segurança

8. API Pública

8.1 Inicialização

```cpp
Telemetry telemetry;

void setup() {
    telemetry.begin();
    telemetry.setEnabled(true);  // Logging ativo
}
´´´

8.2 Loop Principal

```cpp
void loop() {
    // Atualizar telemetria (amostragem + flush)
    telemetry.update(flightControl, actuators);
    
    // Resto do loop...
}
´´´

8.3 Registo de Eventos

```cpp
// Evento simples
telemetry.logEvent(EVT_ARM);

// Evento com mensagem
telemetry.logEvent(EVT_BATT_LOW, "Tensão: 10.5V");

// Evento personalizado
telemetry.logEvent(EVT_CUSTOM, "Teste de voo #42");
´´´

8.4 Controlo

```cpp
// Desativar logging em voo (se necessário)
telemetry.setEnabled(false);

// Reativar
telemetry.setEnabled(true);
´´´

8.5 Diagnóstico

```cpp
StorageBackend backend = telemetry.getBackend();
uint16_t session = telemetry.getSessionNumber();
uint32_t logged = telemetry.getTotalLogged();
uint32_t dropped = telemetry.getTotalDropped();

Serial.printf("Backend: %d, Sessão: %d, Logged: %u, Dropped: %u\n",
              backend, session, logged, dropped);
´´´

8.6 Encerramento Seguro

```cpp
// Antes de desligar ou reiniciar
telemetry.close();
´´´

9. Exemplo de Análise Pós-Voo (Python)

```python
import pandas as pd
import matplotlib.pyplot as plt

# Ler dados da caixa negra
df = pd.read_csv('flight_001.csv')

# Converter timestamp de microssegundos para segundos
df['time_s'] = df['timestamp_us'] / 1_000_000

# Plotar atitude
plt.figure(figsize=(12, 8))
plt.subplot(3, 1, 1)
plt.plot(df['time_s'], df['roll'], label='Roll')
plt.plot(df['time_s'], df['pitch'], label='Pitch')
plt.plot(df['time_s'], df['yaw'], label='Yaw')
plt.legend()
plt.ylabel('Ângulo (graus)')

# Plotar altitude
plt.subplot(3, 1, 2)
plt.plot(df['time_s'], df['alt_fused'], label='Altitude')
plt.plot(df['time_s'], df['target_alt'], '--', label='Target')
plt.legend()
plt.ylabel('Altitude (m)')

# Plotar PWM do motor
plt.subplot(3, 1, 3)
plt.plot(df['time_s'], df['pwm_motor'], label='Motor PWM')
plt.legend()
plt.xlabel('Tempo (s)')
plt.ylabel('PWM (µs)')

plt.tight_layout()
plt.show()
´´´

10. Debug e Diagnóstico

10.1 Ativação

```cpp
// Ativar debug (já ativo por padrão durante desenvolvimento)
// As mensagens aparecem na Serial
´´´

10.2 Mensagens Típicas

[Telemetry] begin() — Inicializando data logger (caixa negra)...
[Telemetry] _initSD() — Tentando inicializar SD Card (CS=15)...
[Telemetry] _initSD() — Cartão SD: tipo=1, tamanho=7814 MB
[Telemetry] Backend: SD CARD
[Telemetry] Sessão #001
[Telemetry] Ficheiro de voo:    /flight_001.csv
[Telemetry] Ficheiro de eventos: /events_001.csv
[Telemetry] Cabeçalho CSV escrito no ficheiro de voo
[Telemetry] Evento crítico: BOOT — Sessão #001 | Backend: SD
[Telemetry] Telemetry inicializado com sucesso
[Telemetry] Flush: 20 entradas escritas (total: 20, descartadas: 0)
[Telemetry] Evento crítico: ARM — 
[Telemetry] Flush: 200 entradas escritas (total: 220, descartadas: 0)

10.3 Erros Comuns

Mensagem                          Causa                             Solução
Falha na inicialização            SD não conectado                  Verificar fiação e alimentação
Nenhum cartão detectado           SD ausente                        Inserir cartão SD
Buffer cheio!                     Flush muito lento                 Reduzir TELEMETRY_FLUSH_INTERVAL_MS
Não foi possível abrir ficheiros  Sistema de ficheiros corrompido   Reformatar SD/SPIFFS

11. Performance

Métrica                 Valor
Taxa de amostragem      10 Hz (100ms)
Tamanho da linha CSV    ~200 bytes
Taxa de dados           ~2 KB/s
Buffer RAM              ~16 KB (200 entradas)
Flush interval          2 segundos
Tempo de log()          ~5 µs (apenas memcpy)
Tempo de flush()        ~10-50 ms (depende do SD)

Fim da documentação – Telemetry BeaconFly v2.0.0

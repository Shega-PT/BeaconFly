/**
 * =================================================================================
 * LORA.CPP — IMPLEMENTAÇÃO DO DRIVER LoRa 868MHz (ESP2 → GS)
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
 * 1. O ESP2 NUNCA recebe via LoRa — apenas transmite
 * 2. Implementa fila de prioridades para evitar perda de pacotes
 * 3. Suporta SX1262 (recomendado) e SX1276 (fallback)
 * 4. Utiliza a biblioteca RadioLib para abstração de hardware
 * 
 * =================================================================================
 * BIBLIOTECAS NECESSÁRIAS
 * =================================================================================
 * 
 * Este módulo requer a biblioteca RadioLib:
 *   https://github.com/jgromes/RadioLib
 * 
 * Instalação via PlatformIO: lib_deps = jgromes/RadioLib
 * 
 * =================================================================================
 */

#include "LoRa.h"
#include <RadioLib.h>

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define LORA_DEBUG

#ifdef LORA_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[LoRa] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

LoRa::LoRa()
    : _moduleType(LORA_MODULE_SX1262)
    , _status(LORA_STATUS_IDLE)
    , _debugEnabled(false)
    , _initialized(false)
    , _queueHead(0)
    , _queueTail(0)
    , _queueCount(0)
    , _lastTransmitMs(0)
    , _lastSendAttemptMs(0)
    , _radio(nullptr)
{
    memset(&_config, 0, sizeof(LoRaConfig));
    memset(&_stats, 0, sizeof(LoRaStats));
    memset(_queue, 0, sizeof(_queue));
    
    /* Configuração padrão */
    _config.frequency = LORA_DEFAULT_FREQUENCY;
    _config.txPower = LORA_DEFAULT_TX_POWER;
    _config.spreadingFactor = LORA_DEFAULT_SPREADING_FACTOR;
    _config.bandwidth = LORA_DEFAULT_BANDWIDTH;
    _config.codingRate = LORA_DEFAULT_CODING_RATE;
    _config.crcEnabled = true;
    
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO
 * =================================================================================
 */

bool LoRa::begin(LoRaModuleType moduleType, const LoRaConfig& config) {
    DEBUG_PRINT("begin() — Inicializando módulo LoRa 868MHz...\n");
    
    _moduleType = moduleType;
    _config = config;
    
    bool success = false;
    
    switch (moduleType) {
        case LORA_MODULE_SX1262:
            success = _initSX1262();
            break;
        case LORA_MODULE_SX1276:
            success = _initSX1276();
            break;
        default:
            DEBUG_PRINT("begin() — Tipo de módulo não suportado: %d\n", moduleType);
            return false;
    }
    
    if (!success) {
        _status = LORA_STATUS_ERROR;
        DEBUG_PRINT("begin() — Falha na inicialização do módulo\n");
        return false;
    }
    
    _initialized = true;
    _status = LORA_STATUS_IDLE;
    _stats.lastTransmitTime = millis();
    
    DEBUG_PRINT("begin() — Módulo LoRa inicializado com sucesso\n");
    DEBUG_PRINT("  Frequência: %lu Hz\n", _config.frequency);
    DEBUG_PRINT("  Potência: %d dBm\n", _config.txPower);
    DEBUG_PRINT("  Spreading Factor: %d\n", _config.spreadingFactor);
    DEBUG_PRINT("  Bandwidth: %lu Hz\n", _config.bandwidth);
    DEBUG_PRINT("  Coding Rate: 4/%d\n", _config.codingRate);
    
    return true;
}

/* =================================================================================
 * END — ENCERRAMENTO
 * =================================================================================
 */

void LoRa::end() {
    DEBUG_PRINT("end() — Encerrando módulo LoRa...\n");
    
    if (_radio != nullptr) {
        /* TODO: Encerrar o objeto rádio adequadamente */
        _radio = nullptr;
    }
    
    _initialized = false;
    _status = LORA_STATUS_IDLE;
    _queueCount = 0;
    _queueHead = 0;
    _queueTail = 0;
    
    DEBUG_PRINT("end() — Módulo LoRa encerrado\n");
}

/* =================================================================================
 * INIT SX1262 — INICIALIZAÇÃO DO SX1262
 * =================================================================================
 */

bool LoRa::_initSX1262() {
    DEBUG_PRINT("_initSX1262() — Inicializando SX1262...\n");
    DEBUG_PRINT("  Pinos: CS=%d, DIO1=%d, RESET=%d, BUSY=%d\n",
                LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RESET_PIN, LORA_BUSY_PIN);
    
    /* Criar objeto Module e SX1262 */
    Module* module = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RESET_PIN, LORA_BUSY_PIN);
    SX1262* radio = new SX1262(module);
    _radio = radio;
    
    /* Inicializar hardware */
    int16_t state = radio->begin();
    if (state != RADIOLIB_ERR_NONE) {
        DEBUG_PRINT("_initSX1262() — radio.begin() falhou: %d\n", state);
        delete radio;
        delete module;
        _radio = nullptr;
        return false;
    }
    
    /* Configurar parâmetros RF */
    radio->setFrequency(_config.frequency / 1000000.0f);
    radio->setSpreadingFactor(_config.spreadingFactor);
    radio->setBandwidth(_config.bandwidth);
    radio->setCodingRate(_config.codingRate);
    radio->setOutputPower(_config.txPower);
    
    if (_config.crcEnabled) {
        radio->setCRC(RADIOLIB_CRC_ON);
    } else {
        radio->setCRC(RADIOLIB_CRC_OFF);
    }
    
    DEBUG_PRINT("_initSX1262() — SX1262 inicializado com sucesso\n");
    return true;
}

/* =================================================================================
 * INIT SX1276 — INICIALIZAÇÃO DO SX1276 (FALLBACK)
 * =================================================================================
 */

bool LoRa::_initSX1276() {
    DEBUG_PRINT("_initSX1276() — Inicializando SX1276 (fallback)...\n");
    DEBUG_PRINT("  Pinos: CS=%d, DIO0=%d, RESET=%d\n",
                LORA_NSS_PIN, LORA_DIO0_PIN, LORA_RESET_PIN);
    
    /* Criar objeto Module e SX1276 */
    Module* module = new Module(LORA_NSS_PIN, LORA_DIO0_PIN, LORA_RESET_PIN, RADIOLIB_NC);
    SX1276* radio = new SX1276(module);
    _radio = radio;
    
    /* Inicializar hardware */
    int16_t state = radio->begin();
    if (state != RADIOLIB_ERR_NONE) {
        DEBUG_PRINT("_initSX1276() — radio.begin() falhou: %d\n", state);
        delete radio;
        delete module;
        _radio = nullptr;
        return false;
    }
    
    /* Configurar parâmetros RF */
    radio->setFrequency(_config.frequency / 1000000.0f);
    radio->setSpreadingFactor(_config.spreadingFactor);
    radio->setBandwidth(_config.bandwidth);
    radio->setCodingRate(_config.codingRate);
    radio->setOutputPower(_config.txPower);
    
    if (_config.crcEnabled) {
        radio->setCRC(RADIOLIB_CRC_ON);
    } else {
        radio->setCRC(RADIOLIB_CRC_OFF);
    }
    
    DEBUG_PRINT("_initSX1276() — SX1276 inicializado com sucesso\n");
    return true;
}

/* =================================================================================
 * SEND PACKET — ENVIA PACOTE DIRETAMENTE (HARDWARE)
 * =================================================================================
 */

bool LoRa::_sendPacket(const uint8_t* data, size_t length) {
    if (!_initialized || _radio == nullptr) {
        return false;
    }
    
    if (length > LORA_MAX_PACKET_SIZE) {
        DEBUG_PRINT("_sendPacket() — Pacote demasiado grande: %zu bytes\n", length);
        return false;
    }
    
    int16_t state;
    
    switch (_moduleType) {
        case LORA_MODULE_SX1262: {
            SX1262* radio = static_cast<SX1262*>(_radio);
            state = radio->transmit((uint8_t*)data, length);
            break;
        }
        case LORA_MODULE_SX1276: {
            SX1276* radio = static_cast<SX1276*>(_radio);
            state = radio->transmit((uint8_t*)data, length);
            break;
        }
        default:
            return false;
    }
    
    if (state == RADIOLIB_ERR_NONE) {
        _stats.packetsSent++;
        _stats.lastTransmitTime = millis();
        DEBUG_PRINT("_sendPacket() — Pacote enviado: %zu bytes\n", length);
        return true;
    } else {
        _stats.packetsFailed++;
        DEBUG_PRINT("_sendPacket() — Falha no envio: %d\n", state);
        return false;
    }
}

/* =================================================================================
 * ENQUEUE — ADICIONA PACOTE À FILA
 * =================================================================================
 */

bool LoRa::_enqueue(const uint8_t* data, size_t length, LoRaPriority priority) {
    if (_queueCount >= LORA_TX_QUEUE_SIZE) {
        _stats.queueOverflows++;
        DEBUG_PRINT("_enqueue() — Fila cheia! Pacote descartado\n");
        return false;
    }
    
    uint8_t index = _queueTail;
    LoRaPacket* packet = &_queue[index];
    
    size_t copyLen = (length < LORA_MAX_PACKET_SIZE) ? length : LORA_MAX_PACKET_SIZE;
    memcpy(packet->data, data, copyLen);
    packet->length = copyLen;
    packet->priority = priority;
    packet->timestamp = millis();
    
    _queueTail = (_queueTail + 1) % LORA_TX_QUEUE_SIZE;
    _queueCount++;
    
    DEBUG_PRINT("_enqueue() — Pacote adicionado à fila (prio=%d, size=%zu)\n", priority, copyLen);
    return true;
}

/* =================================================================================
 * DEQUEUE — REMOVE PACOTE DA FILA (MAIOR PRIORIDADE)
 * =================================================================================
 */

bool LoRa::_dequeue(LoRaPacket* packet) {
    if (_queueCount == 0) {
        return false;
    }
    
    /* Encontrar o índice do pacote com maior prioridade (menor número) */
    int bestIndex = -1;
    uint8_t bestPriority = 255;
    uint8_t currentIndex = _queueHead;
    
    for (uint8_t i = 0; i < _queueCount; i++) {
        LoRaPacket* p = &_queue[currentIndex];
        if (p->priority < bestPriority) {
            bestPriority = p->priority;
            bestIndex = currentIndex;
        }
        currentIndex = (currentIndex + 1) % LORA_TX_QUEUE_SIZE;
    }
    
    if (bestIndex == -1) {
        return false;
    }
    
    /* Copiar pacote selecionado */
    memcpy(packet, &_queue[bestIndex], sizeof(LoRaPacket));
    
    /* Remover da fila (compactar) */
    uint8_t newHead = _queueHead;
    uint8_t newTail = _queueTail;
    uint8_t newCount = 0;
    uint8_t newQueue[LORA_TX_QUEUE_SIZE];
    uint8_t newIdx = 0;
    
    currentIndex = _queueHead;
    for (uint8_t i = 0; i < _queueCount; i++) {
        if (currentIndex != bestIndex) {
            memcpy(&newQueue[newIdx], &_queue[currentIndex], sizeof(LoRaPacket));
            newIdx++;
            newCount++;
        }
        currentIndex = (currentIndex + 1) % LORA_TX_QUEUE_SIZE;
    }
    
    /* Reconstruir fila */
    memcpy(_queue, newQueue, sizeof(LoRaPacket) * newCount);
    _queueHead = 0;
    _queueTail = newCount;
    _queueCount = newCount;
    
    DEBUG_PRINT("_dequeue() — Pacote removido da fila (prio=%d, restam=%d)\n", 
                bestPriority, _queueCount);
    
    return true;
}

/* =================================================================================
 * GET NEXT QUEUE INDEX — RETORNA ÍNDICE DO PRÓXIMO PACOTE (MAIOR PRIORIDADE)
 * =================================================================================
 */

int LoRa::_getNextQueueIndex() {
    if (_queueCount == 0) {
        return -1;
    }
    
    int bestIndex = -1;
    uint8_t bestPriority = 255;
    uint8_t currentIndex = _queueHead;
    
    for (uint8_t i = 0; i < _queueCount; i++) {
        LoRaPacket* p = &_queue[currentIndex];
        if (p->priority < bestPriority) {
            bestPriority = p->priority;
            bestIndex = currentIndex;
        }
        currentIndex = (currentIndex + 1) % LORA_TX_QUEUE_SIZE;
    }
    
    return bestIndex;
}

/* =================================================================================
 * SEND — ADICIONA PACOTE À FILA (PÚBLICO)
 * =================================================================================
 */

bool LoRa::send(const uint8_t* data, size_t length, LoRaPriority priority) {
    if (!_initialized) {
        DEBUG_PRINT("send() — Módulo não inicializado\n");
        return false;
    }
    
    return _enqueue(data, length, priority);
}

/* =================================================================================
 * SEND IMMEDIATE — ENVIA PACOTE IMEDIATAMENTE (IGNORA FILA)
 * =================================================================================
 */

bool LoRa::sendImmediate(const uint8_t* data, size_t length) {
    if (!_initialized) {
        DEBUG_PRINT("sendImmediate() — Módulo não inicializado\n");
        return false;
    }
    
    return _sendPacket(data, length);
}

/* =================================================================================
 * TRANSMIT — PROCESSA A FILA (CHAMAR NO LOOP)
 * =================================================================================
 */

void LoRa::transmit() {
    if (!_initialized) {
        return;
    }
    
    /* Se já está a transmitir, não fazer nada */
    if (_status == LORA_STATUS_BUSY) {
        /* Timeout de transmissão */
        if (millis() - _lastSendAttemptMs > LORA_TX_TIMEOUT_MS) {
            DEBUG_PRINT("transmit() — Timeout de transmissão\n");
            _status = LORA_STATUS_IDLE;
        }
        return;
    }
    
    /* Verificar se há pacotes na fila */
    if (_queueCount == 0) {
        return;
    }
    
    /* Obter próximo pacote (maior prioridade) */
    LoRaPacket packet;
    if (!_dequeue(&packet)) {
        return;
    }
    
    /* Enviar */
    _status = LORA_STATUS_BUSY;
    _lastSendAttemptMs = millis();
    
    bool success = _sendPacket(packet.data, packet.length);
    
    _status = LORA_STATUS_IDLE;
    
    if (!success) {
        DEBUG_PRINT("transmit() — Falha ao enviar pacote da fila\n");
    }
}

/* =================================================================================
 * SET FREQUENCY — ALTERA FREQUÊNCIA EM TEMPO REAL
 * =================================================================================
 */

bool LoRa::setFrequency(uint32_t freqHz) {
    if (!_initialized || _radio == nullptr) {
        return false;
    }
    
    int16_t state;
    
    switch (_moduleType) {
        case LORA_MODULE_SX1262: {
            SX1262* radio = static_cast<SX1262*>(_radio);
            state = radio->setFrequency(freqHz / 1000000.0f);
            break;
        }
        case LORA_MODULE_SX1276: {
            SX1276* radio = static_cast<SX1276*>(_radio);
            state = radio->setFrequency(freqHz / 1000000.0f);
            break;
        }
        default:
            return false;
    }
    
    if (state == RADIOLIB_ERR_NONE) {
        _config.frequency = freqHz;
        DEBUG_PRINT("setFrequency() — Nova frequência: %lu Hz\n", freqHz);
        return true;
    }
    
    DEBUG_PRINT("setFrequency() — Falha: %d\n", state);
    return false;
}

/* =================================================================================
 * SET TX POWER — ALTERA POTÊNCIA EM TEMPO REAL
 * =================================================================================
 */

bool LoRa::setTxPower(int8_t txPower) {
    if (!_initialized || _radio == nullptr) {
        return false;
    }
    
    int16_t state;
    
    switch (_moduleType) {
        case LORA_MODULE_SX1262: {
            SX1262* radio = static_cast<SX1262*>(_radio);
            state = radio->setOutputPower(txPower);
            break;
        }
        case LORA_MODULE_SX1276: {
            SX1276* radio = static_cast<SX1276*>(_radio);
            state = radio->setOutputPower(txPower);
            break;
        }
        default:
            return false;
    }
    
    if (state == RADIOLIB_ERR_NONE) {
        _config.txPower = txPower;
        DEBUG_PRINT("setTxPower() — Nova potência: %d dBm\n", txPower);
        return true;
    }
    
    DEBUG_PRINT("setTxPower() — Falha: %d\n", state);
    return false;
}

/* =================================================================================
 * IS READY — VERIFICA SE O MÓDULO ESTÁ PRONTO
 * =================================================================================
 */

bool LoRa::isReady() const {
    return (_initialized && _status != LORA_STATUS_BUSY && _status != LORA_STATUS_ERROR);
}

/* =================================================================================
 * GET STATUS — RETORNA O STATUS ATUAL
 * =================================================================================
 */

LoRaStatus LoRa::getStatus() const {
    return _status;
}

/* =================================================================================
 * GET STATS — RETORNA ESTATÍSTICAS
 * =================================================================================
 */

const LoRaStats& LoRa::getStats() const {
    _stats.queueSize = _queueCount;
    return _stats;
}

/* =================================================================================
 * RESET STATS — LIMPA ESTATÍSTICAS
 * =================================================================================
 */

void LoRa::resetStats() {
    memset(&_stats, 0, sizeof(LoRaStats));
    DEBUG_PRINT("resetStats() — Estatísticas reiniciadas\n");
}

/* =================================================================================
 * CLEAR QUEUE — LIMPA A FILA
 * =================================================================================
 */

void LoRa::clearQueue() {
    _queueHead = 0;
    _queueTail = 0;
    _queueCount = 0;
    DEBUG_PRINT("clearQueue() — Fila limpa\n");
}

/* =================================================================================
 * PRIORITY TO STRING — CONVERTE PRIORIDADE PARA TEXTO
 * =================================================================================
 */

const char* LoRa::priorityToString(LoRaPriority priority) {
    switch (priority) {
        case LORA_PRIORITY_FAILSAFE:  return "FAILSAFE";
        case LORA_PRIORITY_COMMAND:   return "COMMAND";
        case LORA_PRIORITY_TELEMETRY: return "TELEMETRY";
        case LORA_PRIORITY_SHELL:     return "SHELL";
        case LORA_PRIORITY_VIDEO:     return "VIDEO";
        case LORA_PRIORITY_DEBUG:     return "DEBUG";
        default:                      return "UNKNOWN";
    }
}

/* =================================================================================
 * SET DEBUG — ATIVA/DESATIVA DEBUG
 * =================================================================================
 */

void LoRa::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* loraStatusToString(LoRaStatus status) {
    switch (status) {
        case LORA_STATUS_IDLE:       return "IDLE";
        case LORA_STATUS_BUSY:       return "BUSY";
        case LORA_STATUS_ERROR:      return "ERROR";
        case LORA_STATUS_NO_MODULE:  return "NO_MODULE";
        default:                     return "UNKNOWN";
    }
}

const char* loraModuleTypeToString(LoRaModuleType type) {
    switch (type) {
        case LORA_MODULE_SX1262: return "SX1262";
        case LORA_MODULE_SX1276: return "SX1276";
        case LORA_MODULE_RAK811: return "RAK811";
        case LORA_MODULE_CUSTOM: return "CUSTOM";
        default:                 return "UNKNOWN";
    }
}
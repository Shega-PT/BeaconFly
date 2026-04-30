/**
 * =================================================================================
 * LORA.CPP — IMPLEMENTAÇÃO DO DRIVER LoRa 2.4GHz
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-17
 * VERSÃO:     2.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. Utiliza a biblioteca RadioLib para abstração de hardware
 * 2. Suporta múltiplos módulos através de inicialização condicional
 * 3. Operação não-bloqueante: startReceive() + available()
 * 4. Entrega apenas bytes crus — NÃO faz parsing ou validação
 * 
 * =================================================================================
 * BIBLIOTECAS NECESSÁRIAS
 * =================================================================================
 * 
 * Este módulo requer a biblioteca RadioLib:
 *   https://github.com/jgromes/RadioLib
 * 
 * Instalação via PlatformIO: lib_deps = jgromes/RadioLib
 * Instalação via Arduino IDE: Sketch → Include Library → Manage Libraries → RadioLib
 * 
 * =================================================================================
 */

#include "LoRa.h"

/* =================================================================================
 * INCLUSÃO CONDICIONAL DA RADIOLIB (APENAS NO .CPP)
 * =================================================================================
 * 
 * A RadioLib é pesada e tem muitos símbolos. Incluímos apenas no .cpp
 * para reduzir o tempo de compilação e evitar conflitos.
 */

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
 * 
 * Inicializa todas as variáveis internas com valores seguros.
 * O hardware ainda não foi configurado — isso ocorre em begin().
 */

LoRa::LoRa()
    : _rxLength(0)
    , _packetAvailable(false)
    , _initialized(false)
    , _debugEnabled(false)
    , _moduleType(LORA_MODULE_SX1280)
    , _radio(nullptr)
{
    _stats.reset();
    memset(_rxBuffer, 0, sizeof(_rxBuffer));
    
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO DO MÓDULO LoRa
 * =================================================================================
 * 
 * @param moduleType Tipo de módulo (SX1280, LLCC68, etc.)
 * @param pins       Pinos GPIO para CS, DIO1, RST, BUSY
 * @param config     Parâmetros RF (frequência, SF, BW, etc.)
 * @return true se o módulo foi inicializado com sucesso
 */

bool LoRa::begin(LoRaModuleType moduleType, const LoRaPins& pins, const LoRaConfig& config) {
    DEBUG_PRINT("begin() — Inicializando módulo tipo %d\n", (int)moduleType);
    
    _moduleType = moduleType;
    
    /* Inicialização específica por tipo de módulo */
    switch (moduleType) {
        case LORA_MODULE_SX1280:
        case LORA_MODULE_E220_24:   /* E220-2.4 é baseado em SX1280 */
            if (!_initSX1280(pins, config)) {
                DEBUG_PRINT("begin() — Falha na inicialização do SX1280\n");
                return false;
            }
            break;
            
        case LORA_MODULE_LLCC68:
            if (!_initLLCC68(pins, config)) {
                DEBUG_PRINT("begin() — Falha na inicialização do LLCC68\n");
                return false;
            }
            break;
            
        default:
            DEBUG_PRINT("begin() — Tipo de módulo não suportado: %d\n", (int)moduleType);
            return false;
    }
    
    _initialized = true;
    DEBUG_PRINT("begin() — Módulo inicializado com sucesso\n");
    
    return true;
}

/* =================================================================================
 * INIT SX1280 — INICIALIZAÇÃO ESPECÍFICA DO SX1280
 * =================================================================================
 * 
 * O SX1280 é o módulo mais comum para 2.4GHz. Suporta LoRa, FLRC e GFSK.
 * 
 * @param pins   Pinos GPIO (CS, DIO1, RST, BUSY)
 * @param config Parâmetros RF
 * @return true se sucesso
 */

bool LoRa::_initSX1280(const LoRaPins& pins, const LoRaConfig& config) {
    DEBUG_PRINT("_initSX1280() — CS=%d, DIO1=%d, RST=%d, BUSY=%d\n", 
                pins.cs, pins.dio1, pins.rst, pins.busy);
    
    /* Criar o objeto Module da RadioLib */
    Module* module = new Module(pins.cs, pins.dio1, pins.rst, pins.busy);
    
    /* Criar o objeto SX1280 */
    SX1280* radio = new SX1280(module);
    _radio = radio;  /* Guardar ponteiro para uso futuro */
    
    /* Inicializar o hardware */
    int16_t state = radio->begin();
    if (state != RADIOLIB_ERR_NONE) {
        DEBUG_PRINT("_initSX1280() — radio.begin() falhou: %d\n", state);
        delete radio;
        delete module;
        _radio = nullptr;
        return false;
    }
    
    /* Configurar parâmetros RF */
    state = radio->setFrequency(config.frequency / 1000000.0f);
    state |= radio->setSpreadingFactor(config.spreadingFactor);
    state |= radio->setBandwidth(config.bandwidth);
    state |= radio->setCodingRate(config.codingRate);
    state |= radio->setOutputPower(config.txPower);
    
    if (config.useCRC) {
        state |= radio->setCRC(RADIOLIB_CRC_ON);
    } else {
        state |= radio->setCRC(RADIOLIB_CRC_OFF);
    }
    
    if (config.invertIQ) {
        state |= radio->invertIQ(true);
    }
    
    if (state != RADIOLIB_ERR_NONE) {
        DEBUG_PRINT("_initSX1280() — configuração RF falhou: %d\n", state);
        delete radio;
        delete module;
        _radio = nullptr;
        return false;
    }
    
    /* Iniciar receção contínua (não-bloqueante) */
    radio->startReceive();
    
    DEBUG_PRINT("_initSX1280() — Configuração: freq=%.3fMHz, SF=%d, BW=%d, CR=%d, TX=%ddBm\n",
                config.frequency / 1000000.0f,
                config.spreadingFactor,
                config.bandwidth,
                config.codingRate,
                config.txPower);
    
    return true;
}

/* =================================================================================
 * INIT LLCC68 — INICIALIZAÇÃO ESPECÍFICA DO LLCC68 (PLACEHOLDER)
 * =================================================================================
 * 
 * O LLCC68 é um módulo mais económico, compatível com a família SX1280.
 * A implementação completa será adicionada numa versão futura.
 * 
 * @param pins   Pinos GPIO
 * @param config Parâmetros RF
 * @return true se sucesso (atualmente sempre false)
 */

bool LoRa::_initLLCC68(const LoRaPins& pins, const LoRaConfig& config) {
    DEBUG_PRINT("_initLLCC68() — A implementar em versão futura\n");
    
    /* TODO: Implementar inicialização do LLCC68 */
    /* A API é semelhante ao SX1280, mas requer confirmação */
    
    (void)pins;
    (void)config;
    
    return false;
}

/* =================================================================================
 * UPDATE — PROCESSAMENTO NÃO-BLOQUEANTE DE RECEÇÃO
 * =================================================================================
 * 
 * Deve ser chamada frequentemente no loop principal.
 * Verifica se há um pacote disponível no hardware e, se sim, lê os bytes
 * e disponibiliza via readPacket().
 * 
 * ATENÇÃO: Esta função NÃO bloqueia. O rádio opera em modo contínuo.
 */

void LoRa::update() {
    if (!_initialized || _radio == nullptr) {
        return;
    }
    
    /* Obter ponteiro para o rádio (cast para o tipo correto) */
    SX1280* radio = static_cast<SX1280*>(_radio);
    
    /* Verificar se há pacote disponível (não-bloqueante) */
    if (radio->available() > 0) {
        DEBUG_PRINT("update() — Pacote detetado\n");
        
        /* Obter tamanho do pacote */
        size_t len = radio->getPacketLength();
        if (len > sizeof(_rxBuffer)) {
            DEBUG_PRINT("update() — Pacote demasiado grande: %zu bytes (max: %zu)\n", 
                        len, sizeof(_rxBuffer));
            radio->startReceive();  /* Descartar e voltar a ouvir */
            return;
        }
        
        /* Ler os dados do rádio */
        uint8_t buffer[1024];
        int16_t rssi = radio->getRSSI();
        int16_t snr = radio->getSNR();
        int16_t state = radio->readData(buffer, len);
        
        if (state == RADIOLIB_ERR_NONE) {
            DEBUG_PRINT("update() — Pacote recebido: %zu bytes, RSSI=%d, SNR=%d\n", 
                        len, rssi, snr);
            _processRaw(buffer, len, rssi, snr);
        } else {
            DEBUG_PRINT("update() — Erro na leitura: %d\n", state);
            _stats.packetsRejected++;
        }
        
        /* Reiniciar modo de receção para o próximo pacote */
        radio->startReceive();
    }
}

/* =================================================================================
 * PROCESS RAW — PROCESSAMENTO INTERNO DE PACOTE RECEBIDO
 * =================================================================================
 * 
 * Apenas guarda os bytes crus no buffer e atualiza estatísticas.
 * NÃO faz parsing, NÃO valida CRC do protocolo, NÃO chama Security.
 * 
 * @param buffer Bytes recebidos
 * @param length Número de bytes
 * @param rssi   Força do sinal (dBm)
 * @param snr    Relação sinal-ruído (dB)
 */

void LoRa::_processRaw(const uint8_t* buffer, size_t length, int16_t rssi, int16_t snr) {
    /* Atualizar estatísticas */
    _stats.packetsReceived++;
    _stats.lastRSSI = rssi;
    _stats.lastSNR = snr;
    _stats.lastPacketTime = millis();
    
    /* Verificar se o buffer interno tem capacidade */
    if (length <= sizeof(_rxBuffer)) {
        memcpy(_rxBuffer, buffer, length);
        _rxLength = length;
        _packetAvailable = true;
        _stats.packetsValid++;
        
        DEBUG_PRINT("_processRaw() — Pacote armazenado: %zu bytes\n", length);
    } else {
        DEBUG_PRINT("_processRaw() — Buffer cheio: %zu bytes disponíveis, %zu recebidos\n",
                    sizeof(_rxBuffer), length);
        _stats.packetsRejected++;
    }
}

/* =================================================================================
 * AVAILABLE — VERIFICA SE HÁ PACOTE DISPONÍVEL
 * =================================================================================
 * 
 * @return true se há um pacote no buffer interno aguardando leitura
 */

bool LoRa::available() const {
    return _packetAvailable;
}

/* =================================================================================
 * READ PACKET — LÊ O PRÓXIMO PACOTE DISPONÍVEL
 * =================================================================================
 * 
 * @param buffer Buffer de destino (deve ter capacidade para MAX_MESSAGE_SIZE)
 * @param maxLen Tamanho máximo do buffer (recomendado: 1024)
 * @return Número de bytes lidos (0 se nenhum pacote disponível)
 * 
 * NOTA: Após a leitura, o pacote é removido do buffer interno.
 *       O chamador deve alimentar os bytes no Parser byte a byte.
 */

size_t LoRa::readPacket(uint8_t* buffer, size_t maxLen) {
    if (!_packetAvailable) {
        return 0;
    }
    
    if (maxLen < _rxLength) {
        DEBUG_PRINT("readPacket() — Buffer de destino demasiado pequeno: %zu < %zu\n",
                    maxLen, _rxLength);
        return 0;
    }
    
    memcpy(buffer, _rxBuffer, _rxLength);
    _packetAvailable = false;
    
    DEBUG_PRINT("readPacket() — %zu bytes lidos\n", _rxLength);
    
    return _rxLength;
}

/* =================================================================================
 * SEND PACKET — ENVIA UM PACOTE VIA LoRa
 * =================================================================================
 * 
 * @param data   Ponteiro para os dados a enviar
 * @param length Número de bytes a enviar
 * @return true se o envio foi iniciado com sucesso
 * 
 * NOTA: Esta função é bloqueante durante a transmissão (pode levar até ~100ms).
 *       Para telemetria pesada, usar ESP2 com 868MHz.
 */

bool LoRa::sendPacket(const uint8_t* data, size_t length) {
    if (!_initialized || _radio == nullptr) {
        DEBUG_PRINT("sendPacket() — Módulo não inicializado\n");
        return false;
    }
    
    if (length > 256) {
        DEBUG_PRINT("sendPacket() — Pacote demasiado grande: %zu bytes\n", length);
        return false;
    }
    
    SX1280* radio = static_cast<SX1280*>(_radio);
    
    DEBUG_PRINT("sendPacket() — Enviando %zu bytes\n", length);
    
    /* Transmitir (bloqueante) */
    int16_t state = radio->transmit((uint8_t*)data, length);
    
    if (state == RADIOLIB_ERR_NONE) {
        DEBUG_PRINT("sendPacket() — Envio bem-sucedido\n");
        return true;
    } else {
        DEBUG_PRINT("sendPacket() — Erro no envio: %d\n", state);
        return false;
    }
}

/* =================================================================================
 * CONFIGURAÇÃO DINÂMICA
 * =================================================================================
 */

void LoRa::setFrequency(uint32_t freqHz) {
    if (!_initialized || _radio == nullptr) return;
    
    SX1280* radio = static_cast<SX1280*>(_radio);
    float freqMHz = freqHz / 1000000.0f;
    
    int16_t state = radio->setFrequency(freqMHz);
    if (state == RADIOLIB_ERR_NONE) {
        DEBUG_PRINT("setFrequency() — %lu Hz (%.3f MHz)\n", freqHz, freqMHz);
    } else {
        DEBUG_PRINT("setFrequency() — Falhou: %d\n", state);
    }
}

void LoRa::setTxPower(int8_t dBm) {
    if (!_initialized || _radio == nullptr) return;
    
    SX1280* radio = static_cast<SX1280*>(_radio);
    
    int16_t state = radio->setOutputPower(dBm);
    if (state == RADIOLIB_ERR_NONE) {
        DEBUG_PRINT("setTxPower() — %d dBm\n", dBm);
    } else {
        DEBUG_PRINT("setTxPower() — Falhou: %d\n", state);
    }
}

/* =================================================================================
 * ESTATÍSTICAS E DIAGNÓSTICO
 * =================================================================================
 */

const LoRaStats& LoRa::getStats() const {
    return _stats;
}

void LoRa::resetStats() {
    _stats.reset();
    DEBUG_PRINT("resetStats() — Estatísticas reiniciadas\n");
}

void LoRa::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}
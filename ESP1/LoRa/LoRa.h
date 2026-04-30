#pragma once

/**
 * =================================================================================
 * LORA.H — DRIVER DE COMUNICAÇÃO LoRa 2.4GHz (ESP1 → GS)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-17
 * VERSÃO:     2.0.0 (Simplificada — apenas receção de bytes crus)
 * 
 * =================================================================================
 * FILOSOFIA DE DESIGN
 * =================================================================================
 * 
 * Este módulo tem UMA e UMA ÚNICA responsabilidade: converter ondas de rádio
 * em bytes legíveis. NÃO faz parsing, NÃO valida segurança, NÃO desmonta TLV.
 * 
 * RESPONSABILIDADES CLARAS:
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │  LoRa (este módulo)      → Recebe bytes do rádio → disponibiliza bytes  │
 *   │  Parser (outro módulo)   → Byte a byte → mensagem TLV estruturada       │
 *   │  Security (outro módulo) → Mensagem → valida HMAC/SEQ → autenticada     │
 *   │  FlightControl/Shell     → Comando validado → ação                      │
 *   └─────────────────────────────────────────────────────────────────────────┘
 * 
 * PORQUÊ ESTA SEPARAÇÃO?
 * 
 *   1. Single Responsibility Principle — cada módulo faz uma coisa bem feita
 *   2. Testabilidade — podemos simular pacotes sem hardware LoRa
 *   3. Portabilidade — o mesmo LoRa funciona com qualquer parser/security
 *   4. Manutenibilidade — alterar o rádio não afeta a lógica de voo
 * 
 * =================================================================================
 * FLUXO DE DADOS TÍPICO
 * =================================================================================
 * 
 *   Ground Station (GS) → LoRa 2.4GHz → ESP1
 *                                            │
 *                                            ▼
 *                                       LoRa::update()
 *                                            │
 *                                            ▼ (raw bytes)
 *                                       LoRa::readPacket(buffer, len)
 *                                            │
 *                                            ▼
 *                                       for each byte:
 *                                           Parser::feed(byte)
 *                                            │
 *                                            ▼
 *                                       if (Parser::hasMessage())
 *                                            │
 *                                            ▼
 *                                       Security::verifyPacket(msg)
 *                                            │
 *                                            ▼ (autenticada)
 *                                       FlightControl::processCommand(msg)
 *                                       Shell::processCommand(msg)
 * 
 * =================================================================================
 * MÓDULOS LoRa SUPORTADOS
 * =================================================================================
 * 
 *   • SX1280     — Semtech, 2.4GHz, mais comum e testado
 *   • LLCC68     — Versão económica, mesma família SX1280
 *   • E220-2.4   — Módulo Ebyte baseado em SX1280
 * 
 * A arquitetura é extensível para novos módulos através da enum LoRaModuleType.
 * 
 * =================================================================================
 */

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

/* =================================================================================
 * TIPOS DE MÓDULOS LoRa SUPORTADOS
 * =================================================================================
 */

enum LoRaModuleType : uint8_t {
    LORA_MODULE_SX1280 = 0,     /* Semtech SX1280 — 2.4GHz, recomendado */
    LORA_MODULE_LLCC68,         /* LLCC68 — versão económica */
    LORA_MODULE_E220_24,        /* Ebyte E220-2.4 — baseado em SX1280 */
    LORA_MODULE_CUSTOM          /* Para integração de drivers personalizados */
};

/* =================================================================================
 * CONFIGURAÇÃO DE HARDWARE (PINOS)
 * =================================================================================
 * 
 * Os pinos variam conforme a PCB e o módulo utilizado.
 * Esta struct permite configurar dinamicamente sem recompilar.
 */

struct LoRaPins {
    uint8_t cs;          /* Chip Select (SPI) — pino de seleção do módulo */
    uint8_t dio1;        /* Digital I/O 1 — interrupção de pacote recebido */
    uint8_t rst;         /* Reset — reinicialização do módulo */
    uint8_t busy;        /* Busy — indica se o módulo está ocupado */
    
    /* Valores padrão para PCB BeaconFly v1.0 */
    static LoRaPins defaultPins() {
        LoRaPins pins;
        pins.cs = 5;
        pins.dio1 = 4;
        pins.rst = 2;
        pins.busy = 15;
        return pins;
    }
};

/* =================================================================================
 * CONFIGURAÇÃO RF (PARÂMETROS DE RÁDIO)
 * =================================================================================
 * 
 * Estes parâmetros afetam o alcance, a velocidade e a robustez da ligação.
 * Devem ser consistentes entre a Ground Station e o ESP1.
 */

struct LoRaConfig {
    uint32_t frequency;          /* Frequência central em Hz (ex: 2.400.000.000) */
    uint8_t  spreadingFactor;    /* Spreading Factor (5 a 12) — maior = mais alcance */
    uint8_t  bandwidth;          /* Largura de banda (0=7.8kHz a 9=1625kHz) */
    uint8_t  codingRate;         /* Coding Rate (5=4/5, 6=4/6, 7=4/7, 8=4/8) */
    int8_t   txPower;            /* Potência de transmissão em dBm (-18 a +13) */
    bool     useCRC;             /* Ativar CRC interno do rádio (recomendado) */
    bool     invertIQ;           /* Inverted IQ — reduz interferência entre módulos */
    
    /* Configuração padrão para voo normal (alcance vs velocidade equilibrado) */
    static LoRaConfig defaultConfig() {
        LoRaConfig cfg;
        cfg.frequency       = 2400000000UL;  /* 2.4 GHz */
        cfg.spreadingFactor = 9;             /* SF9 — bom equilíbrio */
        cfg.bandwidth       = 4;             /* 400 kHz */
        cfg.codingRate      = 7;             /* 4/7 */
        cfg.txPower         = 10;            /* 10 dBm ~ 10mW */
        cfg.useCRC          = true;
        cfg.invertIQ        = false;
        return cfg;
    }
};

/* =================================================================================
 * ESTATÍSTICAS DE LIGAÇÃO (DIAGNÓSTICO)
 * =================================================================================
 * 
 * Útil para monitorizar a qualidade do link e diagnosticar problemas de RF.
 * Os valores são atualizados automaticamente a cada pacote recebido.
 */

struct LoRaStats {
    uint32_t packetsReceived;    /* Total de pacotes recebidos (incluindo inválidos) */
    uint32_t packetsValid;       /* Pacotes que passaram o CRC do rádio */
    uint32_t packetsRejected;    /* Pacotes rejeitados (buffer cheio ou erro interno) */
    int16_t  lastRSSI;           /* Received Signal Strength Indicator (dBm) */
    int16_t  lastSNR;            /* Signal-to-Noise Ratio (dB) */
    uint32_t lastPacketTime;     /* Timestamp do último pacote (millis) */
    
    /* Reseta todas as estatísticas para zero */
    void reset() {
        packetsReceived = 0;
        packetsValid = 0;
        packetsRejected = 0;
        lastRSSI = 0;
        lastSNR = 0;
        lastPacketTime = 0;
    }
};

/* =================================================================================
 * CLASSE LoRa — DRIVER PRINCIPAL
 * =================================================================================
 * 
 * ATENÇÃO: Esta classe NÃO faz parsing nem validação de segurança.
 *          Apenas entrega bytes crus. O Parser e o Security fazem o resto.
 */

class LoRa {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    /**
     * Construtor — inicializa todas as variáveis internas com valores seguros
     */
    LoRa();
    
    /**
     * Inicializa o módulo LoRa com a configuração especificada
     * 
     * @param moduleType Tipo de módulo (SX1280, LLCC68, etc.)
     * @param pins       Pinos GPIO para CS, DIO1, RST, BUSY
     * @param config     Parâmetros RF (frequência, SF, BW, etc.)
     * @return true se o módulo foi inicializado com sucesso
     */
    bool begin(LoRaModuleType moduleType, const LoRaPins& pins, const LoRaConfig& config);
    
    /* =========================================================================
     * OPERAÇÃO PRINCIPAL (CHAMAR NO LOOP)
     * ========================================================================= */
    
    /**
     * Processa a receção de pacotes — NÃO BLOQUEANTE
     * 
     * Deve ser chamada frequentemente no loop principal (a cada ciclo).
     * Verifica se há um pacote disponível no hardware, lê os bytes e
     * disponibiliza via readPacket().
     * 
     * ATENÇÃO: Esta função NÃO bloqueia. O rádio opera em modo contínuo.
     */
    void update();
    
    /* =========================================================================
     * LEITURA DE PACOTES (BYTES CRUS)
     * ========================================================================= */
    
    /**
     * Verifica se existe um pacote disponível para leitura
     * 
     * @return true se há um pacote no buffer interno
     */
    bool available() const;
    
    /**
     * Lê o próximo pacote disponível (bytes crus)
     * 
     * @param buffer Buffer de destino (deve ter capacidade para MAX_MESSAGE_SIZE)
     * @param maxLen Tamanho máximo do buffer (recomendado: 1024)
     * @return Número de bytes lidos (0 se nenhum pacote disponível)
     * 
     * NOTA: Os bytes são EXATAMENTE como vieram do rádio, sem qualquer
     *       processamento. O chamador (Parser) deve alimentá-los byte a byte.
     */
    size_t readPacket(uint8_t* buffer, size_t maxLen);
    
    /* =========================================================================
     * TRANSMISSÃO (RESPOSTAS/ACKS)
     * ========================================================================= */
    
    /**
     * Envia um pacote via LoRa (para ACKs ou telemetria leve)
     * 
     * @param data   Ponteiro para os dados a enviar
     * @param length Número de bytes a enviar
     * @return true se o envio foi iniciado com sucesso
     * 
     * NOTA: Esta função é bloqueante durante a transmissão.
     *       Para telemetria pesada, usar ESP2 com 868MHz.
     */
    bool sendPacket(const uint8_t* data, size_t length);
    
    /* =========================================================================
     * CONFIGURAÇÃO DINÂMICA (EM RUNTIME)
     * ========================================================================= */
    
    /**
     * Altera a frequência de operação em tempo real
     * 
     * @param freqHz Nova frequência em Hz (ex: 2400000000)
     */
    void setFrequency(uint32_t freqHz);
    
    /**
     * Altera a potência de transmissão em tempo real
     * 
     * @param dBm Nova potência em dBm (-18 a +13)
     */
    void setTxPower(int8_t dBm);
    
    /* =========================================================================
     * ESTATÍSTICAS E DIAGNÓSTICO
     * ========================================================================= */
    
    /**
     * Retorna as estatísticas atuais da ligação
     * 
     * @return Referência const para a struct de estatísticas
     */
    const LoRaStats& getStats() const;
    
    /**
     * Reseta todas as estatísticas (packetsReceived, etc.) para zero
     */
    void resetStats();
    
    /**
     * Ativa/desativa o modo debug (imprime via Serial)
     * 
     * @param enable true = ativar mensagens de diagnóstico
     */
    void setDebug(bool enable);
    
private:
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    LoRaStats   _stats;              /* Estatísticas de ligação */
    uint8_t     _rxBuffer[1024];     /* Buffer de receção (bytes crus) */
    size_t      _rxLength;           /* Tamanho do último pacote recebido */
    bool        _packetAvailable;    /* Há pacote disponível para leitura? */
    bool        _initialized;        /* Módulo inicializado com sucesso? */
    bool        _debugEnabled;       /* Debug ativo? */
    LoRaModuleType _moduleType;      /* Tipo de módulo em uso */
    
    /* Ponteiro opaco para o objeto rádio (evita include de RadioLib no .h) */
    void*       _radio;              /* Ponteiro para SX1280*, LLCC68*, etc. */
    
    /* =========================================================================
     * MÉTODOS PRIVADOS DE INICIALIZAÇÃO POR MÓDULO
     * ========================================================================= */
    
    /**
     * Inicializa o módulo SX1280
     * 
     * @param pins   Pinos GPIO (CS, DIO1, RST, BUSY)
     * @param config Parâmetros RF
     * @return true se sucesso
     */
    bool _initSX1280(const LoRaPins& pins, const LoRaConfig& config);
    
    /**
     * Inicializa o módulo LLCC68
     * 
     * @param pins   Pinos GPIO
     * @param config Parâmetros RF
     * @return true se sucesso (placeholder para futuro)
     */
    bool _initLLCC68(const LoRaPins& pins, const LoRaConfig& config);
    
    /**
     * Processa um pacote recebido do hardware
     * 
     * @param buffer Bytes recebidos
     * @param length Número de bytes
     * @param rssi   Força do sinal (dBm)
     * @param snr    Relação sinal-ruído (dB)
     */
    void _processRaw(const uint8_t* buffer, size_t length, int16_t rssi, int16_t snr);
};

#endif /* LORA_H */
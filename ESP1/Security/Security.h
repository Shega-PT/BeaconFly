#pragma once

/**
 * =================================================================================
 * SECURITY.H — MÓDULO DE SEGURANÇA TRANSVERSAL (ESP1/ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-17
 * VERSÃO:     2.0.0 (Fusão VA + VB com implementação real de HMAC)
 * 
 * =================================================================================
 * FILOSOFIA DE DESIGN — "FAIL SECURE"
 * =================================================================================
 * 
 * Este módulo é a última linha de defesa do sistema BeaconFly.
 * 
 * PRINCÍPIO FUNDAMENTAL:
 *   Em caso de dúvida ou anomalia → BLOQUEAR.
 *   
 *   Uma mensagem bloqueada indevidamente é recuperável (perda de pacote).
 *   Um comando perigoso executado pode ser catastrófico (perda da aeronave).
 * 
 * =================================================================================
 * CATEGORIAS DE PROTEÇÃO
 * =================================================================================
 * 
 *   1. SEGURANÇA DE COMUNICAÇÃO (ameaças externas)
 *      • Autenticação HMAC-SHA256 — garante que a mensagem vem da GS legítima
 *      • Anti-replay com janela deslizante — impede gravação e reenvio
 *      • Rate limiting — proteção contra flood de comandos
 *      • Challenge-response — autenticação inicial da Ground Station
 *      • Lockdown — bloqueio total após violações repetidas
 * 
 *   2. SEGURANÇA OPERACIONAL (proteção contra erros)
 *      • Sanidade de comandos — limites físicos (ex: roll < 60°)
 *      • Sanidade de sensores — valores impossíveis são rejeitados
 *      • Contexto de voo — comandos só permitidos no estado correto
 *      • Envelope de voo — limites estruturais da aeronave
 *      • Geofencing — restrição geográfica (opcional)
 *      • Watchdog — deteção de travamentos
 * 
 * =================================================================================
 * ESTADOS DE SEGURANÇA
 * =================================================================================
 * 
 *   NORMAL    → Operação normal, todos os comandos permitidos dentro dos limites
 *   CAUTION   → Limites reduzidos, alerta ativo
 *   WARNING   → Limites muito reduzidos, apenas ações essenciais
 *   CRITICAL  → Apenas comandos de emergência (RTL, LAND, DISARM)
 *   LOCKDOWN  → Apenas MSG_FAILSAFE é aceite, requer reset físico
 * 
 * =================================================================================
 */

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include "Protocol.h"

/* =================================================================================
 * CONFIGURAÇÃO DE SEGURANÇA
 * =================================================================================
 */

/**
 * SECURITY_HMAC_KEY_SIZE — Tamanho da chave HMAC em bytes
 * 
 * SHA-256 requer chave de 32 bytes (256 bits)
 */
#define SECURITY_HMAC_KEY_SIZE      32

/**
 * SECURITY_HMAC_SIZE — Tamanho do HMAC-SHA256 em bytes
 */
#define SECURITY_HMAC_SIZE          32

/**
 * SECURITY_SEQ_WINDOW — Janela anti-replay (bits)
 * 
 * 32 bits = 32 mensagens. Uma janela maior aumenta a tolerância
 * a pacotes fora de ordem mas consome mais memória.
 */
#define SECURITY_SEQ_WINDOW         32

/**
 * SECURITY_MAX_VIOLATIONS — Número máximo de violações antes do lockdown
 */
#define SECURITY_MAX_VIOLATIONS     10

/* =================================================================================
 * RATE LIMITING — Intervalos mínimos entre comandos (milissegundos)
 * =================================================================================
 * 
 * Críticos:   ARM, DISARM, REBOOT, SHUTDOWN — 500ms
 * Controlo:   SET_ROLL, SET_PITCH, SET_YAW, SET_THROTTLE — 20ms (50Hz)
 * Manutenção: SET_PARAM, CALIB, etc. — 5s
 */

#define RATE_LIMIT_CRITICAL_MS      500
#define RATE_LIMIT_CONTROL_MS        20
#define RATE_LIMIT_MAINTENANCE_MS  5000

/* =================================================================================
 * LIMITES DE SANIDADE — COMANDOS
 * =================================================================================
 * 
 * Estes são os limites MÁXIMOS absolutos. Comandos que ultrapassem
 * estes valores são rejeitados imediatamente (fail-secure).
 */

#define SANITY_ROLL_MAX             60.0f      /* Graus — máximo ângulo de roll */
#define SANITY_PITCH_MAX            45.0f      /* Graus — máximo ângulo de pitch */
#define SANITY_YAW_MAX             180.0f      /* Graus/segundo — máximo yaw rate */
#define SANITY_THROTTLE_MAX          1.0f      /* 0-1 — potência normalizada */
#define SANITY_ALT_MAX            1000.0f      /* Metros — altitude máxima segura */

/* =================================================================================
 * LIMITES DE SANIDADE — SENSORES
 * =================================================================================
 * 
 * Valores de sensores fora destes limites são considerados inválidos
 * e não devem ser usados para controlo de voo.
 */

#define SANITY_ACCEL_MAX           100.0f      /* m/s² — aceleração máxima (10G) */
#define SANITY_GYRO_MAX           2000.0f      /* °/s — velocidade angular máxima */
#define SANITY_BATT_V_MIN            6.0f      /* Volts — bateria mínima (2S LiPo) */
#define SANITY_BATT_V_MAX           30.0f      /* Volts — bateria máxima (6S LiPo) */
#define SANITY_TEMP_MIN           -20.0f       /* °C — temperatura mínima */
#define SANITY_TEMP_MAX            80.0f       /* °C — temperatura máxima */

/* =================================================================================
 * ENUMS PRINCIPAIS
 * =================================================================================
 */

/**
 * SecurityResult — Resultado da verificação de segurança
 * 
 * Utilizado como返回值 de todas as funções de validação.
 * Valores diferentes de SEC_OK indicam uma violação de segurança.
 */
enum SecurityResult : uint8_t {
    SEC_OK                    = 0,   /* Tudo ok — mensagem/comando válido */
    SEC_INVALID_HMAC          = 1,   /* HMAC inválido — autenticação falhou */
    SEC_REPLAY_DETECTED       = 2,   /* Replay detectado — sequência repetida */
    SEC_RATE_LIMITED          = 3,   /* Rate limit excedido — flood detectado */
    SEC_SANITY_FAILED         = 4,   /* Sanidade falhou — valor fora dos limites */
    SEC_LOCKDOWN              = 5,   /* Lockdown ativo — apenas failsafe aceite */
    SEC_WRONG_STATE           = 6,   /* Estado errado — comando não permitido agora */
    SEC_AUTH_REQUIRED         = 7,   /* Autenticação necessária — GS não autenticada */
    SEC_ENVELOPE_EXCEEDED     = 8,   /* Envelope de voo excedido */
    SEC_GEOFENCE_VIOLATION    = 9,   /* Geofencing violado (opcional) */
    SEC_WATCHDOG_TIMEOUT      = 10   /* Watchdog timeout — sistema travado? */
};

/**
 * FlightState — Estado atual do voo (contexto para validações)
 * 
 * Utilizado para validar se um comando é permitido no estado atual.
 * Exemplo: ARM só permitido em solo, DISARM só permitido após voo.
 */
enum FlightState : uint8_t {
    STATE_GROUND_DISARMED = 0,   /* Em solo, sistema desarmado */
    STATE_GROUND_ARMED    = 1,   /* Em solo, sistema armado (hélices prontas) */
    STATE_TAKEOFF         = 2,   /* Descolagem em progresso */
    STATE_FLYING          = 3,   /* Em voo normal */
    STATE_LANDING         = 4,   /* Aterragem em progresso */
    STATE_FAILSAFE        = 5    /* Modo failsafe ativo */
};

/**
 * SecurityLevel — Nível atual de segurança (progressivo)
 * 
 * Quanto maior o nível, mais restritivo é o sistema.
 * O nível aumenta automaticamente com violações recorrentes.
 */
enum SecurityLevel : uint8_t {
    SECURITY_LEVEL_NORMAL    = 0,   /* Operação normal — todos os comandos OK */
    SECURITY_LEVEL_CAUTION   = 1,   /* Atenção — limites reduzidos */
    SECURITY_LEVEL_WARNING   = 2,   /* Perigo — limites muito reduzidos */
    SECURITY_LEVEL_CRITICAL  = 3,   /* Crítico — apenas comandos de emergência */
    SECURITY_LEVEL_LOCKDOWN  = 4    /* Lockdown — apenas failsafe aceite */
};

/* =================================================================================
 * ESTRUTURAS DE DADOS
 * =================================================================================
 */

/**
 * SecurityStats — Estatísticas de segurança (para telemetria e diagnóstico)
 * 
 * Útil para monitorizar a saúde do sistema e detectar ataques.
 */
struct SecurityStats {
    uint32_t packetsVerified = 0;      /* Total de pacotes verificados */
    uint32_t hmacFailures = 0;         /* Falhas de HMAC */
    uint32_t replayAttempts = 0;       /* Tentativas de replay detectadas */
    uint32_t rateLimitHits = 0;        /* Rate limit excedido */
    uint32_t sanityFailures = 0;       /* Falhas de sanidade */
    uint32_t wrongStateBlocks = 0;     /* Comandos bloqueados por estado errado */
    uint32_t envelopeViolations = 0;   /* Violações de envelope de voo */
    uint32_t lockdownEvents = 0;       /* Número de lockdowns ativados */
    uint32_t lastViolationTime = 0;    /* Timestamp da última violação */
    SecurityResult lastViolationReason = SEC_OK;  /* Última razão de violação */
};

/**
 * SecurityConfig — Configuração avançada do módulo
 * 
 * Permite ativar/desativar funcionalidades opcionais.
 */
struct SecurityConfig {
    bool enableFailSecure = true;      /* Ativar modo Fail Secure (recomendado) */
    bool enableGeofence   = false;     /* Ativar geofencing (requer GPS) */
    bool enableWatchdog   = true;      /* Ativar watchdog interno */
    float geofenceRadius  = 500.0f;    /* Raio do geofence em metros (se ativo) */
    float geofenceLat     = 0.0f;      /* Centro do geofence — latitude */
    float geofenceLon     = 0.0f;      /* Centro do geofence — longitude */
};

/* =================================================================================
 * CLASSE SECURITY
 * =================================================================================
 */

class Security {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    /**
     * Construtor — inicializa todas as variáveis internas
     */
    Security();
    
    /**
     * Inicializa o módulo de segurança
     * 
     * Carrega a chave HMAC (de NVS/eFuse em produção, de fallback em dev),
     * inicializa contadores e estados, e gera o primeiro challenge.
     */
    void begin();
    
    /**
     * Carrega uma chave HMAC personalizada (para produção)
     * 
     * @param key Ponteiro para a chave (32 bytes)
     * @param keyLen Tamanho da chave (deve ser SECURITY_HMAC_KEY_SIZE)
     * @return true se a chave foi carregada com sucesso
     */
    bool loadKey(const uint8_t* key, size_t keyLen);

    /* =========================================================================
     * VERIFICAÇÃO DE PACOTES (COMUNICAÇÃO GS → ESP1)
     * ========================================================================= */
    
    /**
     * Verificação completa de pacote recebido da Ground Station
     * 
     * Deve ser chamada para TODOS os pacotes ANTES de qualquer processamento.
     * Esta função verifica:
     *   1. Lockdown — rejeita tudo exceto MSG_FAILSAFE
     *   2. Autenticação da GS — exceto pacotes críticos
     *   3. HMAC — integridade e autenticidade
     *   4. Anti-replay — sequência única
     *   5. Rate limiting — proteção contra flood
     * 
     * @param msg    Mensagem TLV a verificar
     * @param seqNum Número de sequência (do cabeçalho da mensagem)
     * @param hmac   HMAC-SHA256 da mensagem (32 bytes)
     * @return SecurityResult — SEC_OK se tudo válido
     */
    SecurityResult verifyPacket(const TLVMessage& msg, uint32_t seqNum, const uint8_t* hmac);

    /* =========================================================================
     * VALIDAÇÃO DE COMANDOS (SEGURANÇA OPERACIONAL)
     * ========================================================================= */
    
    /**
     * Verifica sanidade e contexto de um comando antes de executar
     * 
     * @param cmdID ID do comando (ex: CMD_SET_ROLL)
     * @param value Valor do comando (se aplicável)
     * @return SecurityResult — SEC_OK se comando seguro
     */
    SecurityResult verifyCommand(uint8_t cmdID, float value);
    
    /**
     * Verifica sanidade de dados de sensores antes de usar no controlo
     * 
     * @param fieldID ID do campo TLV (ex: FLD_ROLL)
     * @param value Valor do sensor
     * @return SecurityResult — SEC_OK se valor dentro dos limites
     */
    SecurityResult verifySensorData(uint8_t fieldID, float value);

    /* =================================================================================
     * CONTEXTO DE VOO (VALIDAÇÕES CONTEXTUAIS)
     * ================================================================================= */
    
    /**
     * Atualiza o estado atual de voo
     * 
     * Deve ser chamado sempre que o FlightControl muda de estado.
     * 
     * @param state Novo estado de voo
     */
    void setFlightState(FlightState state);
    
    /**
     * Retorna o estado atual de voo
     */
    FlightState getFlightState() const;
    
    /**
     * Retorna o nível atual de segurança
     */
    SecurityLevel getSecurityLevel() const;

    /* =========================================================================
     * AUTENTICAÇÃO DA GROUND STATION (CHALLENGE-RESPONSE)
     * ========================================================================= */
    
    /**
     * Gera um challenge para autenticação da Ground Station
     * 
     * @return Challenge de 32 bits (número aleatório)
     */
    uint32_t generateChallenge();
    
    /**
     * Verifica a resposta da Ground Station ao challenge
     * 
     * @param response Resposta da GS (HMAC do challenge)
     * @param len Tamanho da resposta (deve ser SECURITY_HMAC_SIZE)
     * @return true se a resposta é válida
     */
    bool verifyChallengeResponse(const uint8_t* response, size_t len);
    
    /**
     * Verifica se a Ground Station está autenticada
     */
    bool isGSAuthenticated() const;

    /* =========================================================================
     * HMAC (CRIPTOGRAFIA)
     * ========================================================================= */
    
    /**
     * Calcula HMAC-SHA256 de um buffer
     * 
     * Utilizado pelo módulo de comunicação para assinar pacotes de saída.
     * 
     * @param data   Dados a assinar
     * @param len    Tamanho dos dados
     * @param output Buffer de saída (deve ter SECURITY_HMAC_SIZE bytes)
     */
    void computeHMAC(const uint8_t* data, size_t len, uint8_t* output);

    /* =========================================================================
     * LOCKDOWN E VIOLAÇÕES
     * ========================================================================= */
    
    /**
     * Ativa o modo Lockdown manualmente
     * 
     * @param reason Razão textual para o lockdown (para logging)
     */
    void triggerLockdown(const char* reason);
    
    /**
     * Verifica se o sistema está em lockdown
     */
    bool isInLockdown() const;
    
    /**
     * Reseta o contador de violações (chamar após pacote válido)
     */
    void resetViolationCount();

    /* =========================================================================
     * ESTATÍSTICAS E DIAGNÓSTICO
     * ========================================================================= */
    
    /**
     * Retorna estatísticas de segurança (para telemetria)
     */
    const SecurityStats& getStats() const;
    
    /**
     * Reseta todas as estatísticas
     */
    void resetStats();

    /* =========================================================================
     * CONFIGURAÇÃO
     * ========================================================================= */
    
    /**
     * Configuração avançada (Geofence, Watchdog, etc.)
     * 
     * @param config Nova configuração
     */
    void configure(const SecurityConfig& config);
    
    /**
     * Ativa/desativa modo debug
     * 
     * @param enable true = ativar mensagens de diagnóstico
     */
    void setDebug(bool enable);

private:
    /* =========================================================================
     * CHAVE E ESTADO HMAC
     * ========================================================================= */
    
    uint8_t _hmacKey[SECURITY_HMAC_KEY_SIZE];   /* Chave secreta HMAC */
    bool    _keyLoaded;                          /* Chave carregada? */
    
    /* =========================================================================
     * ANTI-REPLAY (JANELA DESLIZANTE)
     * ========================================================================= */
    
    uint32_t _lastSeqNum;                        /* Último número de sequência */
    uint32_t _seqBitmap;                         /* Bitmap de 32 bits (janela) */
    
    /* =========================================================================
     * RATE LIMITING
     * ========================================================================= */
    
    uint32_t _lastCriticalMs;    /* Último comando crítico (ARM, DISARM, etc.) */
    uint32_t _lastControlMs;     /* Último comando de controlo (ROLL, PITCH, etc.) */
    uint32_t _lastMaintenanceMs; /* Último comando de manutenção (SET_PARAM, etc.) */
    
    /* =========================================================================
     * CONTEXTO DE VOO
     * ========================================================================= */
    
    FlightState   _flightState;                  /* Estado atual do voo */
    SecurityLevel _currentLevel;                 /* Nível atual de segurança */
    
    /* =========================================================================
     * AUTENTICAÇÃO DA GROUND STATION
     * ========================================================================= */
    
    bool     _gsAuthenticated;                   /* GS autenticada? */
    uint32_t _currentChallenge;                  /* Challenge ativo */
    
    /* =========================================================================
     * LOCKDOWN E VIOLAÇÕES
     * ========================================================================= */
    
    bool        _lockdown;                       /* Lockdown ativo? */
    uint8_t     _violationCount;                 /* Contador de violações */
    SecurityStats _stats;                        /* Estatísticas */
    SecurityConfig _config;                      /* Configuração */
    
    /* =========================================================================
     * DEBUG
     * ========================================================================= */
    
    bool _debugEnabled;                          /* Debug ativo? */
    
    /* =========================================================================
     * FUNÇÕES PRIVADAS DE VERIFICAÇÃO
     * ========================================================================= */
    
    /**
     * Verifica HMAC da mensagem
     * 
     * @param msg  Mensagem TLV
     * @param hmac HMAC recebido (32 bytes)
     * @return true se HMAC válido
     */
    bool _checkHMAC(const TLVMessage& msg, const uint8_t* hmac);
    
    /**
     * Verifica número de sequência (anti-replay com janela deslizante)
     * 
     * @param seqNum Número de sequência recebido
     * @return true se sequência válida (não replay)
     */
    bool _checkSequence(uint32_t seqNum);
    
    /**
     * Verifica rate limiting por categoria de comando
     * 
     * @param cmdID ID do comando
     * @return true se dentro do limite
     */
    bool _checkRateLimit(uint8_t cmdID);
    
    /**
     * Verifica sanidade física de um comando
     * 
     * @param cmdID ID do comando
     * @param value Valor do comando
     * @return true se dentro dos limites
     */
    bool _checkCommandSanity(uint8_t cmdID, float value);
    
    /**
     * Verifica se o comando é permitido no estado atual de voo
     * 
     * @param cmdID ID do comando
     * @return true se permitido
     */
    bool _checkCommandState(uint8_t cmdID);
    
    /**
     * Verifica envelope de voo (limites estruturais da aeronave)
     * 
     * @param cmdID ID do comando
     * @param value Valor do comando
     * @return true se dentro do envelope
     */
    bool _checkEnvelope(uint8_t cmdID, float value);
    
    /**
     * Verifica sanidade de dados de sensores
     * 
     * @param fieldID ID do campo TLV
     * @param value Valor do sensor
     * @return true se dentro dos limites
     */
    bool _checkSensorSanity(uint8_t fieldID, float value);
    
    /**
     * Verifica geofencing (se ativo)
     * 
     * @param lat Latitude atual
     * @param lon Longitude atual
     * @return true se dentro do geofence
     */
    bool _checkGeofence(float lat, float lon) const;
    
    /**
     * Regista uma violação de segurança
     * 
     * @param reason Razão da violação
     */
    void _recordViolation(SecurityResult reason);
    
    /**
     * Avalia FailSecure — pode ativar níveis de segurança mais altos
     * 
     * @param reason Razão da violação (para contexto)
     */
    void _evaluateFailSecure(SecurityResult reason);
    
    /**
     * Atualiza o nível de segurança com base nas violações
     */
    void _updateSecurityLevel();
    
    /* =========================================================================
     * CRIPTOGRAFIA — HMAC-SHA256
     * ========================================================================= */
    
    /**
     * Implementação real de HMAC-SHA256
     * 
     * Utiliza mbedtls no ESP32 para hardware-accelerated crypto.
     * 
     * @param key     Chave secreta
     * @param keyLen  Tamanho da chave
     * @param data    Dados a autenticar
     * @param dataLen Tamanho dos dados
     * @param output  Buffer de saída (32 bytes)
     */
    void _hmacSHA256(const uint8_t* key, size_t keyLen,
                     const uint8_t* data, size_t dataLen,
                     uint8_t* output);
    
    /**
     * Serializa uma mensagem TLV para cálculo de HMAC
     * 
     * @param msg       Mensagem a serializar
     * @param output    Buffer de saída
     * @param maxLen    Tamanho máximo do buffer
     * @return          Número de bytes escritos
     */
    size_t _serializeForHMAC(const TLVMessage& msg, uint8_t* output, size_t maxLen);
};

#endif /* SECURITY_H */
/**
 * =================================================================================
 * VIDEO.H — PROCESSAMENTO E TRANSMISSÃO DE VÍDEO (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-29
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * DESCRIÇÃO GERAL
 * =================================================================================
 * 
 * Este módulo é responsável por processar e transmitir vídeo do ESP2 para a
 * Ground Station via LoRa 868MHz.
 * 
 * NOTA: O vídeo é uma funcionalidade de PROVA DE CONCEITO (POC). Não é crítica
 * para a operação do UAS. Perdas de pacote são aceitáveis.
 * 
 * =================================================================================
 * FUNCIONALIDADES
 * =================================================================================
 * 
 *   1. Receber frames de vídeo do ESP1 via UART
 *   2. Fragmentar frames grandes em chunks pequenos (max 128 bytes)
 *   3. Organizar chunks em fila de transmissão
 *   4. Enviar chunks via LoRa 868MHz
 *   5. Garantir ordem de chunks (frameID + chunkID)
 * 
 * =================================================================================
 * FLUXO DE DADOS
 * =================================================================================
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP1                                           │
 *   │                                                                             │
 *   │   Câmera → Compressão → Frame → UART →                                      │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 *                                        │
 *                                        ▼ (UART RX)
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                              ESP2                                           │
 *   │                                                                             │
 *   │   ┌─────────────────────────────────────────────────────────────────────┐  │
 *   │   │                    Video::process()                                 │  │
 *   │   │                                                                      │  │
 *   │   │   Frame completo → Fragmentação em chunks (max 128 bytes)            │  │
 *   │   │   • Chunk 0: frameID=1, chunkID=0, data[0-127]                       │  │
 *   │   │   • Chunk 1: frameID=1, chunkID=1, data[128-255]                     │  │
 *   │   │   • ...                                                              │  │
 *   │   │   • Chunk N: frameID=1, chunkID=N, data[...]                        │  │
 *   │   └─────────────────────────────────────────────────────────────────────┘  │
 *   │                                        │                                   │
 *   │                                        ▼                                   │
 *   │                    ┌─────────────────────────────────────┐                │
 *   │                    │        Fila de Transmissão          │                │
 *   │                    │  (chunks organizados por prioridade) │                │
 *   │                    └─────────────────────────────────────┘                │
 *   │                                        │                                   │
 *   │                                        ▼                                   │
 *   │                    ┌─────────────────────────────────────┐                │
 *   │                    │        Video::sendNextSlot()        │                │
 *   │                    │  • Envia próximo chunk da fila      │                │
 *   │                    │  • Prioridade: LORA_PRIORITY_VIDEO   │                │
 *   │                    └─────────────────────────────────────┘                │
   *   │                                        │                                   │
   *   │                                        ▼                                   │
   *   │                              LoRa 868MHz → GS                              │
   *   └─────────────────────────────────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * PROTOCOLO DE VÍDEO (TLV)
 * =================================================================================
 * 
 *   Mensagem MSG_VIDEO (ID=0x16) contém os seguintes campos TLV:
 * 
 *   ┌─────────────────┬──────────┬────────────────────────────────────────────────┐
 *   │ Campo           │ ID (hex) │ Descrição                                      │
 *   ├─────────────────┼──────────┼────────────────────────────────────────────────┤
 *   │ FLD_VIDEO_FRAME │ 0xB0     │ Número do frame (uint16_t)                     │
 *   │ FLD_VIDEO_CHUNK │ 0xB1     │ Índice do chunk dentro do frame (uint8_t)      │
 *   │ FLD_VIDEO_TOTAL │ 0xB2     │ Total de chunks neste frame (uint8_t)          │
 *   │ FLD_VIDEO_DATA  │ 0xB3     │ Payload de vídeo (até MAX_TLV_VIDEO_DATA=128)  │
 *   └─────────────────┴──────────┴────────────────────────────────────────────────┘
 * 
 * =================================================================================
 * PRIORIDADE
 * =================================================================================
 * 
 *   O vídeo tem prioridade BAIXA no sistema (LORA_PRIORITY_VIDEO = 4).
 *   Em caso de congestionamento, pacotes de vídeo podem ser descartados.
 *   A perda de chunks é aceitável para a prova de conceito.
 * 
 * =================================================================================
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <vector>
#include "Protocol.h"
#include "Parser.h"
#include "LoRa.h"

/* =================================================================================
 * CONSTANTES DE CONFIGURAÇÃO
 * =================================================================================
 */

#define VIDEO_MAX_CHUNK_SIZE        128     /* Tamanho máximo de cada chunk (bytes) */
#define VIDEO_MAX_QUEUE_SIZE        50      /* Máximo de chunks na fila */
#define VIDEO_MAX_FRAME_SIZE        4096    /* Tamanho máximo do frame (bytes) */
#define VIDEO_FRAGMENT_TIMEOUT_MS   5000    /* Timeout para frame incompleto (ms) */

/* =================================================================================
 * ENUMS
 * =================================================================================
 */

/**
 * VideoStatus — Estado atual do módulo de vídeo
 */
enum VideoStatus : uint8_t {
    VIDEO_STATUS_IDLE       = 0,   /* Aguardando dados */
    VIDEO_STATUS_RECEIVING  = 1,   /* A receber frame */
    VIDEO_STATUS_PROCESSING = 2,   /* A processar fragmentação */
    VIDEO_STATUS_SENDING    = 3,   /* A enviar chunks */
    VIDEO_STATUS_ERROR      = 4    /* Erro */
};

/**
 * VideoCompression — Tipo de compressão de vídeo
 */
enum VideoCompression : uint8_t {
    VIDEO_COMPRESS_NONE     = 0,   /* Sem compressão (raw) */
    VIDEO_COMPRESS_JPEG     = 1,   /* JPEG (implementação futura) */
    VIDEO_COMPRESS_MJPEG    = 2    /* MJPEG (implementação futura) */
};

/* =================================================================================
 * ESTRUTURAS DE DADOS
 * =================================================================================
 */

/**
 * VideoChunk — Estrutura de um chunk de vídeo
 */
struct VideoChunk {
    uint16_t frameId;               /* ID do frame (0-65535) */
    uint8_t  chunkId;               /* Índice do chunk (0-255) */
    uint8_t  totalChunks;           /* Total de chunks neste frame */
    uint8_t  data[VIDEO_MAX_CHUNK_SIZE]; /* Dados do chunk */
    size_t   dataLen;               /* Tamanho real dos dados */
    uint32_t timestamp;             /* Timestamp de criação (ms) */
};

/**
 * VideoFrame — Estrutura de um frame em receção
 */
struct VideoFrame {
    uint16_t frameId;               /* ID do frame */
    uint8_t  totalChunks;           /* Total de chunks esperados */
    uint8_t  receivedChunks;        /* Chunks recebidos */
    uint8_t* data;                  /* Dados do frame (buffer dinâmico) */
    size_t   dataLen;               /* Tamanho total dos dados */
    uint32_t lastUpdate;            /* Última atualização (ms) */
    bool     complete;              /* Frame completo? */
};

/* =================================================================================
 * CLASSE VIDEO
 * =================================================================================
 */

class Video {
public:
    /* =========================================================================
     * CONSTRUTOR E INICIALIZAÇÃO
     * ========================================================================= */
    
    Video();
    
    /**
     * Inicializa o módulo de vídeo
     * 
     * @param lora Ponteiro para o módulo LoRa (para envio)
     */
    void begin(LoRa* lora);
    
    /**
     * Encerra o módulo de vídeo e liberta recursos
     */
    void end();
    
    /* =========================================================================
     * PROCESSAMENTO DE DADOS (RECEÇÃO DO ESP1)
     * ========================================================================= */
    
    /**
     * Processa um frame de vídeo recebido do ESP1 via UART
     * 
     * Fragmenta o frame em chunks e adiciona à fila de transmissão.
     * 
     * @param data     Dados do frame (buffer)
     * @param len      Tamanho dos dados (bytes)
     * @return true se processado com sucesso
     */
    bool processFrame(const uint8_t* data, size_t len);
    
    /**
     * Processa uma mensagem TLV de vídeo (futuro)
     * 
     * @param msg Mensagem TLV do tipo MSG_VIDEO
     */
    void processMessage(const TLVMessage& msg);
    
    /* =========================================================================
     * TRANSMISSÃO (ENVIO PARA GS)
     * ========================================================================= */
    
    /**
     * Envia o próximo chunk da fila via LoRa
     * 
     * Deve ser chamada periodicamente no loop().
     * Utiliza prioridade LORA_PRIORITY_VIDEO (baixa).
     */
    void sendNext();
    
    /**
     * Envia um chunk específico via LoRa
     * 
     * @param chunk Chunk a enviar
     * @return true se enviado com sucesso
     */
    bool sendChunk(const VideoChunk& chunk);
    
    /* =========================================================================
     * CONFIGURAÇÃO
     * ========================================================================= */
    
    /**
     * Define o tipo de compressão de vídeo
     * 
     * @param compression Tipo de compressão (NONE, JPEG, MJPEG)
     */
    void setCompression(VideoCompression compression);
    
    /**
     * Ativa/desativa o módulo de vídeo
     */
    void setEnabled(bool enable);
    
    /**
     * Verifica se o módulo está ativo
     */
    bool isEnabled() const;
    
    /* =========================================================================
     * ESTATÍSTICAS
     * ========================================================================= */
    
    /**
     * Retorna o estado atual do módulo
     */
    VideoStatus getStatus() const;
    
    /**
     * Retorna o número de chunks na fila
     */
    size_t getQueueSize() const;
    
    /**
     * Retorna o número de chunks enviados
     */
    uint32_t getChunksSent() const;
    
    /**
     * Retorna o número de chunks descartados
     */
    uint32_t getChunksDropped() const;
    
    /**
     * Reseta as estatísticas
     */
    void resetStats();
    
    /* =========================================================================
     * DEBUG
     * ========================================================================= */
    
    void setDebug(bool enable);

private:
    /* =========================================================================
     * PONTEIROS
     * ========================================================================= */
    
    LoRa* _lora;
    
    /* =========================================================================
     * ESTADO INTERNO
     * ========================================================================= */
    
    VideoStatus   _status;
    bool          _enabled;
    bool          _debugEnabled;
    VideoCompression _compression;
    
    /* =========================================================================
     * FILA DE TRANSMISSÃO
     * ========================================================================= */
    
    std::vector<VideoChunk> _queue;
    
    /* =========================================================================
     * ESTATÍSTICAS
     * ========================================================================= */
    
    uint32_t      _framesProcessed;
    uint32_t      _chunksSent;
    uint32_t      _chunksDropped;
    uint32_t      _chunksFailed;
    uint16_t      _nextFrameId;
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — FRAGMENTAÇÃO
     * ========================================================================= */
    
    /**
     * Fragmenta um frame em chunks
     * 
     * @param data     Dados do frame
     * @param len      Tamanho dos dados
     * @param frameId  ID do frame
     * @return Número de chunks gerados
     */
    size_t _fragmentFrame(const uint8_t* data, size_t len, uint16_t frameId);
    
    /**
     * Adiciona um chunk à fila de transmissão
     * 
     * @param chunk Chunk a adicionar
     * @return true se adicionado com sucesso
     */
    bool _enqueueChunk(const VideoChunk& chunk);
    
    /**
     * Remove um chunk da fila
     */
    void _dequeueChunk();
    
    /* =========================================================================
     * MÉTODOS PRIVADOS — CONSTRUÇÃO TLV
     * ========================================================================= */
    
    /**
     * Constrói um pacote TLV para um chunk de vídeo
     * 
     * @param chunk  Chunk a serializar
     * @param buffer Buffer de saída
     * @param size   Tamanho do buffer
     * @return Tamanho do pacote (0 se erro)
     */
    size_t _buildVideoPacket(const VideoChunk& chunk, uint8_t* buffer, size_t size);
    
    /* =========================================================================
     * INSTÂNCIA GLOBAL PARA HANDLERS ESTÁTICOS
     * ========================================================================= */
    
    static Video* _instance;
};

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* videoStatusToString(VideoStatus status);
const char* videoCompressionToString(VideoCompression compression);
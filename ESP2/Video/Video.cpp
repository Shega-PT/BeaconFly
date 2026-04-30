/**
 * =================================================================================
 * VIDEO.CPP — IMPLEMENTAÇÃO DO PROCESSAMENTO DE VÍDEO (ESP2)
 * =================================================================================
 * 
 * AUTOR:      BeaconFly UAS Team
 * DATA:       2026-04-29
 * VERSÃO:     1.0.0
 * 
 * =================================================================================
 * NOTAS DE IMPLEMENTAÇÃO
 * =================================================================================
 * 
 * 1. Vídeo é uma funcionalidade de PROVA DE CONCEITO (POC)
 * 2. Perdas de pacote são aceitáveis
 * 3. Prioridade baixa na fila LoRa
 * 4. Fragmentação automática de frames grandes
 * 
 * =================================================================================
 */

#include "Video.h"
#include <Arduino.h>
#include <cstring>

/* =================================================================================
 * DEBUG — Macro configurável
 * =================================================================================
 */

#define VIDEO_DEBUG

#ifdef VIDEO_DEBUG
    #define DEBUG_PRINT(fmt, ...) if (_debugEnabled) { Serial.printf("[Video] " fmt, ##__VA_ARGS__); }
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* =================================================================================
 * INSTÂNCIA GLOBAL
 * =================================================================================
 */

Video* Video::_instance = nullptr;

/* =================================================================================
 * FUNÇÕES AUXILIARES
 * =================================================================================
 */

const char* videoStatusToString(VideoStatus status) {
    switch (status) {
        case VIDEO_STATUS_IDLE:       return "IDLE";
        case VIDEO_STATUS_RECEIVING:  return "RECEIVING";
        case VIDEO_STATUS_PROCESSING: return "PROCESSING";
        case VIDEO_STATUS_SENDING:    return "SENDING";
        case VIDEO_STATUS_ERROR:      return "ERROR";
        default:                      return "UNKNOWN";
    }
}

const char* videoCompressionToString(VideoCompression compression) {
    switch (compression) {
        case VIDEO_COMPRESS_NONE:  return "NONE";
        case VIDEO_COMPRESS_JPEG:  return "JPEG";
        case VIDEO_COMPRESS_MJPEG: return "MJPEG";
        default:                   return "UNKNOWN";
    }
}

/* =================================================================================
 * CONSTRUTOR
 * =================================================================================
 */

Video::Video()
    : _lora(nullptr)
    , _status(VIDEO_STATUS_IDLE)
    , _enabled(true)
    , _debugEnabled(false)
    , _compression(VIDEO_COMPRESS_NONE)
    , _framesProcessed(0)
    , _chunksSent(0)
    , _chunksDropped(0)
    , _chunksFailed(0)
    , _nextFrameId(0)
{
    _queue.clear();
    _queue.reserve(VIDEO_MAX_QUEUE_SIZE);
    _instance = this;
    
    DEBUG_PRINT("Construtor chamado\n");
}

/* =================================================================================
 * BEGIN — INICIALIZAÇÃO
 * =================================================================================
 */

void Video::begin(LoRa* lora) {
    if (!lora) {
        DEBUG_PRINT("begin() falhou: ponteiro LoRa nulo\n");
        _status = VIDEO_STATUS_ERROR;
        return;
    }
    
    _lora = lora;
    _queue.clear();
    _nextFrameId = 0;
    _framesProcessed = 0;
    _chunksSent = 0;
    _chunksDropped = 0;
    _chunksFailed = 0;
    _status = VIDEO_STATUS_IDLE;
    
    DEBUG_PRINT("begin() — Módulo de vídeo inicializado\n");
    DEBUG_PRINT("  Compressão: %s\n", videoCompressionToString(_compression));
    DEBUG_PRINT("  Max chunk size: %d bytes\n", VIDEO_MAX_CHUNK_SIZE);
    DEBUG_PRINT("  Max queue size: %d chunks\n", VIDEO_MAX_QUEUE_SIZE);
}

/* =================================================================================
 * END — ENCERRAMENTO
 * =================================================================================
 */

void Video::end() {
    DEBUG_PRINT("end() — Encerrando módulo de vídeo\n");
    
    _queue.clear();
    _status = VIDEO_STATUS_IDLE;
    _lora = nullptr;
    
    DEBUG_PRINT("end() — Vídeo encerrado\n");
}

/* =================================================================================
 * FRAGMENT FRAME — FRAGMENTA UM FRAME EM CHUNKS
 * =================================================================================
 */

size_t Video::_fragmentFrame(const uint8_t* data, size_t len, uint16_t frameId) {
    if (len == 0) {
        DEBUG_PRINT("_fragmentFrame() — Frame vazio\n");
        return 0;
    }
    
    size_t numChunks = (len + VIDEO_MAX_CHUNK_SIZE - 1) / VIDEO_MAX_CHUNK_SIZE;
    
    DEBUG_PRINT("_fragmentFrame() — Frame %d: %zu bytes → %zu chunks\n", 
                frameId, len, numChunks);
    
    for (size_t i = 0; i < numChunks; i++) {
        VideoChunk chunk;
        chunk.frameId = frameId;
        chunk.chunkId = (uint8_t)i;
        chunk.totalChunks = (uint8_t)numChunks;
        chunk.timestamp = millis();
        
        size_t offset = i * VIDEO_MAX_CHUNK_SIZE;
        size_t remaining = len - offset;
        chunk.dataLen = (remaining < VIDEO_MAX_CHUNK_SIZE) ? remaining : VIDEO_MAX_CHUNK_SIZE;
        
        memcpy(chunk.data, data + offset, chunk.dataLen);
        
        if (!_enqueueChunk(chunk)) {
            DEBUG_PRINT("_fragmentFrame() — Falha ao enfileirar chunk %d/%zu\n", i, numChunks);
            return i;
        }
    }
    
    return numChunks;
}

/* =================================================================================
 * ENQUEUE CHUNK — ADICIONA CHUNK À FILA
 * =================================================================================
 */

bool Video::_enqueueChunk(const VideoChunk& chunk) {
    if (_queue.size() >= VIDEO_MAX_QUEUE_SIZE) {
        DEBUG_PRINT("_enqueueChunk() — Fila cheia! Descartando chunk %d/%d\n", 
                    chunk.chunkId, chunk.totalChunks);
        _chunksDropped++;
        return false;
    }
    
    _queue.push_back(chunk);
    
    DEBUG_PRINT("_enqueueChunk() — Chunk %d/%d do frame %d adicionado à fila (fila: %zu)\n",
                chunk.chunkId, chunk.totalChunks, chunk.frameId, _queue.size());
    
    return true;
}

/* =================================================================================
 * DEQUEUE CHUNK — REMOVE CHUNK DA FILA
 * =================================================================================
 */

void Video::_dequeueChunk() {
    if (_queue.empty()) return;
    
    _queue.erase(_queue.begin());
}

/* =================================================================================
 * BUILD VIDEO PACKET — CONSTRÓI PACOTE TLV PARA CHUNK
 * =================================================================================
 */

size_t Video::_buildVideoPacket(const VideoChunk& chunk, uint8_t* buffer, size_t size) {
    TLVMessage msg;
    msg.startByte = START_BYTE;
    msg.msgID = MSG_VIDEO;
    msg.tlvCount = 0;
    msg.checksum = 0;
    
    /* Adicionar campos TLV */
    addTLVUint16(&msg, FLD_VIDEO_FRAME_ID, chunk.frameId);
    addTLVUint8(&msg, FLD_VIDEO_CHUNK_ID, chunk.chunkId);
    addTLVUint8(&msg, FLD_VIDEO_TOTAL, chunk.totalChunks);
    addTLV(&msg, FLD_VIDEO_PAYLOAD, chunk.data, chunk.dataLen);
    
    return buildMessage(&msg, MSG_VIDEO, buffer, size);
}

/* =================================================================================
 * PROCESS FRAME — PROCESSAMENTO DE FRAME RECEBIDO DO ESP1
 * =================================================================================
 */

bool Video::processFrame(const uint8_t* data, size_t len) {
    if (!_enabled) {
        DEBUG_PRINT("processFrame() — Vídeo desativado\n");
        return false;
    }
    
    if (len == 0 || len > VIDEO_MAX_FRAME_SIZE) {
        DEBUG_PRINT("processFrame() — Tamanho inválido: %zu bytes\n", len);
        return false;
    }
    
    _status = VIDEO_STATUS_PROCESSING;
    
    /* Fragmentar frame */
    uint16_t frameId = _nextFrameId++;
    size_t chunksGenerated = _fragmentFrame(data, len, frameId);
    
    if (chunksGenerated > 0) {
        _framesProcessed++;
        DEBUG_PRINT("processFrame() — Frame %d processado: %zu chunks\n", 
                    frameId, chunksGenerated);
    } else {
        DEBUG_PRINT("processFrame() — Falha ao processar frame %d\n", frameId);
        _status = VIDEO_STATUS_ERROR;
        return false;
    }
    
    _status = VIDEO_STATUS_IDLE;
    return true;
}

/* =================================================================================
 * PROCESS MESSAGE — PROCESSAMENTO DE MENSAGEM TLV (FUTURO)
 * =================================================================================
 */

void Video::processMessage(const TLVMessage& msg) {
    if (!_enabled) return;
    
    if (msg.msgID != MSG_VIDEO) return;
    
    /* TODO: Implementar receção de vídeo do ESP1 via TLV */
    DEBUG_PRINT("processMessage() — Mensagem de vídeo recebida (futuro)\n");
}

/* =================================================================================
 * SEND CHUNK — ENVIA UM CHUNK VIA LoRa
 * =================================================================================
 */

bool Video::sendChunk(const VideoChunk& chunk) {
    if (!_lora || !_enabled) {
        return false;
    }
    
    uint8_t buffer[256];
    size_t len = _buildVideoPacket(chunk, buffer, sizeof(buffer));
    
    if (len == 0) {
        DEBUG_PRINT("sendChunk() — Falha ao construir pacote\n");
        return false;
    }
    
    /* Enviar via LoRa com prioridade baixa */
    bool success = _lora->send(buffer, len, LORA_PRIORITY_VIDEO);
    
    if (success) {
        _chunksSent++;
        DEBUG_PRINT("sendChunk() — Chunk %d/%d do frame %d enviado (%zu bytes)\n",
                    chunk.chunkId, chunk.totalChunks, chunk.frameId, len);
    } else {
        _chunksFailed++;
        DEBUG_PRINT("sendChunk() — Falha ao enviar chunk %d/%d\n", 
                    chunk.chunkId, chunk.totalChunks);
    }
    
    return success;
}

/* =================================================================================
 * SEND NEXT — ENVIA O PRÓXIMO CHUNK DA FILA
 * =================================================================================
 */

void Video::sendNext() {
    if (!_enabled) return;
    if (_queue.empty()) return;
    
    _status = VIDEO_STATUS_SENDING;
    
    VideoChunk chunk = _queue.front();
    bool success = sendChunk(chunk);
    
    if (success) {
        _dequeueChunk();
    }
    
    _status = (_queue.empty()) ? VIDEO_STATUS_IDLE : VIDEO_STATUS_SENDING;
}

/* =================================================================================
 * SET COMPRESSION — DEFINE TIPO DE COMPRESSÃO
 * =================================================================================
 */

void Video::setCompression(VideoCompression compression) {
    _compression = compression;
    DEBUG_PRINT("setCompression() — Compressão: %s\n", videoCompressionToString(compression));
}

/* =================================================================================
 * SET ENABLED — ATIVA/DESATIVA VÍDEO
 * =================================================================================
 */

void Video::setEnabled(bool enable) {
    _enabled = enable;
    DEBUG_PRINT("setEnabled() — Vídeo %s\n", enable ? "ATIVADO" : "DESATIVADO");
    
    if (!enable) {
        _queue.clear();
    }
}

/* =================================================================================
 * IS ENABLED — VERIFICA SE VÍDEO ESTÁ ATIVO
 * =================================================================================
 */

bool Video::isEnabled() const {
    return _enabled;
}

/* =================================================================================
 * GET STATUS — RETORNA ESTADO ATUAL
 * =================================================================================
 */

VideoStatus Video::getStatus() const {
    return _status;
}

/* =================================================================================
 * GET QUEUE SIZE — RETORNA TAMANHO DA FILA
 * =================================================================================
 */

size_t Video::getQueueSize() const {
    return _queue.size();
}

/* =================================================================================
 * GET CHUNKS SENT — RETORNA NÚMERO DE CHUNKS ENVIADOS
 * =================================================================================
 */

uint32_t Video::getChunksSent() const {
    return _chunksSent;
}

/* =================================================================================
 * GET CHUNKS DROPPED — RETORNA NÚMERO DE CHUNKS DESCARTADOS
 * =================================================================================
 */

uint32_t Video::getChunksDropped() const {
    return _chunksDropped;
}

/* =================================================================================
 * RESET STATS — RESETA ESTATÍSTICAS
 * =================================================================================
 */

void Video::resetStats() {
    _framesProcessed = 0;
    _chunksSent = 0;
    _chunksDropped = 0;
    _chunksFailed = 0;
    DEBUG_PRINT("resetStats() — Estatísticas resetadas\n");
}

/* =================================================================================
 * SET DEBUG — ATIVA/DESATIVA DEBUG
 * =================================================================================
 */

void Video::setDebug(bool enable) {
    _debugEnabled = enable;
    DEBUG_PRINT("setDebug() — Modo debug %s\n", enable ? "ATIVADO" : "desativado");
}
#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>
#include "PortalConfig.h"
#include "PortalHID.h"

#define AUDIO_RING_BUFFER_SIZE 8192

// Lock-free Single-Producer Single-Consumer (SPSC) Ring Buffer
class AudioRingBuffer {
public:
    AudioRingBuffer() : _head(0), _tail(0) {}

    size_t write(const uint8_t* data, size_t len) {
        size_t written = 0;
        while (written < len) {
            size_t nextHead = (_head + 1) % AUDIO_RING_BUFFER_SIZE;
            if (nextHead == _tail) {
                break; // Buffer full
            }
            _buffer[_head] = data[written++];
            _head = nextHead;
        }
        return written;
    }

    size_t available() const {
        if (_head >= _tail) {
            return _head - _tail;
        }
        return AUDIO_RING_BUFFER_SIZE - _tail + _head;
    }

    size_t read(uint8_t* out, size_t maxLen) {
        size_t count = 0;
        while (count < maxLen && _tail != _head) {
            out[count++] = _buffer[_tail];
            _tail = (_tail + 1) % AUDIO_RING_BUFFER_SIZE;
        }
        return count;
    }

    void clear() {
        _tail = _head;
    }

private:
    uint8_t _buffer[AUDIO_RING_BUFFER_SIZE];
    volatile size_t _head;
    volatile size_t _tail;
};

class PortalWebSocket {
public:
    PortalWebSocket(
        SkylanderSlot*     slots,
        volatile uint8_t&  changeCounter,
        uint16_t           port = HTTP_PORT
    );

    void begin();
    void loop();

    // Audio Streaming API (Traptanium Portal Speaker)
    void pushAudio(const uint8_t* data, size_t len);
    void signalAudioEnd();
    void broadcastAudioEnd();

private:
    WebSocketsServer   _webSocket;
    SkylanderSlot*     _slots;
    volatile uint8_t&  _changeCounter;
    
    uint8_t _lastChangeCounter;
    bool    _lastModifiedState[NUM_SLOTS] = {false};

    // Lock-free audio ring buffer between TinyUSB (producer) and loop (consumer)
    AudioRingBuffer _audioRing;
    uint8_t         _preBuffer[128] = {0};
    bool            _audioActive    = false;
    unsigned long   _lastVoiceTime  = 0;
    unsigned long   _lastAudioSendTime = 0;
    size_t          _totalVoiceBytes = 0;
    volatile bool   _signalEndRequested = false;

    void _onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
    void _broadcastSlots();
    void _broadcastAudioChunk(const uint8_t* pcm_data, size_t len);
    
    // AES Encryption helpers
    // Returns length of allocated buffer, caller must free(*outCiphertext)
    size_t _encryptAES(const uint8_t* payload, size_t payloadLen, uint8_t** outCiphertext);
    // Returns length of allocated buffer, caller must free(*outPlaintext)
    size_t _decryptAES(const uint8_t* ciphertext, size_t cipherLen, uint8_t** outPlaintext);
    
    // API commands
    void _handleMessage(uint8_t num, const uint8_t* payload, size_t length);
};

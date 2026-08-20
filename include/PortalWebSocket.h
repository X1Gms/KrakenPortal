#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>
#include "PortalConfig.h"
#include "PortalHID.h"

class PortalWebSocket {
public:
    PortalWebSocket(
        SkylanderSlot*     slots,
        volatile uint8_t&  changeCounter,
        uint16_t           port = HTTP_PORT
    );

    void begin();
    void loop();

private:
    WebSocketsServer   _webSocket;
    SkylanderSlot*     _slots;
    volatile uint8_t&  _changeCounter;
    
    uint8_t _lastChangeCounter;
    bool    _lastModifiedState[NUM_SLOTS] = {false};

    void _onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
    void _broadcastSlots();
    
    // AES Encryption helpers
    // Returns length of allocated buffer, caller must free(*outCiphertext)
    size_t _encryptAES(const uint8_t* payload, size_t payloadLen, uint8_t** outCiphertext);
    // Returns length of allocated buffer, caller must free(*outPlaintext)
    size_t _decryptAES(const uint8_t* ciphertext, size_t cipherLen, uint8_t** outPlaintext);
    
    // API commands
    void _handleMessage(uint8_t num, const uint8_t* payload, size_t length);
};

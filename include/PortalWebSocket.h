#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <mutex>
#include "PortalConfig.h"
#include "PortalHID.h"

class PortalWebSocket {
public:
    PortalWebSocket(
        SkylanderSlot*     slots,
        volatile uint8_t&  changeCounter,
        std::mutex&        portalMutex,
        uint16_t           port = HTTP_PORT
    );

    void begin();
    void loop();

private:
    WebSocketsServer   _webSocket;
    SkylanderSlot*     _slots;
    volatile uint8_t&  _changeCounter;
    std::mutex&        _mutex;
    
    uint8_t _lastChangeCounter;

    void _onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
    void _broadcastSlots();
    
    // AES Encryption helpers
    String _encryptAES(const String& payload);
    String _decryptAES(const String& ciphertextBase64);
    
    // API commands
    void _handleMessage(uint8_t num, const String& msg);
};

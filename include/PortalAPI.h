#pragma once

// ============================================================
//  PortalAPI.h — Skylanders Remote Portal HTTP API
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <mutex>
#include "PortalConfig.h"
#include "PortalHID.h"

class PortalAPI {
public:
    PortalAPI(
        SkylanderSlot*     slots,
        volatile uint8_t&  changeCounter,
        std::mutex&        portalMutex,
        volatile uint32_t& usbCommandCounter,
        uint16_t           port = HTTP_PORT
    );

    void begin();
    void handle();

private:
    WiFiServer         _server;
    SkylanderSlot*     _slots;
    volatile uint8_t&  _changeCounter;
    std::mutex&        _mutex;
    volatile uint32_t& _usbCommandCounter;

    void _handleGetSlots   (WiFiClient& client);
    void _handleGetSlot    (WiFiClient& client, int slot);
    void _handlePostSlot   (WiFiClient& client, int slot, int contentLength, const String& filename);
    void _handleDeleteSlot (WiFiClient& client, int slot);
    void _handleGetPortalType(WiFiClient& client);
    void _handleSetPortalType(WiFiClient& client, const String& uri);

    void _sendHeader(WiFiClient& client, int code, const String& contentType, size_t bodyLength, const String& extraHeaders = "");
    void _sendError (WiFiClient& client, int code, const String& message);
};

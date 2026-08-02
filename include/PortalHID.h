#pragma once
#include <Arduino.h>
#include <mutex>
#include <queue>
#include <array>
#include "USB.h"
#include "tusb.h"
#include "PortalConfig.h"

// ============================================================
//  PortalHID.h — Skylander Portal USB HID Emulation
// ============================================================

struct SkylanderSlot {
    uint8_t dump_data[FIGURE_SIZE];
    bool    occupied    = false;
    String  file_name   = "";
    bool    is_modified = false;
};

class SkylandersPortal {
public:
    SkylandersPortal(
        SkylanderSlot*     slots,
        std::mutex&        portalMutex,
        volatile uint8_t&  changeCounter,
        volatile uint32_t& usbCommandCounter,
        uint8_t            portalType = 2
    );

    void begin();
    void loop(); // Periodic status sender
    
    uint8_t getPortalType() const { return _portalType; }

    void buildStatusReport(uint8_t out[32]);
    bool sendReport(const uint8_t* data, size_t len);

    // Callbacks for TinyUSB
    void handleOutput(const uint8_t* buffer, uint16_t len);
    uint16_t handleGetFeature(uint8_t* buffer, uint16_t len);

private:
    SkylanderSlot*     _slots;
    std::mutex&        _mutex;
    volatile uint8_t&  _changeCounter;
    volatile uint32_t& _usbCommandCounter;

    uint8_t  _statusSeq    = 0;
    bool     _portalActive = false;

    bool     _wasLoaded[NUM_SLOTS]      = {false};
    bool     _arrivalPending[NUM_SLOTS] = {false};
    bool     _removalPending[NUM_SLOTS] = {false};

    uint8_t  _lastChangeCounter = 0;
    unsigned long _lastStatusTx = 0;

    uint8_t _portalType = 2;

    std::queue<std::array<uint8_t, 32>> _respQueue;

    void _handleCommand(const uint8_t* cmd, uint16_t len);
};

extern SkylandersPortal* g_portalInstance;

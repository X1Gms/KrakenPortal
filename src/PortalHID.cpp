// ============================================================
//  PortalHID.cpp — Skylander Portal USB HID Emulation
//  Exact TinyUSB Report Descriptor & Protocol from KaosDual Reference
// ============================================================

#include "PortalHID.h"

SkylandersPortal* g_portalInstance = nullptr;

// Custom HID Report Descriptor for Skylander Traptanium / Spyro Portal of Power
static const uint8_t portal_report_descriptor[] = {
    0x06, 0x00, 0xFF,
    0x09, 0x01,
    0xA1, 0x01,
    0x19, 0x01, 0x29, 0x40, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x20, 0x81, 0x00,
    0x19, 0x01, 0x29, 0x40, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x20, 0x91, 0x00,
    0xC0
};

#define PORTAL_EP_IN 0x81

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
static const uint8_t portal_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(portal_report_descriptor), PORTAL_EP_IN, 32, 1)
};

// -----------------------------------------------------------------------
// Global TinyUSB Callbacks
// -----------------------------------------------------------------------

extern "C" {

uint8_t const * tud_descriptor_device_cb(void) {
    static tusb_desc_device_t desc = {
        .bLength            = sizeof(tusb_desc_device_t),
        .bDescriptorType    = TUSB_DESC_DEVICE,
        .bcdUSB             = 0x0200,
        .bDeviceClass       = 0x00,
        .bDeviceSubClass    = 0x00,
        .bDeviceProtocol    = 0x00,
        .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor           = PORTAL_USB_VID,
        .idProduct          = PORTAL_USB_PID,
        .bcdDevice          = 0x0200,
        .iManufacturer      = 0x01,
        .iProduct           = 0x02,
        .iSerialNumber      = 0x03,
        .bNumConfigurations = 0x01
    };
    return (uint8_t const*)&desc;
}

uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return portal_configuration_descriptor;
}

uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t buf[32];
    uint8_t n = 0;

    const char *strs[] = {
        (const char[]){0x09, 0x04}, // 0: English
        PORTAL_USB_MANUFACTURER,    // 1: Manufacturer
        PORTAL_USB_PRODUCT,         // 2: Product
        PORTAL_USB_SERIAL           // 3: Serial
    };

    if (index == 0) { 
        memcpy(&buf[1], strs[0], 2); 
        n = 1; 
    } else {
        if (index >= (sizeof(strs) / sizeof(strs[0]))) return NULL;
        const char *str = strs[index];
        n = strlen(str);
        if (n > 31) n = 31;
        for (uint8_t i = 0; i < n; i++) {
            buf[1 + i] = str[i];
        }
    }
    buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return buf;
}

uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return portal_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void)instance; (void)report_id;
    if (report_type == HID_REPORT_TYPE_INPUT || report_type == HID_REPORT_TYPE_FEATURE) {
        if (g_portalInstance) {
            return g_portalInstance->handleGetFeature(buffer, reqlen);
        }
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void)instance; (void)report_id;
    if (report_type == HID_REPORT_TYPE_OUTPUT || report_type == HID_REPORT_TYPE_FEATURE) {
        if (g_portalInstance) {
            g_portalInstance->handleOutput(buffer, bufsize);
        }
    }
}

} // extern "C"

// -----------------------------------------------------------------------

SkylandersPortal::SkylandersPortal(
    SkylanderSlot*     slots,
    std::mutex&        portalMutex,
    volatile uint8_t&  changeCounter,
    volatile uint32_t& usbCommandCounter
)
    : _slots(slots),
      _mutex(portalMutex),
      _changeCounter(changeCounter),
      _usbCommandCounter(usbCommandCounter)
{
    g_portalInstance = this;
}

void SkylandersPortal::begin() {
    USB.begin(); // Initializes the PHY. Descriptors will be picked up from our global TinyUSB callbacks.
    Serial.println("[HID] USB HID Portal of Power initialized.");
}

void SkylandersPortal::handleOutput(const uint8_t* buffer, uint16_t len) {
    if (len >= 1) {
        _usbCommandCounter++;
        _handleCommand(buffer, len);
    }
}

uint16_t SkylandersPortal::handleGetFeature(uint8_t* buffer, uint16_t len) {
    uint8_t resp[32];
    buildStatusReport(resp);
    uint16_t n = (len < 32) ? len : 32;
    memcpy(buffer, resp, n);
    return n;
}

void SkylandersPortal::_handleCommand(const uint8_t* cmd, uint16_t len) {
    (void)len;
    uint8_t resp[32];
    memset(resp, 0, sizeof(resp));
    bool hasResp = true;

    Serial.printf("[USB-HID] PS3 Command: %c (0x%02X)\n", (cmd[0] >= 32 && cmd[0] <= 126) ? cmd[0] : '?', cmd[0]);

    switch (cmd[0]) {
    case 'R': // Reset / Query Model
        _portalActive = true;
        resp[0] = 'R';
        resp[1] = 0x02; // Traptanium Portal model type
        resp[2] = 0x18;
        break;

    case 'A': // Activate Portal / LED Colors
        _portalActive = (cmd[1] == 0x01);
        resp[0] = 0x41;
        resp[1] = cmd[1];
        resp[2] = 0xFF;
        resp[3] = 0x00;
        break;

    case 'S': // Query Status
        buildStatusReport(resp);
        break;

    case 'Q': // Read 16-byte block from Skylander figure
        {
            uint8_t rawIdx = cmd[1];
            uint8_t blk    = cmd[2];
            uint8_t slot;

            if      (rawIdx >= 0x20) slot = rawIdx - 0x20;
            else if (rawIdx >= 0x10) slot = rawIdx - 0x10;
            else                     slot = rawIdx;

            if (blk == 0 && slot < NUM_SLOTS) {
                _arrivalPending[slot] = false;
            }

            resp[0] = 'Q';
            resp[2] = blk;

            std::lock_guard<std::mutex> lock(_mutex);
            if (slot < NUM_SLOTS && _slots[slot].occupied && blk < 64) {
                resp[1] = rawIdx; // Echo raw index expected by game
                memcpy(&resp[3], &_slots[slot].dump_data[blk * 16], 16);
            } else {
                resp[1] = 0x01; // Slot empty or block out of range
            }
        }
        break;

    case 'W': // Write 16-byte block to Skylander figure
        {
            uint8_t rawIdx = cmd[1];
            uint8_t blk    = cmd[2];
            uint8_t slot;

            if      (rawIdx >= 0x20) slot = rawIdx - 0x20;
            else if (rawIdx >= 0x10) slot = rawIdx - 0x10;
            else                     slot = rawIdx;

            resp[0] = 'W';
            resp[1] = 0x00;
            resp[2] = blk;

            if (slot < NUM_SLOTS && blk < 64) {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_slots[slot].occupied) {
                    memcpy(&_slots[slot].dump_data[blk * 16], &cmd[3], 16);
                    _slots[slot].is_modified = true;
                }
            }
        }
        break;

    case 'J': // Challenge / Crypto Response
        resp[0] = 'J';
        resp[1] = cmd[1];
        break;

    case 'C':
    case 'L':
    case 'M':
    case 'V':
    default:
        hasResp = false;
        break;
    }

    if (hasResp) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_respQueue.size() < 16) {
            std::array<uint8_t, 32> pkt;
            memcpy(pkt.data(), resp, 32);
            _respQueue.push(pkt);
        }
    }
}

void SkylandersPortal::buildStatusReport(uint8_t out[32]) {
    memset(out, 0, 32);
    out[0] = 'S';

    uint32_t bits = 0;
    std::lock_guard<std::mutex> lock(_mutex);

    if (_changeCounter != _lastChangeCounter) {
        _lastChangeCounter = _changeCounter;
        for (int i = 0; i < NUM_SLOTS; ++i) {
            if (_slots[i].occupied && !_wasLoaded[i])  _arrivalPending[i] = true;
            if (!_slots[i].occupied && _wasLoaded[i]) _removalPending[i] = true;
        }
    }

    for (int i = 0; i < NUM_SLOTS; ++i) {
        bool loaded = _slots[i].occupied;

        if (loaded && !_wasLoaded[i])  _arrivalPending[i] = true;
        if (!loaded && _wasLoaded[i]) _removalPending[i] = true;
        _wasLoaded[i] = loaded;

        uint32_t slotBits = 0;
        if (_arrivalPending[i]) {
            slotBits = 0x3; // Arrival flag
        } else if (_removalPending[i]) {
            slotBits = 0x2; // Removal flag
            _removalPending[i] = false;
        } else if (loaded) {
            slotBits = 0x1; // Loaded & present
        }

        bits |= (slotBits << (i * 2));
    }

    out[1] = (bits >>  0) & 0xFF;
    out[2] = (bits >>  8) & 0xFF;
    out[3] = (bits >> 16) & 0xFF;
    out[4] = (bits >> 24) & 0xFF;
    out[5] = _statusSeq++;
    out[6] = 0x01; // Always active
}

bool SkylandersPortal::sendReport(const uint8_t* data, size_t len) {
    if (!tud_mounted() || !tud_hid_n_ready(0)) {
        return false;
    }
    return tud_hid_n_report(0, 0, data, (uint8_t)len);
}

void SkylandersPortal::loop() {
    if (tud_mounted() && tud_hid_n_ready(0)) {
        bool sent = false;
        
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_respQueue.empty()) {
                auto pkt = _respQueue.front();
                _respQueue.pop();
                tud_hid_n_report(0, 0, pkt.data(), 32);
                _lastStatusTx = millis();
                sent = true;
            }
        }

        if (!sent && (millis() - _lastStatusTx >= 20)) {
            _lastStatusTx = millis();
            uint8_t statusPkt[32];
            buildStatusReport(statusPkt);
            tud_hid_n_report(0, 0, statusPkt, 32);
        }
    }
}

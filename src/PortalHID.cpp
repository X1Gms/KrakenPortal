// ============================================================
//  PortalHID.cpp — Skylander Portal USB HID Emulation
//  Exact TinyUSB Report Descriptor & Protocol from KaosDual Reference
// ============================================================

#include "PortalHID.h"

SkylandersPortal* g_portalInstance = nullptr;

// Traptanium HID Report Descriptor (32 bytes IN/OUT, single IN endpoint with EP0 OUT)
static const uint8_t traptanium_report_descriptor[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (0x01)
    0xA1, 0x01,        // Collection (Application)
    0x19, 0x01,        //   Usage Minimum (0x01)
    0x29, 0x40,        //   Usage Maximum (0x40)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x20,        //   Report Count (32)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x19, 0x01,        //   Usage Minimum (0x01)
    0x29, 0x40,        //   Usage Maximum (0x40)
    0x91, 0x00,        //   Output (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xC0               // End Collection
};

// Spyro's Adventure HID Report Descriptor (29 bytes IN/OUT) from original KAOS project
static const uint8_t spyro_report_descriptor[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (0x01)
    0xA1, 0x01,        // Collection (Application)
    0x19, 0x01,        //   Usage Minimum (0x01)
    0x29, 0x40,        //   Usage Maximum (0x40)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x20,        //   Report Count (32)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x19, 0x01,        //   Usage Minimum (0x01)
    0x29, 0xFF,        //   Usage Maximum (0xFF)
    0x91, 0x00,        //   Output (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xC0               // End Collection
};

#define PORTAL_EP_IN 0x81

#define CONFIG_TOTAL_LEN_TRAPTANIUM (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
static const uint8_t traptanium_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN_TRAPTANIUM, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(traptanium_report_descriptor), PORTAL_EP_IN, 32, 1)
};

// Spyro's Adventure Configuration Descriptor (41 bytes, 2 Endpoints IN & OUT) from KAOS
static const uint8_t spyro_configuration_descriptor[] = {
    0x09,        // bLength
    0x02,        // bDescriptorType (Configuration)
    0x29, 0x00,  // wTotalLength (41 bytes)
    0x01,        // bNumInterfaces 1
    0x01,        // bConfigurationValue
    0x00,        // iConfiguration (String Index)
    0x80,        // bmAttributes
    0xFA,        // bMaxPower 500mA
    
    0x09,        // bLength
    0x04,        // bDescriptorType (Interface)
    0x00,        // bInterfaceNumber 0
    0x00,        // bAlternateSetting
    0x02,        // bNumEndpoints 2 (IN and OUT)
    0x03,        // bInterfaceClass (HID)
    0x00,        // bInterfaceSubClass
    0x00,        // bInterfaceProtocol
    0x00,        // iInterface (String Index)
    
    0x09,        // bLength
    0x21,        // bDescriptorType (HID)
    0x11, 0x01,  // bcdHID 1.11
    0x00,        // bCountryCode
    0x01,        // bNumDescriptors
    0x22,        // bDescriptorType[0] (HID)
    0x1D, 0x00,  // wDescriptorLength[0] (29 bytes)
    
    0x07,        // bLength
    0x05,        // bDescriptorType (Endpoint)
    0x81,        // bEndpointAddress (IN/D2H)
    0x03,        // bmAttributes (Interrupt)
    0x40, 0x00,  // wMaxPacketSize 64
    0x01,        // bInterval 1
    
    0x07,        // bLength
    0x05,        // bDescriptorType (Endpoint)
    0x02,        // bEndpointAddress (OUT/H2D)
    0x03,        // bmAttributes (Interrupt)
    0x40, 0x00,  // wMaxPacketSize 64
    0x01         // bInterval 1
};

// -----------------------------------------------------------------------
// Global TinyUSB Callbacks
// -----------------------------------------------------------------------

extern "C" {

uint8_t const * tud_descriptor_device_cb(void) {
    uint8_t pType = g_portalInstance ? g_portalInstance->getPortalType() : 2;

    static tusb_desc_device_t desc = {
        .bLength            = sizeof(tusb_desc_device_t),
        .bDescriptorType    = TUSB_DESC_DEVICE,
        .bcdUSB             = 0x0200,
        .bDeviceClass       = 0x00,
        .bDeviceSubClass    = 0x00,
        .bDeviceProtocol    = 0x00,
        .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor           = PORTAL_USB_VID,
        .idProduct          = 0x0150, // Always 0x0150 for PS3/Wii
        // .bcdDevice          = (uint16_t)((pType == 2) ? 0x0200 : 0x0100),
        .bcdDevice          = (uint16_t)(0x0100),
        .iManufacturer      = 0x01,
        .iProduct           = 0x02,
        .iSerialNumber      = 0x03,
        .bNumConfigurations = 0x01
    };
    return (uint8_t const*)&desc;
}

uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    uint8_t pType = g_portalInstance ? g_portalInstance->getPortalType() : 2;
    // if (pType == 2) {
    //     return traptanium_configuration_descriptor;
    // }
    return spyro_configuration_descriptor;
}

uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t buf[32];
    uint8_t n = 0;
    
    uint8_t pType = g_portalInstance ? g_portalInstance->getPortalType() : 2;
    const char* prodString = "Spyro Porta";
    // if (pType == 2) prodString = "Traptanium Portal";

    const char *strs[] = {
        (const char[]){0x09, 0x04}, // 0: English
        PORTAL_USB_MANUFACTURER,    // 1: Manufacturer
        prodString,                 // 2: Product
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
    uint8_t pType = g_portalInstance ? g_portalInstance->getPortalType() : 2;
    // if (pType == 2) {
    //     return traptanium_report_descriptor;
    // }
    return spyro_report_descriptor;
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
    (void)instance; (void)report_id; (void)report_type;
    if (g_portalInstance) {
        g_portalInstance->handleOutput(buffer, bufsize);
    }
}

} // extern "C"

// -----------------------------------------------------------------------

SkylandersPortal::SkylandersPortal(
    SkylanderSlot*     slots,
    volatile uint8_t&  changeCounter,
    volatile uint32_t& usbCommandCounter,
    uint8_t            portalType
)
    : _slots(slots),
      _changeCounter(changeCounter),
      _usbCommandCounter(usbCommandCounter),
      _portalType(portalType)
{
    g_portalInstance = this;
}

void SkylandersPortal::begin() {
    _lastChangeCounter = _changeCounter;
    Serial.println("[HID] USB HID Portal of Power initialized.");
}

void SkylandersPortal::handleOutput(const uint8_t* buffer, uint16_t len) {
    _usbCommandCounter++;

    // Audio stream from Traptanium Portal (PS3 / Wii / Wii U):
    // 1. Packets longer than 32 bytes are always raw audio OUT transfers (64 bytes)
    if (len > 32) {
        if (_audioCallback) {
            _audioCallback(buffer, len);
        }
        return;
    }

    // 2. While speaker is active, capture 32-byte audio packets as well
    if (_speakerActive) {
        uint8_t c = buffer[0];
        // If not a known portal command character, treat as 32-byte PCM audio chunk
        if (c != 'M' && c != 'L' && c != 'Q' && c != 'W' && c != 'R' && 
            c != 'A' && c != 'S' && c != 'C' && c != 'J' && c != 'V') {
            if (_audioCallback) {
                _audioCallback(buffer, len);
            }
            return;
        }
    }

    _handleCommand(buffer, len);
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
    _lastCmdTime = millis();

    // Removed Serial.printf here! Logging every command fills the UART TX buffer
    // and blocks the TinyUSB task during heavy traffic (like level transitions),
    // causing the PS3 to timeout and drop the portal connection.

    switch (cmd[0]) {
    case 'R': // Reset / Query Model
        _portalActive = true;
        resp[0] = 'R';
        switch(_portalType) {
            case 0: // Spyro Portals (Matches KAOS)
                resp[1] = 0x02; resp[2] = 0x1B;
                break;
            case 2: // Traptanium Portal (Default)
            default:
                resp[1] = 0x02; resp[2] = 0x18;
                break;
        }
        break;

    case 'A': // Activate Portal / LED Colors (Matches KAOS)
        _portalActive = (cmd[1] == 0x01);
        resp[0] = 0x41;
        resp[1] = cmd[1];
        resp[2] = 0xFF;
        resp[3] = 0x77;
        break;

    case 'S': // Query Status (Matches KAOS)
        buildStatusReport(resp, true); // <--- TRUE means it's an explicit query from the game
        break;

    case 'Q': // Read 16-byte block from Skylander figure (Matches KAOS)
        {
            uint8_t rawIdx = cmd[1];
            uint8_t blk    = cmd[2];
            uint8_t slot   = rawIdx & 0x0F;

            // When the game reads block 0, it has recognized the figure!
            // Transition status from 0x3 (ADDED) to 0x1 (READY)
            if (blk == 0 && slot < NUM_SLOTS) {
                _arrivalPending[slot] = false;
            }

            resp[0] = 'Q';
            resp[1] = (uint8_t)(0x10 | slot); // Matches KAOS: response slot is 0x10 | slot
            resp[2] = blk;

            if (slot < NUM_SLOTS && _slots[slot].occupied && blk < (FIGURE_SIZE / 16)) {
                memcpy(&resp[3], &_slots[slot].dump_data[blk * 16], 16);
            } else {
                resp[1] = 0x01; // Slot empty or block out of range
            }
        }
        break;

    case 'W': // Write 16-byte block to Skylander figure (Matches KAOS)
        {
            uint8_t rawIdx = cmd[1];
            uint8_t blk    = cmd[2];
            uint8_t slot   = rawIdx & 0x0F;

            if (slot < NUM_SLOTS && blk < (FIGURE_SIZE / 16)) {
                Serial.printf("[HID-WRITE] PS3 wrote Slot %d, Block %02d\n", slot, blk);
            }

            resp[0] = 'W';
            resp[1] = (uint8_t)(0x10 | slot); // Matches KAOS: outbuffer[1] = 0x10 | slot
            resp[2] = blk;
            memcpy(&resp[3], &cmd[3], 16);     // Matches KAOS: Echo complete 16-byte payload back to console

            if (slot < NUM_SLOTS && blk < (FIGURE_SIZE / 16)) {
                if (_slots[slot].occupied) {
                    memcpy(&_slots[slot].dump_data[blk * 16], &cmd[3], 16);
                    _slots[slot].is_modified = true;
                    _slots[slot].last_modified_time = millis();
                }
            }
        }
        break;

    case 'M': // Activate / Deactivate speaker
        resp[0] = 'M';
        resp[1] = cmd[1]; // Echo requested state (e.g. 0x01 = active)
        resp[2] = 0x00;
        resp[3] = 0x19;   // Audio Firmware version (v1.25 / v0.25 from Traptanium portal)
        Serial.printf("[HID] Host sent 'M' speaker command: state=0x%02X\n", cmd[1]);
        if (cmd[1] != 0) {
            _speakerActive = true;
        } else {
            _speakerActive = false;
            if (_audioEndCallback) {
                _audioEndCallback();
            }
        }
        break;

    case 'C': // Skylander Portal Color (Matches KAOS)
        memcpy(resp, cmd, 32);
        break;

    case 'J': // Challenge / Sound (Matches KAOS)
        resp[0] = 'J';
        break;

    case 'L': // Light (Active during audio streaming, matches Dolphin)
        memcpy(resp, cmd, 32);
        break;

    case 'V':
    default:
        hasResp = false;
        break;
    }

    if (hasResp) {
        int timeout_us = 10000;
        while (!tud_hid_n_ready(0) && timeout_us > 0) {
            delayMicroseconds(50);
            timeout_us -= 50;
        }
        tud_hid_n_report(0, 0, resp, 32);
    }
}

void SkylandersPortal::buildStatusReport(uint8_t out[32], bool is_query) {
    memset(out, 0, 32);
    out[0] = 'S';

    uint32_t bits = 0;
    
    for (int i = 0; i < NUM_SLOTS; ++i) {
        bool loaded = _slots[i].occupied;

        if (loaded && !_wasLoaded[i]) {
            _arrivalPending[i] = true;
            _removalPending[i] = false;
            Serial.printf("[STATE] Slot %d INSERTED (flag 0x03 ADDED)\n", i);
        }
        if (!loaded && _wasLoaded[i]) {
            _removalPending[i] = true;
            _arrivalPending[i] = false;
            Serial.printf("[STATE] Slot %d REMOVED (flag 0x02 REMOVING)\n", i);
        }
        _wasLoaded[i] = loaded;

        uint32_t slotBits = 0;
        if (_arrivalPending[i]) {
            slotBits = 0x3; // Arrival flag (3 = 0b11 = ADDED, matches Dolphin & Traptanium)
        } else if (_removalPending[i]) {
            slotBits = 0x2; // Removal flag (2 = 0b10 = REMOVING)
            if (is_query) {
                _removalPending[i] = false;
            }
        } else if (loaded) {
            slotBits = 0x1; // Loaded & present (1 = 0b01 = READY)
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
    if (tud_mounted()) {
        if (_lastCmdTime > 0 && millis() - _lastCmdTime > 2000) {
            if (millis() - _lastTimeoutWarnTime > 2000) {
                Serial.println("[WARN] PS3 SILENCE > 2000ms! (Timeout or Game Paused)");
                _lastTimeoutWarnTime = millis();
            }
        }

        // When a figure is inserted or removed, push a Sense report immediately to the console
        // (Matches KAOS: "Needs to send Sense as soon as a skylander is loaded to be detected")
        if (_changeCounter != _lastChangeCounter) {
            if (tud_hid_n_ready(0)) {
                _lastChangeCounter = _changeCounter;
                uint8_t statusPkt[32];
                buildStatusReport(statusPkt, false);
                tud_hid_n_report(0, 0, statusPkt, 32);
                Serial.println("[HID] Pushed Sense (figure changed) to console");
            }
        }
    }
}

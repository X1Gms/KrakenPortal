// ============================================================
//  main.cpp — KrakenPortal Main Firmware
//  ESP32-S3 Skylander Remote Portal (USB HID + WiFi HTTP API)
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "esp_partition.h"
#include "USB.h"

#include "PortalConfig.h"
#include "PortalHID.h"
#include "PortalAPI.h"

// ── Shared State ─────────────────────────────────────────────
SkylanderSlot     slots[NUM_SLOTS];
volatile uint8_t  changeCounter      = 0;
std::mutex*       portalMutex         = nullptr;
volatile uint32_t usbCommandCounter = 0;

// ── Pointers for safe dynamic construction ───────────────────
SkylandersPortal* portal = nullptr;
PortalAPI*        api    = nullptr;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[KRAKEN-PORTAL] Starting Skylander ESP32-S3 Portal...");

    // Safe dynamic initialization of FreeRTOS/C++ Mutex
    portalMutex = new std::mutex();
    portal      = new SkylandersPortal(slots, *portalMutex, changeCounter, usbCommandCounter);
    api         = new PortalAPI(slots, changeCounter, *portalMutex, usbCommandCounter, HTTP_PORT);

    // ── 1. USB Native HID setup (Register HID BEFORE starting USB) ──
    portal->begin();

    USB.manufacturerName(PORTAL_USB_MANUFACTURER);
    USB.productName(PORTAL_USB_PRODUCT);
    USB.VID(PORTAL_USB_VID);
    USB.PID(PORTAL_USB_PID);
    USB.serialNumber(PORTAL_USB_SERIAL);
    USB.usbVersion(0x0200);
    USB.usbClass(0);
    USB.usbSubClass(0);
    USB.usbProtocol(0);
    USB.begin();
    Serial.printf("[KRAKEN-PORTAL] USB HID Portal of Power initialized (VID: 0x%04X, PID: 0x%04X)\n", PORTAL_USB_VID, PORTAL_USB_PID);

    // ── 2. LittleFS Storage Init ──────────────────────────────
    if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
        Serial.println("[KRAKEN-PORTAL] LittleFS mount failed — formatting...");
    } else {
        Serial.println("[KRAKEN-PORTAL] LittleFS mounted successfully.");
    }

    // ── 3. WiFi Setup: Static IP STA with AP Fallback ──────────
    WiFi.mode(WIFI_STA);
    WiFi.config(PORTAL_IP, PORTAL_GATEWAY, PORTAL_SUBNET, PORTAL_DNS);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[KRAKEN-PORTAL] Connecting to Home WiFi: " + String(WIFI_SSID));

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[KRAKEN-PORTAL] Connected to Home WiFi!");
        Serial.println("[KRAKEN-PORTAL] Static IP: http://" + WiFi.localIP().toString() + ":" + String(HTTP_PORT));
    } else {
        Serial.println("[KRAKEN-PORTAL] WiFi not connected. Starting Fallback AP...");
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(AP_IP, AP_IP, PORTAL_SUBNET);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        Serial.println("[KRAKEN-PORTAL] Fallback AP Started: " + String(AP_SSID));
        Serial.println("[KRAKEN-PORTAL] AP IP: http://" + WiFi.softAPIP().toString() + ":" + String(HTTP_PORT));
    }

    // ── 4. Restore Active Slots from LittleFS Backup ───────────
    {
        std::lock_guard<std::mutex> lock(*portalMutex);
        for (int i = 0; i < NUM_SLOTS; ++i) {
            String path = "/active_" + String(i) + ".bin";
            File f = LittleFS.open(path, "r");
            if (f && f.size() == FIGURE_SIZE) {
                f.read(slots[i].dump_data, FIGURE_SIZE);
                slots[i].occupied = true;
                f.close();

                String namePath = "/active_" + String(i) + ".name";
                File nf = LittleFS.open(namePath, "r");
                if (nf) {
                    slots[i].file_name = nf.readString();
                    nf.close();
                } else {
                    slots[i].file_name = "restored_slot_" + String(i + 1) + ".bin";
                }
                Serial.printf("[KRAKEN-PORTAL] Restored slot %d: %s\n", i + 1, slots[i].file_name.c_str());
            } else if (f) {
                f.close();
            }
        }
        changeCounter = 1; // Signal USB HID status report
    }

    // ── 5. Start HTTP API Server ──────────────────────────────
    api->begin();
    Serial.println("[KRAKEN-PORTAL] System ready!");
}

void loop() {
    // Process incoming HTTP requests from Flutter App
    if (api) api->handle();

    // Process periodic USB HID status reporting (50ms interval)
    if (portal) portal->loop();

    // Deferred Save: Write console-modified slots back to LittleFS
    for (int i = 0; i < NUM_SLOTS; ++i) {
        if (slots[i].is_modified) {
            std::lock_guard<std::mutex> lock(*portalMutex);
            slots[i].is_modified = false;

            File f = LittleFS.open("/active_" + String(i) + ".bin", "w");
            if (f) {
                f.write(slots[i].dump_data, FIGURE_SIZE);
                f.close();
                Serial.printf("[KRAKEN-PORTAL] Deferred save: slot %d updated on LittleFS\n", i + 1);
            }
        }
    }

    delay(2);
}

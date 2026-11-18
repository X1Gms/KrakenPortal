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
#include "PortalWebSocket.h"
#include "ConfigManager.h"

// ── Shared State ─────────────────────────────────────────────
SkylanderSlot     slots[NUM_SLOTS];
volatile uint8_t  changeCounter      = 0;
volatile uint32_t usbCommandCounter = 0;

// ── Pointers for safe dynamic construction ───────────────────
SkylandersPortal* portal = nullptr;
PortalWebSocket*  wsApi  = nullptr;



void setup() {
    // ── 0. Performance Optimization: Force Max Clock Speed ────────
    setCpuFrequencyMhz(240);

    Serial.begin(115200);
    delay(500);
    Serial.println("\n[KRAKEN-PORTAL] Starting Skylander ESP32-S3 Portal...");

    // ── 1. USB Enumeration Delay ──────────────────────────────────
    // The PS3 needs time to power the port before we attach.
    // LittleFS was accidentally providing this delay. We replace it with a clean delay.
    delay(1000);

    uint8_t pType = 0; // 0 = Spyro's Adventure (matches KAOS 'R' command response 0x1B)

    // Safe dynamic initialization
    portal      = new SkylandersPortal(slots, changeCounter, usbCommandCounter, pType);
    wsApi       = new PortalWebSocket(slots, changeCounter, HTTP_PORT);

    portal->setAudioCallback([](const uint8_t* data, size_t len) {
        if (wsApi) wsApi->pushAudio(data, len);
    });
    portal->setAudioEndCallback([]() {
        if (wsApi) wsApi->signalAudioEnd();
    });

    // ── 2. USB Native HID setup (Register HID BEFORE starting USB) ──
    portal->begin();

    USB.manufacturerName(PORTAL_USB_MANUFACTURER);
    // USB.productName((pType == 2) ? "Traptanium Portal" : "Spyro Porta");
    USB.productName("Spyro Porta");
    USB.VID(PORTAL_USB_VID);
    USB.PID(0x0150); // Hardcoded to 0x0150 as required by PS3 OS
    USB.serialNumber(PORTAL_USB_SERIAL);
    // USB.usbVersion((pType == 2) ? 0x0200 : 0x0110);
    USB.usbVersion(0x0110);
    USB.usbClass(0);
    USB.usbSubClass(0);
    USB.usbProtocol(0);
    USB.begin();
    
    Serial.printf("[KRAKEN-PORTAL] USB HID Portal of Power initialized (VID: 0x%04X, PID: 0x%04X)\n", PORTAL_USB_VID, 0x0150);

    if (!ConfigManager::loadOrGenerate()) {
        // Halt execution because the file is missing
        while (true) {
            yield();
        }
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    WiFi.config(PORTAL_IP, PORTAL_GATEWAY, PORTAL_SUBNET, PORTAL_DNS);
    WiFi.begin(ConfigManager::getSsid().c_str(), ConfigManager::getPassword().c_str());
    Serial.print("[KRAKEN-PORTAL] Connecting to Home WiFi: " + ConfigManager::getSsid());

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[KRAKEN-PORTAL] Static IP: http://" + WiFi.localIP().toString() + ":" + String(HTTP_PORT));
    } else {
        Serial.println("[KRAKEN-PORTAL] WiFi connection failed. Please restart or clear config.json to reconfigure.");
    }

    // ── 4. Active Slots are now purely RAM-based ───────────
    changeCounter = 0;

    // ── 5. Start WebSocket API Server ──────────────────────────────
    wsApi->begin();

    // ── 6. (Removed) Single Threaded Execution ─────────────────
    Serial.println("[KRAKEN-PORTAL] FreeRTOS: Single threaded execution active.");

    Serial.println("[KRAKEN-PORTAL] System ready!");
}

// Global debounce state for LittleFS saves (Legacy/Unused but kept for structure)
unsigned long slot_modified_time[NUM_SLOTS] = {0};
bool slot_pending_save[NUM_SLOTS] = {false};

void loop() {
    // Process periodic USB HID status reporting (50ms interval)
    if (portal) portal->loop();

    // Process WebSocket API Server
    if (wsApi) wsApi->loop();

    // Yield to avoid watchdog resets, but do not sleep/delay
    // to ensure maximum USB HID polling rate.
    yield();
}

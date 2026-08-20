#include "ConfigManager.h"

String ConfigManager::_ssid = "";
String ConfigManager::_password = "";
String ConfigManager::_token = "SkylanderPortalSuperSecretKey123";

bool ConfigManager::loadOrGenerate() {
    if (!LittleFS.begin(true)) {
        Serial.println("[SECURITY] Failed to mount LittleFS!");
        return false;
    }

    if (!LittleFS.exists("/config.json")) {
        Serial.println("[SECURITY] ==============================================================");
        Serial.println("[SECURITY] config.json NOT FOUND in LittleFS!");
        Serial.println("[SECURITY] Execution halted.");
        Serial.println("[SECURITY] Please create a 'data' folder in your project with the");
        Serial.println("[SECURITY] 'config.json' file, run 'Upload File System Image' and reboot.");
        Serial.println("[SECURITY] ==============================================================");
        return false;
    }

    // File exists, let's load it
    File file = LittleFS.open("/config.json", "r");
    if (!file) {
        Serial.println("[SECURITY] Failed to open existing config.json.");
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.println("[SECURITY] Failed to parse config.json. Invalid format.");
        file.close();
        return false;
    }

    _ssid = doc["ssid"] | "";
    _password = doc["password"] | "";
    _token = doc["token"] | "SkylanderPortalSuperSecretKey123";
    file.close();

    Serial.println("[SECURITY] config.json loaded successfully!");
    return true;
}

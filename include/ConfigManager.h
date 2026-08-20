#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

class ConfigManager {
public:
    // Returns true if the file was loaded successfully.
    // Returns false if the file is missing (prompts for manual creation).
    static bool loadOrGenerate();
    
    static String getSsid() { return _ssid; }
    static String getPassword() { return _password; }
    static String getToken() { return _token; }

private:
    static String _ssid;
    static String _password;
    static String _token;
};

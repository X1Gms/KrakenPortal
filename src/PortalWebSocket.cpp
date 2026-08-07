#include "PortalWebSocket.h"
#include <ArduinoJson.h>
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"

// 32-byte Key for AES-256 (null terminator ignored)
static const unsigned char aes_key[32] = {
    'S','k','y','l','a','n','d','e','r','P','o','r','t','a','l',
    'S','u','p','e','r','S','e','c','r','e','t','K','e','y','1','2','3'
};
// 16-byte IV for CBC (null terminator ignored)
static const unsigned char aes_iv_orig[16] = {
    'P','o','r','t','a','l','I','n','i','t','i','a','l','V','e','c'
};

PortalWebSocket::PortalWebSocket(
    SkylanderSlot*     slots,
    volatile uint8_t&  changeCounter,
    std::mutex&        portalMutex,
    uint16_t           port
) : _webSocket(port), 
    _slots(slots), 
    _changeCounter(changeCounter), 
    _mutex(portalMutex),
    _lastChangeCounter(255) 
{}

void PortalWebSocket::begin() {
    _webSocket.begin();
    _webSocket.onEvent([this](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
        this->_onWebSocketEvent(num, type, payload, length);
    });
    Serial.println("[KRAKEN-WS] WebSocket server started.");
}

void PortalWebSocket::loop() {
    _webSocket.loop();
    
    // Check if slots changed (e.g., figure added/removed by USB)
    uint8_t currentCounter = _changeCounter;
    if (currentCounter != _lastChangeCounter) {
        _lastChangeCounter = currentCounter;
        _broadcastSlots();
    }
}

void PortalWebSocket::_onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[KRAKEN-WS] [%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            Serial.printf("[KRAKEN-WS] [%u] Connected!\n", num);
            // Immediately send current state on connect
            _lastChangeCounter = _changeCounter - 1; // force trigger
            break;
        case WStype_TEXT: {
            String msg = String((char*)payload, length);
            _handleMessage(num, msg);
            break;
        }
        default:
            break;
    }
}

void PortalWebSocket::_broadcastSlots() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (int i = 0; i < NUM_SLOTS; i++) {
            JsonObject obj = arr.add<JsonObject>();
            obj["slot"] = i + 1;
            obj["occupied"] = _slots[i].occupied;
            obj["file_name"] = _slots[i].file_name;
            obj["is_modified"] = _slots[i].is_modified;
        }
    }
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    
    // Encrypt the payload before sending
    String encrypted = _encryptAES(jsonStr);
    _webSocket.broadcastTXT(encrypted);
}

void PortalWebSocket::_handleMessage(uint8_t num, const String& msg) {
    // Decrypt incoming message
    String decrypted = _decryptAES(msg);
    if (decrypted.isEmpty()) return;
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, decrypted);
    if (err) return;
    
    String action = doc["action"] | "";
    int slot = doc["slot"] | -1;
    String reqId = doc["req_id"] | "";
    
    bool success = false;

    if (action == "GET") {
        _lastChangeCounter = _changeCounter - 1;
        Serial.println("[KRAKEN-WS] Forced broadcast via GET API");
        success = true;
    }
    else if (slot >= 1 && slot <= NUM_SLOTS) {
        if (action == "DELETE") {
            std::lock_guard<std::mutex> lock(_mutex);
            _slots[slot - 1].occupied = false;
            _slots[slot - 1].file_name = "";
            _slots[slot - 1].is_modified = true;
            memset(_slots[slot - 1].dump_data, 0, FIGURE_SIZE);
            _changeCounter++;
            Serial.printf("[KRAKEN-WS] Slot %d cleared via API\n", slot);
            success = true;
        }
        else if (action == "POST") {
            String filename = doc["filename"] | "unknown.dump";
            String b64data = doc["data"] | "";
            
            if (b64data.length() > 0) {
                size_t decodedLen = 0;
                unsigned char* decoded = (unsigned char*)malloc(b64data.length());
                if (decoded) {
                    mbedtls_base64_decode(decoded, b64data.length(), &decodedLen, (const unsigned char*)b64data.c_str(), b64data.length());
                    
                    if (decodedLen == FIGURE_SIZE) {
                        std::lock_guard<std::mutex> lock(_mutex);
                        memcpy(_slots[slot - 1].dump_data, decoded, FIGURE_SIZE);
                        _slots[slot - 1].file_name = filename;
                        _slots[slot - 1].is_modified = true;
                        _slots[slot - 1].occupied = true;
                        _changeCounter++;
                        Serial.printf("[KRAKEN-WS] Slot %d loaded via API: %s\n", slot, filename.c_str());
                        success = true;
                    }
                    free(decoded);
                }
            }
        }
    }

    if (!reqId.isEmpty()) {
        JsonDocument ackDoc;
        ackDoc["ack"] = reqId;
        ackDoc["status"] = success ? "success" : "error";
        String ackStr;
        serializeJson(ackDoc, ackStr);
        String encryptedAck = _encryptAES(ackStr);
        if (!encryptedAck.isEmpty()) {
            _webSocket.sendTXT(num, encryptedAck);
        }
    }
}

String PortalWebSocket::_encryptAES(const String& payload) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, aes_key, 256);
    
    size_t payloadLen = payload.length();
    uint8_t padding = 16 - (payloadLen % 16);
    size_t paddedLen = payloadLen + padding;
    
    unsigned char* input = (unsigned char*)malloc(paddedLen);
    unsigned char* output = (unsigned char*)malloc(paddedLen);
    if (!input || !output) {
        if (input) free(input);
        if (output) free(output);
        mbedtls_aes_free(&aes);
        return "";
    }

    memcpy(input, payload.c_str(), payloadLen);
    for (size_t i = payloadLen; i < paddedLen; i++) {
        input[i] = padding;
    }
    
    unsigned char iv[16];
    esp_fill_random(iv, 16);
    
    unsigned char iv_copy[16];
    memcpy(iv_copy, iv, 16);
    
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv_copy, input, output);
    mbedtls_aes_free(&aes);
    
    size_t combinedLen = 16 + paddedLen;
    unsigned char* combined = (unsigned char*)malloc(combinedLen);
    if (!combined) {
        free(input);
        free(output);
        return "";
    }
    
    memcpy(combined, iv, 16);
    memcpy(combined + 16, output, paddedLen);
    
    size_t b64Len = 0;
    mbedtls_base64_encode(nullptr, 0, &b64Len, combined, combinedLen);
    unsigned char* b64Out = (unsigned char*)malloc(b64Len);
    if (!b64Out) {
        free(input);
        free(output);
        free(combined);
        return "";
    }
    
    mbedtls_base64_encode(b64Out, b64Len, &b64Len, combined, combinedLen);
    String result = String((char*)b64Out);
    
    free(input);
    free(output);
    free(combined);
    free(b64Out);
    
    return result;
}

String PortalWebSocket::_decryptAES(const String& ciphertextBase64) {
    size_t decodedLen = 0;
    mbedtls_base64_decode(nullptr, 0, &decodedLen, (const unsigned char*)ciphertextBase64.c_str(), ciphertextBase64.length());
    if (decodedLen < 32 || (decodedLen - 16) % 16 != 0) return "";
    
    unsigned char* decoded = (unsigned char*)malloc(decodedLen);
    if (!decoded) return "";
    
    mbedtls_base64_decode(decoded, decodedLen, &decodedLen, (const unsigned char*)ciphertextBase64.c_str(), ciphertextBase64.length());
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, aes_key, 256);
    
    unsigned char iv[16];
    memcpy(iv, decoded, 16);
    
    size_t cipherLen = decodedLen - 16;
    unsigned char* output = (unsigned char*)malloc(cipherLen);
    if (!output) {
        free(decoded);
        mbedtls_aes_free(&aes);
        return "";
    }
    
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipherLen, iv, decoded + 16, output);
    mbedtls_aes_free(&aes);
    
    uint8_t padding = output[cipherLen - 1];
    String result = "";
    if (padding <= 16 && padding <= cipherLen) {
        result = String((char*)output, cipherLen - padding);
    }
    
    free(decoded);
    free(output);
    return result;
}

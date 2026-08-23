#include "PortalWebSocket.h"
#include "ConfigManager.h"
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"

// We dynamically generate the AES key based on the token
static void get_aes_key(unsigned char* out_key) {
    String token = ConfigManager::getToken();
    if (token == "") token = "SkylanderPortalSuperSecretKey123";
    mbedtls_sha256((const unsigned char*)token.c_str(), token.length(), out_key, 0);
}

PortalWebSocket::PortalWebSocket(
    SkylanderSlot*     slots,
    volatile uint8_t&  changeCounter,
    uint16_t           port
) : _webSocket(port), 
    _slots(slots), 
    _changeCounter(changeCounter), 
    _lastChangeCounter(255) 
{}

void PortalWebSocket::begin() {
    _webSocket.begin();
    _webSocket.onEvent([this](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
        this->_onWebSocketEvent(num, type, payload, length);
    });
    Serial.println("[KRAKEN-WS] Binary WebSocket server started.");
}

void PortalWebSocket::loop() {
    _webSocket.loop();
    
    bool needsBroadcast = false;
    
    // Check for physical insertion/removal changes (from HID)
    uint8_t currentCounter = _changeCounter;
    if (currentCounter != _lastChangeCounter) {
        _lastChangeCounter = currentCounter;
        needsBroadcast = true;
    }
    
    // Check for modification state changes (from PS3 Writes)
    static unsigned long lastModifiedCheck = 0;
    if (millis() - lastModifiedCheck > 100) {
        lastModifiedCheck = millis();
        for(int i = 0; i < NUM_SLOTS; i++) {
            if (_slots[i].occupied && _slots[i].is_modified) {
                // Debounce: Only broadcast if the PS3 has stopped writing for 1000ms
                if (millis() - _slots[i].last_modified_time > 1000) {
                    if (!_lastModifiedState[i]) {
                        _lastModifiedState[i] = true;
                        needsBroadcast = true;
                    }
                }
            } else {
                if (_lastModifiedState[i]) {
                    _lastModifiedState[i] = false;
                    needsBroadcast = true;
                }
            }
        }
    }
    
    if (needsBroadcast) {
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
            // Disconnect any ghost connections to prevent the 5-client limit lockout
            for (int i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
                if (i != num) {
                    _webSocket.disconnect(i);
                }
            }
            _lastChangeCounter = _changeCounter - 1; // force trigger
            break;
        case WStype_BIN: {
            _handleMessage(num, payload, length);
            break;
        }
        case WStype_TEXT:
            Serial.println("[KRAKEN-WS] Ignored WStype_TEXT. Only BIN supported.");
            break;
        default:
            break;
    }
}

void PortalWebSocket::_broadcastSlots() {
    uint8_t plain[600];
    size_t offset = 0;
    
    plain[offset++] = 0x81;
    plain[offset++] = NUM_SLOTS;
    
    {
        for (int i = 0; i < NUM_SLOTS; i++) {
            plain[offset++] = i + 1;
            plain[offset++] = _slots[i].occupied ? 1 : 0;
            plain[offset++] = _slots[i].is_modified ? 1 : 0;
            
            size_t nameLen = _slots[i].file_name.length();
            if (nameLen > 32) nameLen = 32;
            plain[offset++] = (uint8_t)nameLen;
            
            if (nameLen > 0) {
                memcpy(&plain[offset], _slots[i].file_name.c_str(), nameLen);
                offset += nameLen;
            }
        }
    }
    
    uint8_t* cipher = nullptr;
    size_t cipherLen = _encryptAES(plain, offset, &cipher);
    if (cipher && cipherLen > 0) {
        _webSocket.broadcastBIN(cipher, cipherLen);
        free(cipher);
    }
}

void PortalWebSocket::_handleMessage(uint8_t num, const uint8_t* payload, size_t length) {
    uint8_t* decrypted = nullptr;
    size_t decLen = _decryptAES(payload, length, &decrypted);
    if (!decrypted || decLen < 7) {
        if (decrypted) free(decrypted);
        return;
    }
    
    uint8_t action = decrypted[0]; // 1 = GET, 2 = POST, 3 = DELETE
    uint8_t slot = decrypted[1];
    uint32_t reqId = *((uint32_t*)&decrypted[2]);
    uint8_t nameLen = decrypted[6];
    
    bool success = false;
    uint8_t* responseData = nullptr;
    size_t responseLen = 0;

    if (action == 0x01) { // GET
        _lastChangeCounter = _changeCounter - 1;
        success = true;
    }
    else if (slot >= 1 && slot <= NUM_SLOTS) {
        if (action == 0x03) { // DELETE
            bool has_dump = false;
            {
                if (_slots[slot - 1].occupied) {
                    responseData = (uint8_t*)malloc(FIGURE_SIZE);
                    if (responseData) {
                        memcpy(responseData, _slots[slot - 1].dump_data, FIGURE_SIZE);
                        responseLen = FIGURE_SIZE;
                        has_dump = true;
                    }
                }
                _slots[slot - 1].occupied = false;
                _slots[slot - 1].file_name = "";
                _slots[slot - 1].is_modified = true;
                memset(_slots[slot - 1].dump_data, 0, FIGURE_SIZE);
                _changeCounter++;
                Serial.printf("[KRAKEN-WS] Slot %d cleared via API\n", slot);
                success = true;
            }
        }
        else if (action == 0x02) { // POST
            if (decLen >= 7 + nameLen + FIGURE_SIZE) {
                char fname[64] = {0};
                if (nameLen > 0 && nameLen < 63) {
                    memcpy(fname, &decrypted[7], nameLen);
                }
                
                {
                    memcpy(_slots[slot - 1].dump_data, &decrypted[7 + nameLen], FIGURE_SIZE);
                    _slots[slot - 1].file_name = String(fname);
                    _slots[slot - 1].is_modified = false; // We just uploaded it, no need to sync back!
                    _slots[slot - 1].occupied = true;
                    _changeCounter++;
                    
                    Serial.printf("[KRAKEN-WS] Slot %d loaded via API: %s\n", slot, fname);
                }
                success = true;
            }
        }
        else if (action == 0x04) { // SYNC
            if (_slots[slot - 1].occupied && _slots[slot - 1].is_modified) {
                responseData = (uint8_t*)malloc(FIGURE_SIZE);
                if (responseData) {
                    memcpy(responseData, _slots[slot - 1].dump_data, FIGURE_SIZE);
                    responseLen = FIGURE_SIZE;
                    _slots[slot - 1].is_modified = false;
                    
                    Serial.printf("[KRAKEN-WS] Slot %d auto-synced to App\n", slot);
                    success = true;
                }
            }
        }
    }

    if (reqId != 0) {
        size_t ackPlainLen = 6 + responseLen;
        uint8_t* ackPlain = (uint8_t*)malloc(ackPlainLen);
        if (ackPlain) {
            ackPlain[0] = 0x80;
            ackPlain[1] = success ? 1 : 0;
            memcpy(&ackPlain[2], &reqId, 4);
            if (responseLen > 0 && responseData) {
                memcpy(&ackPlain[6], responseData, responseLen);
            }
            
            uint8_t* ackCipher = nullptr;
            size_t ackCipherLen = _encryptAES(ackPlain, ackPlainLen, &ackCipher);
            if (ackCipher && ackCipherLen > 0) {
                _webSocket.sendBIN(num, ackCipher, ackCipherLen);
                free(ackCipher);
            }
            free(ackPlain);
        }
    }
    
    if (responseData) free(responseData);
    if (decrypted) free(decrypted);
}

size_t PortalWebSocket::_encryptAES(const uint8_t* payload, size_t payloadLen, uint8_t** outCiphertext) {
    if (!payload || payloadLen == 0 || !outCiphertext) return 0;
    
    unsigned char dynamic_key[32];
    get_aes_key(dynamic_key);
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, dynamic_key, 256);
    
    uint8_t padding = 16 - (payloadLen % 16);
    size_t paddedLen = payloadLen + padding;
    
    uint8_t* input = (uint8_t*)malloc(paddedLen);
    uint8_t* output = (uint8_t*)malloc(paddedLen);
    if (!input || !output) {
        if (input) free(input);
        if (output) free(output);
        mbedtls_aes_free(&aes);
        *outCiphertext = nullptr;
        return 0;
    }

    memcpy(input, payload, payloadLen);
    for (size_t i = payloadLen; i < paddedLen; i++) {
        input[i] = padding;
    }
    
    uint8_t iv[16];
    esp_fill_random(iv, 16);
    
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16);
    
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv_copy, input, output);
    mbedtls_aes_free(&aes);
    
    size_t combinedLen = 16 + paddedLen;
    uint8_t* combined = (uint8_t*)malloc(combinedLen);
    if (!combined) {
        free(input);
        free(output);
        *outCiphertext = nullptr;
        return 0;
    }
    
    memcpy(combined, iv, 16);
    memcpy(combined + 16, output, paddedLen);
    
    free(input);
    free(output);
    
    *outCiphertext = combined;
    return combinedLen;
}

size_t PortalWebSocket::_decryptAES(const uint8_t* ciphertext, size_t cipherLen, uint8_t** outPlaintext) {
    if (!ciphertext || cipherLen < 32 || (cipherLen - 16) % 16 != 0 || !outPlaintext) return 0;
    
    unsigned char dynamic_key[32];
    get_aes_key(dynamic_key);
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, dynamic_key, 256);
    
    uint8_t iv[16];
    memcpy(iv, ciphertext, 16);
    
    size_t encLen = cipherLen - 16;
    uint8_t* output = (uint8_t*)malloc(encLen);
    if (!output) {
        mbedtls_aes_free(&aes);
        *outPlaintext = nullptr;
        return 0;
    }
    
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, encLen, iv, &ciphertext[16], output);
    mbedtls_aes_free(&aes);
    
    uint8_t padding = output[encLen - 1];
    if (padding > 16 || padding > encLen) {
        free(output);
        *outPlaintext = nullptr;
        return 0;
    }
    
    size_t plainLen = encLen - padding;
    uint8_t* result = (uint8_t*)malloc(plainLen);
    if (!result) {
        free(output);
        *outPlaintext = nullptr;
        return 0;
    }
    
    memcpy(result, output, plainLen);
    free(output);
    
    *outPlaintext = result;
    return plainLen;
}

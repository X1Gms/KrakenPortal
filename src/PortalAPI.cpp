// ============================================================
//  PortalAPI.cpp — Skylanders Remote Portal HTTP API
// ============================================================

#include "PortalAPI.h"

PortalAPI::PortalAPI(
    SkylanderSlot*     slots,
    volatile uint8_t&  changeCounter,
    std::mutex&        portalMutex,
    volatile uint32_t& usbCommandCounter,
    uint16_t           port
)
    : _server(port),
      _slots(slots),
      _changeCounter(changeCounter),
      _mutex(portalMutex),
      _usbCommandCounter(usbCommandCounter)
{}

void PortalAPI::begin() {
    _server.begin();
    Serial.printf("[API] HTTP server listening on port %d\n", HTTP_PORT);
}

void PortalAPI::handle() {
    WiFiClient client = _server.available();
    if (!client) return;

    client.setNoDelay(true);

    // Wait for incoming HTTP request line (timeout 1s)
    unsigned long waitStart = millis();
    while (!client.available() && (millis() - waitStart < 1000)) delay(1);

    if (!client.available()) { client.stop(); return; }

    String reqLine = client.readStringUntil('\n');
    reqLine.trim();
    if (reqLine.length() == 0) { client.stop(); return; }

    int sp1 = reqLine.indexOf(' ');
    int sp2 = reqLine.indexOf(' ', sp1 + 1);
    if (sp1 == -1 || sp2 == -1) { client.stop(); return; }

    String method = reqLine.substring(0, sp1);
    String uri    = reqLine.substring(sp1 + 1, sp2);

    int    contentLength = 0;
    String filename      = "skylander_remote.bin";

    // Read HTTP headers
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) break; // Blank line signals end of headers

        int colon = line.indexOf(':');
        if (colon == -1) continue;

        String hName = line.substring(0, colon);  hName.trim();
        String hVal  = line.substring(colon + 1); hVal.trim();

        if (hName.equalsIgnoreCase("Content-Length")) {
            contentLength = hVal.toInt();
        } else if (hName.equalsIgnoreCase("Content-Disposition")) {
            int pos = hVal.indexOf("filename=");
            if (pos != -1) {
                filename = hVal.substring(pos + 9);
                filename.replace("\"", "");
                filename.trim();
            }
        }
    }

    if      (method == "GET"    && uri == "/slots")          _handleGetSlots(client);
    else if (method == "GET"    && uri.startsWith("/slot/")) _handleGetSlot(client,    uri.substring(6).toInt() - 1);
    else if (method == "POST"   && uri.startsWith("/slot/")) _handlePostSlot(client,   uri.substring(6).toInt() - 1, contentLength, filename);
    else if (method == "DELETE" && uri.startsWith("/slot/")) _handleDeleteSlot(client, uri.substring(6).toInt() - 1);
    else if (method == "GET"    && uri.startsWith("/api/portal_type")) _handleGetPortalType(client);
    else if (method == "POST"   && uri.startsWith("/api/portal_type")) _handleSetPortalType(client, uri);
    else                                                     _sendError(client, 404, "Not Found");

    client.stop();
}

// ── Route Handlers ───────────────────────────────────────────

// GET /slots
void PortalAPI::_handleGetSlots(WiFiClient& client) {
    String json = "[";
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (i > 0) json += ",";

        bool   occupied = false;
        String fname    = "";
        bool   remote   = false;

        if (i < NUM_SLOTS) { // Active RAM slot
            std::lock_guard<std::mutex> lock(_mutex);
            occupied = _slots[i].occupied;
            fname    = _slots[i].file_name;
            remote   = _slots[i].occupied;
        } else { // Virtual LittleFS slot
            String path = "/inactive_" + String(i) + ".bin";
            if (LittleFS.exists(path)) {
                occupied = true;
                remote   = true;
                String namePath = "/inactive_" + String(i) + ".name";
                if (LittleFS.exists(namePath)) {
                    File f = LittleFS.open(namePath, "r");
                    fname  = f.readString();
                    f.close();
                } else {
                    fname = "virtual_slot_" + String(i + 1) + ".bin";
                }
            }
        }

        fname.replace("\"", "\\\"");
        json += "{\"slot\":"     + String(i + 1) +
                ",\"occupied\":" + (occupied ? "true" : "false") +
                ",\"fileName\":\"" + fname + "\"" +
                ",\"remote\":"   + (remote ? "true" : "false") + "}";
    }
    json += "]";

    _sendHeader(client, 200, "application/json", json.length());
    client.print(json);
}

// GET /slot/<id>
void PortalAPI::_handleGetSlot(WiFiClient& client, int slot) {
    if (slot < 0 || slot >= MAX_SLOTS) {
        _sendError(client, 400, "Slot invalido");
        return;
    }

    uint8_t buffer[FIGURE_SIZE];
    bool    hasData = false;
    String  fname   = "";

    if (slot < NUM_SLOTS) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_slots[slot].occupied) {
            memcpy(buffer, _slots[slot].dump_data, FIGURE_SIZE);
            fname   = _slots[slot].file_name;
            hasData = true;
        }
    } else {
        String path = "/inactive_" + String(slot) + ".bin";
        if (LittleFS.exists(path)) {
            File f = LittleFS.open(path, "r");
            if (f && f.size() == FIGURE_SIZE) {
                f.read(buffer, FIGURE_SIZE);
                hasData = true;
            }
            if (f) f.close();

            String namePath = "/inactive_" + String(slot) + ".name";
            if (LittleFS.exists(namePath)) {
                File nf = LittleFS.open(namePath, "r");
                fname   = nf.readString();
                nf.close();
            } else {
                fname = "virtual_slot_" + String(slot + 1) + ".bin";
            }
        }
    }

    if (hasData) {
        String disp = "Content-Disposition: attachment; filename=\"" + fname + "\"\r\n";
        _sendHeader(client, 200, "application/octet-stream", FIGURE_SIZE, disp);
        client.write(buffer, FIGURE_SIZE);
    } else {
        _sendError(client, 404, "Slot vazio");
    }
}

// POST /slot/<id>
void PortalAPI::_handlePostSlot(WiFiClient& client, int slot, int contentLength, const String& filename) {
    if (slot < 0 || slot >= MAX_SLOTS) {
        _sendError(client, 400, "Slot invalido");
        return;
    }
    if (contentLength != FIGURE_SIZE) {
        _sendError(client, 400, "Erro: Dump deve ter 1024 bytes. Recebido: " + String(contentLength));
        return;
    }

    bool occupied = false;
    if (slot < NUM_SLOTS) {
        std::lock_guard<std::mutex> lock(_mutex);
        occupied = _slots[slot].occupied;
    } else {
        occupied = LittleFS.exists("/inactive_" + String(slot) + ".bin");
    }

    if (occupied) {
        _sendError(client, 409, "Slot ocupado com Skylander remoto. Faca DELETE primeiro.");
        return;
    }

    // Read exactly 1024 bytes from TCP stream with 5s timeout
    uint8_t buffer[FIGURE_SIZE];
    int     bytesRead = 0;
    unsigned long t0  = millis();

    while (bytesRead < FIGURE_SIZE && (millis() - t0 < 5000)) {
        while (client.available() && bytesRead < FIGURE_SIZE) {
            buffer[bytesRead++] = client.read();
        }
        delay(1);
    }

    if (bytesRead != FIGURE_SIZE) {
        _sendError(client, 400, "Erro ao ler body. Esperado 1024, lido: " + String(bytesRead));
        return;
    }

    if (slot < NUM_SLOTS) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            memcpy(_slots[slot].dump_data, buffer, FIGURE_SIZE);
            _slots[slot].occupied    = true;
            _slots[slot].file_name   = filename;
            _slots[slot].is_modified = false;
        }
        File f = LittleFS.open("/active_" + String(slot) + ".bin", "w");
        if (f) { f.write(buffer, FIGURE_SIZE); f.close(); }
        File nf = LittleFS.open("/active_" + String(slot) + ".name", "w");
        if (nf) { nf.print(filename); nf.close(); }

        _changeCounter++; // Signal HID status to update arrival bitfield
        Serial.printf("[API] POST slot %d OK: %s\n", slot + 1, filename.c_str());
    } else {
        File f = LittleFS.open("/inactive_" + String(slot) + ".bin", "w");
        if (f) { f.write(buffer, FIGURE_SIZE); f.close(); }
        File nf = LittleFS.open("/inactive_" + String(slot) + ".name", "w");
        if (nf) { nf.print(filename); nf.close(); }
        Serial.printf("[API] POST virtual slot %d OK: %s\n", slot + 1, filename.c_str());
    }

    String msg = "Skylander carregado com sucesso";
    _sendHeader(client, 200, "text/plain; charset=utf-8", msg.length());
    client.print(msg);
}

// DELETE /slot/<id>
void PortalAPI::_handleDeleteSlot(WiFiClient& client, int slot) {
    if (slot < 0 || slot >= MAX_SLOTS) {
        _sendError(client, 400, "Slot invalido");
        return;
    }

    uint8_t buffer[FIGURE_SIZE];
    bool    hasData = false;
    String  fname   = "";

    if (slot < NUM_SLOTS) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_slots[slot].occupied) {
            memcpy(buffer, _slots[slot].dump_data, FIGURE_SIZE);
            fname   = _slots[slot].file_name;
            hasData = true;

            _slots[slot].occupied    = false;
            _slots[slot].file_name   = "";
            _slots[slot].is_modified = false;

            LittleFS.remove("/active_" + String(slot) + ".bin");
            LittleFS.remove("/active_" + String(slot) + ".name");

            _changeCounter++; // Signal HID status to update removal bitfield
            Serial.printf("[API] DELETE slot %d OK\n", slot + 1);
        }
    } else {
        String path     = "/inactive_" + String(slot) + ".bin";
        String namePath = "/inactive_" + String(slot) + ".name";
        if (LittleFS.exists(path)) {
            File f = LittleFS.open(path, "r");
            if (f && f.size() == FIGURE_SIZE) {
                f.read(buffer, FIGURE_SIZE);
                hasData = true;
            }
            if (f) f.close();

            if (LittleFS.exists(namePath)) {
                File nf = LittleFS.open(namePath, "r");
                fname   = nf.readString();
                nf.close();
            } else {
                fname = "virtual_slot_" + String(slot + 1) + ".bin";
            }
            LittleFS.remove(path);
            LittleFS.remove(namePath);
            Serial.printf("[API] DELETE virtual slot %d OK\n", slot + 1);
        }
    }

    if (hasData) {
        String disp = "Content-Disposition: attachment; filename=\"" + fname + "\"\r\n";
        _sendHeader(client, 200, "application/octet-stream", FIGURE_SIZE, disp);
        client.write(buffer, FIGURE_SIZE);
    } else {
        _sendError(client, 404, "Slot vazio");
    }
}

// ── Private Helpers ──────────────────────────────────────────

void PortalAPI::_sendHeader(WiFiClient& client, int code, const String& contentType, size_t bodyLength, const String& extraHeaders) {
    client.printf("HTTP/1.1 %d OK\r\n", code);
    client.printf("Content-Type: %s\r\n", contentType.c_str());
    client.printf("Content-Length: %u\r\n", (unsigned int)bodyLength);
    client.print("Connection: close\r\n");
    if (extraHeaders.length() > 0) client.print(extraHeaders);
    client.print("\r\n");
}

void PortalAPI::_sendError(WiFiClient& client, int code, const String& message) {
    _sendHeader(client, code, "text/plain; charset=utf-8", message.length());
    client.print(message);
}

void PortalAPI::_handleGetPortalType(WiFiClient& client) {
    int currentType = 2; // default
    File f = LittleFS.open("/portal_type.txt", "r");
    if (f) {
        String ts = f.readString();
        currentType = ts.toInt();
        f.close();
    }
    String json = "{\"type\":" + String(currentType) + "}";
    _sendHeader(client, 200, "application/json", json.length());
    client.print(json);
}

void PortalAPI::_handleSetPortalType(WiFiClient& client, const String& uri) {
    int pos = uri.indexOf("type=");
    if (pos == -1) {
        _sendError(client, 400, "Missing type parameter");
        return;
    }
    
    int type = uri.substring(pos + 5).toInt();
    if (type != 0 && type != 2) {
        _sendError(client, 400, "Invalid type (must be 0 or 2)");
        return;
    }

    File f = LittleFS.open("/portal_type.txt", "w");
    if (f) {
        f.print(type);
        f.close();
        
        String msg = "Portal type saved! Rebooting...";
        _sendHeader(client, 200, "text/plain", msg.length());
        client.print(msg);
        
        delay(500);
        ESP.restart();
    } else {
        _sendError(client, 500, "Failed to save configuration");
    }
}

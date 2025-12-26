/*
MULTICORE-SAFE WEBSERVER.H
Alle CPU-Zugriffe mit Mutex geschützt
Display-Buffer ist thread-safe
*/

//benötigt für webserver
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>

// Forward declarations
class PDP1;
class RIMLoader;

// Externe Referenzen (werden in pdp1_simulator_multicore.ino definiert)
extern PDP1 cpu;
extern SemaphoreHandle_t cpuMutex;  // MULTICORE: Mutex für CPU-Zugriffe

// WiFi Credentials
const char* ssid = "Your SSID here";
const char* password = "Your Passphrase here";

// Webserver & WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ============================================================================
// MULTICORE: Display Buffer mit Mutex-Schutz
// ============================================================================
static std::vector<uint32_t> displayBuffer;
static SemaphoreHandle_t displayMutex = NULL;

// Globaler Buffer für Punch 
static std::vector<uint8_t> punchBuffer;
static SemaphoreHandle_t punchMutex = NULL;

// NEU: Web-Tape Mount System
static bool webTapeMounted = false;
static std::vector<uint8_t> webTapeData;
static size_t webTapePosition = 0;
static SemaphoreHandle_t webTapeMutex = NULL;

// Status
bool displayConnected = false;

void setup_wifi(){
    // WiFi verbinden
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

// Base64 Dekodierung
uint8_t* base64_decode(const char* input, size_t* output_length) {
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t input_length = strlen(input);
    if (input_length % 4 != 0) {
        Serial.println("Base64: invalid Length!");
        return nullptr;
    }
    
    *output_length = input_length / 4 * 3;
    if (input[input_length - 1] == '=') (*output_length)--;
    if (input[input_length - 2] == '=') (*output_length)--;
    
    uint8_t* output = new uint8_t[*output_length];
    
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t a, b, c, d;
        
        if (input[i] == '=') {
            a = 0; i++;
        } else {
            const char* pos = strchr(base64_chars, input[i++]);
            if (pos == nullptr) {
                Serial.printf("Base64: invalid Char on Position %d\n", i-1);
                delete[] output;
                return nullptr;
            }
            a = pos - base64_chars;
        }
        
        if (input[i] == '=') {
            b = 0; i++;
        } else {
            const char* pos = strchr(base64_chars, input[i++]);
            if (pos == nullptr) {
                delete[] output;
                return nullptr;
            }
            b = pos - base64_chars;
        }
        
        if (input[i] == '=') {
            c = 0; i++;
        } else {
            const char* pos = strchr(base64_chars, input[i++]);
            if (pos == nullptr) {
                delete[] output;
                return nullptr;
            }
            c = pos - base64_chars;
        }
        
        if (input[i] == '=') {
            d = 0; i++;
        } else {
            const char* pos = strchr(base64_chars, input[i++]);
            if (pos == nullptr) {
                delete[] output;
                return nullptr;
            }
            d = pos - base64_chars;
        }
        
        uint32_t triple = (a << 18) + (b << 12) + (c << 6) + d;
        
        if (j < *output_length) output[j++] = (triple >> 16) & 0xFF;
        if (j < *output_length) output[j++] = (triple >> 8) & 0xFF;
        if (j < *output_length) output[j++] = triple & 0xFF;
    }
    
    return output;
}



// ========================================
// Status Messages
// ========================================

void sendMessage(const char* text) {
    if (!ws.count()) return;
    
    StaticJsonDocument<256> doc;
    doc["type"] = "message";
    doc["text"] = text;
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

std::vector<uint8_t> getWebTapeData() {
    std::vector<uint8_t> copy;
    if (webTapeMutex && xSemaphoreTake(webTapeMutex, 100) == pdTRUE) {
        copy = webTapeData;
        xSemaphoreGive(webTapeMutex);
    }
    return copy;
}

// ========================================
// Tape Position Updates (für Animation)
// ========================================
void sendReaderPosition(size_t position) {
    static unsigned long lastSend = 0;
    if (millis() - lastSend < 50) return;  // Rate-limit: alle 50ms (20 FPS)
    
    if (!ws.count()) return;  // Keine Clients connected
    
    String json = "{\"type\":\"reader_position\",\"position\":" + String(position) + "}";
    ws.textAll(json);
    lastSend = millis();
}

// ========================================
// WebSocket Event Handler
// ========================================

// MULTICORE-SAFE: Alle CPU-Zugriffe mit Mutex
void handleMountReader(JsonDocument& doc) {
    const char* base64Data = doc["data"];
    size_t dataLen;
    
    // Base64 dekodieren
    uint8_t* rimData = base64_decode(base64Data, &dataLen);
    
    if (rimData == nullptr) {
        sendMessage("ERROR: Invalid RIM data!");
        return;
    }
    
    // Als virtuelles Tape mounten (NICHT laden!)
    if (webTapeMutex && xSemaphoreTake(webTapeMutex, 100) == pdTRUE) {
        webTapeData.clear();
        webTapeData.assign(rimData, rimData + dataLen);
        webTapePosition = 0;
        webTapeMounted = true;
        xSemaphoreGive(webTapeMutex);
        
        Serial.printf("[WEBSERVER] Tape mounted: %d bytes\n", dataLen);
        sendMessage("Paper Tape mounted! Use READ IN switch to load.");
        
        // Tape im Browser anzeigen mit pos daten
        String json = "{\"type\":\"reader_mounted\",\"position\":0,\"data\":\"";
        json += base64Data;
        json += "\"}";
        ws.textAll(json);
    }
    
    delete[] rimData;
}

// ============================================================================
// handleUnmountReader - NEU
// ============================================================================

void handleUnmountReader() {
    if (webTapeMutex && xSemaphoreTake(webTapeMutex, 100) == pdTRUE) {
        webTapeMounted = false;
        webTapeData.clear();
        webTapePosition = 0;
        xSemaphoreGive(webTapeMutex);
        
        ws.textAll("{\"type\":\"reader_unmounted\"}");
        sendMessage("Paper Tape unmounted");
        Serial.println("[WEBSERVER] Tape unmounted");
    }
}


// ============================================================================
// isWebTapeMounted - Check-Funktion
// ============================================================================

bool isWebTapeMounted() {
    bool mounted = false;
    if (webTapeMutex && xSemaphoreTake(webTapeMutex, 1) == pdTRUE) {
        mounted = webTapeMounted;
        xSemaphoreGive(webTapeMutex);
    }
    return mounted;
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
    
    switch(type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WEBSERVER] WebSocket client #%u connected from %s\n", 
                         client->id(), client->remoteIP().toString().c_str());
            break;
            
        case WS_EVT_DISCONNECT:
            Serial.printf("[WEBSERVER] WebSocket client #%u disconnected\n", client->id());
            if (displayConnected) {
                displayConnected = false;
            }
            break;
            
        case WS_EVT_DATA:
            {
                AwsFrameInfo *info = (AwsFrameInfo*)arg;
                
                if (info->final && info->index == 0 && info->len == len && 
                    info->opcode == WS_TEXT) {
                    
                    data[len] = 0;  // Null-terminieren
                    
                    // JSON parsen
                    StaticJsonDocument<4096> doc;
                    DeserializationError error = deserializeJson(doc, (char*)data);
                    
                    if (error) {
                        Serial.println("[WEBSERVER] JSON parse error");
                        return;
                    }
                    
                    const char* msgType = doc["type"];
                    
                    if (strcmp(msgType, "connect_dpy") == 0) {
                        displayConnected = true;
                        ws.textAll("{\"type\":\"dpy_connected\"}");
                        Serial.println("[WEBSERVER] Display connected");
                        
                    } else if (strcmp(msgType, "disconnect_dpy") == 0) {
                        displayConnected = false;
                        ws.textAll("{\"type\":\"dpy_disconnected\"}");
                        Serial.println("[WEBSERVER] Display disconnected");
                        
                    } else if (strcmp(msgType, "mount_reader") == 0) {
                        handleMountReader(doc);
                        
                    } else if (strcmp(msgType, "unmount_reader") == 0) {
                        Serial.println("[WEBSERVER] Unmount paper tape reader");
                        handleUnmountReader();
                        
                    } else if (strcmp(msgType, "key") == 0) {
                        uint8_t keyCode = doc["value"];
                        Serial.printf("[WEBSERVER] Key pressed: 0x%02X (%c)\n", keyCode, 
                                     (keyCode >= 32 && keyCode < 127) ? keyCode : '?');
                        // TODO: An PDP-1 Keyboard Interface weiterleiten
                    }
                }
            }
            break;
            
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

// ========================================
// Display Output Funktionen (MULTICORE-SAFE)
// ========================================

// WIRD VON CPU-TASK (CORE 1) AUFGERUFEN
void handleDisplayOutput(int16_t x, int16_t y, uint8_t intensity) {
    // x, y: -511 bis +511
    // intensity: 0-7
 
    // Zu unsigned konvertieren (0-1023)
    uint16_t x_u = (x + 512) & 0x3FF;
    uint16_t y_u = (y + 512) & 0x3FF;
    
    // Packen
    uint32_t point = (intensity << 20) | (y_u << 10) | x_u;
    
    // MULTICORE: Buffer-Zugriff mit Mutex schützen
    if (displayMutex != NULL) {
        if (xSemaphoreTake(displayMutex, 1) == pdTRUE) {
            displayBuffer.push_back(point);
            xSemaphoreGive(displayMutex);
        }
        // Wenn Mutex busy: Point verwerfen (besser als CPU zu blockieren)
    }
}

// WIRD VON MAIN LOOP (CORE 0) AUFGERUFEN
void sendDisplayPointsBatch() {
    // MULTICORE: Buffer kopieren und dann leeren
    std::vector<uint32_t> localBuffer;
    
    if (displayMutex != NULL) {
        if (xSemaphoreTake(displayMutex, 10) == pdTRUE) {
            localBuffer = displayBuffer;  // Kopie erstellen
            displayBuffer.clear();         // Original leeren
            xSemaphoreGive(displayMutex);
        } else {
            // Mutex timeout - skip this frame
            return;
        }
    } else {
        // Fallback ohne Mutex (sollte nicht passieren)
        localBuffer = displayBuffer;
        displayBuffer.clear();
    }
    
    if (localBuffer.empty()) return;
    
    // JSON erstellen (ohne Mutex - arbeitet mit Kopie)
    String json = "{\"type\":\"points\",\"points\":[";
    
    for (size_t i = 0; i < localBuffer.size(); i++) {
        if (i > 0) json += ",";
        json += String(localBuffer[i]);
    }
    
    json += "]}";
    ws.textAll(json);
}

void testDisplay(){
    for(int i = 0; i < 16; i++){
        handleDisplayOutput(-150, 250, 7);
        handleDisplayOutput(-100, 200, 7);
        handleDisplayOutput(200, -300, 7);
        handleDisplayOutput(150, -250, 7);
        handleDisplayOutput(100, -200, 7);
        delay(50);
    }
}

// ========================================
// Paper Tape Punch Output
// ========================================

// void sendPunchData(uint8_t byte) {
//     if (!ws.count()) return;
    
//     String json = "{\"type\":\"punch_data\",\"value\":" + String(byte) + "}";
//     ws.textAll(json);
// }

// void sendPunchData(uint8_t byte) {
//     static unsigned long lastPunch = 0;
//     static const unsigned long PUNCH_DELAY = 10;  // 10ms = 100 cps (original speed)
    
//     // Warte bis genug Zeit vergangen ist (simuliert mechanischen Puncher)
//     while (millis() - lastPunch < PUNCH_DELAY) {
//         delay(1);
//     }
    
//     if (!ws.count()) return;
    
//     String json = "{\"type\":\"punch_data\",\"value\":" + String(byte) + "}";
//     ws.textAll(json);
    
//     lastPunch = millis();
// }

void sendPunchData(uint8_t byte) {
    if (punchMutex != NULL) {
        if (xSemaphoreTake(punchMutex, 1) == pdTRUE) {
            punchBuffer.push_back(byte);
            xSemaphoreGive(punchMutex);
        }
    }
}

void sendPunchDataBatch() {
    std::vector<uint8_t> localBuffer;
    
    if (punchMutex != NULL) {
        if (xSemaphoreTake(punchMutex, 10) == pdTRUE) {
            localBuffer = punchBuffer;
            punchBuffer.clear();
            xSemaphoreGive(punchMutex);
        } else {
            return;
        }
    }
    
    if (localBuffer.empty() || !ws.count()) return;
    
    String json = "{\"type\":\"punch_batch\",\"data\":[";
    for (size_t i = 0; i < localBuffer.size(); i++) {
        if (i > 0) json += ",";
        json += String(localBuffer[i]);
    }
    json += "]}";
    ws.textAll(json);
}

// ========================================
// Typewriter Output
// ========================================

void sendTypewriterChar(uint8_t ch) {
    if (!ws.count()) return;
    
    String json = "{\"type\":\"char\",\"value\":" + String(ch) + "}";
    ws.textAll(json);
}

void sendTypewriterString(const char* str) {
    while (*str) {
        sendTypewriterChar(*str++);
    }
}

void setTypewriterRed() {
    sendTypewriterChar(0x1B);
    sendTypewriterChar('[');
    sendTypewriterChar('3');
    sendTypewriterChar('1');
    sendTypewriterChar('m');
}

void setTypewriterBlack() {
    sendTypewriterChar(0x1B);
    sendTypewriterChar('[');
    sendTypewriterChar('3');
    sendTypewriterChar('9');
    sendTypewriterChar(';');
    sendTypewriterChar('4');
    sendTypewriterChar('9');
    sendTypewriterChar('m');
}

// ========================================
// Setup
// ========================================

void setupWebserver() {
    // MULTICORE: Display-Mutex erstellen
    displayMutex = xSemaphoreCreateMutex();
    if (displayMutex == NULL) {
        Serial.println("[WEBSERVER] Error: Display-Mutex coul'd not create!");
    } else {
        Serial.println("[WEBSERVER] Display-Mutex created");
    }

    // PUNCH-MUTEX (NEU!)
    punchMutex = xSemaphoreCreateMutex();
    if (punchMutex == NULL) {
        Serial.println("[WEBSERVER] Error: Punch-Mutex!");
    } else {
        Serial.println("[WEBSERVER] Punch-Mutex created");
    }    

    // Web-Tape-Mutex erstellen
    webTapeMutex = xSemaphoreCreateMutex();
    if (webTapeMutex == NULL) {
        Serial.println("[WEBSERVER] Error: Web-Tape-Mutex!");
    } else {
        Serial.println("[WEBSERVER] Web-Tape-Mutex created");
    }

    // WebSocket Handler
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    
    // Statische Dateien von SD-Karte servieren
    server.serveStatic("/", SD, "/web/").setDefaultFile("index.html");
    
    // 404 Handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });
    
    // Server starten
    server.begin();
    Serial.println("[WEBSERVER] HTTP server started");
}

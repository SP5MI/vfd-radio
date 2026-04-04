// =============================================================================
// web_task.cpp — interfejs webowy
// =============================================================================

#include "Arduino.h"
#include "WiFi.h"
#include "LittleFS.h"
#include "ESPAsyncWebServer.h"
#include "ArduinoJson.h"
#include "radio_state.h"
#include "audio_task.h"
#include "radio_task.h"

// =============================================================================
// SERWER
// =============================================================================

static AsyncWebServer server(80);

// =============================================================================
// POMOCNICZE — buduj JSON ze stanem radia
// =============================================================================

static String buildStatusJson() {
    JsonDocument doc;

    doc["mode"]          = (int)gState.mode;
    doc["frequency"]     = gState.frequency;
    doc["volume"]        = gState.volume;
    doc["muted"]         = gState.muted;
    doc["stationName"]   = (const char*)gState.stationName;
    doc["rdsText"]       = (const char*)gState.rdsText;
    doc["streamUrl"]     = (const char*)gState.streamUrl;
    doc["wifiConnected"] = gState.wifiConnected;
    doc["ip"]            = WiFi.localIP().toString();
    doc["rssi"]          = WiFi.RSSI();

    String out;
    serializeJson(doc, out);
    return out;
}

// =============================================================================
// INICJALIZACJA ROUTÓW
// =============================================================================

static void setupRoutes() {

    // --- Strona główna z LittleFS ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/index.html")) {
            req->send(LittleFS, "/index.html", "text/html");
        } else {
            req->send(200, "text/html",
                "<h2>Hybrid Radio</h2>"
                "<p>Brak pliku /index.html na LittleFS.</p>"
                "<p>Wgraj pliki przez: <code>pio run -e esp32s3 --target uploadfs</code></p>"
            );
        }
    });

    // --- Pliki statyczne (CSS, JS, ikony) ---
    server.serveStatic("/", LittleFS, "/");

    // --- Status radia ---
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", buildStatusJson());
    });

    // --- Pobierz stations.json ---
    server.on("/api/stations", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (LittleFS.exists("/stations.json")) {
            req->send(LittleFS, "/stations.json", "application/json");
        } else {
            req->send(404, "application/json", "{\"error\":\"Brak pliku stations.json\"}");
        }
    });

    // --- Upload stations.json ---
    server.on("/api/stations", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            req->send(200, "application/json", "{\"ok\":true,\"msg\":\"Plik zapisany. Uruchom ponownie radio.\"}");
        },
        [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            static File uploadFile;
            if (index == 0) {
                Serial.printf("[Web] Upload: %s\n", filename.c_str());
                uploadFile = LittleFS.open("/stations.json", "w");
            }
            if (uploadFile) {
                uploadFile.write(data, len);
            }
            if (final && uploadFile) {
                uploadFile.close();
                Serial.printf("[Web] Upload zakończony: %u bajtów\n", index + len);
            }
        }
    );

    // --- Przełącz tryb ---
    // Body: {"mode": 0}  (0=internet, 1=FM, 2=AM, 3=SW, 4=SSB_LSB, 5=SSB_USB)
    server.on("/api/mode", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"Błędny JSON\"}");
                return;
            }
            int mode = doc["mode"] | -1;
            if (mode < 0 || mode > 5) {
                req->send(400, "application/json", "{\"error\":\"Nieprawidłowy tryb\"}");
                return;
            }
            gState.mode = (RadioMode_t)mode;
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // --- Głośność ---
    // Body: {"volume": 50}  (0–100)
    server.on("/api/volume", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"Błędny JSON\"}");
                return;
            }
            uint8_t vol = doc["volume"] | 255;
            if (vol > 100) {
                req->send(400, "application/json", "{\"error\":\"Głośność 0–100\"}");
                return;
            }
            // Wyślij do odpowiedniej kolejki zależnie od trybu
            if (gState.mode == MODE_INTERNET_RADIO) {
                AudioCmd_t cmd = { AUDIO_CMD_VOLUME, 0, vol };
                xQueueSend(qAudioCmds, &cmd, 0);
            } else {
                RadioCmd_t cmd = { RADIO_CMD_SET_VOLUME, 0, 0, vol };
                xQueueSend(qRadioCmds, &cmd, 0);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // --- Następna stacja (tylko tryb internet) ---
    server.on("/api/next", HTTP_POST, [](AsyncWebServerRequest *req) {
        AudioCmd_t cmd = { AUDIO_CMD_NEXT, 0, 0 };
        xQueueSend(qAudioCmds, &cmd, 0);
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // --- Poprzednia stacja (tylko tryb internet) ---
    server.on("/api/prev", HTTP_POST, [](AsyncWebServerRequest *req) {
        AudioCmd_t cmd = { AUDIO_CMD_PREV, 0, 0 };
        xQueueSend(qAudioCmds, &cmd, 0);
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // --- Ustaw częstotliwość (FM/AM/SW) ---
    // Body: {"frequency": 100000}  (kHz, np. 100000 = 100.0 MHz)
    server.on("/api/frequency", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"Błędny JSON\"}");
                return;
            }
            uint32_t freq = doc["frequency"] | 0;
            if (freq == 0) {
                req->send(400, "application/json", "{\"error\":\"Brak częstotliwości\"}");
                return;
            }
            RadioCmd_t cmd = { RADIO_CMD_SET_FREQ, freq, 0, 0 };
            xQueueSend(qRadioCmds, &cmd, 0);
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // --- 404 ---
    server.onNotFound([](AsyncWebServerRequest *req) {
        req->send(404, "application/json", "{\"error\":\"Nie znaleziono\"}");
    });
}

// =============================================================================
// TASK
// =============================================================================

void taskWeb(void *pvParameters) {
    Serial.println("[WebTask] started");

    // Czekaj na WiFi
    while (!gState.wifiConnected) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Inicjalizuj LittleFS (jeśli jeszcze nie zamontowany przez taskAudio)
    if (!LittleFS.begin(false)) {
        Serial.println("[Web] LittleFS nie zamontowany — próba montowania...");
        LittleFS.begin(true);
    }

    setupRoutes();
    server.begin();

    Serial.printf("[Web] Serwer uruchomiony: http://%s\n",
        WiFi.localIP().toString().c_str());

    // AsyncWebServer działa w tle — task tylko monitoruje
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
// =============================================================================
// wifi_task.cpp — obsługa WiFi
// =============================================================================

#include "Arduino.h"
#include "WiFi.h"
#include "wifi_config.h"
#include "radio_state.h"

// Interwał próby reconnectu w ms
#define WIFI_RECONNECT_INTERVAL_MS  10000
#define WIFI_CONNECT_TIMEOUT_MS     15000

// =============================================================================

static void wifi_connect() {
    Serial.printf("[WiFi] Łączenie z: %s\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("[WiFi] Timeout — nie udało się połączyć.");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
    }

    Serial.printf("\n[WiFi] Połączono. IP: %s  RSSI: %d dBm\n",
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI());

    gState.wifiConnected = true;
}

// =============================================================================

void taskWiFi(void *pvParameters) {
    Serial.println("[WiFiTask] started");

    // Pierwsze połączenie
    wifi_connect();

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Utracono połączenie. Reconnect...");
            gState.wifiConnected = false;
            WiFi.disconnect();
            vTaskDelay(pdMS_TO_TICKS(1000));
            wifi_connect();
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL_MS));
    }
}
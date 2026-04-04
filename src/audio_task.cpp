// =============================================================================
// audio_task.cpp — radio internetowe (ESP32-audioI2S)
// =============================================================================

#include "Arduino.h"
#include "Audio.h"
#include "LittleFS.h"
#include "ArduinoJson.h"
#include "radio_state.h"
#include "audio_task.h"

// =============================================================================
// PINY I2S
// =============================================================================

#define I2S_BCLK    15
#define I2S_LRCLK   16
#define I2S_DATA    17

// =============================================================================
// STACJE RADIOWE
// =============================================================================

#define MAX_STATIONS    32
#define STATIONS_FILE   "/stations.json"

typedef struct {
    char name[32];
    char url[256];
} Station_t;

static Station_t  stations[MAX_STATIONS];
static int        stationCount  = 0;
static int        currentStation = 0;

// =============================================================================
// KOLEJKA KOMEND AUDIO
// =============================================================================

QueueHandle_t qAudioCmds = NULL;

// =============================================================================
// OBIEKT AUDIO
// =============================================================================

static Audio audio;

// =============================================================================
// CALLBACKI BIBLIOTEKI ESP32-audioI2S
// =============================================================================

void audio_info(Audio::msg_t m) {
    switch (m.e) {
        case Audio::evt_info:
            Serial.printf("[Audio] %s\n", m.msg);
            break;
        case Audio::evt_name:
            // Nazwa stacji z ICY metadata
            strncpy((char*)gState.stationName, m.msg, sizeof(gState.stationName) - 1);
            Serial.printf("[Audio] Stacja: %s\n", m.msg);
            break;
        case Audio::evt_streamtitle:
            // Tytuł utworu z ICY metadata
            strncpy((char*)gState.rdsText, m.msg, sizeof(gState.rdsText) - 1);
            Serial.printf("[Audio] Tytuł: %s\n", m.msg);
            break;
        case Audio::evt_eof:
            Serial.println("[Audio] Koniec strumienia.");
            break;
        default:
            break;
    }
}

// =============================================================================
// WCZYTAJ STACJE Z PLIKU JSON
// =============================================================================

static bool loadStations() {
    if (!LittleFS.begin(true)) {
        Serial.println("[Audio] LittleFS mount failed!");
        return false;
    }

    if (!LittleFS.exists(STATIONS_FILE)) {
        Serial.printf("[Audio] Brak pliku %s — tworzę domyślny.\n", STATIONS_FILE);

        // Utwórz domyślny plik ze stacjami
        File f = LittleFS.open(STATIONS_FILE, "w");
        if (!f) {
            Serial.println("[Audio] Błąd tworzenia pliku stacji!");
            return false;
        }
        f.print(
            "[\n"
            "  {\"name\": \"RMF FM\",      \"url\": \"http://195.150.20.242:8000/rmf_fm\"},\n"
            "  {\"name\": \"Radio ZET\",   \"url\": \"http://n-22-13.dcs.redcdn.pl/sc/o2/Eurozet/live/audio.livx\"},\n"
            "  {\"name\": \"Trójka\",      \"url\": \"http://stream3.polskieradio.pl:8904/\"},\n"
            "  {\"name\": \"PR1\",         \"url\": \"http://stream3.polskieradio.pl:8900/\"},\n"
            "  {\"name\": \"PR2\",         \"url\": \"http://stream3.polskieradio.pl:8902/\"}\n"
            "]\n"
        );
        f.close();
        Serial.println("[Audio] Zapisano domyślne stacje.");
    }

    // Wczytaj plik
    File f = LittleFS.open(STATIONS_FILE, "r");
    if (!f) {
        Serial.println("[Audio] Błąd otwarcia pliku stacji!");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[Audio] JSON error: %s\n", err.c_str());
        return false;
    }

    stationCount = 0;
    for (JsonObject s : doc.as<JsonArray>()) {
        if (stationCount >= MAX_STATIONS) break;
        strncpy(stations[stationCount].name, s["name"] | "???",  sizeof(stations[0].name)  - 1);
        strncpy(stations[stationCount].url,  s["url"]  | "",     sizeof(stations[0].url)   - 1);
        stationCount++;
    }

    Serial.printf("[Audio] Wczytano %d stacji.\n", stationCount);
    return stationCount > 0;
}

// =============================================================================
// POŁĄCZ Z AKTUALNĄ STACJĄ
// =============================================================================

static void connectCurrentStation() {
    if (stationCount == 0) return;

    Station_t &s = stations[currentStation];
    Serial.printf("[Audio] Łączę: [%d/%d] %s\n", currentStation + 1, stationCount, s.name);

    strncpy((char*)gState.streamUrl,   s.url,  sizeof(gState.streamUrl)  - 1);
    strncpy((char*)gState.stationName, s.name, sizeof(gState.stationName) - 1);
    strncpy((char*)gState.rdsText,     "",     sizeof(gState.rdsText)    - 1);

    audio.connecttohost(s.url);
}

// =============================================================================
// TASK
// =============================================================================

void taskAudio(void *pvParameters) {
    Serial.println("[AudioTask] started");

    // Inicjalizacja I2S
    audio.setPinout(I2S_BCLK, I2S_LRCLK, I2S_DATA);
    audio.setVolume(gState.volume / 3);  // skala 0–21 w bibliotece
    Audio::audio_info_callback = audio_info;

    // Czekaj na WiFi
    Serial.println("[AudioTask] Czekam na WiFi...");
    while (!gState.wifiConnected) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.println("[AudioTask] WiFi OK — ładuję stacje.");

    // Wczytaj stacje i połącz z pierwszą
    if (loadStations()) {
        connectCurrentStation();
    }

    AudioCmd_t cmd;

    for (;;) {
        // Obsługa komend z kolejki UI
        if (xQueueReceive(qAudioCmds, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case AUDIO_CMD_NEXT:
                    currentStation = (currentStation + 1) % stationCount;
                    connectCurrentStation();
                    break;

                case AUDIO_CMD_PREV:
                    currentStation = (currentStation - 1 + stationCount) % stationCount;
                    connectCurrentStation();
                    break;

                case AUDIO_CMD_SELECT:
                    if (cmd.stationIndex >= 0 && cmd.stationIndex < stationCount) {
                        currentStation = cmd.stationIndex;
                        connectCurrentStation();
                    }
                    break;

                case AUDIO_CMD_VOLUME:
                    gState.volume = cmd.value;
                    audio.setVolume(cmd.value / 3);
                    break;

                case AUDIO_CMD_STOP:
                    audio.stopSong();
                    break;

                case AUDIO_CMD_PLAY:
                    connectCurrentStation();
                    break;
            }
        }

        // Odtwarzanie — musi być wywoływane w pętli
        audio.loop();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
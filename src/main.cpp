// =============================================================================
// Hybrid Radio — main.cpp
// ESP32-S3 N16R8
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include "wifi_config.h"
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// =============================================================================
// PINY
// =============================================================================

// --- I2S (wyjście audio → 74HC4053 → MAX98357 L+R) ---
#define PIN_I2S_BCLK        15
#define PIN_I2S_LRCLK       16
#define PIN_I2S_DATA        17

// --- 74HC4053 — selektor źródła I2S ---
// LOW  = ESP32 (radio internetowe)
// HIGH = SI4732 (FM/AM/SW/SSB)
#define PIN_I2S_SEL         18

// --- I2C (magistrala: SI4732 + Arduino VFD) ---
#define PIN_I2C_SDA         8
#define PIN_I2C_SCL         9

// --- SI4732 ---
#define PIN_SI4732_RESET    38

// --- Enkoder obrotowy ---
#define PIN_ENC_CLK         39
#define PIN_ENC_DT          40
#define PIN_ENC_SW          41

// --- Touch (przyciski pojemnościowe) ---
#define PIN_TOUCH_1         1   // tryb FM
#define PIN_TOUCH_2         2   // tryb AM/SW
#define PIN_TOUCH_3         4   // radio internetowe
#define PIN_TOUCH_4         5   // wyciszenie
#define PIN_TOUCH_5         6   // zapamiętaj stację
#define PIN_TOUCH_6         7   // lista stacji

// --- Adresy I2C ---
#define I2C_ADDR_SI4732     0x11    // gdy SENB → VCC (SI4732-A10)
#define I2C_ADDR_ARDUINO    0x27    // Arduino Pro Mini (slave VFD)

// =============================================================================
// TRYBY PRACY
// =============================================================================

typedef enum {
    MODE_INTERNET_RADIO = 0,
    MODE_FM,
    MODE_AM,
    MODE_SW,
    MODE_SSB_LSB,
    MODE_SSB_USB
} RadioMode_t;

// =============================================================================
// STAN GLOBALNY
// =============================================================================

typedef struct {
    RadioMode_t mode;           // aktualny tryb
    uint32_t    frequency;      // częstotliwość w kHz (FM: 87500 = 87.5 MHz)
    uint8_t     volume;         // 0–63 (skala SI4732) / 0–100 (audio I2S)
    bool        muted;          // wyciszenie
    char        stationName[9]; // RDS PS (8 znaków + null)
    char        rdsText[65];    // RDS RadioText (64 znaki + null)
    char        streamUrl[256]; // URL aktualnego streamu internetowego
    bool        wifiConnected;
} RadioState_t;

volatile RadioState_t gState;

void initState() {
    gState.mode          = MODE_INTERNET_RADIO;
    gState.frequency     = 100000;  // 100.0 MHz jako domyślne FM
    gState.volume        = 40;
    gState.muted         = false;
    gState.wifiConnected = false;
    strncpy((char*)gState.stationName, "--------", sizeof(gState.stationName));
    strncpy((char*)gState.rdsText,     "",         sizeof(gState.rdsText));
    strncpy((char*)gState.streamUrl,   "http://stream.example.com/radio", sizeof(gState.streamUrl));
}

// =============================================================================
// KOLEJKI I SEMAFORY FREERTOS
// =============================================================================

// Zdarzenia UI → logika (enkoder, touch)
typedef enum {
    UI_EVENT_ENC_CW = 0,    // enkoder w prawo
    UI_EVENT_ENC_CCW,        // enkoder w lewo
    UI_EVENT_ENC_PRESS,      // przycisk enkodera
    UI_EVENT_TOUCH_1,
    UI_EVENT_TOUCH_2,
    UI_EVENT_TOUCH_3,
    UI_EVENT_TOUCH_4,
    UI_EVENT_TOUCH_5,
    UI_EVENT_TOUCH_6,
} UIEvent_t;

// Komendy dla wyświetlacza VFD
typedef struct {
    char line1[17];     // 16 znaków + null
    char line2[17];     // dla ewentualnego 2-liniowego VFD
} DisplayCmd_t;

QueueHandle_t   qUIEvents;      // UI → UITask
QueueHandle_t   qDisplayCmds;   // UITask → DisplayTask
SemaphoreHandle_t xI2CMutex;    // ochrona magistrali I2C

// =============================================================================
// DEKLARACJE TASKÓW
// =============================================================================

void taskAudio(void *pvParameters);     // rdzeń 0 — strumieniowanie audio I2S
void taskRadioRF(void *pvParameters);   // rdzeń 0 — obsługa SI4732
void taskUI(void *pvParameters);        // rdzeń 1 — logika UI, przełączanie trybów
void taskDisplay(void *pvParameters);   // rdzeń 1 — wysyłanie danych do VFD
void taskWiFi(void *pvParameters);      // rdzeń 1 — Wi-Fi, lista stacji

// =============================================================================
// POMOCNICZE
// =============================================================================

// Przełącz źródło I2S
void setI2SSource(RadioMode_t mode) {
    if (mode == MODE_INTERNET_RADIO) {
        digitalWrite(PIN_I2S_SEL, LOW);     // ESP32 → MAX98357
    } else {
        digitalWrite(PIN_I2S_SEL, HIGH);    // SI4732 → MAX98357
    }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("[BOOT] Hybrid Radio starting...");
    initState();

    // --- I2C ---
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);  // 400 kHz Fast Mode
    Serial.println("[BOOT] I2C OK");

    // --- Piny cyfrowe ---
    pinMode(PIN_I2S_SEL,      OUTPUT);
    pinMode(PIN_SI4732_RESET, OUTPUT);
    pinMode(PIN_ENC_CLK,      INPUT_PULLUP);
    pinMode(PIN_ENC_DT,       INPUT_PULLUP);
    pinMode(PIN_ENC_SW,       INPUT_PULLUP);

    // Domyślnie: źródło I2S = ESP32 (radio internetowe)
    setI2SSource(MODE_INTERNET_RADIO);

    // SI4732 reset — trzymaj LOW przez chwilę, potem HIGH
    digitalWrite(PIN_SI4732_RESET, LOW);
    delay(10);
    digitalWrite(PIN_SI4732_RESET, HIGH);
    delay(500);
    Serial.println("[BOOT] SI4732 reset OK");

    // --- Kolejki FreeRTOS ---
    qUIEvents    = xQueueCreate(16, sizeof(UIEvent_t));
    qDisplayCmds = xQueueCreate(8,  sizeof(DisplayCmd_t));
    xI2CMutex    = xSemaphoreCreateMutex();

    if (!qUIEvents || !qDisplayCmds || !xI2CMutex) {
        Serial.println("[ERROR] FreeRTOS queue/semaphore creation failed!");
        while(1) { delay(1000); }
    }
    Serial.println("[BOOT] FreeRTOS queues OK");

    // --- Uruchomienie tasków ---
    // Rdzeń 0: audio (wymaga stabilnego timingu)
    xTaskCreatePinnedToCore(taskAudio,   "AudioTask",   8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(taskRadioRF, "RadioTask",   4096, NULL, 4, NULL, 0);

    // Rdzeń 1: UI, wyświetlacz, Wi-Fi
    xTaskCreatePinnedToCore(taskUI,      "UITask",      4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(taskDisplay, "DisplayTask", 2048, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(taskWiFi,    "WiFiTask",    8192, NULL, 1, NULL, 1);

    Serial.println("[BOOT] All tasks started.");
}

// =============================================================================
// LOOP — pusty, wszystko w taskach FreeRTOS
// =============================================================================

void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}

// =============================================================================
// IMPLEMENTACJE TASKÓW — szkielety (wypełnimy w kolejnych modułach)
// =============================================================================

void taskAudio(void *pvParameters) {
    // TODO: inicjalizacja ESP32-audioI2S
    // TODO: połączenie z Wi-Fi i odtwarzanie streamu
    Serial.println("[AudioTask] started");
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void taskRadioRF(void *pvParameters) {
    // TODO: inicjalizacja SI4732 (PU2CLR)
    // TODO: obsługa RDS, SSB patch
    Serial.println("[RadioTask] started");
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void taskUI(void *pvParameters) {
    // TODO: obsługa enkodera (przerwania lub polling)
    // TODO: obsługa touch (touchRead())
    // TODO: logika przełączania trybów, zmiana częstotliwości
    Serial.println("[UITask] started");
    UIEvent_t event;
    for(;;) {
        if (xQueueReceive(qUIEvents, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            // obsługa zdarzeń — do implementacji
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void taskDisplay(void *pvParameters) {
    // TODO: odbieranie DisplayCmd z kolejki
    // TODO: wysyłanie przez I2C do Arduino Pro Mini
    Serial.println("[DisplayTask] started");
    DisplayCmd_t cmd;
    for(;;) {
        if (xQueueReceive(qDisplayCmds, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            // wyślij do Arduino — do implementacji
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void taskWiFi(void *pvParameters) {
    // TODO: połączenie z Wi-Fi (credentials z NVS lub hardcode)
    // TODO: lista stacji internetowych
    Serial.println("[WiFiTask] started");
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// =============================================================================
// ui_task.cpp — obsługa enkodera i przycisków touch
// =============================================================================

#include "Arduino.h"
#include "radio_state.h"
#include "audio_task.h"
#include "radio_task.h"

// =============================================================================
// PINY
// =============================================================================

#define PIN_ENC_CLK     39
#define PIN_ENC_DT      40
#define PIN_ENC_SW      41

#define PIN_TOUCH_1     1
#define PIN_TOUCH_2     2
#define PIN_TOUCH_3     4
#define PIN_TOUCH_4     5
#define PIN_TOUCH_5     6
#define PIN_TOUCH_6     7

// Próg wykrycia dotyku (im niższa wartość tym dotyk)
#define TOUCH_THRESHOLD     40

// =============================================================================
// ENKODER — prosta obsługa przez polling
// =============================================================================

static int  encLastCLK    = HIGH;
static bool encBtnPressed = false;

static void handleEncoder() {
    int clk = digitalRead(PIN_ENC_CLK);
    int dt  = digitalRead(PIN_ENC_DT);

    if (clk != encLastCLK && clk == LOW) {
        UIEvent_t evt = (dt != clk) ? UI_EVENT_ENC_CW : UI_EVENT_ENC_CCW;
        xQueueSend(qUIEvents, &evt, 0);
    }
    encLastCLK = clk;

    // Przycisk enkodera
    bool pressed = (digitalRead(PIN_ENC_SW) == LOW);
    if (pressed && !encBtnPressed) {
        UIEvent_t evt = UI_EVENT_ENC_PRESS;
        xQueueSend(qUIEvents, &evt, 0);
    }
    encBtnPressed = pressed;
}

// =============================================================================
// TOUCH — polling z debouncingiem
// =============================================================================

static uint32_t touchLastTime[6] = {0};
#define TOUCH_DEBOUNCE_MS 300

static void handleTouch() {
    const int pins[6]   = { PIN_TOUCH_1, PIN_TOUCH_2, PIN_TOUCH_3,
                             PIN_TOUCH_4, PIN_TOUCH_5, PIN_TOUCH_6 };
    const UIEvent_t evts[6] = {
        UI_EVENT_TOUCH_1, UI_EVENT_TOUCH_2, UI_EVENT_TOUCH_3,
        UI_EVENT_TOUCH_4, UI_EVENT_TOUCH_5, UI_EVENT_TOUCH_6
    };

    uint32_t now = millis();
    for (int i = 0; i < 6; i++) {
        if (touchRead(pins[i]) < TOUCH_THRESHOLD) {
            if (now - touchLastTime[i] > TOUCH_DEBOUNCE_MS) {
                touchLastTime[i] = now;
                xQueueSend(qUIEvents, &evts[i], 0);
            }
        }
    }
}

// =============================================================================
// OBSŁUGA ZDARZEŃ UI
// =============================================================================

static void processEvent(UIEvent_t evt) {
    switch (evt) {

        // Enkoder w prawo — następna stacja / częstotliwość w górę
        case UI_EVENT_ENC_CW:
            if (gState.mode == MODE_INTERNET_RADIO) {
                AudioCmd_t cmd = { AUDIO_CMD_NEXT, 0, 0 };
                xQueueSend(qAudioCmds, &cmd, 0);
            } else {
                RadioCmd_t cmd = { RADIO_CMD_FREQ_UP, 0, 0, 0 };
                xQueueSend(qRadioCmds, &cmd, 0);
            }
            break;

        // Enkoder w lewo — poprzednia stacja / częstotliwość w dół
        case UI_EVENT_ENC_CCW:
            if (gState.mode == MODE_INTERNET_RADIO) {
                AudioCmd_t cmd = { AUDIO_CMD_PREV, 0, 0 };
                xQueueSend(qAudioCmds, &cmd, 0);
            } else {
                RadioCmd_t cmd = { RADIO_CMD_FREQ_DOWN, 0, 0, 0 };
                xQueueSend(qRadioCmds, &cmd, 0);
            }
            break;

        // Przycisk enkodera — wyciszenie
        case UI_EVENT_ENC_PRESS:
            gState.muted = !gState.muted;
            if (gState.mode == MODE_INTERNET_RADIO) {
                AudioCmd_t cmd = { gState.muted ? AUDIO_CMD_STOP : AUDIO_CMD_PLAY, 0, 0 };
                xQueueSend(qAudioCmds, &cmd, 0);
            }
            Serial.printf("[UI] Mute: %s\n", gState.muted ? "ON" : "OFF");
            break;

        // Touch 1 — tryb FM
        case UI_EVENT_TOUCH_1:
            gState.mode = MODE_FM;
            Serial.println("[UI] Tryb: FM");
            break;

        // Touch 2 — tryb AM/SW
        case UI_EVENT_TOUCH_2:
            gState.mode = MODE_AM;
            Serial.println("[UI] Tryb: AM");
            break;

        // Touch 3 — radio internetowe
        case UI_EVENT_TOUCH_3:
            gState.mode = MODE_INTERNET_RADIO;
            Serial.println("[UI] Tryb: Internet");
            break;

        // Touch 4 — wyciszenie
        case UI_EVENT_TOUCH_4:
            gState.muted = !gState.muted;
            Serial.printf("[UI] Mute: %s\n", gState.muted ? "ON" : "OFF");
            break;

        // Touch 5 — zapamiętaj stację (TODO)
        case UI_EVENT_TOUCH_5:
            Serial.println("[UI] Zapamiętaj stację — TODO");
            break;

        // Touch 6 — lista stacji (TODO)
        case UI_EVENT_TOUCH_6:
            Serial.println("[UI] Lista stacji — TODO");
            break;

        default:
            break;
    }
}

// =============================================================================
// TASK
// =============================================================================

void taskUI(void *pvParameters) {
    Serial.println("[UITask] started");

    UIEvent_t evt;
    for (;;) {
        // Polling enkodera i touch
        handleEncoder();
        handleTouch();

        // Obsługa zdarzeń z kolejki
        while (xQueueReceive(qUIEvents, &evt, 0) == pdTRUE) {
            processEvent(evt);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
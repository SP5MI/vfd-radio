// =============================================================================
// display_task.cpp — komunikacja z Arduino Pro Mini → VFD 16LF01UA4
// =============================================================================

#include "Arduino.h"
#include "Wire.h"
#include "radio_state.h"

#define I2C_ADDR_ARDUINO    0x27
#define DISPLAY_UPDATE_MS   250     // odświeżaj co 250ms

// =============================================================================
// WYŚLIJ KOMENDĘ DO ARDUINO
// =============================================================================

static void sendDisplay(const char *line1, const char *line2) {
    Wire.beginTransmission(I2C_ADDR_ARDUINO);
    Wire.write(0x01);               // komenda: ustaw tekst
    Wire.write((uint8_t*)line1, 16);
    Wire.write((uint8_t*)line2, 16);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        Serial.printf("[Display] I2C error: %d\n", err);
    }
}

// =============================================================================
// BUDUJ TEKST WYŚWIETLACZA NA PODSTAWIE STANU
// =============================================================================

static void buildDisplayText(char *line1, char *line2) {
    memset(line1, ' ', 16); line1[16] = '\0';
    memset(line2, ' ', 16); line2[16] = '\0';

    switch (gState.mode) {
        case MODE_INTERNET_RADIO:
            snprintf(line1, 17, "%-16s", gState.stationName);
            snprintf(line2, 17, "%-16s", gState.rdsText[0] ? gState.rdsText : "Internet Radio  ");
            break;

        case MODE_FM:
            snprintf(line1, 17, "FM %6.1f MHz   ", gState.frequency / 1000.0f);
            snprintf(line2, 17, "%-16s", gState.stationName[0] ? gState.stationName : "                ");
            break;

        case MODE_AM:
            snprintf(line1, 17, "AM %7u kHz  ", gState.frequency);
            snprintf(line2, 17, "%-16s", gState.rdsText[0] ? gState.rdsText : "                ");
            break;

        case MODE_SW:
            snprintf(line1, 17, "SW %6.3f MHz  ", gState.frequency / 1000.0f);
            snprintf(line2, 17, "%-16s", gState.stationName[0] ? gState.stationName : "                ");
            break;

        case MODE_SSB_LSB:
            snprintf(line1, 17, "LSB%6.3f MHz  ", gState.frequency / 1000.0f);
            snprintf(line2, 17, "%-16s", "SSB             ");
            break;

        case MODE_SSB_USB:
            snprintf(line1, 17, "USB%6.3f MHz  ", gState.frequency / 1000.0f);
            snprintf(line2, 17, "%-16s", "SSB             ");
            break;

        default:
            snprintf(line1, 17, "Hybrid Radio    ");
            snprintf(line2, 17, "                ");
            break;
    }

    // Wyciszenie
    if (gState.muted) {
        memcpy(line2 + 12, "MUTE", 4);
    }
}

// =============================================================================
// TASK
// =============================================================================

void taskDisplay(void *pvParameters) {
    Serial.println("[DisplayTask] started");

    char line1[17], line2[17];
    char prevLine1[17] = {0}, prevLine2[17] = {0};

    // Powitanie
    sendDisplay("Hybrid Radio    ", "  Starting...   ");
    vTaskDelay(pdMS_TO_TICKS(2000));

    for (;;) {
        // Odbierz komendę z kolejki jeśli jest
        DisplayCmd_t cmd;
        if (xQueueReceive(qDisplayCmds, &cmd, 0) == pdTRUE) {
            // Bezpośrednia komenda — wyślij natychmiast
            sendDisplay(cmd.line1, cmd.line2);
            strncpy(prevLine1, cmd.line1, 16);
            strncpy(prevLine2, cmd.line2, 16);
        } else {
            // Automatyczne odświeżanie ze stanu globalnego
            buildDisplayText(line1, line2);

            // Wyślij tylko jeśli coś się zmieniło
            if (memcmp(line1, prevLine1, 16) != 0 ||
                memcmp(line2, prevLine2, 16) != 0) {
                sendDisplay(line1, line2);
                memcpy(prevLine1, line1, 16);
                memcpy(prevLine2, line2, 16);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_MS));
    }
}
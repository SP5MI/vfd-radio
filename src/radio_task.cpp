// =============================================================================
// radio_task.cpp — obsługa SI4732 (FM/AM/SW/SSB)
// =============================================================================

#include "Arduino.h"
#include "radio_state.h"
#include "radio_task.h"

// Definicja kolejki komend SI4732
QueueHandle_t qRadioCmds = NULL;

// =============================================================================
// TASK — szkielet, wypełnimy w kolejnym kroku
// =============================================================================

void taskRadioRF(void *pvParameters) {
    Serial.println("[RadioTask] started");

    // TODO: inicjalizacja SI4732 (PU2CLR)
    // TODO: wgranie patcha SSB
    // TODO: obsługa RDS
    // TODO: obsługa kolejki qRadioCmds

    RadioCmd_t cmd;
    for (;;) {
        if (xQueueReceive(qRadioCmds, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            // obsługa komend — do implementacji
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
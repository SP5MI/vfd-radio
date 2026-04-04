#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// =============================================================================
// KOMENDY AUDIO
// =============================================================================

typedef enum {
    AUDIO_CMD_NEXT = 0,     // następna stacja
    AUDIO_CMD_PREV,         // poprzednia stacja
    AUDIO_CMD_SELECT,       // wybierz stację po indeksie
    AUDIO_CMD_VOLUME,       // zmień głośność (value: 0–100)
    AUDIO_CMD_STOP,         // zatrzymaj
    AUDIO_CMD_PLAY,         // wznów
} AudioCmdType_t;

typedef struct {
    AudioCmdType_t type;
    int            stationIndex;  // dla AUDIO_CMD_SELECT
    uint8_t        value;         // dla AUDIO_CMD_VOLUME
} AudioCmd_t;

// Kolejka komend audio — wypełniana przez taskUI
extern QueueHandle_t qAudioCmds;

// Deklaracja taska
void taskAudio(void *pvParameters);
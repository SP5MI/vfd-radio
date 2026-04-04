#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// =============================================================================
// KOMENDY DLA SI4732
// =============================================================================

typedef enum {
    RADIO_CMD_SET_MODE = 0,     // zmień tryb (FM/AM/SW/SSB)
    RADIO_CMD_SET_FREQ,         // ustaw częstotliwość
    RADIO_CMD_FREQ_UP,          // częstotliwość w górę o krok
    RADIO_CMD_FREQ_DOWN,        // częstotliwość w dół o krok
    RADIO_CMD_SET_VOLUME,       // głośność 0–63
    RADIO_CMD_SEEK_UP,          // szukaj stacji w górę
    RADIO_CMD_SEEK_DOWN,        // szukaj stacji w dół
} RadioCmdType_t;

typedef struct {
    RadioCmdType_t type;
    uint32_t       frequency;   // dla RADIO_CMD_SET_FREQ (kHz)
    uint8_t        mode;        // dla RADIO_CMD_SET_MODE (RadioMode_t)
    uint8_t        value;       // dla RADIO_CMD_SET_VOLUME
} RadioCmd_t;

// Kolejka komend SI4732 — wypełniana przez taskUI i taskWeb
extern QueueHandle_t qRadioCmds;

// Deklaracja taska
void taskRadioRF(void *pvParameters);
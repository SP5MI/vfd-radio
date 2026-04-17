#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// =============================================================================
// KOMENDY DLA SI4732
// =============================================================================

typedef enum {
    RADIO_CMD_SET_MODE = 0,     // ustaw tryb (RadioMode_t w polu mode)
    RADIO_CMD_NEXT_MODE,        // FM->AM->SW->LSB->USB->FM
    RADIO_CMD_SET_FREQ,         // ustaw częstotliwość (kHz lub 10kHz dla FM)
    RADIO_CMD_FREQ_UP,          // krok w górę (BFO dla SSB)
    RADIO_CMD_FREQ_DOWN,        // krok w dół  (BFO dla SSB)
    RADIO_CMD_SEEK_UP,          // szukaj stacji w górę (FM/AM)
    RADIO_CMD_SEEK_DOWN,        // szukaj stacji w dół  (FM/AM)
    RADIO_CMD_SET_VOLUME,       // głośność 0–63
} RadioCmdType_t;

typedef struct {
    RadioCmdType_t type;
    uint32_t       frequency;   // dla RADIO_CMD_SET_FREQ
    uint8_t        mode;        // dla RADIO_CMD_SET_MODE (RadioMode_t)
    uint8_t        value;       // dla RADIO_CMD_SET_VOLUME
} RadioCmd_t;

// Kolejka komend SI4732
extern QueueHandle_t qRadioCmds;

// Deklaracja taska
void taskRadioRF(void *pvParameters);

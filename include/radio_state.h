#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

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
    RadioMode_t mode;
    uint32_t    frequency;
    uint8_t     volume;
    bool        muted;
    char        stationName[9];
    char        rdsText[65];
    char        streamUrl[256];
    bool        wifiConnected;
} RadioState_t;

extern volatile RadioState_t gState;

// =============================================================================
// KOLEJKI I SEMAFORY
// =============================================================================

typedef enum {
    UI_EVENT_ENC_CW = 0,
    UI_EVENT_ENC_CCW,
    UI_EVENT_ENC_PRESS,
    UI_EVENT_TOUCH_1,
    UI_EVENT_TOUCH_2,
    UI_EVENT_TOUCH_3,
    UI_EVENT_TOUCH_4,
    UI_EVENT_TOUCH_5,
    UI_EVENT_TOUCH_6,
} UIEvent_t;

typedef struct {
    char line1[17];
    char line2[17];
} DisplayCmd_t;

extern QueueHandle_t    qUIEvents;
extern QueueHandle_t    qDisplayCmds;
extern SemaphoreHandle_t xI2CMutex;
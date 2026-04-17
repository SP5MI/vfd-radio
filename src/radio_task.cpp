// =============================================================================
// radio_task.cpp — obsługa SI4732-A10 (FM/AM/SW/LW/SSB)
// =============================================================================
//
// UWAGA: I2S z SI4732 NIE działa gdy wgrany jest patch SSB.
// Dlatego SI4732 zawsze używa wyjścia ANALOGOWEGO (LOUT/ROUT).
// Przełączanie między PCM5102A (internet) a SI4732 (RF) robi TS5A23157
// sterowany przez PIN_I2S_SEL (GPIO18).
//
// Kroki strojenia:
//   FM  = 100 kHz  (jednostki 10 kHz w SI4732, np. 10000 = 100.0 MHz)
//   AM  =   1 kHz
//   SW  =   1 kHz
//   SSB = 100 Hz   (BFO offset)
// =============================================================================

#include "Arduino.h"
#include "Wire.h"
#include "SI4735.h"
#include "patch_init.h"     // patch SSB — z repozytorium PU2CLR SI4735
#include "radio_state.h"
#include "radio_task.h"

// =============================================================================
// KOLEJKA KOMEND
// =============================================================================

QueueHandle_t qRadioCmds = NULL;

// =============================================================================
// PINY
// =============================================================================

#define PIN_SI4732_RESET    38
#define PIN_I2S_SEL         18      // LOW=PCM5102A(internet), HIGH=SI4732(RF)

// =============================================================================
// ZAKRESY CZĘSTOTLIWOŚCI
// =============================================================================

#define FM_FREQ_MIN     6400        // 64.0 MHz (jednostki 10 kHz)
#define FM_FREQ_MAX    10800        // 108.0 MHz
#define FM_FREQ_DEF    10000        // 100.0 MHz
#define FM_STEP           10        // 100 kHz = 10 jednostek

#define AM_FREQ_MIN      520        // kHz
#define AM_FREQ_MAX     1710        // kHz
#define AM_FREQ_DEF      999        // kHz
#define AM_STEP             1       // 1 kHz

#define SW_FREQ_MIN     2300        // kHz
#define SW_FREQ_MAX    26100        // kHz
#define SW_FREQ_DEF     7100        // kHz (40m)
#define SW_STEP             1       // 1 kHz

#define SSB_FREQ_MIN    1800        // kHz
#define SSB_FREQ_MAX   30000        // kHz
#define SSB_FREQ_DEF    7074        // kHz (FT8 40m)
#define SSB_BFO_STEP     100        // Hz

// =============================================================================
// DEFINICJE STANOW SI47xx
// =============================================================================

#define FM_BAND_TYPE 0
#define AM_BAND_TYPE 1
#define SW_BAND_TYPE 1

// =============================================================================
// STAN LOKALNY
// =============================================================================

static SI4735   si4735;
static bool     ssbPatchLoaded = false;
static int      currentBFO     = 0;
static uint8_t  currentSSBMode = LSB;

// =============================================================================
// POMOCNICZE
// =============================================================================

static uint32_t getFreqMin() {
    switch (gState.mode) {
        case MODE_FM:      return FM_FREQ_MIN;
        case MODE_AM:      return AM_FREQ_MIN;
        default:           return SW_FREQ_MIN;
    }
}

static uint32_t getFreqMax() {
    switch (gState.mode) {
        case MODE_FM:      return FM_FREQ_MAX;
        case MODE_AM:      return AM_FREQ_MAX;
        default:           return SSB_FREQ_MAX;
    }
}

static uint32_t getFreqStep() {
    switch (gState.mode) {
        case MODE_FM:      return FM_STEP;
        case MODE_AM:      return AM_STEP;
        default:           return SW_STEP;
    }
}

// =============================================================================
// INICJALIZACJA TRYBÓW
// =============================================================================

static void startFM() {
    si4735.setTuneFrequencyAntennaCapacitor(0);
    si4735.setup(PIN_SI4732_RESET, FM_BAND_TYPE);
    si4735.setFM(FM_FREQ_MIN, FM_FREQ_MAX, (uint16_t)gState.frequency, FM_STEP);
    si4735.setVolume(gState.volume);
    si4735.setSeekFmLimits(FM_FREQ_MIN, FM_FREQ_MAX);
    si4735.setSeekFmSpacing(10);
    si4735.setRdsConfig(1, 2, 2, 2, 2);
    Serial.printf("[RF] FM @ %.1f MHz\n", gState.frequency / 100.0f);
}

static void startAM() {
    si4735.setup(PIN_SI4732_RESET, AM_BAND_TYPE);
    si4735.setAM(AM_FREQ_MIN, AM_FREQ_MAX, (uint16_t)gState.frequency, AM_STEP);
    si4735.setVolume(gState.volume);
    Serial.printf("[RF] AM @ %u kHz\n", gState.frequency);
}

static void startSW() {
    si4735.setup(PIN_SI4732_RESET, SW_BAND_TYPE);
    si4735.setSW(SW_FREQ_MIN, SW_FREQ_MAX, (uint16_t)gState.frequency, SW_STEP);
    si4735.setVolume(gState.volume);
    Serial.printf("[RF] SW @ %u kHz\n", gState.frequency);
}

static void startSSB(uint8_t ssbMode) {
    if (!ssbPatchLoaded) {
        Serial.println("[RF] Wgrywam patch SSB...");
        si4735.setup(PIN_SI4732_RESET, AM_BAND_TYPE);
        delay(10);
        si4735.queryLibraryId();
        si4735.patchPowerUp();
        delay(50);
        const uint16_t patchSize = sizeof ssb_patch_content;
        si4735.downloadPatch(ssb_patch_content, patchSize);
        ssbPatchLoaded = true;
        Serial.printf("[RF] Patch SSB OK (%u bajtow)\n", patchSize);
    }

    currentSSBMode = ssbMode;
    currentBFO = 0;

    si4735.setSSB(SSB_FREQ_MIN, SSB_FREQ_MAX,
                  (uint16_t)gState.frequency, SW_STEP, ssbMode);
    si4735.setSSBBfo(currentBFO);
    si4735.setSSBAudioBandwidth(1);
    si4735.setSBBSidebandCutoffFilter(0);
    //si4735.setSSBAvcAmMaxGain(48);
    si4735.setVolume(gState.volume);

    Serial.printf("[RF] SSB %s @ %u kHz BFO=%d Hz\n",
        ssbMode == LSB ? "LSB" : "USB", gState.frequency, currentBFO);
}

// =============================================================================
// PRZEŁĄCZ TRYB
// =============================================================================

static void applyMode(RadioMode_t mode) {
    digitalWrite(PIN_I2S_SEL, HIGH);   // zawsze SI4732 dla trybu RF

    if (ssbPatchLoaded && mode != MODE_SSB_LSB && mode != MODE_SSB_USB)
        ssbPatchLoaded = false;

    switch (mode) {
        case MODE_FM:
            if (gState.frequency < FM_FREQ_MIN || gState.frequency > FM_FREQ_MAX)
                gState.frequency = FM_FREQ_DEF;
            startFM();
            break;
        case MODE_AM:
            if (gState.frequency < AM_FREQ_MIN || gState.frequency > AM_FREQ_MAX)
                gState.frequency = AM_FREQ_DEF;
            startAM();
            break;
        case MODE_SW:
            if (gState.frequency < SW_FREQ_MIN || gState.frequency > SW_FREQ_MAX)
                gState.frequency = SW_FREQ_DEF;
            startSW();
            break;
        case MODE_SSB_LSB:
            if (gState.frequency < SSB_FREQ_MIN || gState.frequency > SSB_FREQ_MAX)
                gState.frequency = SSB_FREQ_DEF;
            startSSB(LSB);
            break;
        case MODE_SSB_USB:
            if (gState.frequency < SSB_FREQ_MIN || gState.frequency > SSB_FREQ_MAX)
                gState.frequency = SSB_FREQ_DEF;
            startSSB(USB);
            break;
        default:
            break;
    }
}

// =============================================================================
// STROJENIE
// =============================================================================

static void tuneUp() {
    uint32_t step = getFreqStep();
    gState.frequency += step;
    if (gState.frequency > getFreqMax())
        gState.frequency = getFreqMin();
    si4735.setFrequency((uint16_t)gState.frequency);
    if (gState.mode == MODE_SSB_LSB || gState.mode == MODE_SSB_USB) {
        currentBFO = 0;
        si4735.setSSBBfo(0);
    }
    Serial.printf("[RF] freq -> %u\n", gState.frequency);
}

static void tuneDown() {
    uint32_t step = getFreqStep();
    if (gState.frequency <= getFreqMin() + step)
        gState.frequency = getFreqMax();
    else
        gState.frequency -= step;
    si4735.setFrequency((uint16_t)gState.frequency);
    if (gState.mode == MODE_SSB_LSB || gState.mode == MODE_SSB_USB) {
        currentBFO = 0;
        si4735.setSSBBfo(0);
    }
    Serial.printf("[RF] freq -> %u\n", gState.frequency);
}

static void bfoUp() {
    currentBFO += SSB_BFO_STEP;
    if (currentBFO > 1000) currentBFO = 1000;
    si4735.setSSBBfo(currentBFO);
    Serial.printf("[RF] BFO -> %d Hz\n", currentBFO);
}

static void bfoDown() {
    currentBFO -= SSB_BFO_STEP;
    if (currentBFO < -1000) currentBFO = -1000;
    si4735.setSSBBfo(currentBFO);
    Serial.printf("[RF] BFO -> %d Hz\n", currentBFO);
}

// =============================================================================
// SEKWENCJA TRYBÓW — FM → AM → SW → LSB → USB → FM
// =============================================================================

static RadioMode_t nextMode(RadioMode_t current) {
    switch (current) {
        case MODE_FM:       return MODE_AM;
        case MODE_AM:       return MODE_SW;
        case MODE_SW:       return MODE_SSB_LSB;
        case MODE_SSB_LSB:  return MODE_SSB_USB;
        case MODE_SSB_USB:  return MODE_FM;
        default:            return MODE_FM;
    }
}

// =============================================================================
// RDS
// =============================================================================

static uint32_t lastRdsCheck = 0;

static void processRDS() {
    if (gState.mode != MODE_FM) return;
    if (millis() - lastRdsCheck < 200) return;
    lastRdsCheck = millis();

    if (!si4735.getRdsReady()) return;
    si4735.getRdsStatus();
    if (!si4735.getRdsSystem()) return;

    // Nazwa stacji PS (8 znaków)
    char *ps = si4735.getRdsText0A();
    if (ps && strlen(ps) > 0 &&
        strncmp((char*)gState.stationName, ps, 8) != 0) {
        strncpy((char*)gState.stationName, ps, sizeof(gState.stationName) - 1);
        Serial.printf("[RDS] PS: %s\n", ps);
    }

    // RadioText RT (64 znaki)
    char *rt = si4735.getRdsText2A();
    if (rt && strlen(rt) > 0 &&
        strncmp((char*)gState.rdsText, rt, 64) != 0) {
        strncpy((char*)gState.rdsText, rt, sizeof(gState.rdsText) - 1);
        Serial.printf("[RDS] RT: %s\n", rt);
    }
}

// =============================================================================
// OBSŁUGA KOMEND
// =============================================================================

static void handleCmd(const RadioCmd_t &cmd) {
    switch (cmd.type) {

        case RADIO_CMD_SET_MODE: {
            RadioMode_t m = (RadioMode_t)cmd.mode;
            gState.mode = m;
            strncpy((char*)gState.stationName, "--------", sizeof(gState.stationName));
            strncpy((char*)gState.rdsText, "", sizeof(gState.rdsText));
            applyMode(m);
            break;
        }

        case RADIO_CMD_NEXT_MODE: {
            RadioMode_t m = nextMode(gState.mode);
            gState.mode = m;
            strncpy((char*)gState.stationName, "--------", sizeof(gState.stationName));
            strncpy((char*)gState.rdsText, "", sizeof(gState.rdsText));
            applyMode(m);
            break;
        }

        case RADIO_CMD_SET_FREQ:
            gState.frequency = cmd.frequency;
            si4735.setFrequency((uint16_t)gState.frequency);
            if (gState.mode == MODE_SSB_LSB || gState.mode == MODE_SSB_USB) {
                currentBFO = 0;
                si4735.setSSBBfo(0);
            }
            break;

        case RADIO_CMD_FREQ_UP:
            if (gState.mode == MODE_SSB_LSB || gState.mode == MODE_SSB_USB)
                bfoUp();
            else
                tuneUp();
            break;

        case RADIO_CMD_FREQ_DOWN:
            if (gState.mode == MODE_SSB_LSB || gState.mode == MODE_SSB_USB)
                bfoDown();
            else
                tuneDown();
            break;

        case RADIO_CMD_SEEK_UP:
            if (gState.mode == MODE_FM || gState.mode == MODE_AM)
                si4735.seekStationUp();
            break;

        case RADIO_CMD_SEEK_DOWN:
            if (gState.mode == MODE_FM || gState.mode == MODE_AM)
                si4735.seekStationDown();
            break;

        case RADIO_CMD_SET_VOLUME:
            gState.volume = cmd.value;
            si4735.setVolume(cmd.value > 63 ? 63 : cmd.value);
            break;
    }
}

// =============================================================================
// TASK
// =============================================================================

void taskRadioRF(void *pvParameters) {
    Serial.println("[RadioTask] started");

    qRadioCmds = xQueueCreate(8, sizeof(RadioCmd_t));

    pinMode(PIN_I2S_SEL, OUTPUT);
    digitalWrite(PIN_I2S_SEL, HIGH);   // domyślnie SI4732

    // Domyślny tryb startowy — FM
    gState.mode      = MODE_FM;
    gState.frequency = FM_FREQ_DEF;

    applyMode(MODE_FM);

    RadioCmd_t   cmd;
    uint32_t     lastFreqPoll = 0;

    for (;;) {
        // Komendy z kolejki
        while (xQueueReceive(qRadioCmds, &cmd, 0) == pdTRUE) {
            handleCmd(cmd);
        }

        // Poll aktualnej częstotliwości z chipa (po seek)
        if (millis() - lastFreqPoll > 500) {
            lastFreqPoll = millis();
            uint16_t chipFreq = si4735.getCurrentFrequency();
            if (chipFreq > 0 && (uint32_t)chipFreq != gState.frequency)
                gState.frequency = chipFreq;
        }

        // RDS
        processRDS();

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

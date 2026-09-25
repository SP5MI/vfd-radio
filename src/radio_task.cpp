// =============================================================================
// radio_task.cpp — obsługa SI4732-A10 (FM / AM(LW+MW+SW) / SSB)
// =============================================================================
//
// UWAGA: I2S z SI4732 NIE działa gdy wgrany jest patch SSB.
// Dlatego SI4732 zawsze używa wyjścia ANALOGOWEGO (LOUT/ROUT).
// Przełączanie między PCM5102A (internet) a SI4732 (RF) robi TS5A23157
// sterowany przez PIN_I2S_SEL (GPIO18).
//
// Tryby odbiornika w bibliotece PU2CLR SI4735 to tylko FM / AM / SSB
// (FM_CURRENT_MODE / AM_CURRENT_MODE / SSB_CURRENT_MODE) — nie ma osobnej
// funkcji "SW". LW, MW i SW to ten sam tryb AM, różniący się wyłącznie
// zakresem strojenia i doborem pojemności anteny. Dlatego jest jeden
// MODE_AM obejmujący 150 kHz – 26.1 MHz.
//
// Kroki strojenia:
//   FM  = 100 kHz  (jednostki 10 kHz w SI4732, np. 10000 = 100.0 MHz)
//   AM  =   1 kHz
//   SSB =   1 kHz  + BFO co 100 Hz
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

// AM = LW + MW + SW (jeden tryb odbiornika SI4732)
#define AM_FREQ_MIN      150        // kHz — początek fal długich
#define AM_FREQ_MAX    26100        // kHz — koniec fal krótkich
#define AM_FREQ_DEF      999        // kHz
#define AM_STEP            1        // 1 kHz
#define AM_SEEK_SPACING    5        // kHz — raster przeszukiwania
#define AM_SW_THRESHOLD 1710        // powyżej tej częstotliwości = antena SW

#define SSB_FREQ_MIN    1800        // kHz
#define SSB_FREQ_MAX   30000        // kHz
#define SSB_FREQ_DEF    7074        // kHz (FT8 40m)
#define SSB_STEP           1        // 1 kHz — zgrubne strojenie
#define SSB_BFO_STEP     100        // Hz — dostrajanie BFO
#define SSB_BFO_LIMIT   1000        // Hz — zakres BFO

// =============================================================================
// GŁOŚNOŚĆ
// =============================================================================
// gState.volume jest w skali 0–100 (wspólnej z radiem internetowym i REST API),
// a SI4735::setVolume() przyjmuje 0–63.

#define SI4735_VOL_MAX    63

static inline uint8_t rfVolume() {
    uint32_t v = gState.volume;
    if (v > 100) v = 100;
    return (uint8_t)((v * SI4735_VOL_MAX) / 100);
}

// =============================================================================
// STAN LOKALNY
// =============================================================================

static SI4735   si4735;
static bool     ssbPatchLoaded = false;
static int      currentBFO     = 0;
static uint8_t  currentSSBMode = LSB_MODE;    // LSB_MODE / USB_MODE z SI4735.h
static int      lastAntCap     = -1;          // ostatnio ustawiona pojemność anteny

// =============================================================================
// DOSTĘP DO I2C — magistrala dzielona z Arduino Pro Mini (VFD)
// =============================================================================

static inline void i2cLock()   { xSemaphoreTake(xI2CMutex, portMAX_DELAY); }
static inline void i2cUnlock() { xSemaphoreGive(xI2CMutex); }

// =============================================================================
// POMOCNICZE
// =============================================================================

static bool isSSB() {
    return gState.mode == MODE_SSB_LSB || gState.mode == MODE_SSB_USB;
}

static uint32_t getFreqMin() {
    switch (gState.mode) {
        case MODE_FM:      return FM_FREQ_MIN;
        case MODE_AM:      return AM_FREQ_MIN;
        default:           return SSB_FREQ_MIN;
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
        default:           return SSB_STEP;
    }
}

// Dobór pojemności strojenia anteny dla trybów AM/SSB:
//   0 = automatyczna (pętla ferrytowa LW/MW)
//   1 = stała, minimalna (antena zewnętrzna/druciana SW)
// setTuneFrequencyAntennaCapacitor() przestraja układ, więc wołamy ją
// tylko wtedy, gdy wartość faktycznie się zmienia.
static void applyAntennaCap(uint32_t freqKHz) {
    int cap = (freqKHz > AM_SW_THRESHOLD) ? 1 : 0;
    if (cap == lastAntCap) return;
    lastAntCap = cap;
    si4735.setTuneFrequencyAntennaCapacitor(cap);
}

// =============================================================================
// INICJALIZACJA TRYBÓW  (wołane z zajętym xI2CMutex)
// =============================================================================

static void startFM() {
    lastAntCap = -1;
    si4735.setTuneFrequencyAntennaCapacitor(0);
    si4735.setFM(FM_FREQ_MIN, FM_FREQ_MAX, (uint16_t)gState.frequency, FM_STEP);
    si4735.setVolume(rfVolume());
    si4735.setSeekFmLimits(FM_FREQ_MIN, FM_FREQ_MAX);
    si4735.setSeekFmSpacing(FM_STEP);
    si4735.setRdsConfig(1, 2, 2, 2, 2);
    Serial.printf("[RF] FM @ %.1f MHz\n", gState.frequency / 100.0f);
}

static void startAM() {
    lastAntCap = -1;
    applyAntennaCap(gState.frequency);
    si4735.setAM(AM_FREQ_MIN, AM_FREQ_MAX, (uint16_t)gState.frequency, AM_STEP);
    si4735.setVolume(rfVolume());
    si4735.setSeekAmLimits(AM_FREQ_MIN, AM_FREQ_MAX);
    si4735.setSeekAmSpacing(AM_SEEK_SPACING);
    si4735.setBandwidth(0, 1);      // AMCHFLT=0 (6 kHz), AMPLFLT=1 (filtr mocy wł.)
    Serial.printf("[RF] AM @ %u kHz (%s)\n", gState.frequency,
        gState.frequency > AM_SW_THRESHOLD ? "SW" : "LW/MW");
}

// usblsb: LSB_MODE (1) lub USB_MODE (2) — stałe z SI4735.h
static void startSSB(uint8_t usblsb) {
    if (!ssbPatchLoaded) {
        Serial.println("[RF] Wgrywam patch SSB...");
        si4735.setI2CFastModeCustom(400000);    // transfer patcha na 400 kHz
        si4735.queryLibraryId();
        si4735.patchPowerUp();
        delay(50);
        const uint16_t patchSize = sizeof(ssb_patch_content);
        si4735.downloadPatch(ssb_patch_content, patchSize);
        si4735.setI2CFastModeCustom(400000);    // zegar magistrali jak w main.cpp
        // AUDIOBW=1 (2.0 kHz), SBCUTFLT=1, AVC_DIVIDER=0, AVCEN=1,
        // SMUTESEL=0, DSP_AFCDIS=1 (AFC wyłączone — wymagane dla SSB)
        si4735.setSSBConfig(1, 1, 0, 1, 0, 1);
        delay(25);
        ssbPatchLoaded = true;
        Serial.printf("[RF] Patch SSB OK (%u bajtow)\n", patchSize);
    }

    currentSSBMode = usblsb;
    currentBFO     = 0;

    lastAntCap = -1;
    applyAntennaCap(gState.frequency);
    si4735.setSSB(SSB_FREQ_MIN, SSB_FREQ_MAX,
                  (uint16_t)gState.frequency, SSB_STEP, usblsb);
    si4735.setSSBBfo(currentBFO);
    si4735.setVolume(rfVolume());

    Serial.printf("[RF] SSB %s @ %u kHz BFO=%d Hz\n",
        usblsb == LSB_MODE ? "LSB" : "USB", gState.frequency, currentBFO);
}

// =============================================================================
// PRZEŁĄCZ TRYB
// =============================================================================

static void applyMode(RadioMode_t mode) {
    digitalWrite(PIN_I2S_SEL, HIGH);   // zawsze SI4732 dla trybu RF

    // setFM()/setAM() robią powerDown()+powerUp(), co kasuje patch z RAM SI4732.
    if (mode != MODE_SSB_LSB && mode != MODE_SSB_USB)
        ssbPatchLoaded = false;

    i2cLock();
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
        case MODE_SSB_LSB:
            if (gState.frequency < SSB_FREQ_MIN || gState.frequency > SSB_FREQ_MAX)
                gState.frequency = SSB_FREQ_DEF;
            startSSB(LSB_MODE);
            break;
        case MODE_SSB_USB:
            if (gState.frequency < SSB_FREQ_MIN || gState.frequency > SSB_FREQ_MAX)
                gState.frequency = SSB_FREQ_DEF;
            startSSB(USB_MODE);
            break;
        default:
            break;
    }
    i2cUnlock();
}

// =============================================================================
// STROJENIE  (wołane z zajętym xI2CMutex)
// =============================================================================

static void tuneTo(uint32_t freq) {
    gState.frequency = freq;
    if (gState.mode != MODE_FM)
        applyAntennaCap(freq);
    si4735.setFrequency((uint16_t)freq);
    if (isSSB()) {
        currentBFO = 0;
        si4735.setSSBBfo(0);
    }
}

static void tuneUp() {
    uint32_t step = getFreqStep();
    uint32_t f    = gState.frequency + step;
    if (f > getFreqMax()) f = getFreqMin();
    tuneTo(f);
    Serial.printf("[RF] freq -> %u\n", gState.frequency);
}

static void tuneDown() {
    uint32_t step = getFreqStep();
    uint32_t f;
    if (gState.frequency <= getFreqMin() + step)
        f = getFreqMax();
    else
        f = gState.frequency - step;
    tuneTo(f);
    Serial.printf("[RF] freq -> %u\n", gState.frequency);
}

static void bfoUp() {
    currentBFO += SSB_BFO_STEP;
    if (currentBFO > SSB_BFO_LIMIT) currentBFO = SSB_BFO_LIMIT;
    si4735.setSSBBfo(currentBFO);
    Serial.printf("[RF] BFO -> %d Hz\n", currentBFO);
}

static void bfoDown() {
    currentBFO -= SSB_BFO_STEP;
    if (currentBFO < -SSB_BFO_LIMIT) currentBFO = -SSB_BFO_LIMIT;
    si4735.setSSBBfo(currentBFO);
    Serial.printf("[RF] BFO -> %d Hz\n", currentBFO);
}

// =============================================================================
// SEKWENCJA TRYBÓW — FM → AM → LSB → USB → FM
// =============================================================================

static RadioMode_t nextMode(RadioMode_t current) {
    switch (current) {
        case MODE_FM:       return MODE_AM;
        case MODE_AM:       return MODE_SSB_LSB;
        case MODE_SSB_LSB:  return MODE_SSB_USB;
        case MODE_SSB_USB:  return MODE_FM;
        default:            return MODE_FM;
    }
}

// =============================================================================
// RDS (tylko FM)
// =============================================================================

static uint32_t lastRdsCheck = 0;

static void processRDS() {
    if (gState.mode != MODE_FM) return;
    if (millis() - lastRdsCheck < 200) return;
    lastRdsCheck = millis();

    i2cLock();
    si4735.getRdsStatus();                      // odświeża bufory z FIFO chipa
    bool ready = si4735.getRdsReceived() && si4735.getRdsSync();
    // getRdsText0A()/2A() tylko parsują dane pobrane przez getRdsStatus()
    char *ps = ready ? si4735.getRdsText0A() : NULL;
    char *rt = ready ? si4735.getRdsText2A() : NULL;
    i2cUnlock();

    if (!ready) return;

    // Nazwa stacji PS (8 znaków)
    if (ps && ps[0] && strncmp((char*)gState.stationName, ps, 8) != 0) {
        strncpy((char*)gState.stationName, ps, sizeof(gState.stationName) - 1);
        gState.stationName[sizeof(gState.stationName) - 1] = '\0';
        Serial.printf("[RDS] PS: %s\n", ps);
    }

    // RadioText RT (64 znaki)
    if (rt && rt[0] && strncmp((char*)gState.rdsText, rt, 64) != 0) {
        strncpy((char*)gState.rdsText, rt, sizeof(gState.rdsText) - 1);
        gState.rdsText[sizeof(gState.rdsText) - 1] = '\0';
        Serial.printf("[RDS] RT: %s\n", rt);
    }
}

// =============================================================================
// OBSŁUGA KOMEND
// =============================================================================

static void switchMode(RadioMode_t m) {
    gState.mode = m;
    strncpy((char*)gState.stationName, "--------", sizeof(gState.stationName));
    gState.rdsText[0] = '\0';
    applyMode(m);
}

static void handleCmd(const RadioCmd_t &cmd) {
    switch (cmd.type) {

        case RADIO_CMD_SET_MODE:
            switchMode((RadioMode_t)cmd.mode);
            break;

        case RADIO_CMD_NEXT_MODE:
            switchMode(nextMode(gState.mode));
            break;

        case RADIO_CMD_SET_FREQ: {
            uint32_t f = cmd.frequency;
            if (f < getFreqMin() || f > getFreqMax()) {
                Serial.printf("[RF] freq %u poza zakresem %u-%u\n",
                    f, getFreqMin(), getFreqMax());
                break;
            }
            i2cLock();
            tuneTo(f);
            i2cUnlock();
            break;
        }

        case RADIO_CMD_FREQ_UP:
            i2cLock();
            if (isSSB()) bfoUp(); else tuneUp();
            i2cUnlock();
            break;

        case RADIO_CMD_FREQ_DOWN:
            i2cLock();
            if (isSSB()) bfoDown(); else tuneDown();
            i2cUnlock();
            break;

        case RADIO_CMD_SEEK_UP:
            if (gState.mode == MODE_FM || gState.mode == MODE_AM) {
                i2cLock();
                si4735.seekStationUp();
                gState.frequency = si4735.getFrequency();
                i2cUnlock();
            }
            break;

        case RADIO_CMD_SEEK_DOWN:
            if (gState.mode == MODE_FM || gState.mode == MODE_AM) {
                i2cLock();
                si4735.seekStationDown();
                gState.frequency = si4735.getFrequency();
                i2cUnlock();
            }
            break;

        case RADIO_CMD_SET_VOLUME:
            gState.volume = cmd.value > 100 ? 100 : cmd.value;
            i2cLock();
            si4735.setVolume(rfVolume());
            i2cUnlock();
            break;
    }
}

// =============================================================================
// TASK
// =============================================================================

void taskRadioRF(void *pvParameters) {
    (void)pvParameters;
    Serial.println("[RadioTask] started");

    pinMode(PIN_I2S_SEL, OUTPUT);
    digitalWrite(PIN_I2S_SEL, HIGH);   // domyślnie SI4732

    // Inicjalizacja układu — wykonywana RAZ. Późniejsza zmiana trybu odbywa się
    // przez setFM()/setAM()/setSSB(), które same robią power down/up.
    i2cLock();
    int16_t addr = si4735.getDeviceI2CAddress(PIN_SI4732_RESET);
    if (addr == 0) {
        i2cUnlock();
        Serial.println("[RF] BŁĄD: nie znaleziono SI4732 na I2C!");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    Serial.printf("[RF] SI4732 @ 0x%02X\n", addr);
    si4735.setup(PIN_SI4732_RESET, FM_CURRENT_MODE);
    i2cUnlock();

    // Domyślny tryb startowy — FM
    gState.frequency = FM_FREQ_DEF;
    switchMode(MODE_FM);

    RadioCmd_t   cmd;
    uint32_t     lastFreqPoll = 0;

    for (;;) {
        // Komendy z kolejki
        while (xQueueReceive(qRadioCmds, &cmd, 0) == pdTRUE) {
            handleCmd(cmd);
        }

        // Synchronizacja częstotliwości ze stanem chipa (po seek / AFC)
        if (gState.mode != MODE_INTERNET_RADIO && millis() - lastFreqPoll > 500) {
            lastFreqPoll = millis();
            i2cLock();
            uint16_t chipFreq = si4735.getFrequency();
            i2cUnlock();
            if (chipFreq > 0 && (uint32_t)chipFreq != gState.frequency)
                gState.frequency = chipFreq;
        }

        // RDS
        processRDS();

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

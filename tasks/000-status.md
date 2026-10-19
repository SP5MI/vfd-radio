# Stan projektu (aktualizacja: 2025-10-16)

## Co działa (z najnowszego log.log)

- **Kompilacja** — `pio run -e esp32s3` przechodzi, projekt się wgrywa
- **WiFi** — łączy z siecią, IP: 192.168.1.112, RSSI: -60 dBm
- **Radio internetowe (audio)** — strumieniowanie HTTP/HTTPS działa:
  - Wczytuje 5 stacji z LittleFS
  - Łączy z RMF FM (195.150.20.242:8000/rmf_fm)
  - MP3 dekoder inicjalizowany, stream ready
- **Web serwer** — AsyncWebServer na porcie 80, panel HTML widoczny, API HTTP działa
- **SI4732 wykryty na I2C** — adres `0x63` (SENB = VCC), FM @ 100.0 MHz zainicjalizowany

## Co nie działa / wymaga uwagi

### 1. Błędy I2C na VFD
```
[Display] I2C error: 4
i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE
```
Związane z Arduino Pro Mini (slave I2C na 0x27). Error 4 to ESP_ERR_INVALID_STATE — magistrala I2C może być w niepoprawnym stanie. Potrzebny jest osobny szkic Arduino dla slave I2C (patrz Priorytet 2 w CLAUDE.md).

### 2. Tryby RF (FM/AM/SSB) — nie przetestowane
SI4732 jest wykryty i FM działa, ale AM, SW, SSB (LSB/USB) nie были przetestowane. Wymaga fizycznego podłączenia anteny i testów.

## Stan zadań

| Zadanie | Status |
|---------|--------|
| 001 — Naprawa kompilacji radio_task.cpp | ✅ Zakończone |
| 002 — Dodanie patch_init.h | ✅ Zakończone (plik skopiowany do include/) |
| 003 — Aktualizacja ui_task.cpp | ⬜ Do zrobienia |
| 004 — Panel webowy index.html | ⬜ Do zrobienia |
| 005 — Integracja MQTT | ⬜ Do zrobienia |

## Następne kroki

1. **003** — Przejść do aktualizacji `ui_task.cpp` (logika przełączania trybów RF przez enkoder/długie naciśnięcie)
2. **004** — Rozwijaj panel webowy (dodaj suwak głośności, przełączanie trybów, wyświetlanie stacji)
3. **005** — Integracja z Home Assistant przez MQTT
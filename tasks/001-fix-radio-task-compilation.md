# Napraw błąd kompilacji w radio_task.cpp (Priorytet 1)

## Problem
Plik `src/radio_task.cpp` zawiera błędy uniemożliwiające kompilację:
- 'LSB' was not declared in this scope
- 'USB' was not declared in this scope
- 'class SI4735' has no member named 'setSW'
- 'class SI4735' has no member named 'getRdsReady'
- 'class SI4735' has no member named 'getRdsSystem'

## Kroki do wykonania
1. Zbadaj aktualne API biblioteki SI4735 w zainstalowanej wersji:
   ```bash
   cat .pio/libdeps/esp32s3/SI4735/src/SI4735.h
   ```
   lub poszukaj konkretnych definicji:
   ```bash
   grep -n "LSB\|USB\|setSW\|getRds" .pio/libdeps/esp32s3/SI4735/src/SI4735.h
   ```

2. Na podstawie wyników:
   - Znajdź właściwe stałe dla trybów LSB/USB (prawdopodobnie `SI47XX_LSB`, `SI47XX_USB` lub podobne)
   - Znajdź właściwą metodę do konfiguracji pasma SW (sprawdź czy jest `setAM()`, `setSWBand()`, lub podobne)
   - Znajdź właściwe metody odczytu statusu RDS (sprawdź `getRdsStatus()`, `getRdsBlockA()`, `getRdsReady()` itp.)

3. Otwórz plik `src/radio_task.cpp` i zamień:
   - Nieodnalezione stałe `LSB`/`USB` na poprawne nazwy z nagłówka
   - Wywołania `setSW()` na właściwą metodę (z odpowiednimi parametrami jeśli potrzebne)
   - Wywołania `getRdsReady()` i `getRdsSystem()` na właściwe metody RDS

4. Skompiluj aby zweryfikować fix:
   ```bash
   pio run -e esp32s3
   ```

## Kryteria akceptacji
- Projekt kompiluje się bez błędów związanych z wymienionymi symbolami
- Nie wprowadzono regresji w istniejącej funkcjonalności radiowej (sprawdź FM/AM po kompilacji)
- Kod wykorzystuje oficjalne API biblioteki SI4735 zgodnie z jej dokumentacją
- Po pomyślnej kompilacji, przejdź do zadania 002 (dodanie patch_init.h)

## Przydatne linki
- Repozytorium SI4735: https://github.com/pu2clr/SI4735
- Dokumentacja API: https://github.com/pu2clr/SI4735/tree/master/src
- Przykłady użycia: https://github.com/pu2clr/SI4735/tree/master/examples

## Powiązane pliki
- `src/radio_task.cpp` (do edycji)
- `.pio/libdeps/esp32s3/SI4735/src/SI4735.h` (referencja - nie edytować!)
- `tasks/002-add-patch-init.md` (następne zadanie priorytetu 1)
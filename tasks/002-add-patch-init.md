# Dodaj brakujący plik patch_init.h (Priorytet 1)

## Problem
Plik `include/patch_init.h` jest wymagany przez kod radiowy (SSB), ale nie istnieje w repozytorium.
Zgodnie z instrukcjami w CLAUDE.md, należy pobrać go z:
https://github.com/pu2clr/SI4735/tree/master/extras/patches

## Kroki do wykonania
1. Pobierz plik `patch_init.h` z powyższego linku lub ze źródła biblioteki SI4735:
   - Szukaj pliku w folderze `extras/patches/` w repozytorium pu2clr/SI4735
   - Plik zawiera tablicę `ssb_patch_content[]` wgrywaną do RAM SI4732 przy trybie SSB

2. Zapisz plik w lokalizacji:
   ```
   /home/user/vfd-radio/include/patch_init.h
   ```

3. Sprawdź czy plik zawiera wymagany symbol `ssb_patch_content[]`:
   ```bash
   grep -n "ssb_patch_content" include/patch_init.h
   ```

4. Skompiluj aby zweryfikować:
   ```bash
   pio run -e esp32s3
   ```

## Kryteria akceptacji
- Plik `include/patch_init.h` istnieje i zawiera tablicę `ssb_patch_content[]`
- Projekt kompiluje się bez błędów związanych z brakującym plikiem
- Obie części Priorytet 1 (001 i 002) są ukończone przed przejściem do Priorytetu 2

## Powiązane pliki
- `include/patch_init.h` (do utworzenia)
- `src/radio_task.cpp` (prawdopodobnie dołącza ten plik)
- `tasks/001-fix-radio-task-compilation.md` (poprzednie zadanie priorytetu 1)
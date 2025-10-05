# ESP32 Event‑Driven Clean Architecture — README / Instrukcja obsługi (PL)

**Projekt:** `esp32_event_clean_arch`  
**Cel:** Gotowy do rozszerzania, **event‑driven** szkielet aplikacji dla rodziny ESP32
(ESP32, S2, S3, C3, C6, H2, P4) zaprojektowany w duchu **Clean Architecture**
(ports & adapters, serwisy asynchroniczne, brak blokad w logice).

---

## 0. Szybka ściąga (Cheat Sheet) — 60 sekund do startu
sudo rm -rf /home/wz/esp32_event_clean_arch/firmware/projects/demo_lcd_rgb/build
sudo rm -f  /home/wz/esp32_event_clean_arch/firmware/projects/demo_lcd_rgb/sdkconfig
TARGET=esp32c6 CONSOLE=uart   ESPPORT=$(/home/wz/esp32_event_clean_arch/scripts/find-port.sh)   /home/wz/esp32_event_clean_arch/scripts/flash-monitor.sh
TARGET=esp32c6 CONSOLE=uart   ESPPORT=$(scripts/find-port.sh)   scripts/flash-monitor.sh
RESET_SDKCONFIG=1 TARGET=esp32c6 CONSOLE=uart scripts/flash-monitor.sh

# Po zmianie defaults wykonaj raz build z resetem, żeby sdkconfig został odtworzony z nowych warstw:
RESET_SDKCONFIG=1 TARGET=esp32c6 CONSOLE=uart scripts/flash-monitor.sh

### 0.A 60 s do startu (Docker, zalecane)
```bash
./scripts/build-docker.sh
./scripts/init.sh
export PROJ=demo_lcd_rgb
export TARGET=esp32c6
./scripts/idf.sh fullclean && ./scripts/build.sh && ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```

### 0.B „Hard‑clean” projektu i świeży flash (per‑projekt)
> Gdy chcesz porzucić poprzedni `sdkconfig` i katalog `build` konkretnego projektu.
```bash
rm -f  firmware/projects/demo_lcd_rgb/sdkconfig*
rm -rf firmware/projects/demo_lcd_rgb/build
TARGET=esp32c6 CONSOLE=uart ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```

#### 0.B.1 Weryfikacja po „hard‑clean” (na co patrzeć)
**CMake:** 3× „Loading defaults file …”  
```text
Loading defaults file .../sdkconfig.defaults...
Loading defaults file .../sdkconfig.console.uart.defaults...
Loading defaults file .../sdkconfig.esp32c6.defaults...
```
**Flash (esptool):**  
```text
esptool.py ... --flash_size 8MB ...
```
**Bootloader:**  
```text
I (...) boot.esp32c6: SPI Flash Size : 8MB
```
oraz **brak ostrzeżenia o 2 MB**.

### 0.C Szybki test po flashu (REPL `esp>`)
```text
esp> help
esp> logrb stat
esp> logrb tail 256
esp> loglvl * D
```
> **Uwaga:** alias `logrb get --fmt json|hex --meta` nie jest jeszcze dostępny; obecnie użyj `stat|tail|dump`.

### 0.D Łatwe przełączanie układów (C6/C3/S3/H2) i USB↔UART
```bash
# C6 + UART
TARGET=esp32c6 CONSOLE=uart RESET_SDKCONFIG=1 ESPPORT=/dev/ttyUSB0 ./scripts/flash-monitor.sh
# C6 + USB‑Serial‑JTAG
TARGET=esp32c6 CONSOLE=usb  RESET_SDKCONFIG=1 ESPPORT=/dev/ttyACM0 ./scripts/flash-monitor.sh
# C3 + UART
TARGET=esp32c3 CONSOLE=uart RESET_SDKCONFIG=1 ESPPORT=/dev/ttyUSB0 ./scripts/flash-monitor.sh
# S3 + UART
TARGET=esp32s3 CONSOLE=uart RESET_SDKCONFIG=1 ESPPORT=/dev/ttyUSB0 ./scripts/flash-monitor.sh
# H2 + UART
TARGET=esp32h2 CONSOLE=uart RESET_SDKCONFIG=1 ESPPORT=/dev/ttyUSB0 ./scripts/flash-monitor.sh
```
> Jeśli dodasz `sdkconfig.<target>.defaults` (np. piny I²C, parametry LCD per‑układ), skrypty dołączą je automatycznie
> do generowanego `sdkconfig`. Dzięki temu `RESET_SDKCONFIG=1` przenosi sensowne wartości między targetami. fileciteturn0file0

### 0.E Najczęstsze filtry monitora (IDF_MONITOR_FILTER)
```bash
# Global INFO, a wybrane tagi na DEBUG
IDF_MONITOR_FILTER="APP:D DFR_LCD:D *:W" ./scripts/flash-monitor.sh
# Cicho (WARN+) z wyjątkiem INFRA na INFO
IDF_MONITOR_FILTER="*:W INFRA:I" ./scripts/flash-monitor.sh
# Maksymalnie gadatliwe
IDF_MONITOR_FILTER="*:V" ./scripts/flash-monitor.sh
```

### 0.F Tylko flash / tylko monitor
```bash
ESPPORT=$(./scripts/find-port.sh) ./scripts/idf.sh -p ${ESPPORT} flash
ESPPORT=$(./scripts/find-port.sh) ./scripts/monitor.sh
```

### 0.G Zmiana wersji ESP‑IDF jednym parametrem
```bash
IDF_IMAGE=esp32-idf:5.5.1   ./scripts/build.sh
IDF_IMAGE=espressif/idf:v5.5.1 ./scripts/build.sh   # oficjalny tag ma literę 'v'
```

### 0.H Wariant „native” (bez Dockera)
```bash
. "$IDF_PATH/export.sh"
idf.py set-target esp32c6
idf.py fullclean && idf.py build
idf.py -p "$(./scripts/find-port.sh)" flash monitor
```

### 0.I Diagnostyka w 30 sekund
- **Monitor „milczy”** — *pause* (`Ctrl+T,Y`), `ESPPORT`, filtr.  
  Napraw: `ESPPORT=$(./scripts/find-port.sh) ./scripts/monitor.sh` oraz `IDF_MONITOR_FILTER="*:I" ...`.
- **Brak `esp>` / `LOGCLI: ... REPL started`** — sprawdź `CONSOLE=uart|usb`, TX/RX, REPL w configu.  
  Napraw: `TARGET=esp32c6 CONSOLE=uart RESET_SDKCONFIG=1 ...`.
- **CMake nie ładuje 3× defaults** — zrób clean `sdkconfig*` i `build/` (sekcja 0.B).  
- **Flash 2 MB zamiast 8 MB** — wymuś clean + `RESET_SDKCONFIG=1` i sprawdź parametry `esptool`/bootlog.  
- **„Krzaki” na LCD / brak `EV_LCD_READY`** — adresy I²C (`LCD=0x3E`, `RGB=0x62/0x2D`), filtr `DFR_LCD:D`, obniż I²C do 100 kHz, *Delay po 0x38 = 40 ms*, *Pauza=1 ms*.  
- **„Nie znaleziono portu”** — wskaż ręcznie `ESPPORT=/dev/ttyUSB0 ...`; rozważ rozszerzony `find-port` dla macOS. fileciteturn0file1

---

## Spis treści
1. [Co to jest i co obsługuje](#co-to-jest-i-co-obsługuje)
2. [Wymagania i zależności](#wymagania-i-zależności)
3. [Szybki start — najkrótsza ścieżka](#szybki-start--najkrótsza-ścieżka)
3a. [Jak to uruchomić (jednorazowe „oczyszczenie”)](#jak-to-uruchomić-jednorazowe-oczyszczenie)
4. [Uruchomienie na ESP32‑C6‑DevKitC‑1 (UART, bez USB)](#uruchomienie-na-esp32c6devkitc1-uart-bez-usb)
5. [Warianty: przełączanie układów i trybu konsoli (UART/USB‑Serial‑JTAG)](#warianty-przełączanie-układów-i-trybu-konsoli-uartusbserialjtag)
6. [Skrypty deweloperskie (co robią i jak używać)](#skrypty-deweloperskie-co-robia-i-jak-używać)
7. [Konfiguracja przez Kconfig (menuconfig)](#konfiguracja-przez-kconfig-menuconfig)
8. [Połączenia sprzętowe (LCD1602 RGB, DS18B20)](#połączenia-sprzętowe-lcd1602-rgb-ds18b20)
9. [Monitor, REPL i logowanie](#monitor-repl-i-logowanie)
10. [Architektura i komponenty](#architektura-i-komponenty)
11. [Przepływ zdarzeń — jak to działa w praktyce](#przepływ-zdarzeń--jak-to-działa-w-praktyce)
12. [Przykładowe projekty](#przykładowe-projekty)
13. [Docker oraz wersje ESP‑IDF (IDF_IMAGE)](#docker-oraz-wersje-espidf-idf_image)
14. [Migracja do ESP‑IDF 6.x — plan bezpieczny](#migracja-do-espidf-6x--plan-bezpieczny)
15. [Instrukcja — praca ze skryptami i logowaniem](#instrukcja--praca-ze-skryptami-i-logowaniem)
16. [Typowe problemy i szybkie rozwiązania](#typowe-problemy-i-szybkie-rozwiązania)
17. [Diagnostyka w 30 sekund](#diagnostyka-w-30-sekund)
18. [Generowanie dokumentacji (Doxygen + Graphviz)](#generowanie-dokumentacji-doxygen--graphviz)
19. [FAQ](#faq)

---

## Co to jest i co obsługuje

**Szkielet wieloplatformowy** dla ESP32, oparty o **event‑bus** i separację warstw:
- `components/core__ev` — event‑bus (pub/sub, broadcast, oddzielne kolejki) i pętla zdarzeń.
- `components/ports` — „czyste” interfejsy (np. I²C).
- `components/infrastructure__idf_i2c_port` — adapter nowego I²C (`driver/i2c_master.h`).
- `components/services__i2c` — asynchroniczny worker I²C publikujący `EV_I2C_DONE`.
- `components/services__timer` — `EV_TICK_100MS`, `EV_TICK_1S` na `esp_timer`.
- `components/drivers__lcd1602rgb_dfr_async` — DFR0464 (ST7032+PCA9633) jako FSM, bez blokad; parametry przez **Kconfig**.
- `components/services__ds18b20_ev` — asynchroniczny DS18B20 (one‑shot, brak `vTaskDelay`). fileciteturn0file1

---

## Wymagania i zależności

**Sprzęt:** dowolny wspierany wariant ESP32 (sprawdzone m.in. ESP32‑C6‑DevKitC‑1).  
**Oprogramowanie:** rekomendowane środowisko **Docker** z obrazem ESP‑IDF.  
**Host:** Bash/sh, `ls`, `stat`; dostęp do `/dev/ttyUSB*`/`/dev/ttyACM*`.

> Bez Dockera skrypty działają jako wrappery na `idf.py`, ale przygotowanie środowiska IDF jest po Twojej stronie. fileciteturn0file1

---

## Szybki start — najkrótsza ścieżka

```bash
# 0) Docker: obraz + wolumeny (jednorazowo)
./scripts/build-docker.sh
./scripts/init.sh

# 1) Wybór projektu i targetu
export PROJ=demo_lcd_rgb
export TARGET=esp32c6

# 2) Konfiguracja (menuconfig) – ustaw parametry LCD
./scripts/menuconfig.sh

# 3) Budowanie
./scripts/build.sh

# 4) Flash + monitor (auto-port)
ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```
**Wariant na obrazie IDF:**  
`./scripts/idf.sh fullclean && IDF_IMAGE=esp32-idf:5.5.1 ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh`.
fileciteturn0file1

---

## Jak to uruchomić (jednorazowe „oczyszczenie”)

```bash
rm -f  firmware/projects/demo_lcd_rgb/sdkconfig*
rm -rf firmware/projects/demo_lcd_rgb/build
TARGET=esp32c6 CONSOLE=uart ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```
Oczekiwane logi: 3× *Loading defaults…* (CMake), `--flash_size 8MB` (esptool), `SPI Flash Size : 8MB` (boot). fileciteturn0file1

---

## Uruchomienie na ESP32‑C6‑DevKitC‑1 (UART, bez USB)

```bash
TARGET=esp32c6 CONSOLE=uart RESET_SDKCONFIG=1       IDF_MONITOR_FILTER="*"       ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```
Po starcie: `LOGCLI: UART REPL started`, prompt `esp>`, skróty monitora (`Ctrl+T,Y`, `Ctrl+]`, `Ctrl+T,H`). fileciteturn0file0

---

## Warianty: przełączanie układów i trybu konsoli (UART/USB‑Serial‑JTAG)

(patrz **0.D** — one‑linery). Dodatkowo: `sdkconfig.<target>.defaults` są automatycznie włączane przez skrypty. fileciteturn0file0

---

## Skrypty deweloperskie (co robią i jak używać)

- `scripts/find-port.sh` — autowykrycie portu (prosty algorytm `ls` + pierwszy wynik).  
- `scripts/flash-monitor.sh` — flash + monitor, rozdział `ESPBAUD`/`MONBAUD`, wielokrotne `--print_filter`, czytelne komunikaty.  
- `scripts/idf.sh` — wrapper na `idf.py` (*fullclean*, `set-target`, `-p PORT flash`).  
- `scripts/build.sh`, `scripts/monitor.sh`, `scripts/menuconfig.sh`, `scripts/build-docker.sh`, `scripts/init.sh`.  
**Warianty:** `./scripts/idf.sh fullclean`, `./scripts/idf.sh set-target ${TARGET}`, `ESPPORT=$(./scripts/find-port.sh) ./scripts/monitor.sh`. fileciteturn0file1

---

## Konfiguracja przez Kconfig (menuconfig)

**App configuration → I2C (LCD)** — LCD1602 RGB (DFRobot DFR0464 v2.0):  
`C[3:0]=12`, `C[5:4]=1`, `Delay po 0x38 = 40 ms`, `Paczka DDRAM = 8 B`, `Pauza = 1 ms`, `RGB = 0x62 (lub 0x2D)`.  
Częstotliwość I²C zacznij od `100 kHz`, potem testuj `400 kHz`. fileciteturn0file1

---

## Połączenia sprzętowe (LCD1602 RGB, DS18B20)

**DFRobot LCD1602 RGB (DFR0464 v2.0):** 3V3/GND, I²C: `GPIO10/11` (C6, domyślnie), `LCD=0x3E`, `RGB=0x62/0x2D`.  
**DS18B20:** `DQ` → wybrany `GPIO` + `4.7 kΩ` do `3V3`, zasilanie `3V3/GND`. fileciteturn0file1

---

## Monitor, REPL i logowanie

**Filtry monitora (`IDF_MONITOR_FILTER`)**: tokeny `TAG:LVL` (`E,W,I,D,V`), wildcard `*` — przykłady w **0.E**.  
**Ring‑buffer (REPL `esp>`):** `logrb stat|tail <N>|dump [--limit N]|clear`. **Poziomy w locie:** `loglvl * D`, `loglvl APP W`. fileciteturn0file0

---

## Architektura i komponenty

Warstwy: Core (event‑bus), Ports, Infrastructure (adaptery IDF), Services (I²C/Timer), Drivers (FSM np. LCD), Apps.  
Zalety: brak `vTaskDelay` w domenie (opóźnienia przez `esp_timer`), testowalność, czyste granice. fileciteturn0file1

---

## Przepływ zdarzeń — jak to działa w praktyce

1. Timer publikuje `EV_TICK_100MS/1S`.  
2. Driver LCD wykonuje kroki init/flush sterowane zdarzeniami i `EV_I2C_DONE`.  
3. Po init: `EV_LCD_READY` → aplikacja wyświetla pierwszą treść.  
4. DS18B20 — cykl one‑shot bez blokad. fileciteturn0file1

---

## Przykładowe projekty

`demo_hello_ev`, `demo_lcd_rgb`, `demo_ds18b20_ev` — patrz **Szybki start** i **Cheat Sheet**. fileciteturn0file1

---

## Docker oraz wersje ESP‑IDF (IDF_IMAGE)

Przełączanie wersji bez modyfikacji źródeł przez `IDF_IMAGE` (lokalny `esp32-idf:*` lub oficjalny `espressif/idf:v*`).  
Rekomendacja: po zmianie obrazu `./scripts/idf.sh fullclean`. fileciteturn0file1

---

## Migracja do ESP‑IDF 6.x — plan bezpieczny

**Wariant A — oficjalny obraz Espressifa (`v6.0`/`v6.0.1`):**
```bash
export IDF_IMAGE="espressif/idf:v6.0"
./scripts/idf.sh fullclean && ./scripts/build.sh
ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```
**Wariant B — własny obraz (Doxygen/Graphviz):**
```Dockerfile
FROM espressif/idf:v6.0
RUN apt-get update && apt-get install -y --no-install-recommends doxygen graphviz && rm -rf /var/lib/apt/lists/*
WORKDIR /fw
```
```bash
docker build --pull -f Docker/Dockerfile.idf-6.0 -t esp32-idf:6.0 .
export IDF_IMAGE="esp32-idf:6.0"
./scripts/idf.sh fullclean && ./scripts/build.sh
ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```
**Rollback:** `IDF_IMAGE="esp32-idf:5.5.1"` → `fullclean` → `build` → `flash+monitor`.  
**Checklist:** `menuconfig` po pierwszym buildzie; I²C od `100 kHz`; adresy `LCD=0x3E`, `RGB=0x2D/0x62`; SPI Flash = 8MB w logu. fileciteturn0file1

---

## Instrukcja — praca ze skryptami i logowaniem

**`scripts/find-port.sh`** — bardzo prosty i skuteczny (pierwszy znaleziony port).  
Ograniczenia: wiele urządzeń/macOS. **Ulepszenie (opcjonalnie):** wariant wybierający „ostatnio podłączony” i obsługujący macOS (`/dev/tty.usbserial*`/`/dev/tty.usbmodem*`).

**`scripts/flash-monitor.sh`** — bezpieczne `set -Eeuo pipefail`, autodetekcja portu, osobne `ESPBAUD`/`MONBAUD`, wielokrotne filtry.  
**Filtry monitora** — używaj `IDF_MONITOR_FILTER` (patrz **0.E**).  
**Ring‑buffer** — `logrb stat|tail|dump|clear|snapshot`; integruje się z Twoim filtrem (np. `DFR_LCD:D`). fileciteturn0file1

---

## Typowe problemy i szybkie rozwiązania

- **„Krzaki” na LCD po starcie:** zwiększ *Delay po 0x38*, `C[5:4]=1`, *Pauza między paczkami*=1 ms, I²C=50–100 kHz, sprawdź pull‑upy `4.7 kΩ`.  
- **RGB nie świeci:** sprawdź `APP_RGB_ADDR` (`0x62` vs `0x2D`).  
- **Monitor milczy:** zobacz **0.I** (diagnostyka). fileciteturn0file1

---

## Diagnostyka w 30 sekund

Zobacz **0.I Diagnostyka w 30 sekund** (na początku pliku) — matryca objaw→sprawdź→napraw, z gotowymi poleceniami.

---

## Generowanie dokumentacji (Doxygen + Graphviz)

```bash
./scripts/gen-docs.sh
# Otwórz: docs/html/index.html
```
(W obrazach lokalnych `esp32-idf:*` dostępne narzędzia Doxygen/Graphviz; dla oficjalnych obrazów dodaj Dockerfile). fileciteturn0file1

---

## FAQ

**Czy mogę użyć USB‑Serial‑JTAG zamiast UART?** — Tak; `CONSOLE=usb` i odpowiedni port `/dev/ttyACM*`.  
**Jak podbić poziom logów bez rekompilacji?** — REPL (`loglvl * D`) oraz `IDF_MONITOR_FILTER`.  
**Jak sprawdzić adresy I²C?** — włącz DEBUG w driverze LCD i odczytaj skan (`0x3E`, `0x2D/0x62`). fileciteturn0file0

**Miłej pracy!**

# ESP32 Event‑Driven Clean Architecture — README / Instrukcja (PL)

**Repo:** `esp32_event_clean_arch`  
**Cel:** Event‑driven szkielet aplikacji dla rodziny **ESP32** (ESP32, S2, S3, C3, **C6**, H2, P4) w duchu **Clean Architecture**
(ports & adapters, serwisy asynchroniczne, brak blokad w logice domenowej).

---

## 0. Szybka ściąga — 60 sekund do startu {#szybka-sciaga}

> Wymagany Docker. Skrypty same zadbają o poprawne środowisko IDF w kontenerze.

```bash
# 0) Zbuduj obraz z Doxygen/Graphviz + autopin digesta (jednorazowo / po zmianie wersji)
./scripts/build-docker.sh

# 1) Szybki healthcheck środowiska i projektu
./scripts/doctor.sh

# 2) Flash + monitor na ESP32‑C6 (UART) — port znajdzie się automatycznie
TARGET=esp32c6 CONSOLE=uart ./scripts/flash-monitor.sh
```

**Po zmianie `sdkconfig*.defaults`** (np. parametrów LCD/pinów) odtwórz `sdkconfig` z warstw defaults:

```bash
RESET_SDKCONFIG=1 TARGET=esp32c6 CONSOLE=uart ./scripts/flash-monitor.sh
```

### 0.A Twarde wyczyszczenie projektu (per‑projekt) {#twardy-clean}
> Gdy chcesz porzucić poprzedni `sdkconfig` i `build/`.

```bash
rm -f  firmware/projects/demo_lcd_rgb/sdkconfig*
rm -rf firmware/projects/demo_lcd_rgb/build
TARGET=esp32c6 CONSOLE=uart ./scripts/flash-monitor.sh
```

**Weryfikacja po cleanie (na to patrz):**
- CMake: **3×** `Loading defaults file ...`
  (`sdkconfig.defaults`, `sdkconfig.console.uart.defaults`, `sdkconfig.esp32c6.defaults`)
- `esptool.py`: ma **--flash_size detect** (a log bootloadera pokaże np. *8MB*)
- Boot: `boot.esp32c6: SPI Flash Size : 8MB` (dla płytki z 8 MB)

### 0.B REPL po starcie (prompt `esp>`) {#repl}
```text
esp> help
esp> logrb stat
esp> logrb tail 256
esp> loglvl * D
```

### 0.C Szybkie przełączanie układów i konsoli {#przelaczanie-ukladow}
```bash
# C6 + UART
TARGET=esp32c6 CONSOLE=uart RESET_SDKCONFIG=1 ./scripts/flash-monitor.sh

# C6 + USB‑Serial‑JTAG
TARGET=esp32c6 CONSOLE=usb  RESET_SDKCONFIG=1 ./scripts/flash-monitor.sh

# C3 + UART
TARGET=esp32c3 CONSOLE=uart RESET_SDKCONFIG=1 ./scripts/flash-monitor.sh

# S3 + UART
TARGET=esp32s3 CONSOLE=uart RESET_SDKCONFIG=1 ./scripts/flash-monitor.sh

# H2 + UART
TARGET=esp32h2 CONSOLE=uart RESET_SDKCONFIG=1 ./scripts/flash-monitor.sh
```
> Jeśli masz `sdkconfig.<target>.defaults` (np. piny I²C różne dla C3/C6/S3), skrypt włączy je automatycznie
> do generowanego `sdkconfig`. Dzięki temu `RESET_SDKCONFIG=1` przenosi sensowne wartości między targetami.

### 0.D Filtry monitora (`IDF_MONITOR_FILTER`) {#filtry-monitora}
```bash
# Global INFO, a wybrane tagi na DEBUG
IDF_MONITOR_FILTER="APP:D DFR_LCD:D *:I" ./scripts/flash-monitor.sh
# Cicho (WARN+) z wyjątkiem INFRA na INFO
IDF_MONITOR_FILTER="*:W INFRA:I" ./scripts/flash-monitor.sh
# Maksymalnie gadatliwie
IDF_MONITOR_FILTER="*:V" ./scripts/flash-monitor.sh
```

### 0.E Tylko flash / tylko monitor {#tylko-flash-tylko-monitor}
```bash
ESPPORT=$(./scripts/find-port.sh) ./scripts/idf.sh -p "${ESPPORT}" flash
ESPPORT=$(./scripts/find-port.sh) ./scripts/idf.sh -p "${ESPPORT}" -b "${MONBAUD:-115200}" monitor
```

### 0.F Zmiana wersji ESP‑IDF {#zmiana-wersji-idf}
```bash
# Zbuduj obraz wg SSOT z .env (autopinning digesta, jeśli trzeba)
./scripts/build-docker.sh

# (opcjonalnie) Override jednorazowo:
# IDF_TAG=5.5.2 ./scripts/build-docker.sh

# Użyj konkretnego obrazu na czas komendy (opcjonalnie)
IDF_IMAGE="${IDF_IMAGE}" ./scripts/idf.sh --version
```

### 0.G Wariant „native” (bez Dockera) {#bez-dockera}
```bash
. "$IDF_PATH/export.sh"
idf.py set-target esp32c6
idf.py fullclean && idf.py build
idf.py -p "$(./scripts/find-port.sh)" flash monitor
```

---

## Spis treści {#spis-tresci}
1. [Opis projektu](#opis-projektu)
2. [Wymagania](#wymagania)
3. [Struktura repo i komponenty](#struktura-repo-i-komponenty)
4. [Skrypty deweloperskie](#skrypty-deweloperskie)
5. [Konfiguracja: `sdkconfig*.defaults`](#konfiguracja-sdkconfigdefaults)
6. [Polaczenia sprzetowe](#polaczenia-sprzetowe)
7. [Monitor, REPL i logowanie](#monitor-repl-i-logowanie)
8. [Docker & wersje IDF](#docker--wersje-idf)
9. [Generowanie dokumentacji](#generowanie-dokumentacji)
10. [Typowe problemy (quick fix)](#typowe-problemy-quick-fix)

---

## Opis projektu {#opis-projektu}
Wieloplatformowy szkielet **event‑driven** dla ESP32, rozdzielający warstwy domeny, portów i adapterów.
- **Asynchroniczność**: brak `vTaskDelay` w logice – opóźnienia przez `esp_timer` i zdarzenia.
- **Testowalność**: logika w portach/adapters, prosta wymiana implementacji.
- **Skalowalność**: warianty per target (`esp32c3/c6/s3/h2`) i per konsola (`uart/usb`).

## Wymagania {#wymagania}
- **Sprzęt**: płytka z ESP32 (sprawdzone na ESP32‑C6‑DevKitC‑1).
- **Host**: Linux/macOS/WSL, Bash, Docker, dostęp do `/dev/ttyUSB*`/`/dev/ttyACM*`.
- **Zalecane**: Docker + lokalny obraz IDF (budowany skryptem).

## Struktura repo i komponenty {#struktura-repo-i-komponenty}
Wybrane komponenty (prefiks = warstwa):
- `components/core__ev` — event‑bus (pub/sub, broadcast, kolejki) i pętla zdarzeń.
- `components/ports` — „czyste” interfejsy (np. I²C).
- `components/infrastructure__idf_i2c_port` — adapter nowego I²C (`driver/i2c_master.h`).
- `components/services__i2c` — asynchroniczny worker I²C (`EV_I2C_DONE`).
- `components/services__timer` — deadline-driven timer: uzbraja najbliższy deadline i publikuje zdarzenia; opcjonalnie (Kconfig) generuje legacy `EV_TICK_*`.
- `components/drivers__lcd1602rgb_dfr_async` — DFR0464 (ST7032 + PCA9633) jako FSM, bez blokad; parametry przez Kconfig.
- `components/services__ds18b20_ev` — asynchroniczny DS18B20 (tryb one‑shot).

Projekty przykładowe:
- `firmware/projects/demo_lcd_rgb` — demonstrator LCD + REPL (`esp>`).
- `firmware/projects/demo_hello_ev` — minimalny szablon event‑driven.
- `firmware/projects/demo_ds18b20_ev` — czujnik temperatury DS18B20.

## Skrypty deweloperskie {#skrypty-deweloperskie}
- **`scripts/build-docker.sh`** — buduje obraz `IDF_IMAGE` (domyślnie `esp32-idf:5.5.2-docs`), **autopinuje digest** do `.env`.
- **`scripts/doctor.sh`** — szybka diagnostyka (Docker, obraz, HOME mount, port szeregowy, `idf.sh --version`, pliki defaults).
- **`scripts/find-port.sh`** — wykrywa port:
  - Linux: preferuje stabilne `/dev/serial/by-id/*` (w tym `*-if00*`), dalej najnowszy `ttyUSB*`/`ttyACM*`,
  - macOS: `/dev/cu.usbserial*` / `/dev/cu.usbmodem*`,
  - WSL: `/dev/ttyS*`.
- **`scripts/idf.sh`** — wrapper na `idf.py` w kontenerze, m.in.:
  - wycisza entrypoint obrazu i ładuje `export.sh` po cichu,
  - bind‑mountuje repo i użytkownika (`--user uid:gid`), włącza `ccache`,
  - przykłady: `scripts/idf.sh build`, `scripts/idf.sh fullclean`, `scripts/idf.sh set-target esp32c6`, `scripts/idf.sh menuconfig`,
    `scripts/idf.sh -p "$(./scripts/find-port.sh)" flash`, `scripts/idf.sh -p ... -b ... monitor`.
- **`scripts/flash-monitor.sh`** — sekwencja `ensure target` → `build` → `flash` → `monitor`.
  - `RESET_SDKCONFIG=1` wykona `fullclean` + nowy `sdkconfig`.
  - Obsługuje wiele filtrów przez `IDF_MONITOR_FILTER="TAG:LVL, *:I, APP:D"`, osobne `ESPBAUD` i `MONBAUD`.
- **`scripts/gen-docs.sh`** — generuje Doxygen/Graphviz **na bind‑mouncie** (bez wolumenów), wynik w `docs/html/`.

> Uwaga: wszystkie skrypty same ładują `scripts/common.env.sh` — ręczne `source` nie jest wymagane.

## Konfiguracja: `sdkconfig*.defaults` {#konfiguracja-sdkconfigdefaults}
- W projekcie **`demo_lcd_rgb`** `CMakeLists.txt` **jawnie** ładuje warstwy defaults:
  1) `sdkconfig.defaults` (wspólne),
  2) `sdkconfig.console.<uart|usb>.defaults` (wg `CONSOLE`),
  3) `sdkconfig.<target>.defaults` (wg `TARGET`).  
  Skoro są **jawnie** wskazywane, **pliki muszą istnieć** (mogą być **puste**).
- W pozostałych projektach (`demo_hello_ev`, `demo_ds18b20_ev`) defaults są w `main/`. Jeśli chcesz je faktycznie stosować,
  dopnij je w `CMakeLists.txt`, np.:
  ```cmake
  set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_SOURCE_DIR}/main/sdkconfig.defaults")
  ```
- **Pro tip:** `./scripts/doctor.sh` sprawdza wymagane defaults dla aktywnego projektu. Możesz automatycznie
  utworzyć puste stuby:
  ```bash
  DOCTOR_AUTOFIX_DEFAULTS=1 ./scripts/doctor.sh
  ```

## Polaczenia sprzetowe {#polaczenia-sprzetowe}
- **DFRobot LCD1602 RGB (DFR0464 v2.0)**: 3V3 / GND, I²C: domyślnie **GPIO10/11 (C6)**, adresy: `LCD=0x3E`, `RGB=0x62` lub `0x2D`.
- **DS18B20**: `DQ` → wybrany GPIO + rezystor **4.7 kΩ** do 3V3, zasilanie 3V3/GND.

## Monitor, REPL i logowanie {#monitor-repl-i-logowanie}
- **Monitor (IDF)**: sterujesz poziomami przez `IDF_MONITOR_FILTER` (tokeny `TAG:LVL`, `LVL ∈ {E,W,I,D,V}`, wspierany wildcard `*`).
  Przykłady w sekcji **0.D**.
- **REPL (`esp>`)** — dostępne polecenia:
  - `logrb stat|tail <N>|dump [--limit N]|clear` — introspekcja ring‑buffera,
  - `loglvl <TAG|*> <E|W|I|D|V>` — zmiana poziomu logów w locie,
  - `help` — lista komend.
- Skróty monitora: `Ctrl+]` (wyjście), `Ctrl+T,H` (pomoc), `Ctrl+T,Y` (pause).

## Docker & wersje IDF {#docker--wersje-idf}
- Obraz budujesz skryptem **`scripts/build-docker.sh`** (dodaje **Doxygen/Graphviz**).
- Skrypt **autopinuje digest** konkretnej platformy do `.env` jako `IDF_DIGEST`.
- Domyślny obraz to `esp32-idf:5.5.2-docs`. Możesz użyć oficjalnego `espressif/idf:v5.5.2`, ale wtedy **brak** Doxygen/Graphviz.
- Zmiana wersji: uruchom ponownie `./scripts/build-docker.sh` z `IDF_TAG=<nowa>`.

## Generowanie dokumentacji {#generowanie-dokumentacji}
```bash
./scripts/gen-docs.sh
# wynik: docs/html/index.html
```
> Jeśli Doxygen zgłosi błąd mapowania `md=markdown`, usuń `EXTENSION_MAPPING` z `Doxyfile` i zostaw `MARKDOWN_SUPPORT=YES`.
> Ostrzeżenie o języku „polish” jest kosmetyczne (część etykiet może być po angielsku).

## Typowe problemy (quick fix) {#typowe-problemy-quick-fix}
- **Monitor „milczy”** — zweryfikuj port (`ESPPORT=$(./scripts/find-port.sh)`), prędkość (`MONBAUD`), filtr (`IDF_MONITOR_FILTER`), tryb konsoli (`CONSOLE=uart|usb`).
- **CMake nie ładuje 3× defaults** — wykonaj clean `sdkconfig*` + `build/` i uruchom z `RESET_SDKCONFIG=1`.
- **Flash 2 MB zamiast 8 MB** — `RESET_SDKCONFIG=1`, sprawdź parametry `esptool` i log bootloadera.
- **LCD: znaki/krzaki** — zwiększ *Delay po 0x38* do 40 ms, *Pauza między paczkami*=1 ms, I²C=100 kHz, adresy `0x3E/0x2D/0x62`.
- **RGB nie świeci** — sprawdź `APP_RGB_ADDR` (`0x62` vs `0x2D`).

---

## Zmiana projektu (PROJ) — np. `demo_hello_ev` {#zmiana-projektu}
```bash
# 1) Przełącz projekt
export PROJ=demo_hello_ev

# 2) Stwórz (jeśli trzeba) puste defaults dla aktywnego projektu
DOCTOR_AUTOFIX_DEFAULTS=1 ./scripts/doctor.sh

# 3) Build + flash
TARGET=esp32c6 CONSOLE=uart ./scripts/flash-monitor.sh
```
> W projektach, gdzie defaults są w `main/`, dopnij je w `CMakeLists.txt` (patrz sekcja o konfiguracji).

---

## Zmienne środowiskowe (najczęstsze) {#zmienne-srodowiskowe}
| Zmienna | Znaczenie | Przykład |
|---|---|---|
| `PROJ` | aktywny projekt w `firmware/projects/` | `demo_lcd_rgb` |
| `TARGET` | układ ESP32 | `esp32c6`, `esp32c3`, `esp32s3`, `esp32h2` |
| `CONSOLE` | tryb konsoli | `uart` / `usb` |
| `ESPPORT` | port szeregowy | `/dev/ttyUSB0`, `/dev/ttyACM0` |
| `ESPBAUD` | baudrate do flash | `921600` |
| `MONBAUD` | baudrate monitora | `115200` |
| `IDF_IMAGE` | obraz Dockera | `esp32-idf:5.5.2-docs` |
| `IDF_TAG` / `IDF_DIGEST` | pin wersji IDF | `5.5.2` / `sha256:...` |
| `IDF_MONITOR_FILTER` | filtr logów monitora | `APP:D *:I` |

---

**Miłej pracy!**

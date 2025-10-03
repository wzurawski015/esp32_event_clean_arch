# ESP32 Event-Driven Clean Architecture (Multi-target)

Ekstremalnie **event-driven** szkielet dla rodziny ESP32 (ESP32, S2, S3, C3, C6, H2, P4), zaprojektowany w duchu **Clean Architecture**:
- `components/core__ev` – event-bus (pub/sub, broadcast, oddzielne kolejki).
- `components/ports` – czyste interfejsy (np. I²C).
- `components/infrastructure__idf_i2c_port` – adapter nowego I²C (`driver/i2c_master.h`).
- `components/services__i2c` – asynchroniczny worker I²C (publikuje `EV_I2C_DONE`).
- `components/services__timer` – zegary `EV_TICK_100MS` i `EV_TICK_1S` (esp_timer).
- `components/drivers__lcd1602rgb_dfr_async` – sterownik DFR0464 (ST7032 + PCA9633) jako automat stanów, zero blokad, **parametry przez Kconfig**.
- `components/services__ds18b20_ev` – asynchroniczny DS18B20 (one-shoty, brak `vTaskDelay`).

## Projekty przykładowe

- `demo_hello_ev` – minimalny event-loop + ticki.
- `demo_lcd_rgb` – LCD1602 RGB (DFRobot DFR0464 v2.0) przez **services__i2c**.
- `demo_ds18b20_ev` – DS18B20 (single drop, 1-Wire) sterowany timerami.

## Szybki start (Docker)

```bash
# 0) Docker: obraz + wolumeny
./scripts/build-docker.sh
./scripts/init.sh

# 1) Wybór projektu i targetu
export PROJ=demo_lcd_rgb
export TARGET=esp32c6
# 1a) Budowa i flash (auto-wykrycie portu)
./scripts/idf.sh fullclean && ./scripts/build.sh && ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
# alternatywnie od razu na nowszym obrazie:
./scripts/idf.sh fullclean && IDF_IMAGE=esp32-idf:5.5.1 ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh

# 2) Konfiguracja (menuconfig)
./scripts/menuconfig.sh
# LCD1602RGB (DFRobot DFR0464) – ustawienia:
#   Kontrast: C[3:0]           -> np. 12
#   Kontrast: C[5:4]           -> np. 1
#   Delay po 0x38 (ms)         -> np. 40
#   Paczka DDRAM (B)           -> 8
#   Pauza między paczkami (ms) -> 1
#   Adres RGB                  -> 0x62 (lub 0x2D)

# 3) Budowanie
./scripts/build.sh

# 4) Flash + monitor (Linux auto-port)
ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```

**Przydatne warianty:**

```bash
# pełne czyszczenie
./scripts/idf.sh fullclean

# ustawienie targetu (odbudowuje sdkconfig)
./scripts/idf.sh set-target ${TARGET}

# samo flashowanie
ESPPORT=$(./scripts/find-port.sh) ./scripts/idf.sh -p ${ESPPORT} flash

# sam monitor
ESPPORT=$(./scripts/find-port.sh) ./scripts/monitor.sh
```

## Połączenia sprzętowe

**DFRobot LCD1602 RGB (DFR0464 v2.0)**  
Zasilanie: 3V3, GND.  
I²C: SDA/SCL – domyślnie (dla C6) GPIO10/11 (zmienisz w *App configuration → I2C (LCD)*).  
Adresy: LCD=0x3E, RGB=0x62 (czasem 0x2D – ustaw w Kconfig).

**DS18B20**  
DQ → wybrany GPIO, rezystor 4.7 kΩ do 3V3, zasilanie 3V3, GND. Konfiguracja w `demo_ds18b20_ev`.

## Dokumentacja (Doxygen + Graphviz)

```bash
./scripts/gen-docs.sh
# Otwórz: docs/html/index.html
```

## Uwagi architektoniczne

- Całość jest sterowana zdarzeniami (event-bus + kolejki).
- Brak `vTaskDelay` w logice (opóźnienia przez one-shot `esp_timer`).
- Wzorcowe **ports & adapters**: kod wyższych warstw nie zależy od IDF (tylko od interfejsów).
- Sterownik LCD w pełni reaktywny: init + flush w krokach, I²C przez serwis, pakiety DDRAM + opcjonalna pauza.

## Typowe problemy i szybkie rozwiązania

- **„Krzaki” na LCD po starcie:** zwiększ *Delay po 0x38*, ustaw `C[5:4]=1`, ustaw *Pauza między paczkami* = 1 ms, zmniejsz I²C (np. 50 kHz), sprawdź pull-upy 4.7 kΩ.
- **RGB nie świeci:** sprawdź `APP_RGB_ADDR` (0x62 vs 0x2D).

---

# Jak używać — TL;DR komendy

```bash
# wybierz target i projekt
cd ~/esp32_event_clean_arch
export TARGET=esp32c6
export PROJ=demo_lcd_rgb

# docker (jednorazowo / gdy zmieniałeś obraz)
./scripts/build-docker.sh
./scripts/init.sh

# menuconfig (ustaw LCD1602RGB → kontrast/delay/burst/pauza/adres RGB)
./scripts/menuconfig.sh

# build
./scripts/build.sh

# flash + monitor
ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```

**Dlaczego to usuwa „krzaki”:**
- kontrast stroisz bez rekompilacji (Kconfig),
- pierwszy delay po 0x38 stabilizuje ST7032 po power-on,
- paczki + pauza zapewniają poprawną inkrementację DDRAM,
- adres RGB ustawiasz zgodnie z egzemplarzem (0x62 / 0x2D).

Jeśli chcesz, mogę dorzucić wariant z auto-probe adresu RGB (0x62→0x2D fallback) – ale skoro preferujesz Kconfig, obecne rozwiązanie jest idealnie „czyste” i przewidywalne.

---

## Jak używać (wprost)

> **Uwaga:** oficjalny tag obrazu Espressifa to `espressif/idf:v5.5.1` (z literą **v**).  
> Jeśli budujesz własny obraz z Doxygen/Graphviz, używaj lokalnego tagu `esp32-idf:5.5.1`.

```bash
# 1) Budowa własnego obrazu z Doxygen/Graphviz (jeśli chcesz)
docker build --pull -f Docker/Dockerfile.idf-5.5.1 -t esp32-idf:5.5.1 .

# 2) Build na nowym IDF (lokalny obraz)
IDF_IMAGE=esp32-idf:5.5.1 ./scripts/build.sh

# 3) Flash + monitor (lokalny obraz)
IDF_IMAGE=esp32-idf:5.5.1 ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh

# 4) Rollback do starego (bez zmian w źródłach)
IDF_IMAGE=esp32-idf:5.3-docs ./scripts/build.sh

# (alternatywa: oficjalny obraz Espressifa, bez własnego Dockerfile)
IDF_IMAGE=espressif/idf:v5.5.1 ./scripts/build.sh
```

---

## Migracja do ESP‑IDF 6.x (bez ryzyka)

Poniżej „zielony” plan i gotowe komendy, aby przejść na ESP‑IDF 6.x obok istniejącej wersji (bez dotykania źródeł). Wszystko opiera się o zmienną `IDF_IMAGE` obsługiwaną przez skrypty.

### Wariant A — oficjalny obraz Espressifa (np. `v6.0` lub `v6.0.1`)

1. **Sprawdź dostępność taga** (z literą `v`):
   ```bash
   docker manifest inspect espressif/idf:v6.0   >/dev/null && echo "OK: v6.0 dostępny"
   docker manifest inspect espressif/idf:v6.0.1 >/dev/null && echo "OK: v6.0.1 dostępny"
   ```
2. **Zbuduj projekt „na czysto” i wgraj**:
   ```bash
   export IDF_IMAGE="espressif/idf:v6.0"   # lub dokładny: espressif/idf:v6.0.1
   ./scripts/idf.sh fullclean
   ./scripts/build.sh
   ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
   ```
3. **Zweryfikuj w monitorze**, że masz `ESP-IDF v6.x.y`, skan I²C wykrywa LCD (0x3E) i RGB (0x2D/0x62), a `EV_LCD_READY` się pojawia.

> Chcesz zrobić z tego domyślny obraz dla zespołu? Wystarczy zmienną `IMAGE=` w `scripts/idf.sh` ustawić na `espressif/idf:v6.0`. Nie jest to wymagane — `IDF_IMAGE` nadpisuje domyślną wartość.

### Wariant B — własny obraz (z Doxygen/Graphviz)

1. **Dockerfile dla IDF 6**: `Docker/Dockerfile.idf-6.0`
   ```Dockerfile
   FROM espressif/idf:v6.0
   RUN apt-get update && apt-get install -y --no-install-recommends \
         doxygen graphviz && \
       rm -rf /var/lib/apt/lists/*
   WORKDIR /fw
   ```
2. **Budowa lokalnego obrazu**:
   ```bash
   docker build --pull -f Docker/Dockerfile.idf-6.0 -t esp32-idf:6.0 .
   ```
3. **Test projektu na tym obrazie**:
   ```bash
   export IDF_IMAGE="esp32-idf:6.0"
   ./scripts/idf.sh fullclean
   ./scripts/build.sh
   ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
   ```

### Rollback w 1 krok (bez zmian w źródłach)

```bash
export IDF_IMAGE="esp32-idf:5.5.1"      # albo espressif/idf:v5.5.1
./scripts/idf.sh fullclean
./scripts/build.sh
ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```

### Mini‑checklista po migracji

- **Pełny clean po zmianie wersji**:
  ```bash
  ./scripts/idf.sh fullclean
  ```
  (usuwa cache CMake i środowisko Pythona skojarzone z poprzednim IDF).
- **`menuconfig` po pierwszym buildzie** — zapisz `sdkconfig`, aby przepisać ewentualne symbole zastąpione nowszymi odpowiednikami.
- **I²C/LCD sanity**:
  - zacznij od `APP_I2C_HZ=100 kHz`, potem testuj 400 kHz,
  - sprawdź adresy: LCD=0x3E, RGB=0x2D **lub** 0x62 (zależnie od modułu),
  - w logu powinno być: `found 0x2D` i `found 0x3E`, następnie `LCD/RGB: start init` i `EV_LCD_READY` w aplikacji.
- **SPI Flash (opcjonalnie)**: jeśli zobaczysz ostrzeżenie o „generic flash driver” dla GigaDevice,
  włącz wykrywanie w `menuconfig` → *Component config → SPI Flash driver → Auto-detect flash chips* → zaznacz wsparcie dla GigaDevice.
- **Dokumentacja** (jeśli używasz lokalnego obrazu z Doxygenem):
  ```bash
  ./scripts/gen-docs.sh
  # otwórz: docs/html/index.html
  ```

### Zestawy komend — 1:1 (kopiuj–wklej)

**Oficjalny obraz `v6.0`**:
```bash
export IDF_IMAGE="espressif/idf:v6.0"
./scripts/idf.sh fullclean
./scripts/build.sh
ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```

**Własny obraz `esp32-idf:6.0` (z Doxygen/Graphviz)**:
```bash
docker build --pull -f Docker/Dockerfile.idf-6.0 -t esp32-idf:6.0 .
export IDF_IMAGE="esp32-idf:6.0"
./scripts/idf.sh fullclean
./scripts/build.sh
ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```

**Rollback do `5.5.1`**:
```bash
export IDF_IMAGE="esp32-idf:5.5.1"
./scripts/idf.sh fullclean
./scripts/build.sh
ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
```

### Typowe pułapki i szybkie naprawy

- **Mieszane środowisko Pythona/IDF** w cache → uruchom `fullclean` (zrobisz to szybciej niż diagnoza).
- **Tag obrazu nie istnieje** → pamiętaj o literze `v` w tagu oficjalnym, np. `espressif/idf:v6.0`.
- **Zmiany w Kconfig po migracji** → wejdź w `menuconfig`, zapisz, odbuduj.
- **Zawieszki LCD przy 400 kHz** → wróć na `100 kHz` albo zwiększ `Pauza między paczkami` (1–2 ms).

---

### Sanity-check po podmianie obrazu
- `shellcheck scripts/idf.sh` → brak błędów.
- `IDF_IMAGE=espressif/idf:v6.0 ./scripts/build.sh` → projekt się buduje bez zmian źródeł.
- `IDF_IMAGE=esp32-idf:6.0` (lokalny obraz z Doxygen/Graphviz) działa identycznie; decydujesz zmienną środowiskową.


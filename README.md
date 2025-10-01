# ESP32 Event-Driven Clean Architecture (Multi-target)

**Polski opis** poniżej — projekt jest przygotowany jako **ekstremalnie event‑driven** środowisko startowe
dla rodziny ESP32 (ESP32, S2, S3, C3, C6, H2, P4). W repo znajdują się **3 projekty** przykładowe:

- `demo_hello_ev` – minimalny przykład: event bus + zegary (`EV_TICK_*`).
- `demo_lcd_rgb` – sterownik **DFRobot LCD1602 RGB (DFR0464 v2.0)** po **nowym I²C** (`driver/i2c_master.h`),
  w pełni **nieblokująco** przez usługę `services__i2c` (worker) i automat stanów.
- `demo_ds18b20_ev` – **asynchroniczne** odczyty temperatury z **DS18B20** (single‑drop, 1‑Wire)
  z użyciem **esp_timer** (bez `vTaskDelay` w logice wyższego poziomu).

Wszystko jest zaprojektowane w duchu **Clean Architecture**:
- `components/core__ev` – event‑bus (broadcast, wielu subskrybentów, każdy ma własną kolejkę).
- `components/ports` – czyste interfejsy portów (np. I²C).
- `components/infrastructure__idf_i2c_port` – adapter na **nowy sterownik I²C** (`driver/i2c_master.h`).
- `components/services__i2c` – **asynchroniczny** worker kolejkujący operacje I²C → emituje `EV_I2C_DONE/ERROR`.
- `components/services__timer` – zegary `EV_TICK_100MS` i `EV_TICK_1S` oparte o `esp_timer`.
- `components/drivers__lcd1602rgb_dfr_async` – sterownik DFR0464 (0x3E LCD + 0x2D RGB), automat stanów, bez blokad.
- `components/services__ds18b20_ev` – asynchroniczny serwis DS18B20 (1‑Wire, single‑drop) z zegarami `esp_timer`.

## Szybki start

### 0) Docker
Zbuduj obraz Dockera (IDF 5.3 + doxygen/graphviz) i przygotuj wolumeny narzędzi:
```bash
./scripts/build-docker.sh
./scripts/init.sh
```

### 1) Wybór projektu i układu (target)
```bash
export PROJ=demo_lcd_rgb         # demo_hello_ev | demo_lcd_rgb | demo_ds18b20_ev
export TARGET=esp32c6            # esp32 | esp32s2 | esp32s3 | esp32c3 | esp32c6 | esp32h2 | esp32p4
```

### 2) Budowanie
```bash
./scripts/build.sh
```

### 3) Programowanie + monitor
```bash
ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
# lub: ESPPORT=/dev/ttyUSB0 ./scripts/flash-monitor.sh
```

### 4) Konfiguracja pinów i adresów (menuconfig)
```bash
./scripts/menuconfig.sh
# App configuration → I2C (LCD) → piny SDA/SCL, adresy LCD (0x3E) i RGB (0x2D)
# demo_ds18b20_ev → DS18B20 GPIO i rozdzielczość (9..12 bit)
```

## Połączenia sprzętowe

### DFRobot LCD1602 RGB (DFR0464 v2.0)
- Zasilanie: **VCC=3.3V**, **GND**.
- **I2C SDA/SCL** – domyślnie dla **ESP32‑C6**: **SDA=6**, **SCL=7** (zmienisz w `menuconfig`).
- Adresy I²C: **LCD=0x3E**, **RGB=0x2D**.

### DS18B20 (single‑drop)
- DQ do wybranego **GPIO** (domyślnie 18), **rezystor 4.7 kΩ** do 3V3 (zalecane).
- Masa i zasilanie 3V3 (tryb zasilania normalnego; parasitic nieobsługiwany tu).
- Konfiguracja w `menuconfig` projektu **demo_ds18b20_ev**.

## Dokumentacja (Doxygen + Graphviz)
Wygeneruj dokumentację (HTML) poleceniem:
```bash
./scripts/gen-docs.sh
# Wygenerowane pliki: docs/html/index.html
```

---

## Architektura (skrót)
- **Event bus**: `ev_init()`, `ev_subscribe(&my_queue, len)`, `ev_post(src, code, a0, a1)`.
  Każdy subskrybent ma kolejkę `QueueHandle_t`, do której trafia **każde** zdarzenie (broadcast).
- **I²C**: Kierujemy wywołania przez **services__i2c** – logika (np. driver LCD) nie blokuje się;
  operacje są wykonywane w dedykowanym workerze, a wynik przychodzi eventem.
- **Zegary**: `EV_TICK_100MS`, `EV_TICK_1S` – żadnych `vTaskDelay` w logice aplikacji.
- **DS18B20**: Cały cykl `convert -> opóźnienie -> read` realizowany przez **one‑shot esp_timer**
  + automat stanów. Wątek aplikacji dostaje tylko eventy (`EV_DS18_READY`).

Szczegóły w komentarzach kodu (po polsku) i w dokumentacji Doxygen.

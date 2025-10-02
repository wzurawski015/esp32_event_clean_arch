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

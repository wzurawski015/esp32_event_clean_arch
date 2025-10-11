#!/usr/bin/env bash
set -Eeuo pipefail

# Kanonikalizacja ścieżki działająca na Linux/macOS (bez wymagania readlink -f)
canon() {
  local p="$1"
  if command -v realpath >/dev/null 2>&1; then
    realpath "$p"
  elif command -v readlink >/dev/null 2>&1 && readlink -f "$p" >/dev/null 2>&1; then
    readlink -f "$p"
  elif command -v python3 >/dev/null 2>&1; then
    python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$p"
  else
    # Ostateczny fallback — bez kanonikalizacji
    echo "$p"
  fi
}

# 1) Preferuj stabilne linki by-id (Linux) – zwykle /dev/serial/by-id/usb-...-if00
if [[ -d /dev/serial/by-id ]]; then
  mapfile -t by_id < <(ls -1 /dev/serial/by-id/* 2>/dev/null | sort || true)
  if (( ${#by_id[@]} == 1 )); then
    canon "${by_id[0]}"; exit 0
  fi

  # Jeśli jest wiele, spróbuj wybrać te dla USB-UART (częste: "-if00")
  mapfile -t by_id_if00 < <(ls -1 /dev/serial/by-id/*-if00* 2>/dev/null | sort || true)
  if (( ${#by_id_if00[@]} >= 1 )); then
    canon "${by_id_if00[-1]}"; exit 0
  fi

  # W ostateczności weź ostatni z posortowanych (zwykle najnowszy)
  if (( ${#by_id[@]} >= 1 )); then
    canon "${by_id[-1]}"; exit 0
  fi
fi

# 2) Fallback: najnowszy /dev/ttyUSB* lub /dev/ttyACM* (Linux)
mapfile -t devs_linux < <(ls -1t /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)
if (( ${#devs_linux[@]} >= 1 )); then
  echo "${devs_linux[0]}"; exit 0
fi

# 3) macOS: preferuj /dev/cu.* (zalecane do komunikacji), potem /dev/tty.*
if [[ "$(uname -s)" == "Darwin" ]]; then
  mapfile -t macdevs < <(ls -1t \
    /dev/cu.usbserial* /dev/cu.usbmodem* \
    /dev/tty.usbserial* /dev/tty.usbmodem* \
    2>/dev/null || true)
  if (( ${#macdevs[@]} >= 1 )); then
    echo "${macdevs[0]}"; exit 0
  fi
fi

# 4) WSL (Windows Subsystem for Linux): porty mapowane jako /dev/ttyS*
if [[ -r /proc/version ]] && grep -qiE 'microsoft|wsl' /proc/version 2>/dev/null; then
  mapfile -t wsl_devs < <(ls -1t /dev/ttyS* 2>/dev/null || true)
  if (( ${#wsl_devs[@]} >= 1 )); then
    echo "${wsl_devs[0]}"; exit 0
  fi
fi

echo "ERR: nie znalazłem żadnego portu szeregowego (USB/ACM/serial)!" >&2
exit 2

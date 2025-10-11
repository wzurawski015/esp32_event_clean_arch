#!/usr/bin/env bash
set -Eeuo pipefail

# 1) Preferuj stabilne linki by-id (jeden) – zwykle /dev/serial/by-id/usb-...-if00
if [[ -d /dev/serial/by-id ]]; then
  mapfile -t by_id < <(ls -1 /dev/serial/by-id/* 2>/dev/null | sort)
  if (( ${#by_id[@]} == 1 )); then
    readlink -f "${by_id[0]}"
    exit 0
  fi
  # Jeśli jest wiele, spróbuj wybrać te dla USB-UART (częste: "-if00")
  mapfile -t by_id_if00 < <(ls -1 /dev/serial/by-id/*-if00 2>/dev/null | sort)
  if (( ${#by_id_if00[@]} >= 1 )); then
    readlink -f "${by_id_if00[-1]}"
    exit 0
  fi
  # W ostateczności weź ostatni z posortowanych
  if (( ${#by_id[@]} >= 1 )); then
    readlink -f "${by_id[-1]}"
    exit 0
  fi
fi

# 2) Fallback: najnowszy /dev/ttyUSB* lub /dev/ttyACM*
mapfile -t devs < <(ls -1t /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)
if (( ${#devs[@]} >= 1 )); then
  echo "${devs[0]}"
  exit 0
fi

echo "ERR: nie znalazłem żadnego portu szeregowego (USB/ACM)!" >&2
exit 2

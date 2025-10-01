#!/usr/bin/env bash
# Prosta heurystyka do znalezienia portu szeregowego.
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1

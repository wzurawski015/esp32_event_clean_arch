cmake_minimum_required(VERSION 3.16)

# IDF toolchain / makra
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Twoje komponenty wspólne
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

# ---------- WARSTWY SDKCONFIG (PRZED 'project') ----------

# Konsola: priorytet ma -D CONSOLE_TYPE=..., potem ENV(CONSOLE), domyślnie 'uart'
if(NOT DEFINED CONSOLE_TYPE)
  if(DEFINED ENV{CONSOLE})
    set(CONSOLE_TYPE "$ENV{CONSOLE}" CACHE STRING "Console selection (uart|usb)" FORCE)
  else()
    set(CONSOLE_TYPE "uart" CACHE STRING "Console selection (uart|usb)" FORCE)
  endif()
endif()

# Target: zwykle ustawiany przez 'idf.py set-target'; mamy też domyślną oraz ENV(TARGET)
if(NOT DEFINED IDF_TARGET)
  if(DEFINED ENV{TARGET})
    set(IDF_TARGET "$ENV{TARGET}" CACHE STRING "ESP target (esp32c6|esp32c3|esp32s3|esp32h2)" FORCE)
  else()
    set(IDF_TARGET "esp32c6" CACHE STRING "ESP target (esp32c6|esp32c3|esp32s3|esp32h2)" FORCE)
  endif()
endif()

# Uwaga: JEDNA LINIA (bez nowych linii), separator to średniki
set(_PROJ_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(SDKCONFIG_DEFAULTS "${_PROJ_DIR}/sdkconfig.defaults;${_PROJ_DIR}/sdkconfig.console.${CONSOLE_TYPE}.defaults;${_PROJ_DIR}/sdkconfig.${IDF_TARGET}.defaults"
    CACHE STRING "sdkconfig default layers" FORCE)

# (Opcjonalnie) podgląd w logu CMake
message(STATUS "IDF_TARGET=${IDF_TARGET}")
message(STATUS "CONSOLE_TYPE=${CONSOLE_TYPE}")
message(STATUS "SDKCONFIG_DEFAULTS=${SDKCONFIG_DEFAULTS}")
# ---------------------------------------------------------

project(demo_lcd_rgb)

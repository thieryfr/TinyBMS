# TinyBMS ESP-IDF Build Configuration
# Phase 1: Fondations ESP-IDF (coexistence avec PlatformIO)

cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project definition
project(tinybms_bridge)

# Note: This build system coexists with PlatformIO
# Use 'idf.py build' for ESP-IDF native build
# Use 'pio run' for Arduino/PlatformIO build

#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/.native_build"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

CXX=${CXX:-g++}
CXXFLAGS=(
    -std=c++17
    -Wall
    -Wextra
    -fpermissive
    -I"$ROOT_DIR/include"
    -I"$ROOT_DIR/src"
    -I"$ROOT_DIR/tests/native/stubs"
)

# CVL logic test
$CXX "${CXXFLAGS[@]}" \
    "$ROOT_DIR/tests/test_cvl_logic.cpp" \
    "$ROOT_DIR/src/cvl_logic.cpp" \
    -o "$BUILD_DIR/test_cvl_logic"

# Event bus test (requires stubs for Arduino/FreeRTOS)
$CXX "${CXXFLAGS[@]}" \
    "$ROOT_DIR/tests/native/test_event_bus.cpp" \
    "$ROOT_DIR/src/event_bus.cpp" \
    -o "$BUILD_DIR/test_event_bus"

# UART stub test
$CXX "${CXXFLAGS[@]}" \
    "$ROOT_DIR/tests/native/test_uart_stub.cpp" \
    "$ROOT_DIR/src/uart/tinybms_uart_client.cpp" \
    -o "$BUILD_DIR/test_uart_stub"

"$BUILD_DIR/test_cvl_logic"
"$BUILD_DIR/test_event_bus"
"$BUILD_DIR/test_uart_stub"


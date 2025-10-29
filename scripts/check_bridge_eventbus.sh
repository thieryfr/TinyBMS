#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

# Ignore the bridge event sink implementation which is allowed to talk to EventBus.
violations=$(rg \
  --files-with-matches \
  --glob 'src/bridge_*.cpp' \
  --glob '!src/bridge_event_sink.cpp' \
  --fixed-strings \
  'EventBus::' || true)

if [[ -n "$violations" ]]; then
  echo "Bridge modules must not access EventBus directly (found 'EventBus::'). Violations in:" >&2
  echo "$violations" >&2
  exit 1
fi

violations_include=$(rg \
  --files-with-matches \
  --glob 'src/bridge_*.cpp' \
  --glob '!src/bridge_event_sink.cpp' \
  --fixed-strings \
  '#include "EventBus.h"' || true)

if [[ -n "$violations_include" ]]; then
  echo "Bridge modules must not include EventBus.h. Violations in:" >&2
  echo "$violations_include" >&2
  exit 1
fi

echo "Bridge modules do not reference EventBus directly."

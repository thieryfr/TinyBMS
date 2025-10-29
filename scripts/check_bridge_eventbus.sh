#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

# Ignore the bridge event sink implementation which is allowed to talk to EventBus.
pattern='EventBus(::|\s+[A-Za-z_&*])'
violations=$(rg --pcre2 --files-with-matches --glob 'src/bridge_*.cpp' --glob '!src/bridge_event_sink.cpp' "$pattern" || true)

if [[ -n "$violations" ]]; then
  echo "Bridge modules must not access EventBus directly. Found references in:" >&2
  echo "$violations" >&2
  exit 1
fi

violations_include=$(rg --files-with-matches --glob 'src/bridge_*.cpp' --glob '!src/bridge_event_sink.cpp' '"event_bus.h"' || true)

if [[ -n "$violations_include" ]]; then
  echo "Bridge modules must not include event_bus.h. Found includes in:" >&2
  echo "$violations_include" >&2
  exit 1
fi

echo "Bridge modules correctly use BridgeEventSink."

# Architecture

## Event publishing

Bridge modules are responsible for translating TinyBMS data into events that the
rest of the firmware can consume. To keep the bridge layer decoupled from the
core event bus implementation, bridge code **must not** talk to `EventBus`
directly. Instead, each bridge receives a `BridgeEventSink` instance that it can
use to publish events.

`BridgeEventSink` exposes the subset of functionality that bridges need, while
the concrete sink implementation (`EventBusBridgeEventSink`) owns the
interaction with `EventBus`. This separation ensures that:

- Bridge modules stay focused on acquisition and translation logic.
- Event bus internals can evolve without requiring changes across every
  `bridge_*` translation unit.
- Testing bridges becomes simpler because the sink interface can be mocked.

When adding a new bridge (for example `src/bridge_newsensor.cpp`), inject the
provided `BridgeEventSink&` reference, and call `eventSink()` to forward events.
Never include `event_bus.h` nor call `EventBus::getInstance()` from bridge
modules.

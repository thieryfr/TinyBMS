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

### Event publication pattern

The bridge follows a strict sequence to publish events while keeping the event
bus implementation abstracted away:

1. **Acquire data**: Read from the underlying transport (UART, CAN, mock, …)
   and translate raw bytes into TinyBMS domain objects.
2. **Build the event payload**: Populate the appropriate `Event` structure or
   value object expected by subscribers.
3. **Emit through the sink**: Invoke the sink with the prepared payload. The
   sink takes care of dispatching the event on the bus.
4. **Handle back-pressure / errors**: React to the boolean or status value
   returned by the sink when relevant (e.g., retry, drop, or log failures).

#### Usage example

```cpp
#include "bridge_voltage.h"

void BridgeVoltage::publishMeasurements(const Measurements& data) {
    BridgeEventSink& sink = eventSink();

    VoltageEvent event {
        .pack_id = data.packId,
        .voltages = data.cells,
    };

    if (!sink.post(event)) {
        log_w("Voltage bridge: failed to push event to sink");
    }
}
```

The example above demonstrates the expected flow: the bridge never retrieves
the global event bus and only interacts with the injected sink instance.

#### Responsibilities overview

| Component                    | Responsibility                                                   |
|------------------------------|------------------------------------------------------------------|
| `bridge_*` translation units | Decode transport payloads and build TinyBMS domain events.       |
| `BridgeEventSink` interface  | Provide the minimal API for bridges to publish events.           |
| `EventBusBridgeEventSink`    | Adapt sink calls to the concrete `EventBus` singleton.           |
| `EventBus`                   | Route events to registered listeners across the firmware stack. |

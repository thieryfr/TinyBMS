# TinyBMS UART protocol notes

- TinyBMS Communication Protocols Rev D (§1.3) states that when the device wakes from sleep, the first UART command must be transmitted twice; the initial frame only wakes the BMS and no reply is generated until the second copy is received.
- The same section documents that every UART command frame begins with the start byte `0xAA`.

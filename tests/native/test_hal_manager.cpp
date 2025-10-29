#include <cassert>
#include <Arduino.h>
#include "hal/hal_manager.h"
#include "hal/mock_factory.h"

int main() {
    hal::setFactory(hal::createMockFactory());

    hal::HalConfig config{};
    config.uart.baudrate = 19200;
    config.uart.timeout_ms = 250;
    config.uart.use_dma = false;
    config.can.bitrate = 250000;
    config.storage.type = hal::StorageType::SPIFFS;
    config.storage.format_on_fail = true;
    config.watchdog.timeout_ms = 1000;

    hal::HalManager::instance().initialize(config);

    auto& uart = hal::HalManager::instance().uart();
    uint8_t payload[]{0xAA, 0xBB, 0xCC};
    size_t written = uart.write(payload, sizeof(payload));
    assert(written == sizeof(payload));

    auto& storage = hal::HalManager::instance().storage();
    auto file = storage.open("/log", hal::StorageOpenMode::Write);
    assert(file && file->isOpen());
    file->write(payload, sizeof(payload));
    file->close();

    auto read_file = storage.open("/log", hal::StorageOpenMode::Read);
    assert(read_file && read_file->isOpen());
    uint8_t buffer[3]{};
    size_t read = read_file->read(buffer, sizeof(buffer));
    read_file->close();
    assert(read == sizeof(payload));
    assert(buffer[0] == payload[0]);

    auto& can = hal::HalManager::instance().can();
    hal::CanFrame frame{};
    frame.id = 0x123;
    frame.dlc = 3;
    frame.data[0] = 0x01;
    frame.data[1] = 0x02;
    frame.data[2] = 0x03;
    assert(can.transmit(frame) == hal::Status::Ok);
    hal::CanStats stats = can.getStats();
    assert(stats.tx_success == 1);

    auto& watchdog = hal::HalManager::instance().watchdog();
    assert(watchdog.feed() == hal::Status::Ok);
    hal::WatchdogStats wd_stats = watchdog.getStats();
    assert(wd_stats.feed_count == 1);

    return 0;
}

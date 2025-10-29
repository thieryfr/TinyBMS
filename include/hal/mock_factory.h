#pragma once

#include <memory>
#include "hal/hal_factory.h"

namespace hal {

std::unique_ptr<HalFactory> createMockFactory();

} // namespace hal

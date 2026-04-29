#pragma once

#include <cstdint>
#include <vector>

#include "modeline.h"

namespace evdi_control {

std::vector<std::uint8_t> build_edid(const Modeline& modeline);

}  // namespace evdi_control

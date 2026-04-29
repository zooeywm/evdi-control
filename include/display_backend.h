#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace evdi_control {

struct OutputMode {
    std::string id;
    int width = 0;
    int height = 0;
    double refresh_hz = 0.0;
    bool current = false;
    bool preferred = false;
    bool custom = false;
};

struct OutputInfo {
    std::string name;
    bool connected = false;
    bool enabled = false;
    std::vector<OutputMode> modes;
};

std::vector<OutputInfo> list_drm_outputs();
std::optional<std::string> detect_new_drm_output(const std::vector<OutputInfo>& before,
                                                 std::chrono::milliseconds timeout);

}  // namespace evdi_control

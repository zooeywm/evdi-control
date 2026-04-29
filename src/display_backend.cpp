#include "display_backend.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <regex>
#include <sstream>
#include <thread>

#include "util.h"

namespace evdi_control {

namespace {

constexpr std::chrono::milliseconds kPollInterval{250};
namespace fs = std::filesystem;

std::pair<int, int> parse_resolution(const std::string& text) {
    std::smatch match;
    if (!std::regex_search(text, match, std::regex(R"((\d+)x(\d+))"))) {
        return {0, 0};
    }
    return {parse_int(match[1].str()), parse_int(match[2].str())};
}

}  // namespace

std::vector<OutputInfo> list_drm_outputs() {
    std::vector<OutputInfo> outputs;
    const fs::path drm_root("/sys/class/drm");
    if (!fs::exists(drm_root)) {
        return outputs;
    }

    for (const auto& entry : fs::directory_iterator(drm_root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind("card", 0) != 0) {
            continue;
        }
        if (name.find('-') == std::string::npos) {
            continue;
        }

        OutputInfo output;
        output.name = name;

        const fs::path status_path = entry.path() / "status";
        if (fs::is_regular_file(status_path)) {
            output.connected = trim(read_text_file(status_path.string())) == "connected";
        }

        const fs::path enabled_path = entry.path() / "enabled";
        if (fs::is_regular_file(enabled_path)) {
            output.enabled = trim(read_text_file(enabled_path.string())) == "enabled";
        }

        const fs::path modes_path = entry.path() / "modes";
        if (fs::is_regular_file(modes_path)) {
            std::stringstream modes_stream(read_text_file(modes_path.string()));
            std::string line;
            bool first_mode = true;
            while (std::getline(modes_stream, line)) {
                line = trim(line);
                if (line.empty()) {
                    continue;
                }
                auto [width, height] = parse_resolution(line);
                OutputMode mode;
                mode.id = line;
                mode.width = width;
                mode.height = height;
                mode.current = output.enabled && first_mode;
                mode.preferred = first_mode;
                output.modes.push_back(mode);
                first_mode = false;
            }
        }

        outputs.push_back(output);
    }

    std::sort(outputs.begin(), outputs.end(), [](const OutputInfo& left, const OutputInfo& right) {
        return left.name < right.name;
    });
    return outputs;
}

std::optional<std::string> detect_new_drm_output(const std::vector<OutputInfo>& before,
                                                 std::chrono::milliseconds timeout) {
    std::map<std::string, bool> previous;
    for (const auto& output : before) {
        previous[output.name] = output.connected;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto outputs = list_drm_outputs();
        for (const auto& output : outputs) {
            const auto found = previous.find(output.name);
            if (found == previous.end()) {
                return output.name;
            }
            if (!found->second && output.connected) {
                return output.name;
            }
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return std::nullopt;
}

}  // namespace evdi_control

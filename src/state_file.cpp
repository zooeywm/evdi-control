#include "state_file.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "util.h"

namespace fs = std::filesystem;

namespace evdi_control {

namespace {

std::map<std::string, std::string> to_map(const ManagedDisplayState& state) {
    return {
        {"name", state.name},
        {"pid", std::to_string(state.pid)},
        {"state_file", state.state_file},
        {"log_file", state.log_file},
        {"card_path", state.card_path},
        {"output_name", state.output_name},
        {"requested_width", std::to_string(state.requested_width)},
        {"requested_height", std::to_string(state.requested_height)},
        {"requested_refresh", std::to_string(state.requested_refresh)},
        {"actual_width", std::to_string(state.actual_width)},
        {"actual_height", std::to_string(state.actual_height)},
        {"actual_refresh", std::to_string(state.actual_refresh)},
    };
}

}  // namespace

ManagedDisplayState read_state_file(const std::string& path) {
    ManagedDisplayState state;
    state.state_file = path;

    std::ifstream input(path);
    if (!input) {
        throw CommandError("failed to open state file " + path);
    }

    std::string line;
    while (std::getline(input, line)) {
        const auto delimiter = line.find('=');
        if (delimiter == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, delimiter);
        const std::string value = line.substr(delimiter + 1);

        if (key == "name") state.name = value;
        else if (key == "pid") state.pid = static_cast<pid_t>(parse_int(value));
        else if (key == "log_file") state.log_file = value;
        else if (key == "card_path") state.card_path = value;
        else if (key == "output_name") state.output_name = value;
        else if (key == "requested_width") state.requested_width = parse_int(value);
        else if (key == "requested_height") state.requested_height = parse_int(value);
        else if (key == "requested_refresh") state.requested_refresh = parse_double(value);
        else if (key == "actual_width") state.actual_width = parse_int(value);
        else if (key == "actual_height") state.actual_height = parse_int(value);
        else if (key == "actual_refresh") state.actual_refresh = parse_double(value);
    }

    return state;
}

void write_state_file(const ManagedDisplayState& state) {
    const auto values = to_map(state);
    std::ostringstream stream;
    for (const auto& [key, value] : values) {
        stream << key << '=' << value << '\n';
    }
    write_text_file(state.state_file, stream.str());
}

void update_state_file(const std::string& path, const std::vector<std::pair<std::string, std::string>>& changes) {
    auto values = to_map(read_state_file(path));
    for (const auto& [key, value] : changes) {
        values[key] = value;
    }
    std::ostringstream stream;
    for (const auto& [key, value] : values) {
        stream << key << '=' << value << '\n';
    }
    write_text_file(path, stream.str());
}

std::vector<std::string> list_state_files(const std::string& state_dir) {
    std::vector<std::string> files;
    if (!directory_exists(state_dir)) {
        return files;
    }
    for (const auto& entry : fs::directory_iterator(fs::path(state_dir))) {
        if (entry.is_regular_file() && entry.path().extension() == ".state") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string state_file_path(const std::string& state_dir, const std::string& name) {
    return (fs::path(state_dir) / (name + ".state")).string();
}

std::string log_file_path(const std::string& state_dir, const std::string& name) {
    return (fs::path(state_dir) / (name + ".log")).string();
}

}  // namespace evdi_control

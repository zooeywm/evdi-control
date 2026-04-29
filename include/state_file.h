#pragma once

#include <string>
#include <vector>

namespace evdi_control {

struct ManagedDisplayState {
    std::string name;
    pid_t pid = -1;
    std::string state_file;
    std::string log_file;
    std::string card_path;
    std::string output_name;
    int requested_width = 0;
    int requested_height = 0;
    double requested_refresh = 0.0;
    int actual_width = 0;
    int actual_height = 0;
    double actual_refresh = 0.0;
};

ManagedDisplayState read_state_file(const std::string& path);
void write_state_file(const ManagedDisplayState& state);
void update_state_file(const std::string& path, const std::vector<std::pair<std::string, std::string>>& changes);
std::vector<std::string> list_state_files(const std::string& state_dir);
std::string state_file_path(const std::string& state_dir, const std::string& name);
std::string log_file_path(const std::string& state_dir, const std::string& name);

}  // namespace evdi_control

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/types.h>

namespace evdi_control {

class CommandError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class EvdiError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CommandResult {
    int exit_code = 0;
    std::string stdout_text;
    std::string stderr_text;
};

CommandResult run_command(const std::vector<std::string>& command, bool check = true);
bool command_exists(const std::string& name);
std::string trim(std::string text);
bool file_exists(const std::string& path);
bool directory_exists(const std::string& path);
void ensure_directory(const std::string& path);
std::string runtime_state_dir();
bool process_alive(pid_t pid);
std::string read_text_file(const std::string& path);
void write_text_file(const std::string& path, const std::string& text);
std::string executable_path();
std::string read_link_target(const std::string& path);
std::optional<std::string> getenv_string(const char* key);
double parse_double(const std::string& value);
int parse_int(const std::string& value);
std::string shell_quote(const std::string& value);

}  // namespace evdi_control

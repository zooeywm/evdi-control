#include "util.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace evdi_control {

namespace {

std::string join_command(const std::vector<std::string>& command) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < command.size(); ++index) {
        if (index != 0) {
            stream << ' ';
        }
        stream << shell_quote(command[index]);
    }
    return stream.str();
}

std::string read_fd_all(int fd) {
    std::string output;
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t bytes = ::read(fd, buffer.data(), buffer.size());
        if (bytes == 0) {
            break;
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw CommandError("failed to read command output");
        }
        output.append(buffer.data(), static_cast<std::size_t>(bytes));
    }
    return output;
}

}  // namespace

CommandResult run_command(const std::vector<std::string>& command, bool check) {
    if (command.empty()) {
        throw CommandError("empty command");
    }

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
        throw CommandError("failed to create pipes");
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        throw CommandError("fork failed");
    }

    if (pid == 0) {
        ::dup2(stdout_pipe[1], STDOUT_FILENO);
        ::dup2(stderr_pipe[1], STDERR_FILENO);
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);

        std::vector<char*> argv;
        argv.reserve(command.size() + 1);
        for (const auto& part : command) {
            argv.push_back(const_cast<char*>(part.c_str()));
        }
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    CommandResult result;
    result.stdout_text = read_fd_all(stdout_pipe[0]);
    result.stderr_text = read_fd_all(stderr_pipe[0]);

    ::close(stdout_pipe[0]);
    ::close(stderr_pipe[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        throw CommandError("waitpid failed");
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = 1;
    }

    if (check && result.exit_code != 0) {
        const std::string details = trim(result.stderr_text.empty() ? result.stdout_text : result.stderr_text);
        throw CommandError(join_command(command) + " failed: " +
                           (details.empty() ? ("exit code " + std::to_string(result.exit_code)) : details));
    }

    return result;
}

bool command_exists(const std::string& name) {
    const auto path = getenv_string("PATH");
    if (!path.has_value()) {
        return false;
    }
    std::stringstream stream(*path);
    std::string directory;
    while (std::getline(stream, directory, ':')) {
        const fs::path candidate = fs::path(directory) / name;
        if (::access(candidate.c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
}

std::string trim(std::string text) {
    const auto is_not_space = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), is_not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), is_not_space).base(), text.end());
    return text;
}

bool file_exists(const std::string& path) {
    return fs::is_regular_file(fs::path(path));
}

bool directory_exists(const std::string& path) {
    return fs::is_directory(fs::path(path));
}

void ensure_directory(const std::string& path) {
    std::error_code error;
    fs::create_directories(fs::path(path), error);
    if (error) {
        throw CommandError("failed to create directory " + path + ": " + error.message());
    }
}

std::string runtime_state_dir() {
    if (const auto runtime_dir = getenv_string("XDG_RUNTIME_DIR")) {
        return *runtime_dir + "/evdi-control";
    }
    return "/tmp/evdi-control-" + std::to_string(geteuid());
}

bool process_alive(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    if (::kill(pid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

std::string read_text_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw CommandError("failed to read " + path);
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

void write_text_file(const std::string& path, const std::string& text) {
    std::ofstream output(path);
    if (!output) {
        throw CommandError("failed to write " + path);
    }
    output << text;
}

std::string executable_path() {
    std::array<char, 4096> buffer{};
    const ssize_t size = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size < 0) {
        throw CommandError("failed to resolve /proc/self/exe");
    }
    buffer[static_cast<std::size_t>(size)] = '\0';
    return std::string(buffer.data());
}

std::string read_link_target(const std::string& path) {
    std::array<char, 4096> buffer{};
    const ssize_t size = ::readlink(path.c_str(), buffer.data(), buffer.size() - 1);
    if (size < 0) {
        throw CommandError("failed to read symlink " + path);
    }
    buffer[static_cast<std::size_t>(size)] = '\0';
    return std::string(buffer.data());
}

std::optional<std::string> getenv_string(const char* key) {
    if (const char* value = std::getenv(key)) {
        return std::string(value);
    }
    return std::nullopt;
}

double parse_double(const std::string& value) {
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(value.c_str(), &end);
    if (errno != 0 || end == value.c_str() || *end != '\0') {
        throw CommandError("invalid number: " + value);
    }
    return parsed;
}

int parse_int(const std::string& value) {
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0') {
        throw CommandError("invalid integer: " + value);
    }
    return static_cast<int>(parsed);
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

}  // namespace evdi_control

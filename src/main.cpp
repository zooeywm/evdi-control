#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include <unistd.h>

#include "display_backend.h"
#include "evdi_loader.h"
#include "modeline.h"
#include "state_file.h"
#include "util.h"

namespace evdi_control {

namespace {

namespace fs = std::filesystem;

struct AddOptions {
    std::string name;
    int count = 1;
    int width = 1920;
    int height = 1080;
    double refresh = 60.0;
    bool reduced = false;
};

struct WorkerProcess {
    pid_t pid = -1;
    std::string name;
    std::string state_path;
};

std::vector<WorkerProcess> scan_worker_processes();
bool is_evdi_card_index(int card_index);
std::vector<int> used_evdi_card_indices();
int count_reusable_evdi_cards(const std::set<int>& reserved_indices);
bool can_add_evdi_device();
void ensure_add_capacity(int requested_count);
int select_or_allocate_evdi_device_index(const std::set<int>& reserved_indices);
void cleanup_created_displays(const std::vector<std::string>& names);

void print_help() {
    std::cout
        << "Usage:\n"
        << "  evdi-control add [--name NAME] [--count N] [--width W] [--height H] [--refresh HZ] [--reduced]\n"
        << "  evdi-control remove NAME [--force]\n"
        << "  evdi-control remove --all [--force]\n"
        << "  evdi-control list\n"
        << "  evdi-control outputs\n";
}

std::string next_display_name(const std::string& state_dir) {
    std::set<std::string> used_names;
    for (const auto& path : list_state_files(state_dir)) {
        used_names.insert(read_state_file(path).name);
    }
    for (const auto& worker : scan_worker_processes()) {
        used_names.insert(worker.name);
    }

    int index = 1;
    while (true) {
        const std::string name = "evdi" + std::to_string(index);
        if (used_names.find(name) == used_names.end()) {
            return name;
        }
        ++index;
    }
}

bool is_evdi_card_index(int card_index) {
    const fs::path link = fs::path("/sys/class/drm") / ("card" + std::to_string(card_index));
    if (!fs::is_symlink(link)) {
        return false;
    }
    return fs::read_symlink(link).string().find("/platform/evdi.") != std::string::npos;
}

std::vector<int> used_evdi_card_indices() {
    std::vector<int> indices;
    for (const auto& path : list_state_files(runtime_state_dir())) {
        const auto state = read_state_file(path);
        if (!process_alive(state.pid)) {
            continue;
        }
        const auto pos = state.card_path.find("/dev/dri/card");
        if (pos == std::string::npos) {
            continue;
        }
        indices.push_back(parse_int(state.card_path.substr(pos + std::string("/dev/dri/card").size())));
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

int count_reusable_evdi_cards(const std::set<int>& reserved_indices) {
    const auto used_indices = used_evdi_card_indices();
    int count = 0;
    for (const auto& output : list_drm_outputs()) {
        if (output.name.rfind("card", 0) != 0) {
            continue;
        }
        const auto dash = output.name.find('-');
        if (dash == std::string::npos) {
            continue;
        }
        const int card_index = parse_int(output.name.substr(4, dash - 4));
        if (!is_evdi_card_index(card_index)) {
            continue;
        }
        if (output.connected || output.enabled) {
            continue;
        }
        if (std::find(used_indices.begin(), used_indices.end(), card_index) != used_indices.end()) {
            continue;
        }
        if (reserved_indices.find(card_index) != reserved_indices.end()) {
            continue;
        }
        ++count;
    }
    return count;
}

bool can_add_evdi_device() {
    return ::access("/sys/devices/evdi/add", W_OK) == 0;
}

void ensure_add_capacity(int requested_count) {
    const int reusable_count = count_reusable_evdi_cards({});
    if (requested_count <= reusable_count || can_add_evdi_device()) {
        return;
    }

    std::ostringstream message;
    message << "requested " << requested_count << " displays, but only "
            << reusable_count << " reusable evdi device";
    if (reusable_count != 1) {
        message << 's';
    }
    message << (reusable_count == 1 ? " is" : " are")
            << " available and current user cannot create additional devices via /sys/devices/evdi/add. "
            << "Re-run with sudo or load the evdi module with a larger initial_device_count.";
    throw CommandError(message.str());
}

int select_or_allocate_evdi_device_index(const std::set<int>& reserved_indices) {
    const auto used_indices = used_evdi_card_indices();
    for (const auto& output : list_drm_outputs()) {
        if (output.name.rfind("card", 0) != 0) {
            continue;
        }
        const auto dash = output.name.find('-');
        if (dash == std::string::npos) {
            continue;
        }
        const int card_index = parse_int(output.name.substr(4, dash - 4));
        if (!is_evdi_card_index(card_index)) {
            continue;
        }
        if (output.connected || output.enabled) {
            continue;
        }
        if (std::find(used_indices.begin(), used_indices.end(), card_index) != used_indices.end()) {
            continue;
        }
        if (reserved_indices.find(card_index) != reserved_indices.end()) {
            continue;
        }
        return card_index;
    }

    return allocate_evdi_device_index();
}

void cleanup_created_displays(const std::vector<std::string>& names) {
    std::set<pid_t> killed;
    for (const auto& name : names) {
        const std::string state_path = state_file_path(runtime_state_dir(), name);
        if (file_exists(state_path)) {
            const auto state = read_state_file(state_path);
            if (process_alive(state.pid) && killed.insert(state.pid).second) {
                ::kill(state.pid, SIGTERM);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                if (process_alive(state.pid)) {
                    ::kill(state.pid, SIGKILL);
                }
            }
            ::unlink(state_path.c_str());
            if (!state.log_file.empty()) {
                ::unlink(state.log_file.c_str());
            }
        }
    }
}

std::vector<std::string> split_cmdline(const std::string& raw) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : raw) {
        if (ch == '\0') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

std::vector<WorkerProcess> scan_worker_processes() {
    std::vector<WorkerProcess> workers;
    const std::string executable = executable_path();
    const fs::path proc_root("/proc");
    for (const auto& entry : fs::directory_iterator(proc_root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string dirname = entry.path().filename().string();
        if (dirname.empty() || !std::all_of(dirname.begin(), dirname.end(), ::isdigit)) {
            continue;
        }

        const fs::path cmdline_path = entry.path() / "cmdline";
        std::ifstream input(cmdline_path, std::ios::binary);
        if (!input) {
            continue;
        }
        const std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const auto parts = split_cmdline(raw);
        if (parts.size() < 4) {
            continue;
        }
        if (parts[0] != executable || parts[1] != "_serve") {
            continue;
        }

        WorkerProcess worker;
        worker.pid = static_cast<pid_t>(parse_int(dirname));
        worker.name = parts[2];
        worker.state_path = parts[3];
        workers.push_back(worker);
    }
    return workers;
}

void ensure_name_available(const std::string& state_dir, const std::string& name) {
    const std::string state_path = state_file_path(state_dir, name);
    if (file_exists(state_path)) {
        throw CommandError("managed display name already exists: " + name);
    }
    for (const auto& worker : scan_worker_processes()) {
        if (worker.name == name && process_alive(worker.pid)) {
            throw CommandError("managed display name already exists in running worker: " + name);
        }
    }
}

void wait_for_worker_state(const std::string& state_path, pid_t worker_pid, const std::string& log_path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (file_exists(state_path)) {
            return;
        }
        if (!process_alive(worker_pid)) {
            const std::string details = file_exists(log_path) ? trim(read_text_file(log_path)) : "";
            throw CommandError("worker exited early: " + details);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    throw CommandError("timed out waiting for worker state file");
}

pid_t spawn_worker(const std::string& name,
                   const std::string& state_dir,
                   int device_index,
                   int width,
                   int height,
                   double refresh,
                   bool reduced) {
    const std::string state_path = state_file_path(state_dir, name);
    const std::string log_path = log_file_path(state_dir, name);
    const std::string executable = executable_path();

    const pid_t pid = ::fork();
    if (pid < 0) {
        throw CommandError("fork failed");
    }

    if (pid == 0) {
        ::setsid();
        const int log_fd = ::open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (log_fd >= 0) {
            ::dup2(log_fd, STDOUT_FILENO);
            ::dup2(log_fd, STDERR_FILENO);
            ::close(log_fd);
        }

        std::vector<std::string> args = {
            executable,
            "_serve",
            name,
            state_path,
            std::to_string(device_index),
            std::to_string(width),
            std::to_string(height),
            std::to_string(refresh),
        };
        if (reduced) {
            args.push_back("--reduced");
        }

        std::vector<char*> argv;
        for (auto& part : args) {
            argv.push_back(part.data());
        }
        argv.push_back(nullptr);
        ::execv(argv[0], argv.data());
        _exit(127);
    }

    wait_for_worker_state(state_path, pid, log_path);
    return pid;
}

AddOptions parse_add(int argc, char** argv) {
    AddOptions options;
    for (int index = 2; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--name") {
            options.name = argv[++index];
        } else if (arg == "--count") {
            options.count = parse_int(argv[++index]);
        } else if (arg == "--width") {
            options.width = parse_int(argv[++index]);
        } else if (arg == "--height") {
            options.height = parse_int(argv[++index]);
        } else if (arg == "--refresh") {
            options.refresh = parse_double(argv[++index]);
        } else if (arg == "--reduced") {
            options.reduced = true;
        } else {
            throw CommandError("unexpected add argument: " + arg);
        }
    }
    return options;
}

std::map<std::string, std::string> managed_output_names() {
    std::map<std::string, std::string> names;
    for (const auto& path : list_state_files(runtime_state_dir())) {
        const auto state = read_state_file(path);
        if (!state.output_name.empty()) {
            names[state.output_name] = state.name;
        }
    }
    return names;
}

void command_outputs() {
    const auto managed_names = managed_output_names();
    for (const auto& output : list_drm_outputs()) {
        std::cout << output.name;
        const auto found = managed_names.find(output.name);
        if (found != managed_names.end()) {
            std::cout << " (managed as " << found->second << ')';
        }
        std::cout << ": " << (output.connected ? "connected" : "disconnected") << '\n';
        std::cout << "  enabled: " << (output.enabled ? "yes" : "no") << '\n';
        for (const auto& mode : output.modes) {
            std::cout << "  " << (mode.current ? '*' : ' ')
                      << ' ' << mode.width << 'x' << mode.height
                      << " @ " << mode.refresh_hz << "Hz";
            if (mode.preferred) {
                std::cout << " [preferred]";
            }
            if (mode.custom) {
                std::cout << " [custom]";
            }
            std::cout << '\n';
        }
    }
}

void command_list() {
    const auto live_outputs = list_drm_outputs();
    const auto state_files = list_state_files(runtime_state_dir());
    if (state_files.empty()) {
        std::cout << "No managed displays in " << runtime_state_dir() << ".\n";
        return;
    }

    for (const auto& path : state_files) {
        const auto state = read_state_file(path);
        std::cout << state.name << ": pid=" << state.pid
                  << " status=" << (process_alive(state.pid) ? "running" : "stale")
                  << " output=" << (state.output_name.empty() ? "unknown" : state.output_name)
                  << " requested=" << state.requested_width << 'x' << state.requested_height
                  << '@' << state.requested_refresh << "Hz\n";
        for (const auto& output : live_outputs) {
            if (output.name != state.output_name) {
                continue;
            }
            for (const auto& mode : output.modes) {
                if (mode.current) {
                    std::cout << "  live_mode=" << mode.width << 'x' << mode.height
                              << '@' << mode.refresh_hz << "Hz\n";
                    break;
                }
            }
        }
        std::cout << "  state_file=" << path << '\n';
    }
}

void command_add(int argc, char** argv) {
    const auto options = parse_add(argc, argv);
    if (options.count < 1) {
        throw CommandError("--count must be at least 1");
    }
    if (options.count > 1 && !options.name.empty()) {
        throw CommandError("--name can only be used when --count is 1");
    }

    const std::string state_dir = runtime_state_dir();
    ensure_directory(state_dir);
    std::vector<std::string> created_names;
    std::vector<std::string> pending_output_lines;
    std::set<int> reserved_indices;

    try {
        ensure_add_capacity(options.count);
        for (int index = 0; index < options.count; ++index) {
            const std::string name = (options.count == 1 && !options.name.empty()) ? options.name : next_display_name(state_dir);
            ensure_name_available(state_dir, name);
            const Modeline modeline = generate_modeline(options.width, options.height, options.refresh, options.reduced);
            const auto before_outputs = list_drm_outputs();
            const int device_index = select_or_allocate_evdi_device_index(reserved_indices);
            reserved_indices.insert(device_index);
            (void)spawn_worker(name, state_dir, device_index, options.width, options.height, options.refresh, options.reduced);
            created_names.push_back(name);
            const auto output_name = detect_new_drm_output(before_outputs, std::chrono::seconds(8));
            if (!output_name.has_value()) {
                throw CommandError("failed to detect the new output");
            }

            update_state_file(
                state_file_path(state_dir, name),
                {
                    {"output_name", *output_name},
                    {"actual_width", std::to_string(modeline.hdisplay)},
                    {"actual_height", std::to_string(modeline.vdisplay)},
                    {"actual_refresh", std::to_string(modeline.refresh_hz)},
                });

            std::ostringstream line;
            line << "Created " << name << ": output=" << *output_name
                 << " mode=" << modeline.hdisplay << 'x' << modeline.vdisplay
                 << '@' << modeline.refresh_hz << "Hz";
            pending_output_lines.push_back(line.str());
        }
    } catch (const std::exception& error) {
        const std::size_t rolled_back_count = created_names.size();
        cleanup_created_displays(created_names);
        if (rolled_back_count != 0U) {
            throw CommandError(std::string(error.what()) + ". Rolled back " +
                               std::to_string(rolled_back_count) + " created display(s).");
        }
        throw;
    }

    for (const auto& line : pending_output_lines) {
        std::cout << line << '\n';
    }
}

void command_remove(int argc, char** argv) {
    bool remove_all = false;
    bool force = false;
    std::string name;
    for (int index = 2; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--all") {
            remove_all = true;
        } else if (arg == "--force") {
            force = true;
        } else if (name.empty()) {
            name = arg;
        } else {
            throw CommandError("unexpected remove argument: " + arg);
        }
    }

    std::vector<std::string> targets;
    if (remove_all) {
        targets = list_state_files(runtime_state_dir());
    } else {
        if (name.empty()) {
            throw CommandError("remove requires NAME or --all");
        }
        targets.push_back(state_file_path(runtime_state_dir(), name));
    }

    if (targets.empty()) {
        if (!remove_all) {
            throw CommandError("unknown managed display: " + name);
        }
    }

    std::map<pid_t, WorkerProcess> workers_by_pid;
    for (const auto& worker : scan_worker_processes()) {
        workers_by_pid[worker.pid] = worker;
    }

    std::set<pid_t> killed_pids;
    for (const auto& path : targets) {
        if (!file_exists(path)) {
            continue;
        }

        const auto state = read_state_file(path);
        if (process_alive(state.pid)) {
            ::kill(state.pid, SIGTERM);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (std::chrono::steady_clock::now() < deadline) {
                if (!process_alive(state.pid)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (process_alive(state.pid)) {
                if (!force) {
                    throw CommandError("worker did not exit after SIGTERM, re-run with --force");
                }
                ::kill(state.pid, SIGKILL);
            }
            killed_pids.insert(state.pid);
        }
        ::unlink(path.c_str());
        if (!state.log_file.empty()) {
            ::unlink(state.log_file.c_str());
        }
        std::cout << "Removed " << state.name << '\n';
    }

    for (const auto& [pid, worker] : workers_by_pid) {
        if (killed_pids.find(pid) != killed_pids.end()) {
            continue;
        }
        if (!remove_all && worker.name != name) {
            continue;
        }
        if (!process_alive(pid)) {
            continue;
        }

        ::kill(pid, SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            if (!process_alive(pid)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (process_alive(pid)) {
            if (!force) {
                throw CommandError("worker did not exit after SIGTERM, re-run with --force");
            }
            ::kill(pid, SIGKILL);
        }

        if (!worker.state_path.empty()) {
            ::unlink(worker.state_path.c_str());
        }
        ::unlink(log_file_path(runtime_state_dir(), worker.name).c_str());
        std::cout << "Removed orphan worker " << worker.name << '\n';
    }

    if (!remove_all && targets.empty() && killed_pids.empty()) {
        throw CommandError("unknown managed display: " + name);
    }

    if (remove_all && targets.empty() && workers_by_pid.empty()) {
        std::cout << "No managed displays to remove.\n";
    }
}

void command_serve(int argc, char** argv) {
    if (argc < 8) {
        throw CommandError("_serve requires NAME STATE_FILE DEVICE_INDEX WIDTH HEIGHT REFRESH");
    }
    const bool reduced = argc > 8 && std::string(argv[8]) == "--reduced";
    serve_virtual_display(
        {
            argv[2],
            argv[3],
            parse_int(argv[4]),
            parse_int(argv[5]),
            parse_int(argv[6]),
            parse_double(argv[7]),
            reduced,
        });
}

}  // namespace

int real_main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    const std::string command = argv[1];
    if (command == "--help" || command == "-h") {
        print_help();
        return 0;
    }
    if (command == "add") {
        command_add(argc, argv);
        return 0;
    }
    if (command == "remove") {
        command_remove(argc, argv);
        return 0;
    }
    if (command == "list") {
        command_list();
        return 0;
    }
    if (command == "outputs") {
        command_outputs();
        return 0;
    }
    if (command == "set-mode" || command == "disable-output") {
        throw CommandError(command + " is intentionally unsupported. evdi-control only creates/removes EVDI displays and advertises modes via EDID.");
    }
    if (command == "_serve") {
        command_serve(argc, argv);
        return 0;
    }

    throw CommandError("unknown command: " + command);
}

}  // namespace evdi_control

int main(int argc, char** argv) {
    try {
        return evdi_control::real_main(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

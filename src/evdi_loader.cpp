#include "evdi_loader.h"

#include <csignal>
#include <dlfcn.h>
#include <filesystem>
#include <sys/select.h>
#include <unistd.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <vector>

#include "edid.h"
#include "modeline.h"
#include "state_file.h"

namespace evdi_control {

namespace {

namespace fs = std::filesystem;

std::atomic<bool> g_stop_requested{false};

void stop_handler(int) {
    g_stop_requested.store(true);
}

void noop_dpms(int, void*) {}
void noop_crtc(int, void*) {}
void noop_cursor_set(evdi_cursor_set, void*) {}
void noop_cursor_move(evdi_cursor_move, void*) {}
void noop_ddcci(evdi_ddcci_data, void*) {}

struct PainterContext {
    std::string state_file;
    EvdiLibrary* library = nullptr;
    evdi_handle handle = EVDI_INVALID_HANDLE;
    int mode_width = 0;
    int mode_height = 0;
    bool mode_dirty = true;
    bool update_ready = false;
    bool update_requested = false;
    bool buffer_registered = false;
    int buffer_id = 1;
    std::vector<std::uint8_t> framebuffer;
    std::vector<evdi_rect> rects;
};

void mode_changed_handler(evdi_mode mode, void* user_data) {
    auto* context = static_cast<PainterContext*>(user_data);
    update_state_file(
        context->state_file,
        {
            {"actual_width", std::to_string(mode.width)},
            {"actual_height", std::to_string(mode.height)},
            {"actual_refresh", std::to_string(mode.refresh_rate)},
        });
    context->mode_width = mode.width;
    context->mode_height = mode.height;
    context->mode_dirty = true;
}

void update_ready_handler(int, void* user_data) {
    auto* context = static_cast<PainterContext*>(user_data);
    context->update_ready = true;
}

void configure_buffer_if_needed(PainterContext& context) {
    if (!context.mode_dirty) {
        return;
    }
    if (context.mode_width <= 0 || context.mode_height <= 0) {
        return;
    }

    if (context.buffer_registered) {
        context.library->unregister_buffer(context.handle, context.buffer_id);
        context.buffer_registered = false;
    }

    const int stride = context.mode_width * 4;
    context.framebuffer.assign(static_cast<std::size_t>(stride) * static_cast<std::size_t>(context.mode_height), 0);
    context.rects.assign(16, evdi_rect{});

    evdi_buffer buffer{};
    buffer.id = context.buffer_id;
    buffer.buffer = context.framebuffer.data();
    buffer.width = context.mode_width;
    buffer.height = context.mode_height;
    buffer.stride = stride;
    buffer.rects = context.rects.data();
    buffer.rect_count = static_cast<int>(context.rects.size());

    context.library->register_buffer(context.handle, buffer);
    context.buffer_registered = true;
    context.mode_dirty = false;
    context.update_requested = false;
    context.update_ready = false;
}

void request_update_if_needed(PainterContext& context) {
    if (!context.buffer_registered || context.update_requested) {
        return;
    }
    context.update_requested = true;
    if (context.library->request_update(context.handle, context.buffer_id)) {
        context.update_ready = true;
    }
}

void consume_update_if_ready(PainterContext& context) {
    if (!context.buffer_registered || !context.update_ready) {
        return;
    }
    int rect_count = static_cast<int>(context.rects.size());
    context.library->grab_pixels(context.handle, context.rects.data(), &rect_count);
    context.update_ready = false;
    context.update_requested = false;
}

}  // namespace

EvdiLibrary::EvdiLibrary() {
    for (const char* name : {"libevdi.so.1", "libevdi.so"}) {
        library_handle_ = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (library_handle_ != nullptr) {
            break;
        }
    }
    if (library_handle_ == nullptr) {
        throw EvdiError("failed to load libevdi runtime library");
    }

    load_symbol(open_attached_to_fixed_, "evdi_open_attached_to_fixed");
    load_symbol(open_, "evdi_open");
    load_symbol(add_device_, "evdi_add_device");
    load_symbol(close_, "evdi_close");
    load_symbol(connect_, "evdi_connect");
    load_symbol(disconnect_, "evdi_disconnect");
    load_symbol(register_buffer_, "evdi_register_buffer");
    load_symbol(unregister_buffer_, "evdi_unregister_buffer");
    load_symbol(request_update_, "evdi_request_update");
    load_symbol(grab_pixels_, "evdi_grab_pixels");
    load_symbol(get_event_ready_, "evdi_get_event_ready");
    load_symbol(handle_events_, "evdi_handle_events");
}

EvdiLibrary::~EvdiLibrary() {
    if (library_handle_ != nullptr) {
        ::dlclose(library_handle_);
    }
}

template <typename T>
void EvdiLibrary::load_symbol(T& slot, const char* name) {
    ::dlerror();
    slot = reinterpret_cast<T>(::dlsym(library_handle_, name));
    if (const char* error = ::dlerror()) {
        throw EvdiError(std::string("failed to load symbol ") + name + ": " + error);
    }
}

evdi_handle EvdiLibrary::open_virtual_display() {
    evdi_handle handle = open_attached_to_fixed_(nullptr, 0);
    if (handle == EVDI_INVALID_HANDLE) {
        throw EvdiError("evdi_open_attached_to_fixed failed");
    }
    return handle;
}

evdi_handle EvdiLibrary::open_device(int device_index) {
    evdi_handle handle = open_(device_index);
    if (handle == EVDI_INVALID_HANDLE) {
        throw EvdiError("evdi_open failed for card" + std::to_string(device_index));
    }
    return handle;
}

int EvdiLibrary::add_device() const {
    const int result = add_device_();
    if (result <= 0) {
        throw EvdiError("evdi_add_device failed. Creating additional evdi devices usually requires write access to /sys/devices/evdi/add.");
    }
    return result;
}

void EvdiLibrary::close(evdi_handle handle) const {
    close_(handle);
}

void EvdiLibrary::connect(evdi_handle handle,
                          const unsigned char* edid,
                          unsigned int edid_length,
                          std::uint32_t area_limit) const {
    connect_(handle, edid, edid_length, area_limit);
}

void EvdiLibrary::disconnect(evdi_handle handle) const {
    disconnect_(handle);
}

void EvdiLibrary::register_buffer(evdi_handle handle, const evdi_buffer& buffer) const {
    register_buffer_(handle, buffer);
}

void EvdiLibrary::unregister_buffer(evdi_handle handle, int buffer_id) const {
    unregister_buffer_(handle, buffer_id);
}

bool EvdiLibrary::request_update(evdi_handle handle, int buffer_id) const {
    return request_update_(handle, buffer_id);
}

void EvdiLibrary::grab_pixels(evdi_handle handle, evdi_rect* rects, int* num_rects) const {
    grab_pixels_(handle, rects, num_rects);
}

int EvdiLibrary::get_event_ready(evdi_handle handle) const {
    return get_event_ready_(handle);
}

void EvdiLibrary::handle_events(evdi_handle handle, evdi_event_context* context) const {
    handle_events_(handle, context);
}

std::vector<int> current_evdi_card_indices() {
    std::vector<int> indices;
    const fs::path drm_root("/sys/class/drm");
    if (!fs::exists(drm_root)) {
        return indices;
    }

    for (const auto& entry : fs::directory_iterator(drm_root)) {
        if (!entry.is_symlink()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind("card", 0) != 0 || name.find('-') != std::string::npos) {
            continue;
        }
        const std::string target = fs::read_symlink(entry.path()).string();
        if (target.find("/platform/evdi.") == std::string::npos) {
            continue;
        }
        indices.push_back(parse_int(name.substr(4)));
    }

    std::sort(indices.begin(), indices.end());
    return indices;
}

int allocate_evdi_device_index() {
    EvdiLibrary library;
    const auto before = current_evdi_card_indices();
    library.add_device();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto after = current_evdi_card_indices();
        for (int index : after) {
            if (std::find(before.begin(), before.end(), index) == before.end()) {
                return index;
            }
        }
        ::usleep(100000);
    }

    throw CommandError("timed out waiting for a new evdi card after evdi_add_device");
}

int serve_virtual_display(const ServeOptions& options) {
    if (!directory_exists("/sys/module/evdi")) {
        throw CommandError("evdi kernel module is not loaded. Run `sudo modprobe evdi` first.");
    }

    g_stop_requested.store(false);
    ::signal(SIGTERM, stop_handler);
    ::signal(SIGINT, stop_handler);

    const Modeline modeline = generate_modeline(options.requested_width, options.requested_height, options.requested_refresh,
                                                options.reduced);
    const auto edid = build_edid(modeline);

    EvdiLibrary library;
    const evdi_handle handle = options.device_index >= 0
        ? library.open_device(options.device_index)
        : library.open_virtual_display();
    const int ready_fd = library.get_event_ready(handle);

    ManagedDisplayState state;
    state.name = options.name;
    state.pid = getpid();
    state.state_file = options.state_file;
    state.log_file = log_file_path(runtime_state_dir(), options.name);
    state.card_path = read_link_target("/proc/self/fd/" + std::to_string(ready_fd));
    state.requested_width = options.requested_width;
    state.requested_height = options.requested_height;
    state.requested_refresh = options.requested_refresh;
    state.actual_width = modeline.hdisplay;
    state.actual_height = modeline.vdisplay;
    state.actual_refresh = modeline.refresh_hz;
    write_state_file(state);

    library.connect(handle, edid.data(), static_cast<unsigned int>(edid.size()),
                    static_cast<std::uint32_t>(modeline.hdisplay * modeline.vdisplay));

    PainterContext painter_context;
    painter_context.state_file = options.state_file;
    painter_context.library = &library;
    painter_context.handle = handle;
    painter_context.mode_width = modeline.hdisplay;
    painter_context.mode_height = modeline.vdisplay;
    painter_context.mode_dirty = true;

    evdi_event_context context{};
    context.dpms_handler = noop_dpms;
    context.mode_changed_handler = mode_changed_handler;
    context.update_ready_handler = update_ready_handler;
    context.crtc_state_handler = noop_crtc;
    context.cursor_set_handler = noop_cursor_set;
    context.cursor_move_handler = noop_cursor_move;
    context.ddcci_data_handler = noop_ddcci;
    context.user_data = &painter_context;

    configure_buffer_if_needed(painter_context);
    request_update_if_needed(painter_context);

    while (!g_stop_requested.load()) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(ready_fd, &readfds);
        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        const int result = ::select(ready_fd + 1, &readfds, nullptr, nullptr, &timeout);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (result > 0 && FD_ISSET(ready_fd, &readfds)) {
            library.handle_events(handle, &context);
            configure_buffer_if_needed(painter_context);
            consume_update_if_ready(painter_context);
            request_update_if_needed(painter_context);
        }
    }

    if (painter_context.buffer_registered) {
        library.unregister_buffer(handle, painter_context.buffer_id);
    }
    library.disconnect(handle);
    library.close(handle);
    ::unlink(options.state_file.c_str());
    return 0;
}

}  // namespace evdi_control

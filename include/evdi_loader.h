#pragma once

#include <cstdint>
#include <string>

#include "third_party/evdi/evdi_lib.h"
#include "util.h"

namespace evdi_control {

class EvdiLibrary {
public:
    EvdiLibrary();
    ~EvdiLibrary();

    evdi_handle open_virtual_display();
    evdi_handle open_device(int device_index);
    int add_device() const;
    void close(evdi_handle handle) const;
    void connect(evdi_handle handle, const unsigned char* edid, unsigned int edid_length, std::uint32_t area_limit) const;
    void disconnect(evdi_handle handle) const;
    void register_buffer(evdi_handle handle, const evdi_buffer& buffer) const;
    void unregister_buffer(evdi_handle handle, int buffer_id) const;
    bool request_update(evdi_handle handle, int buffer_id) const;
    void grab_pixels(evdi_handle handle, evdi_rect* rects, int* num_rects) const;
    int get_event_ready(evdi_handle handle) const;
    void handle_events(evdi_handle handle, evdi_event_context* context) const;

private:
    template <typename T>
    void load_symbol(T& slot, const char* name);

    void* library_handle_ = nullptr;
    evdi_handle (*open_attached_to_fixed_)(const char*, size_t) = nullptr;
    evdi_handle (*open_)(int) = nullptr;
    int (*add_device_)() = nullptr;
    void (*close_)(evdi_handle) = nullptr;
    void (*connect_)(evdi_handle, const unsigned char*, unsigned int, std::uint32_t) = nullptr;
    void (*disconnect_)(evdi_handle) = nullptr;
    void (*register_buffer_)(evdi_handle, evdi_buffer) = nullptr;
    void (*unregister_buffer_)(evdi_handle, int) = nullptr;
    bool (*request_update_)(evdi_handle, int) = nullptr;
    void (*grab_pixels_)(evdi_handle, evdi_rect*, int*) = nullptr;
    evdi_selectable (*get_event_ready_)(evdi_handle) = nullptr;
    void (*handle_events_)(evdi_handle, struct evdi_event_context*) = nullptr;
};

struct ServeOptions {
    std::string name;
    std::string state_file;
    int device_index = -1;
    int requested_width = 0;
    int requested_height = 0;
    double requested_refresh = 0.0;
    bool reduced = false;
};

int allocate_evdi_device_index();
int serve_virtual_display(const ServeOptions& options);

}  // namespace evdi_control

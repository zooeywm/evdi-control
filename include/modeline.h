#pragma once

#include <string>

namespace evdi_control {

struct Modeline {
    std::string name;
    double pixel_clock_mhz = 0.0;
    int hdisplay = 0;
    int hsync_start = 0;
    int hsync_end = 0;
    int htotal = 0;
    int vdisplay = 0;
    int vsync_start = 0;
    int vsync_end = 0;
    int vtotal = 0;
    bool hsync_positive = false;
    bool vsync_positive = false;
    double refresh_hz = 0.0;
};

Modeline generate_modeline(int width, int height, double refresh_hz, bool reduced);

}  // namespace evdi_control

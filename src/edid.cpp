#include "edid.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace evdi_control {

namespace {

std::pair<std::uint8_t, std::uint8_t> manufacturer_code(const std::string& name) {
    if (name.size() != 3) {
        throw std::runtime_error("manufacturer code must have three letters");
    }

    int encoded = 0;
    for (char ch : name) {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        encoded = (encoded << 5) | (upper - 'A' + 1);
    }
    return {static_cast<std::uint8_t>((encoded >> 8) & 0xFF), static_cast<std::uint8_t>(encoded & 0xFF)};
}

std::array<std::uint8_t, 18> dtd_from_modeline(const Modeline& modeline, int width_mm, int height_mm) {
    const int hblank = modeline.htotal - modeline.hdisplay;
    const int vblank = modeline.vtotal - modeline.vdisplay;
    const int hsync_offset = modeline.hsync_start - modeline.hdisplay;
    const int hsync_pulse = modeline.hsync_end - modeline.hsync_start;
    const int vsync_offset = modeline.vsync_start - modeline.vdisplay;
    const int vsync_pulse = modeline.vsync_end - modeline.vsync_start;
    const int pixel_clock_10khz = static_cast<int>(std::lround(modeline.pixel_clock_mhz * 100.0));

    std::uint8_t flags = 0x18;
    if (modeline.hsync_positive) {
        flags |= 0x02;
    }
    if (modeline.vsync_positive) {
        flags |= 0x04;
    }

    return {
        static_cast<std::uint8_t>(pixel_clock_10khz & 0xFF),
        static_cast<std::uint8_t>((pixel_clock_10khz >> 8) & 0xFF),
        static_cast<std::uint8_t>(modeline.hdisplay & 0xFF),
        static_cast<std::uint8_t>(hblank & 0xFF),
        static_cast<std::uint8_t>(((modeline.hdisplay >> 8) & 0xF) << 4 | ((hblank >> 8) & 0xF)),
        static_cast<std::uint8_t>(modeline.vdisplay & 0xFF),
        static_cast<std::uint8_t>(vblank & 0xFF),
        static_cast<std::uint8_t>(((modeline.vdisplay >> 8) & 0xF) << 4 | ((vblank >> 8) & 0xF)),
        static_cast<std::uint8_t>(hsync_offset & 0xFF),
        static_cast<std::uint8_t>(hsync_pulse & 0xFF),
        static_cast<std::uint8_t>(((vsync_offset & 0xF) << 4) | (vsync_pulse & 0xF)),
        static_cast<std::uint8_t>(((hsync_offset >> 8) & 0x3) << 6 |
                                  ((hsync_pulse >> 8) & 0x3) << 4 |
                                  ((vsync_offset >> 4) & 0x3) << 2 |
                                  ((vsync_pulse >> 4) & 0x3)),
        static_cast<std::uint8_t>(width_mm & 0xFF),
        static_cast<std::uint8_t>(height_mm & 0xFF),
        static_cast<std::uint8_t>(((width_mm >> 8) & 0xF) << 4 | ((height_mm >> 8) & 0xF)),
        0x00,
        0x00,
        flags,
    };
}

std::array<std::uint8_t, 18> text_descriptor(std::uint8_t tag, const std::string& text) {
    std::array<std::uint8_t, 18> descriptor{};
    descriptor[3] = tag;

    std::string payload = text.substr(0, 13);
    if (payload.size() < 13) {
        if (payload.size() < 12) {
            payload.push_back('\n');
        }
        payload.resize(13, ' ');
    }
    std::copy(payload.begin(), payload.end(), descriptor.begin() + 5);
    return descriptor;
}

}  // namespace

std::vector<std::uint8_t> build_edid(const Modeline& modeline) {
    const int width_mm = std::max(160, static_cast<int>(std::lround(modeline.hdisplay * 0.2646)));
    const int height_mm = std::max(90, static_cast<int>(std::lround(modeline.vdisplay * 0.2646)));
    const auto [manufacturer_hi, manufacturer_lo] = manufacturer_code("EVC");

    std::vector<std::uint8_t> edid(128, 0);
    const auto dtd = dtd_from_modeline(modeline, width_mm, height_mm);
    const auto name = text_descriptor(0xFC, "EVDI Virtual");
    const auto serial = text_descriptor(0xFF, "EV000001");
    const std::array<std::uint8_t, 18> range = {
        0x00, 0x00, 0x00, 0xFD, 0x00, 0x18, 0x90, 0x0F, 0xFF,
        0x3C, 0x01, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    };

    edid[0] = 0x00;
    edid[1] = 0xFF;
    edid[2] = 0xFF;
    edid[3] = 0xFF;
    edid[4] = 0xFF;
    edid[5] = 0xFF;
    edid[6] = 0xFF;
    edid[7] = 0x00;
    edid[8] = manufacturer_hi;
    edid[9] = manufacturer_lo;
    edid[10] = 0x34;
    edid[11] = 0x12;
    edid[12] = 0x01;
    edid[16] = 0x1A;
    edid[17] = 0x01;
    edid[18] = 0x03;
    edid[19] = 0x80;
    edid[20] = static_cast<std::uint8_t>(std::min(255, width_mm / 10));
    edid[21] = static_cast<std::uint8_t>(std::min(255, height_mm / 10));
    edid[22] = 0x78;
    edid[23] = 0x0A;
    edid[24] = 0xEE;
    edid[25] = 0x95;
    edid[26] = 0xA3;
    edid[27] = 0x54;
    edid[28] = 0x4C;
    edid[29] = 0x99;
    edid[30] = 0x26;
    edid[31] = 0x0F;
    for (int index = 36; index <= 53; ++index) {
        edid[static_cast<std::size_t>(index)] = 0x01;
    }

    std::copy(dtd.begin(), dtd.end(), edid.begin() + 54);
    std::copy(name.begin(), name.end(), edid.begin() + 72);
    std::copy(serial.begin(), serial.end(), edid.begin() + 90);
    std::copy(range.begin(), range.end(), edid.begin() + 108);

    std::uint32_t sum = 0;
    for (std::size_t index = 0; index < 127; ++index) {
        sum += edid[index];
    }
    edid[127] = static_cast<std::uint8_t>((256 - (sum & 0xFF)) & 0xFF);
    return edid;
}

}  // namespace evdi_control

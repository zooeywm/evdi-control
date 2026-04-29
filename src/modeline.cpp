#include "modeline.h"

#include <regex>
#include <sstream>

#include "util.h"

namespace evdi_control {

Modeline generate_modeline(int width, int height, double refresh_hz, bool reduced) {
    std::vector<std::string> command = {"cvt"};
    if (reduced) {
        command.push_back("--reduced");
    }
    command.push_back(std::to_string(width));
    command.push_back(std::to_string(height));

    std::ostringstream refresh_text;
    refresh_text << refresh_hz;
    command.push_back(refresh_text.str());

    const auto result = run_command(command);
    const std::regex pattern(R"modeline(^Modeline\s+"([^"]+)"\s+(\S+)\s+(.+)$)modeline");
    std::smatch match;

    std::stringstream stream(result.stdout_text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!std::regex_match(line, match, pattern)) {
            continue;
        }

        Modeline modeline;
        modeline.name = match[1].str();
        modeline.pixel_clock_mhz = parse_double(match[2].str());

        std::stringstream body(match[3].str());
        body >> modeline.hdisplay >> modeline.hsync_start >> modeline.hsync_end >> modeline.htotal;
        body >> modeline.vdisplay >> modeline.vsync_start >> modeline.vsync_end >> modeline.vtotal;
        std::string hsync_token;
        std::string vsync_token;
        body >> hsync_token >> vsync_token;
        modeline.hsync_positive = hsync_token == "+hsync";
        modeline.vsync_positive = vsync_token == "+vsync";
        modeline.refresh_hz = refresh_hz;
        return modeline;
    }

    throw CommandError("failed to parse cvt output");
}

}  // namespace evdi_control

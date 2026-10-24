#include "neighborhood_screen.hpp"

#include "odh/neighborhood_picker.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>

namespace {
void write_json_string(std::ostream& output, const std::string& value) {
    output << '\"';
    for (const char ch : value) {
        if (ch == '\\' || ch == '\"') output << '\\';
        output << ch;
    }
    output << '\"';
}
}

int main(int argc, char** argv) {
    try {
        std::optional<std::string> folder, archive;
        std::optional<std::string> export_frame;
        std::optional<unsigned> mode;
        unsigned language = 1;
        bool have_language = false;
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 >= argc) throw odh::FormatError("missing argument value");
            const std::string key = argv[i], value = argv[i + 1];
            if (key == "--user-data" && !folder) folder = value;
            else if (key == "--ui-graphics" && !archive) archive = value;
            else if (key == "--export-frame" && !export_frame) export_frame = value;
            else if (key == "--language" && !have_language) {
                const auto result = std::from_chars(value.data(), value.data() + value.size(), language);
                if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                    language < 1 || language > 20)
                    throw odh::FormatError("invalid language code");
                have_language = true;
            }
            else if (key == "--mode" && !mode) {
                unsigned number = 0;
                const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
                if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size())
                    throw odh::FormatError("invalid decimal mode");
                mode = number;
            } else throw odh::FormatError("unknown or duplicate option");
        }
        if (!folder || !archive || !mode) throw odh::FormatError(
            "Usage: odh-neighborhood-window --user-data DIR --ui-graphics UIGraphics.far --mode 1..7 [--language 1..20] [--export-frame OUTPUT.ppm]");
        if (!odh::neighborhood_map_config(*mode)) {
            std::cout << "{\"evidence\":\"map_selection_only\",\"mode\":6,\"status\":\"unsupported\",\"selected_house\":null}\n";
            return 0;
        }

        const auto map = odh_host::prepare_neighborhood_map(*folder, *archive, *mode);
        if (export_frame) {
            const auto frame = odh_host::compose_neighborhood_picker_frame(map,
                "Neighborhood", "Cancel", static_cast<std::uint8_t>(language));
            std::ofstream output(*export_frame, std::ios::binary | std::ios::trunc);
            if (!output) throw odh::FormatError("cannot open frame output");
            output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
            for (const auto& pixel : frame.pixels) {
                const char rgb[]{static_cast<char>(pixel[0]), static_cast<char>(pixel[1]),
                                 static_cast<char>(pixel[2])};
                output.write(rgb, sizeof(rgb));
            }
            output.close();
            if (!output) throw odh::FormatError("cannot write frame output");
            const auto existing = std::count_if(map.markers.begin(), map.markers.end(),
                [](const auto& marker) { return marker.file_exists; });
            const auto occupied = std::count_if(map.markers.begin(), map.markers.end(),
                [](const auto& marker) { return marker.occupied; });
            std::cout << "{\"evidence\":\"neighborhood_frame_export_only\",\"mode\":"
                      << *mode << ",\"width\":" << frame.width << ",\"height\":"
                      << frame.height << ",\"lots\":" << map.markers.size()
                      << ",\"house_files\":" << existing << ",\"families\":"
                      << occupied << ",\"missing_layers\":[";
            for (std::size_t i = 0; i < map.missing_layers.size(); ++i) {
                if (i) std::cout << ',';
                write_json_string(std::cout, map.missing_layers[i]);
            }
            std::cout << "]}\n";
            return 0;
        }
        odh_host::LinuxWindow window(800, 600, "OpenDollhouse map picker");
        const auto selected = odh_host::show_neighborhood_picker(window, map);
        std::cout << "{\"evidence\":\"map_selection_only\",\"mode\":" << *mode
                  << ",\"status\":\"" << (selected ? "selected" : "cancelled")
                  << "\",\"selected_house\":";
        if (selected) std::cout << *selected;
        else std::cout << "null";
        std::cout << "}\n";
    } catch (const std::exception& error) {
        std::cerr << "odh-neighborhood-window: " << error.what() << '\n';
        return 2;
    }
}

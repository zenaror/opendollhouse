#include "neighborhood_screen.hpp"

#include "odh/neighborhood_picker.hpp"

#include <charconv>
#include <iostream>

int main(int argc, char** argv) {
    try {
        std::optional<std::string> folder, archive;
        std::optional<unsigned> mode;
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 >= argc) throw odh::FormatError("missing argument value");
            const std::string key = argv[i], value = argv[i + 1];
            if (key == "--user-data" && !folder) folder = value;
            else if (key == "--ui-graphics" && !archive) archive = value;
            else if (key == "--mode" && !mode) {
                unsigned number = 0;
                const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
                if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size())
                    throw odh::FormatError("invalid decimal mode");
                mode = number;
            } else throw odh::FormatError("unknown or duplicate option");
        }
        if (!folder || !archive || !mode) throw odh::FormatError(
            "Usage: odh-neighborhood-window --user-data DIR --ui-graphics UIGraphics.far --mode 1..7");
        if (!odh::neighborhood_map_config(*mode)) {
            std::cout << "{\"evidence\":\"map_selection_only\",\"mode\":6,\"status\":\"unsupported\",\"selected_house\":null}\n";
            return 0;
        }

        const auto map = odh_host::prepare_neighborhood_map(*folder, *archive, *mode);
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

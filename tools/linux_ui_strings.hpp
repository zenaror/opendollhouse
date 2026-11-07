#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "odh/animation.hpp"

namespace odh_host {
enum class GameUiStringResource { menus,live_panels };

// Loads the small set of menu tables used by the Linux game entry in one IFF
// pass. UIText.iff is shared by several screens, so callers reuse this object
// for the duration of one game-entry run.
class GameUiStrings {
public:
    explicit GameUiStrings(std::filesystem::path game_root,GameUiStringResource resource=GameUiStringResource::menus);

    std::string get(std::uint16_t table_id, std::size_t index,
                    std::uint8_t language, const char* fallback);

private:
    void load();

    std::filesystem::path game_root_;
    GameUiStringResource resource_;
    std::map<std::uint16_t, odh::StringTable> tables_;
    bool loaded_ = false;
};

} // namespace odh_host

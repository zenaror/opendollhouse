#include "linux_ui_strings.hpp"

#include "odh/formats.hpp"

#ifdef ODH_HAVE_LINUX_TEXT_CODEC
#include "linux_text_codec.hpp"
#endif

#include <algorithm>
#include <array>
#include <fstream>
#include <utility>

namespace odh_host {
namespace {
bool path_is_within(const std::filesystem::path& root,
                    const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& component : relative)
        if (component == "..") return false;
    return true;
}

bool is_ui_table(std::uint16_t id) {
    static constexpr std::array<std::uint16_t, 5> ids{128, 131, 132, 142, 145};
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
}

GameUiStrings::GameUiStrings(std::filesystem::path game_root)
    : game_root_(std::move(game_root)) {}

void GameUiStrings::load() {
    if (loaded_) return;
    loaded_ = true;

    const auto candidate = game_root_ / "GameData/UIText.iff";
    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error) || error) return;
    const auto path = std::filesystem::canonical(candidate, error);
    if (error) throw odh::FormatError("cannot resolve UIText.iff inside the game installation");
    if (!path_is_within(game_root_, path))
        throw odh::FormatError("UIText.iff resolves outside the selected game installation");

    std::ifstream input(path, std::ios::binary);
    if (!input) throw odh::FormatError("cannot open UIText.iff inside the game installation");
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) throw odh::FormatError("cannot determine UIText.iff size");
    input.seekg(0, std::ios::beg);
    const odh::View view(input, static_cast<std::uint64_t>(end));
    odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
        if (chunk.type != "STR#" || !is_ui_table(chunk.id)) return;
        if (tables_.count(chunk.id))
            throw odh::FormatError("ambiguous UIText STR# resource");
        tables_.emplace(chunk.id, odh::read_strings(chunk.payload));
    });
}

std::string GameUiStrings::get(std::uint16_t table_id, std::size_t index,
                              std::uint8_t language, const char* fallback) {
    load();
    std::string text = fallback;
    const auto table = tables_.find(table_id);
    if (table != tables_.end()) {
        if (const auto* entry = odh::string_in_language_or_english(
                table->second, language, index))
            text = entry->value;
    }
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
    return legacy_text_to_utf8(text, language);
#else
    return text;
#endif
}

} // namespace odh_host

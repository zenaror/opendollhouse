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
    static constexpr std::array<std::uint16_t, 16> ids{128,129,130,131,132,134,136,137,138,142,145,150,151,152,153,160};
    if(id==166)return true;
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
}

GameUiStrings::GameUiStrings(std::filesystem::path game_root,GameUiStringResource resource)
    : game_root_(std::move(game_root)),resource_(resource) {}

void GameUiStrings::load() {
    if (loaded_) return;
    loaded_ = true;

    const auto candidate = game_root_ / (resource_==GameUiStringResource::live_panels?"GameData/Live.iff":"GameData/UIText.iff");
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
        const bool selected=resource_==GameUiStringResource::live_panels?
            (chunk.id==131||chunk.id==132||chunk.id==133||chunk.id==135||chunk.id==138||chunk.id==136||chunk.id==137||chunk.id==140||chunk.id==142):is_ui_table(chunk.id);
        if (chunk.type != "STR#" || !selected) return;
        if (tables_.count(chunk.id))
            throw odh::FormatError("ambiguous UIText STR# resource");
        tables_.emplace(chunk.id, odh::read_strings(chunk.payload));
    });
}

std::string GameUiStrings::get(std::uint16_t table_id, std::size_t index,
                              std::uint8_t language, const char* fallback) {
    load();
    const auto table = tables_.find(table_id);
    if (table != tables_.end()) {
        if (const auto* entry = odh::string_in_language_or_english(
                table->second, language, index)) {
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
            return legacy_text_to_utf8(entry->value, entry->language ? entry->language : 1);
#else
            return entry->value;
#endif
        }
    }
    // Caller-provided UI labels are already UTF-8. Only game STR# bytes use
    // the selected entry's legacy encoding (which may be an English fallback).
    return fallback;
}

} // namespace odh_host

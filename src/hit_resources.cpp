#include "odh/hit_resources.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>

namespace odh {
namespace {
using Text = std::string_view;
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
Text trim(Text s) {
    while (!s.empty() && space(s.front())) s.remove_prefix(1);
    while (!s.empty() && space(s.back())) s.remove_suffix(1);
    return s;
}
std::string lower(Text s) {
    std::string result(s);
    for (auto& c : result) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return result;
}
template<class T> bool number(Text s, T& result, int base = 10) {
    s = trim(s);
    if (base == 16 && s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s.remove_prefix(2);
    if (s.empty()) return false;
    const auto parsed = std::from_chars(s.data(), s.data() + s.size(), result, base);
    return parsed.ec == std::errc{} && parsed.ptr == s.data() + s.size();
}
template<class T> T required_number(Text s, const char* what, int base = 10) {
    T result{};
    if (!number(s, result, base)) throw FormatError(std::string("invalid ") + what);
    return result;
}
std::string text_input(const View& view, const HitResourceLimits& limits) {
    if (view.size() > limits.input_bytes || view.size() > limits.text_bytes ||
        view.size() > std::numeric_limits<std::size_t>::max())
        throw FormatError("HIT text input/text budget exceeded");
    std::string result(static_cast<std::size_t>(view.size()), '\0');
    view.read(0, result.data(), result.size());
    if (result.find('\0') != std::string::npos) throw FormatError("NUL in HIT text");
    return result;
}
template<class F> void lines(const std::string& input, const HitResourceLimits& limits, F consume) {
    std::size_t offset = 0;
    // StreamReader recognizes a UTF-8 BOM. No other encoding is inferred.
    if (input.size() >= 3 && static_cast<unsigned char>(input[0]) == 0xef &&
        static_cast<unsigned char>(input[1]) == 0xbb && static_cast<unsigned char>(input[2]) == 0xbf) offset = 3;
    std::uint64_t line = 0;
    while (offset < input.size()) {
        const auto start = offset;
        while (offset < input.size() && input[offset] != '\n' && input[offset] != '\r') ++offset;
        if (offset - start > limits.line_bytes) throw FormatError("HIT text line budget exceeded");
        consume(trim(Text(input).substr(start, offset - start)), ++line);
        if (offset < input.size()) {
            const auto c = input[offset++];
            if (c == '\r' && offset < input.size() && input[offset] == '\n') ++offset;
        }
    }
}
void record(std::uint64_t& count, const HitResourceLimits& limits) {
    if (count >= limits.records) throw FormatError("HIT resource record budget exceeded");
    ++count;
}
std::vector<Text> csv(Text text) {
    std::vector<Text> result;
    std::size_t start = 0;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '"') quoted = !quoted;
        if (text[i] == ',' && !quoted) { result.push_back(trim(text.substr(start, i - start))); start = i + 1; }
    }
    if (quoted) throw FormatError("unterminated HOT quote");
    result.push_back(trim(text.substr(start)));
    return result;
}
template<class K, class V> void overwrite(std::map<K,V>& map, K key, V value, Hot& hot) {
    if (map.count(key)) ++hot.duplicate_records;
    map.insert_or_assign(std::move(key), std::move(value));
}
void unresolved(std::string& reason, Text label) {
    if (!reason.empty()) reason += "; ";
    reason += "unresolved symbol: "; reason += label;
}
std::optional<std::uint32_t> equate(Text s, const Hot& hot, std::string& reason) {
    std::int32_t n;
    if (number(s, n)) return static_cast<std::uint32_t>(n);
    const auto found = hot.equates.find(std::string(s));
    if (found != hot.equates.end()) return static_cast<std::uint32_t>(found->second);
    unresolved(reason, s); return {};
}
std::optional<std::uint32_t> symbol(Text s, const Hsm* hsm, std::string& reason) {
    std::uint32_t n;
    if (number(s, n)) return n;
    if (hsm) {
        const auto found = hsm->constants.find(lower(s));
        if (found != hsm->constants.end()) return static_cast<std::uint32_t>(found->second);
    }
    unresolved(reason, s); return {};
}
HotHitlist hitlist(Text text, std::uint64_t& total, const HitResourceLimits& limits) {
    HotHitlist result; result.raw = std::string(text);
    if (text.empty()) { result.unsupported_reason = "empty numeric hitlist"; return result; }
    // Symbolic saxophone lists are unsupported in the reference too. Keep the
    // original spelling and do not reinterpret a symbol as a zero or numeric ID.
    for (const auto c : text) if (!(c >= '0' && c <= '9') && c != ',' && c != '-' && !space(c)) {
        result.unsupported_reason = "symbolic hitlist"; return result;
    }
    for (const auto part : csv(text)) {
        const auto dash = part.find('-');
        const auto first = required_number<std::uint32_t>(part.substr(0, dash), "hitlist ID");
        const auto last = dash == Text::npos ? first : required_number<std::uint32_t>(part.substr(dash + 1), "hitlist range");
        if (last < first) throw FormatError("descending HOT hitlist range");
        const std::uint64_t count = static_cast<std::uint64_t>(last) - first + 1;
        if (count > limits.hitlist_ids || total > limits.hitlist_ids - count)
            throw FormatError("HOT hitlist expansion budget exceeded");
        total += count;
        // uint64 induction prevents wrapping at UINT32_MAX.
        for (std::uint64_t value = first; value <= last; ++value) result.ids.push_back(static_cast<std::uint32_t>(value));
    }
    return result;
}
}

Hsm read_hsm(const View& view, const HitResourceLimits& limits) {
    const auto input = text_input(view, limits);
    Hsm result; result.logical_bytes = view.size();
    lines(input, limits, [&](Text line, std::uint64_t line_no) {
        if (line.empty() || line.front() == ';' || line.front() == '#') return;
        record(result.records, limits);
        const auto separator = line.find_first_of(" \t");
        if (separator == Text::npos) throw FormatError("HSM row lacks value");
        const auto name = lower(line.substr(0, separator));
        auto rest = trim(line.substr(separator));
        const auto end = rest.find_first_of(" \t");
        const auto value = required_number<std::int32_t>(rest.substr(0, end), "HSM signed constant");
        if (!result.constants.emplace(name, value).second) ++result.duplicate_names;
        else result.first_lines.emplace(name, line_no);
    });
    return result;
}

Hot read_hot(const View& view, const Hsm* hsm, const HitResourceLimits& limits) {
    const auto input = text_input(view, limits);
    Hot result; result.logical_bytes = view.size();
    std::string section;
    std::uint64_t expanded = 0;
    lines(input, limits, [&](Text line, std::uint64_t) {
        if (line.empty() || line.front() == ';' || line.front() == '#') return;
        if (line.front() == '[') {
            if (line.back() != ']') throw FormatError("malformed HOT section");
            section = std::string(line.substr(1, line.size() - 2)); return;
        }
        record(result.records, limits);
        const auto equal = line.find('=');
        if (equal == Text::npos) throw FormatError("HOT row lacks equals");
        const auto key = trim(line.substr(0, equal)), value = trim(line.substr(equal + 1));
        if (key.empty()) throw FormatError("empty HOT key");
        if (section == "EventMappingEquate") {
            overwrite(result.equates, std::string(key), required_number<std::int32_t>(value, "HOT equate"), result);
        } else if (section == "Options") {
            if (key == "Version" || key == "LoadPriority") {
                auto& option = key == "Version" ? result.version : result.load_priority;
                if (option) ++result.duplicate_records;
                option = required_number<std::int32_t>(value, "HOT option");
            } else ++result.unknown_records;
        } else if (section == "TrackData") {
            const auto id = required_number<std::uint32_t>(key, "HOT hexadecimal track ID", 16);
            const auto offset = required_number<std::uint32_t>(value, "HOT hexadecimal track offset", 16);
            if (!result.track_data.emplace(id, offset).second) throw FormatError("duplicate HOT TrackData ID");
        } else if (section == "EventMapping") {
            const auto fields = csv(value);
            HotEvent event;
            event.event_type = equate(fields[0], result, event.unsupported_reason);
            event.track_id = fields.size() > 1 ? equate(fields[1], result, event.unsupported_reason) : std::optional<std::uint32_t>{0};
            if (!event.unsupported_reason.empty()) ++result.unsupported_records;
            overwrite(result.events, lower(key), std::move(event), result);
        } else if (section == "Track") {
            const auto id = required_number<std::uint32_t>(key, "HOT track ID");
            const auto fields = csv(value);
            if (fields.size() < 9 || fields[7].empty() || fields[7].front() != '@') throw FormatError("malformed HOT track fields");
            HotTrack track;
            track.subroutine_label = std::string(fields[1]);
            track.volume = equate(fields[2], result, track.unsupported_reason);
            track.arg_type = equate(fields[3], result, track.unsupported_reason);
            track.ducking_priority = equate(fields[4], result, track.unsupported_reason);
            track.control_group = equate(fields[5], result, track.unsupported_reason);
            track.sound_pressure_level = equate(fields[6], result, track.unsupported_reason);
            track.hitlist_id = symbol(fields[7].substr(1), hsm, track.unsupported_reason);
            track.patch_id = equate(fields[8], result, track.unsupported_reason);
            for (const auto candidate : {track.hitlist_id, track.patch_id}) if (candidate && *candidate != 0) {
                const auto found = result.track_data.find(*candidate);
                if (found != result.track_data.end()) track.subroutine_offset = found->second;
            }
            if (!track.subroutine_offset) {
                if (!track.unsupported_reason.empty()) track.unsupported_reason += "; ";
                track.unsupported_reason += "missing TrackData bytecode offset";
            }
            if (!track.unsupported_reason.empty()) ++result.unsupported_records;
            overwrite(result.tracks, id, std::move(track), result);
        } else if (section == "Patch") {
            const auto id = required_number<std::uint32_t>(key, "HOT patch ID");
            const auto fields = csv(value);
            if (fields.size() < 5 || fields[2].size() < 2 || fields[2].front() != '"' || fields[2].back() != '"')
                throw FormatError("malformed HOT patch fields/filename");
            HotPatch patch;
            patch.name = std::string(fields[1]);
            patch.filename = std::string(fields[2].substr(1, fields[2].size() - 2));
            patch.looped = required_number<std::int32_t>(fields[3], "HOT loop flag") != 0;
            patch.piano = required_number<std::int32_t>(fields[4], "HOT piano flag") != 0;
            overwrite(result.patches, id, std::move(patch), result);
        } else if (section == "GlobalHitList") {
            const auto id = required_number<std::uint32_t>(key, "HOT hitlist ID");
            auto list = hitlist(value, expanded, limits);
            if (!list.unsupported_reason.empty()) ++result.unsupported_records;
            overwrite(result.hitlists, id, std::move(list), result);
        } else ++result.unknown_records;
    });
    return result;
}

HitFile read_hit(const View& view, const HitResourceLimits& limits) {
    if (view.size() > limits.input_bytes || view.size() > limits.hit_bytes ||
        view.size() > std::numeric_limits<std::size_t>::max()) throw FormatError("HIT byte budget exceeded");
    if (view.size() < 16) throw FormatError("truncated HIT header");
    char magic[4], signature[4];
    view.read(0, magic, 4); view.read(12, signature, 4);
    HitFile result;
    result.major_version = view.le32(4); result.minor_version = view.le32(8);
    if (Text(magic, 4) != "HIT!" || Text(signature, 4) != "TRAX" || result.major_version != 1 ||
        (result.minor_version != 0 && result.minor_version != 8)) throw FormatError("unsupported HIT signature/version");
    result.bytes.resize(static_cast<std::size_t>(view.size()));
    view.read(0, result.bytes.data(), result.bytes.size());
    const std::array<std::uint8_t,4> marker{'E','N','T','P'};
    const auto found = std::search(result.bytes.begin() + 16, result.bytes.end(), marker.begin(), marker.end());
    if (found == result.bytes.end()) return result;
    const auto start = static_cast<std::uint64_t>(found - result.bytes.begin());
    result.entry_table_offset = start;
    auto cursor = start + 4;
    while (true) {
        if (cursor > view.size() || view.size() - cursor < 4) throw FormatError("unterminated HIT ENTP table");
        const auto end = lower(Text(reinterpret_cast<const char*>(result.bytes.data() + cursor), 4));
        if (end == "eent") break;
        if (view.size() - cursor < 8) throw FormatError("truncated HIT ENTP entry");
        if (result.entry_points.size() >= limits.entry_points) throw FormatError("HIT entry point budget exceeded");
        const auto track = view.le32(cursor), address = view.le32(cursor + 4);
        if (address < 16 || address >= start) throw FormatError("HIT entry address outside bytecode");
        if (!result.entry_points.emplace(track, address).second) throw FormatError("duplicate HIT entry track ID");
        cursor += 8;
    }
    return result;
}
}

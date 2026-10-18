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
std::uint32_t native_track_data_number(Text s, const char* what) {
    s = trim(s);
    const bool prefix = s.size() >= 2 && s[0] == '0' &&
        (s[1] == 'x' || s[1] == 'X');
    const bool hex_digit = std::any_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
    return required_number<std::uint32_t>(s, what, prefix || hex_digit ? 16 : 10);
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
void unsupported(std::string& reason, const char* detail) {
    if (!reason.empty()) reason += "; ";
    reason += detail;
}
std::optional<std::uint32_t> equate(Text s, const Hot& hot, std::string& reason) {
    std::int32_t n;
    if (number(s, n)) return static_cast<std::uint32_t>(n);
    const auto found = hot.equates.find(std::string(s));
    if (found != hot.equates.end()) return static_cast<std::uint32_t>(found->second);
    unresolved(reason, s); return {};
}
std::optional<std::int32_t> signed_equate(Text s, const Hot& hot, std::string& reason) {
    if(s.empty()){unsupported(reason,"empty EventMapping argument override");return {};}
    std::int32_t n;
    if(number(s,n))return n;
    const auto found=hot.equates.find(std::string(s));
    if(found!=hot.equates.end())return found->second;
    auto digits=s;
    if(!digits.empty() && digits.front()=='-')digits.remove_prefix(1);
    if(!digits.empty() && std::all_of(digits.begin(),digits.end(),[](char c){return c>='0'&&c<='9';}))
        unsupported(reason,"out-of-range EventMapping argument override");
    else unresolved(reason,s);
    return {};
}
std::optional<std::uint32_t> symbol(Text s, const Hsm* hsm, std::string& reason,
                                    HotReadProfile profile) {
    std::uint32_t n;
    if (profile == HotReadProfile::simitone_local && number(s, n)) return n;
    if (hsm) {
        const auto found = hsm->constants.find(profile == HotReadProfile::native_entry_map
            ? std::string(s) : lower(s));
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

Hsm read_hsm(const View& view, const HitResourceLimits& limits, HsmReadProfile profile) {
    if (profile != HsmReadProfile::simitone_local &&
        profile != HsmReadProfile::native_namespace)
        throw FormatError("unsupported HSM read profile");
    const auto input = text_input(view, limits);
    Hsm result; result.logical_bytes = view.size();
    lines(input, limits, [&](Text line, std::uint64_t line_no) {
        if (line.empty() || line.front() == ';' || line.front() == '#') return;
        record(result.records, limits);
        const auto separator = line.find_first_of(" \t");
        if (separator == Text::npos) throw FormatError("HSM row lacks value");
        const auto name = profile == HsmReadProfile::native_namespace
            ? std::string(line.substr(0, separator)) : lower(line.substr(0, separator));
        auto rest = trim(line.substr(separator));
        const auto end = rest.find_first_of(" \t");
        const auto value = required_number<std::int32_t>(rest.substr(0, end), "HSM signed constant");
        const auto inserted = result.constants.emplace(name, value);
        if (!inserted.second) {
            ++result.duplicate_names;
            if (profile == HsmReadProfile::native_namespace) inserted.first->second = value;
        } else result.first_lines.emplace(name, line_no);
    });
    return result;
}

Hot read_hot(const View& view, const Hsm* hsm, const HitResourceLimits& limits,
             HotReadProfile profile) {
    if (profile != HotReadProfile::simitone_local &&
        profile != HotReadProfile::native_entry_map)
        throw FormatError("unsupported HOT read profile");
    const auto input = text_input(view, limits);
    Hot result; result.logical_bytes = view.size();
    if (profile == HotReadProfile::native_entry_map) result.native_object_order.emplace();
    std::string section;
    std::uint64_t expanded = 0;
    lines(input, limits, [&](Text line, std::uint64_t line_no) {
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
            const auto id = profile == HotReadProfile::native_entry_map
                ? native_track_data_number(key, "HOT native TrackData ID")
                : required_number<std::uint32_t>(key, "HOT hexadecimal track ID", 16);
            const auto offset = profile == HotReadProfile::native_entry_map
                ? native_track_data_number(value, "HOT native TrackData offset")
                : required_number<std::uint32_t>(value, "HOT hexadecimal track offset", 16);
            const auto inserted = result.track_data.emplace(id, offset);
            if (!inserted.second) {
                if (profile == HotReadProfile::simitone_local)
                    throw FormatError("duplicate HOT TrackData ID");
                inserted.first->second = offset;
                ++result.duplicate_records;
                ++result.track_data_replacements;
            }
        } else if (section == "EventMapping") {
            const auto fields = csv(value);
            HotEvent event;
            event.event_type = equate(fields[0], result, event.unsupported_reason);
            event.track_id = fields.size() > 1 ? equate(fields[1], result, event.unsupported_reason) : std::optional<std::uint32_t>{0};
            for(std::size_t n=0;n<event.argument_overrides.size();++n)
                if(fields.size()>n+2)event.argument_overrides[n]=signed_equate(fields[n+2],result,event.unsupported_reason);
            if (!event.unsupported_reason.empty()) ++result.unsupported_records;
            overwrite(result.events, lower(key), std::move(event), result);
        } else if (section == "Track") {
            const auto id = profile == HotReadProfile::native_entry_map
                ? native_track_data_number(key, "HOT native Track ID")
                : required_number<std::uint32_t>(key, "HOT track ID");
            if (profile == HotReadProfile::native_entry_map && !id) return;
            const auto fields = csv(value);
            if (fields.size() < 9 || fields[7].empty() ||
                (profile == HotReadProfile::simitone_local && fields[7].front() != '@'))
                throw FormatError("malformed HOT track fields");
            HotTrack track;
            track.subroutine_label = std::string(fields[1]);
            track.volume = equate(fields[2], result, track.unsupported_reason);
            track.arg_type = equate(fields[3], result, track.unsupported_reason);
            track.ducking_priority = equate(fields[4], result, track.unsupported_reason);
            track.control_group = equate(fields[5], result, track.unsupported_reason);
            track.sound_pressure_level = equate(fields[6], result, track.unsupported_reason);
            // Native CSV7 accepts @symbol, a digit-start numeric key or a
            // known HOT equate. The global catalog resolves its bytecode PC.
            if (fields[7].front() == '@')
                track.hitlist_id = symbol(fields[7].substr(1), hsm,
                                          track.unsupported_reason, profile);
            else if (fields[7].front() >= '0' && fields[7].front() <= '9')
                track.hitlist_id = native_track_data_number(fields[7], "HOT native Track CSV7");
            else {
                const auto found = result.equates.find(std::string(fields[7]));
                if (found != result.equates.end())
                    track.hitlist_id = static_cast<std::uint32_t>(found->second);
                else unresolved(track.unsupported_reason, fields[7]);
            }
            track.patch_id = equate(fields[8], result, track.unsupported_reason);
            if (profile == HotReadProfile::simitone_local) {
                for (const auto candidate : {track.hitlist_id, track.patch_id})
                    if (candidate && *candidate != 0) {
                        const auto found = result.track_data.find(*candidate);
                        if (found != result.track_data.end())
                            track.subroutine_offset = found->second;
                    }
                if (!track.subroutine_offset) {
                    if (!track.unsupported_reason.empty()) track.unsupported_reason += "; ";
                    track.unsupported_reason += "missing TrackData bytecode offset";
                }
            }
            if (!track.unsupported_reason.empty()) ++result.unsupported_records;
            if (result.native_object_order)
                result.native_object_order->push_back({HotObjectKind::track, id, line_no});
            overwrite(result.tracks, id, std::move(track), result);
        } else if (section == "Patch") {
            const auto id = profile == HotReadProfile::native_entry_map
                ? native_track_data_number(key, "HOT native Patch ID")
                : required_number<std::uint32_t>(key, "HOT patch ID");
            if (profile == HotReadProfile::native_entry_map && !id) return;
            HotPatch patch;
            if (profile == HotReadProfile::simitone_local) {
                const auto fields = csv(value);
                if (fields.size() < 5 || fields[2].size() < 2 ||
                    fields[2].front() != '"' || fields[2].back() != '"')
                    throw FormatError("malformed HOT patch fields/filename");
                patch.name = std::string(fields[1]);
                patch.filename = std::string(fields[2].substr(1, fields[2].size() - 2));
                patch.looped = required_number<std::int32_t>(fields[3], "HOT loop flag") != 0;
                patch.piano = required_number<std::int32_t>(fields[4], "HOT piano flag") != 0;
            } else {
                // The native callback splits the first two comma fields, then
                // scans for quotes. Everything before the first quote after
                // field 1 is skipped, including GuineaPig's extra name field.
                const auto first = value.find(',');
                const auto second = first == Text::npos ? Text::npos : value.find(',', first + 1);
                const auto open = second == Text::npos ? Text::npos : value.find('"', second + 1);
                const auto close = open == Text::npos ? Text::npos : value.find('"', open + 1);
                if (close == Text::npos) throw FormatError("malformed HOT native patch filename");
                patch.name = std::string(trim(value.substr(first + 1, second - first - 1)));
                patch.ignored_pre_filename = std::string(value.substr(second + 1, open - second - 1));
                patch.filename = std::string(value.substr(open + 1, close - open - 1));
                auto suffix = trim(value.substr(close + 1));
                if (suffix.empty() || suffix.front() != ',')
                    throw FormatError("malformed HOT native patch flags");
                suffix.remove_prefix(1);
                const auto flags = csv(suffix);
                if (flags.size() < 2) throw FormatError("malformed HOT native patch flags");
                // The native boolean transform accepts exactly the value 1.
                patch.looped = required_number<std::int32_t>(flags[0], "HOT loop flag") == 1;
                patch.piano = required_number<std::int32_t>(flags[1], "HOT piano flag") == 1;
                // The original callback passes five booleans to the Patch
                // constructor. This parser preserves present suffix fields;
                // it does not emulate sscanf behavior for missing arguments.
                const auto optional_native_flag = [&](std::size_t index, const char* what)
                        -> std::optional<bool> {
                    if (index >= flags.size()) return std::nullopt;
                    return required_number<std::int32_t>(flags[index], what) == 1;
                };
                patch.native_id43 = optional_native_flag(2, "HOT native Patch ID43 flag");
                patch.native_id26 = optional_native_flag(3, "HOT native Patch ID26 flag");
                patch.native_id27 = optional_native_flag(4, "HOT native Patch ID27 flag");
            }
            if (result.native_object_order)
                result.native_object_order->push_back({HotObjectKind::patch, id, line_no});
            overwrite(result.patches, id, std::move(patch), result);
        } else if (section == "GlobalHitList") {
            const auto id = profile == HotReadProfile::native_entry_map
                ? native_track_data_number(key, "HOT native GlobalHitList ID")
                : required_number<std::uint32_t>(key, "HOT hitlist ID");
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

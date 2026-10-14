#include "odh/hit_resources.hpp"

#include <iostream>
#include <sstream>

namespace {
int checks = 0;
void require(bool value, const char* why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
template<class F> void rejects(F action, const std::string& reason = {}) {
    bool rejected = false;
    try { action(); } catch (const odh::FormatError& error) {
        rejected = reason.empty() || std::string(error.what()).find(reason) != std::string::npos;
    }
    require(rejected, "malformed or over-budget HIT resource rejects explicitly");
}
auto hsm(const std::string& bytes, odh::HitResourceLimits limits = {}) {
    std::istringstream stream(bytes);
    return odh::read_hsm(odh::View(stream, bytes.size()), limits);
}
auto hot(const std::string& bytes, const odh::Hsm* symbols = nullptr, odh::HitResourceLimits limits = {}) {
    std::istringstream stream(bytes);
    return odh::read_hot(odh::View(stream, bytes.size()), symbols, limits);
}
auto hit(const std::string& bytes, odh::HitResourceLimits limits = {}) {
    std::istringstream stream(bytes);
    return odh::read_hit(odh::View(stream, bytes.size()), limits);
}
void word(std::string& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<char>((value >> (8 * i)) & 255);
}
std::string binary(std::uint32_t minor = 8) {
    std::string bytes(32, '\0');
    bytes.replace(0,4,"HIT!"); word(bytes,4,1); word(bytes,8,minor); bytes.replace(12,4,"TRAX");
    return bytes;
}
std::string entry(std::uint32_t id, std::uint32_t address) {
    std::string bytes(8, '\0'); word(bytes,0,id); word(bytes,4,address); return bytes;
}
}
int main() { try {
    const std::string symbols = "\xef\xbb\xbf" "TKD_Label 901 (0x385)\r\nLabel 13699\rtkd_label 99\nnegative -2147483648\n";
    const auto names = hsm(symbols);
    require(names.logical_bytes == symbols.size() && names.records == 4 && names.duplicate_names == 1,
            "HSM serialized extent and duplicates retain provenance");
    require(names.constants.at("tkd_label") == 901 && names.constants.at("negative") == -2147483647 - 1 &&
            names.first_lines.at("tkd_label") == 1, "HSM ASCII lower names retain first value and line");
    rejects([] { hsm("label\n"); });
    rejects([] { hsm("label 2147483648\n"); });
    rejects([] { hsm("label -2147483649\n"); });
    rejects([] { hsm(std::string("a 1\0", 4)); }, "NUL");
    auto limits = odh::HitResourceLimits{};
    limits.records = 1;
    rejects([&] { hsm("a 1\na 2\n", limits); }, "record budget");
    limits = {}; limits.text_bytes = 3;
    rejects([&] { hsm("a 1\n", limits); }, "text budget");
    limits = {}; limits.input_bytes = 3;
    rejects([&] { hsm("a 1\n", limits); }, "input/text budget");
    limits = {}; limits.line_bytes = 2;
    rejects([&] { hsm("a 1", limits); }, "line budget");

    const std::string table =
        "[TrackData]\r\n0x385=0x3583\n0x7=0x11\n"
        "[EventMappingEquate]\nkPlay=1\nkArg=2\nkDuck=10\nkGroup=3\nkSpl=20\n"
        "[Options]\nVersion=1\nLoadPriority=2\nOther=3\n"
        "[EventMapping]\nHungry=kPlay,3979,0,0\nStop=2\nBroken=absent,5\n"
        "[Track]\n3979=0,Label,1024,kArg,kDuck,kGroup,kSpl,@TKD_Label,0\n"
        "3980=0,Label,1024,kArg,kDuck,kGroup,kSpl,@missing,0\n"
        "3981=0,Label,1024,kArg,kDuck,kGroup,kSpl,@TKD_Label,7\n"
        "[GlobalHitList]\n1291=3212,2756-2757,3981,2249\n20016=Sax_Symbol\n221=\n"
        "[Patch]\n3212=0,sigh,\"sounddata\\voice\\sigh.xa\",0,1,0\n"
        "3212=0,sigh_last,\"with,comma.xa\",1,0,0\n";
    const auto resources = hot(table, &names);
    const auto& track = resources.tracks.at(3979);
    require(track.subroutine_offset == 13699 && track.hitlist_id == 901 && track.patch_id == 0 &&
            track.volume == 1024 && track.arg_type == 2 && track.ducking_priority == 10 &&
            track.control_group == 3 && track.sound_pressure_level == 20 && track.unsupported_reason.empty(),
            "HOT TrackData and HSM resolve all selected metadata");
    require(resources.tracks.at(3981).subroutine_offset == 17, "patch TrackData takes precedence over hitlist TrackData");
    require(!resources.tracks.at(3980).hitlist_id && !resources.tracks.at(3980).subroutine_offset &&
            !resources.tracks.at(3980).unsupported_reason.empty(), "unresolved unrelated track retained without invented zero or HSM offset fallback");
    require(resources.events.at("hungry").event_type == 1 && resources.events.at("hungry").track_id == 3979 &&
            resources.events.at("stop").track_id == 0 && !resources.events.at("broken").event_type,
            "event lookup folds ASCII case and retains unresolved equates");
    require(resources.hitlists.at(1291).ids == std::vector<std::uint32_t>{3212,2756,2757,3981,2249},
            "ordered hitlist range expansion");
    require(resources.hitlists.at(20016).raw == "Sax_Symbol" && resources.hitlists.at(20016).ids.empty() &&
            !resources.hitlists.at(20016).unsupported_reason.empty() &&
            !resources.hitlists.at(221).unsupported_reason.empty(), "symbolic and empty hitlists retain unsupported state");
    require(resources.patches.at(3212).name == "sigh_last" && resources.patches.at(3212).filename == "with,comma.xa" &&
            resources.patches.at(3212).looped && !resources.patches.at(3212).piano && resources.duplicate_records == 1,
            "HOT within-file overwrite counted and quoted comma preserved");
    require(resources.logical_bytes == table.size() && resources.version == 1 && resources.load_priority == 2 &&
            resources.unknown_records == 1 && resources.unsupported_records == 4, "HOT metadata and record classification");
    require(hot("[Patch]\n1=0,n,\"a\\b.xa\",0,0\n").patches.at(1).filename == "a\\b.xa",
            "patch filename retains raw backslashes");
    require(hot("[GlobalHitList]\n1=4294967294-4294967295\n").hitlists.at(1).ids ==
            std::vector<std::uint32_t>{4294967294U,4294967295U}, "UINT32_MAX range does not wrap");
    rejects([] { hot("[GlobalHitList]\n1=0-4294967295\n"); }, "expansion budget");
    rejects([] { hot("[GlobalHitList]\n1=8-7\n"); }, "descending");
    rejects([] { hot("[GlobalHitList]\n1=1--2\n"); });
    rejects([] { hot("[GlobalHitList]\n1=4294967296\n"); });
    rejects([] { hot("[TrackData]\n0x1=0x10\n0x1=0x11\n"); }, "duplicate");
    rejects([] { hot("[Track]\n1=0,no,1,0,0,0,0,no_at,0\n"); });
    rejects([] { hot("[Patch]\n1=0,n,unquoted,0,0\n"); });
    rejects([] { hot("[Patch]\n1=0,n,\"unfinished,0,0\n"); });
    rejects([] { hot("[Track\n"); });
    rejects([] { hot("[TrackData]\nmissing equals\n"); });
    limits = {}; limits.hitlist_ids = 2;
    rejects([&] { hot("[GlobalHitList]\n1=1-2\n1=3\n", nullptr, limits); }, "expansion budget");
    limits = {}; limits.records = 1;
    rejects([&] { hot("[Unknown]\na=1\nb=2\n", nullptr, limits); }, "record budget");

    const auto plain = binary();
    require(hit(plain).bytes == std::vector<std::uint8_t>(plain.begin(), plain.end()) &&
            hit(plain).minor_version == 8 && !hit(plain).entry_table_offset, "HIT native profile preserves bounded bytes without table");
    require(hit(binary(0)).minor_version == 0, "explicit minor-zero profile");
    const auto table_bytes = plain + "ENTP" + entry(3979,16) + entry(3980,31) + "eEnT" + "tail";
    const auto parsed = hit(table_bytes);
    require(parsed.entry_table_offset == 32 && parsed.entry_points.at(3979) == 16 &&
            parsed.entry_points.at(3980) == 31 && parsed.bytes.size() == table_bytes.size(),
            "bounded ENTP entries validate bytecode addresses and retain unknown tail");
    for (std::size_t cut = 0; cut < 16; ++cut) rejects([&] { hit(plain.substr(0,cut)); });
    rejects([&] { hit(plain + "ENTP"); }, "unterminated");
    rejects([&] { hit(plain + "ENTP" + entry(1,16).substr(0,5)); }, "truncated");
    rejects([&] { hit(plain + "ENTP" + entry(1,15) + "EENT"); }, "address");
    rejects([&] { hit(plain + "ENTP" + entry(1,32) + "EENT"); }, "address");
    rejects([&] { hit(plain + "ENTP" + entry(1,16) + entry(1,17) + "EENT"); }, "duplicate");
    rejects([] { hit(binary(9)); }, "version");
    auto bad_header = plain; bad_header[0] = 'X';
    rejects([&] { hit(bad_header); }, "signature");
    bad_header = plain; bad_header[12] = 'X';
    rejects([&] { hit(bad_header); }, "signature");
    limits = {}; limits.hit_bytes = 31;
    rejects([&] { hit(plain, limits); }, "byte budget");
    limits = {}; limits.entry_points = 1;
    rejects([&] { hit(table_bytes, limits); }, "entry point budget");
    rejects([&] {
        std::istringstream short_stream(plain.substr(0,16));
        odh::read_hit(odh::View(short_stream,plain.size()));
    }, "input truncated");
    std::istringstream caller_stream(plain);
    odh::read_hit(odh::View(caller_stream,plain.size()));
    caller_stream.clear(); caller_stream.seekg(0);
    require(caller_stream.get() == 'H', "caller retains stream ownership");
    std::cout << "HIT resource checks: " << checks << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
} }

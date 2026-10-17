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
auto hsm(const std::string& bytes, odh::HitResourceLimits limits = {},
         odh::HsmReadProfile profile = odh::HsmReadProfile::simitone_local) {
    std::istringstream stream(bytes);
    return odh::read_hsm(odh::View(stream, bytes.size()), limits, profile);
}
auto hot(const std::string& bytes, const odh::Hsm* symbols = nullptr,
         odh::HitResourceLimits limits = {},
         odh::HotReadProfile profile = odh::HotReadProfile::simitone_local) {
    std::istringstream stream(bytes);
    return odh::read_hot(odh::View(stream, bytes.size()), symbols, limits, profile);
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
    rejects([] {
        hsm(std::string("a 1\0", 4), {}, static_cast<odh::HsmReadProfile>(255));
    }, "profile");
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
    require(resources.events.at("hungry").argument_overrides==
                std::array<std::optional<std::int32_t>,3>{{0,0,0}} &&
            resources.events.at("stop").argument_overrides==
                std::array<std::optional<std::int32_t>,3>{{0,0,0}},
            "explicit zeros and omitted trailing EventMapping words are known zero");
    require(resources.hitlists.at(1291).ids == std::vector<std::uint32_t>{3212,2756,2757,3981,2249},
            "ordered hitlist range expansion");
    require(resources.hitlists.at(20016).raw == "Sax_Symbol" && resources.hitlists.at(20016).ids.empty() &&
            !resources.hitlists.at(20016).unsupported_reason.empty() &&
            !resources.hitlists.at(221).unsupported_reason.empty(), "symbolic and empty hitlists retain unsupported state");
    require(resources.patches.at(3212).name == "sigh_last" && resources.patches.at(3212).filename == "with,comma.xa" &&
            resources.patches.at(3212).looped && !resources.patches.at(3212).piano &&
            resources.duplicate_records == 1 && resources.track_data_replacements == 0,
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
    const auto native = odh::HotReadProfile::native_entry_map;
    const auto native_names = hsm("Foo 10\nfoo 20\nFoo 30\n123 444\n", {},
                                  odh::HsmReadProfile::native_namespace);
    require(native_names.records == 4 && native_names.duplicate_names == 1 &&
            native_names.constants.size() == 3 &&
            native_names.constants.at("Foo") == 30 &&
            native_names.constants.at("foo") == 20 &&
            native_names.constants.at("123") == 444 &&
            native_names.first_lines.at("Foo") == 1 &&
            native_names.first_lines.at("foo") == 2,
            "native HSM preserves exact case, last duplicate value and first source lines");
    const auto native_labels = hot(
        "[Track]\n"
        "1=0,one,1024,2,0,1,20,@Foo,100\n"
        "2=0,two,1024,2,0,1,20,@foo,100\n"
        "3=0,three,1024,2,0,1,20,@FOO,100\n"
        "4=0,four,1024,2,0,1,20,@123,100\n"
        "5=0,five,1024,2,0,1,20,123,100\n",
        &native_names, {}, native);
    require(native_labels.tracks.at(1).hitlist_id == 30 &&
            native_labels.tracks.at(2).hitlist_id == 20 &&
            !native_labels.tracks.at(3).hitlist_id &&
            !native_labels.tracks.at(3).unsupported_reason.empty() &&
            native_labels.tracks.at(4).hitlist_id == 444 &&
            native_labels.tracks.at(5).hitlist_id == 123 &&
            native_labels.unsupported_records == 1,
            "native HOT @labels use exact HSM names, even digit-only names; bare digits stay numeric");
    require(!hot("[Track]\n1=0,one,1024,2,0,1,20,@123,100\n",
                 nullptr, {}, native).tracks.at(1).hitlist_id,
            "native @digit token without HSM symbol does not fall back to a number");
    const auto default_names = hsm("Foo 10\nfoo 20\nFoo 30\n");
    require(hot("[TrackData]\n10=16\n[Track]\n1=0,one,1024,2,0,1,20,@Foo,100\n",
                &default_names).tracks.at(1).hitlist_id == 10 &&
            hot("[TrackData]\n123=16\n[Track]\n1=0,one,1024,2,0,1,20,@123,100\n")
                .tracks.at(1).hitlist_id == 123,
            "default HSM/HOT profile retains ASCII-folded first-wins behavior");
    const auto native_rows = hot(
        "[TrackData]\n10=10\n0x10=0x10\n1A=2f\n0X10=0X11\n"
        "4294967295=4294967295\na=F\n", nullptr, {}, native);
    require(native_rows.track_data.at(10) == 15 && native_rows.track_data.at(16) == 17 &&
            native_rows.track_data.at(26) == 47 &&
            native_rows.track_data.at(4294967295U) == 4294967295U,
            "native TrackData chooses decimal unless 0x prefix or A-F digit selects hex");
    require(native_rows.track_data.size() == 4 && native_rows.records == 6 &&
            native_rows.duplicate_records == 2 && native_rows.track_data_replacements == 2,
            "native repeated TrackData keys replace values in input order and count each row");
    const auto native_digits = hsm("500 500\n", {}, odh::HsmReadProfile::native_namespace);
    const auto native_track = hot(
        "[Track]\n1=0,Label,1024,2,0,0,20,@500,100\n"
        "2=0,Label,1024,2,0,0,20,@missing,100\n"
        "[TrackData]\n600=20\n", &native_digits, {}, native);
    require(native_track.tracks.at(1).hitlist_id == 500 &&
            native_track.tracks.at(1).patch_id == 100 &&
            !native_track.tracks.at(1).subroutine_offset &&
            native_track.tracks.at(1).unsupported_reason.empty(),
            "native track keeps a globally resolved entry key without local TrackData");
    require(!native_track.tracks.at(2).hitlist_id &&
            !native_track.tracks.at(2).unsupported_reason.empty() &&
            native_track.unsupported_records == 1,
            "native profile still rejects unresolved required track metadata");
    const auto native_numeric_tracks = hot(
        "[Track]\n"
        "1=0,one,1024,2,0,1,20,0x410,100\n"
        "2=0,two,1024,2,0,1,20,10,100\n"
        "3=0,three,1024,2,0,1,20,1A,100\n"
        "4=0,four,1024,2,0,1,20,4294967295,100\n",
        nullptr, {}, native);
    require(native_numeric_tracks.tracks.at(1).hitlist_id == 1040 &&
            native_numeric_tracks.tracks.at(2).hitlist_id == 10 &&
            native_numeric_tracks.tracks.at(3).hitlist_id == 26 &&
            native_numeric_tracks.tracks.at(4).hitlist_id == 4294967295U &&
            native_numeric_tracks.unsupported_records == 0,
            "native Track CSV7 accepts numeric first-digit uint32 keys with observed base rule");
    const auto native_equates = hot(
        "[EventMappingEquate]\nknown=1234\n"
        "[Track]\n1=0,one,1024,2,0,1,20,known,100\n"
        "2=0,two,1024,2,0,1,20,absent,100\n"
        "3=0,three,1024,2,0,1,20,-1,100\n", nullptr, {}, native);
    require(native_equates.tracks.at(1).hitlist_id == 1234 &&
            native_equates.tracks.at(1).unsupported_reason.empty() &&
            !native_equates.tracks.at(2).hitlist_id &&
            !native_equates.tracks.at(2).unsupported_reason.empty() &&
            !native_equates.tracks.at(3).hitlist_id &&
            !native_equates.tracks.at(3).unsupported_reason.empty() &&
            native_equates.unsupported_records == 2,
            "native CSV7 uses known HOT equates and keeps unknown names unsupported");
    rejects([] { hot("[Track]\n1=0,one,1024,2,0,1,20,0x410,100\n"); },
            "malformed HOT track fields");
    for (const auto* key : {"0x", "0x100000000", "4294967296", "2G"})
        rejects([&] {
            hot(std::string("[Track]\n1=0,one,1024,2,0,1,20,") + key +
                ",100\n", nullptr, {}, native);
        }, "HOT native Track CSV7");
    const auto native_patch = hot(
        "[Patch]\n1=0,normal,\"dir,normal.xa\",1,2,0\n"
        "2=0,extra,uninterpreted,\"dir,extra.xa\",2,1,0\n",
        nullptr, {}, native);
    require(native_patch.patches.at(1).name == "normal" &&
            native_patch.patches.at(1).filename == "dir,normal.xa" &&
            native_patch.patches.at(1).ignored_pre_filename.empty() &&
            native_patch.patches.at(1).looped && !native_patch.patches.at(1).piano &&
            native_patch.patches.at(2).name == "extra" &&
            native_patch.patches.at(2).filename == "dir,extra.xa" &&
            native_patch.patches.at(2).ignored_pre_filename == "uninterpreted," &&
            !native_patch.patches.at(2).looped && native_patch.patches.at(2).piano,
            "native Patch scans quoted filename after optional ignored prefix and reads following flags");
    rejects([] { hot("[Patch]\n1=0,extra,uninterpreted,\"file.xa\",1,0\n"); },
            "malformed HOT patch fields");
    rejects([&] { hot("[Patch]\n1=0,name,missing_quotes,1,0\n", nullptr, {}, native); },
            "native patch filename");
    rejects([&] { hot("[Patch]\n1=0,name,\"unterminated,1,0\n", nullptr, {}, native); },
            "native patch filename");
    rejects([&] { hot("[Patch]\n1=0,name,\"file.xa\",1\n", nullptr, {}, native); },
            "native patch flags");
    rejects([&] { hot("[Patch]\n1=0,name,\"file.xa\" junk,1,0\n", nullptr, {}, native); },
            "native patch flags");
    for (const auto* row : {"4294967296=1", "1=4294967296", "0x100000000=1",
                            "1=0x100000000", "0x=1", "1=2G", "-1=2", "1=-2"})
        rejects([&] { hot(std::string("[TrackData]\n") + row + "\n", nullptr, {}, native); },
                "native TrackData");
    rejects([&] {
        hot(std::string("x\0", 2), nullptr, {}, static_cast<odh::HotReadProfile>(255));
    }, "profile");
    rejects([] { hot("[Track]\n1=0,no,1,0,0,0,0,no_at,0\n"); });
    rejects([] { hot("[Patch]\n1=0,n,unquoted,0,0\n"); });
    rejects([] { hot("[Patch]\n1=0,n,\"unfinished,0,0\n"); });
    rejects([] { hot("[Track\n"); });
    rejects([] { hot("[TrackData]\nmissing equals\n"); });
    limits = {}; limits.hitlist_ids = 2;
    rejects([&] { hot("[GlobalHitList]\n1=1-2\n1=3\n", nullptr, limits); }, "expansion budget");
    limits = {}; limits.records = 1;
    rejects([&] { hot("[Unknown]\na=1\nb=2\n", nullptr, limits); }, "record budget");

    const auto signed_events=hot(
        "[EventMappingEquate]\nkLow=-2147483648\nkHigh=2147483647\n"
        "[EventMapping]\n"
        "negative=1,2,kLow,0,kHigh,uninterpreted\n"
        "omitted=1,2\n"
        "partial=1,2,-17\n"
        "literal=1,2,-2147483648,0,2147483647\n"
        "empty=1,2,,0,0\n"
        "empty_third=1,2,4,,0\n"
        "empty_fourth=1,2,4,0,\n"
        "unresolved=1,2,missing,0,0\n"
        "overflow=1,2,2147483648,0,0\n"
        "underflow=1,2,-2147483649,0,0\n"
        "overwrite=1,2,missing\n"
        "overwrite=1,2,-4,0,5\n");
    require(signed_events.events.at("negative").argument_overrides==
                std::array<std::optional<std::int32_t>,3>{{-2147483647-1,0,2147483647}} &&
            signed_events.events.at("negative").unsupported_reason.empty(),
            "signed equates preserve int32 extremes and sixth word remains uninterpreted");
    require(signed_events.events.at("omitted").argument_overrides==
                std::array<std::optional<std::int32_t>,3>{{0,0,0}},
            "missing override suffix is known zero");
    require(signed_events.events.at("partial").argument_overrides==
                std::array<std::optional<std::int32_t>,3>{{-17,0,0}} &&
            signed_events.events.at("literal").argument_overrides==
                std::array<std::optional<std::int32_t>,3>{{-2147483647-1,0,2147483647}},
            "partial row and signed literal extremes resolve without unsigned conversion");
    for(const auto* key:{"empty","unresolved","overflow","underflow"}){
        const auto& event=signed_events.events.at(key);
        require(!event.argument_overrides[0] && !event.unsupported_reason.empty() &&
                event.argument_overrides[1]==0 && event.argument_overrides[2]==0,
                "explicit bad override remains unknown, while other words stay known");
    }
    require(!signed_events.events.at("empty_third").argument_overrides[1] &&
            !signed_events.events.at("empty_fourth").argument_overrides[2] &&
            !signed_events.events.at("empty_third").unsupported_reason.empty() &&
            !signed_events.events.at("empty_fourth").unsupported_reason.empty(),
            "explicit empty fields three and four remain unknown independently");
    require(signed_events.events.at("overwrite").argument_overrides==
                std::array<std::optional<std::int32_t>,3>{{-4,0,5}} &&
            signed_events.events.at("overwrite").unsupported_reason.empty() &&
            signed_events.duplicate_records==1 && signed_events.unsupported_records==7 &&
            signed_events.records==14,
            "overwritten argument row replaces all words but keeps historical row counts");
    limits={};limits.records=2;
    rejects([&]{hot("[EventMapping]\na=1,2,3,4,5\nb=1,2,3,4,5\nc=1,2,3,4,5\n",nullptr,limits);},"record budget");
    limits={};limits.line_bytes=12;
    rejects([&]{hot("[EventMapping]\na=1,2,3,4,5\n",nullptr,limits);},"line budget");

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

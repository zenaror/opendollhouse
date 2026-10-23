#include "person_runtime.hpp"
#ifdef ODH_HAVE_X11
#include "neighborhood_screen.hpp"
#endif
#include "resource.hpp"

#include "odh/session_entry.hpp"
#include "json.hpp"
#ifdef ODH_HAVE_LINUX_USER_CONFIG
#include "linux_user_config.hpp"
#endif

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {
struct Options {
    std::filesystem::path game_root;
    unsigned mode = 0;
    unsigned language = 1;
    std::optional<unsigned> house;
    std::optional<unsigned> resident_id;
    bool plan_only = false;
};

unsigned parse_number(const std::string& text, unsigned maximum, const char* label) {
    unsigned result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        result > maximum)
        throw odh::FormatError(std::string("invalid ") + label);
    return result;
}

Options parse_options(int argc, char** argv) {
    Options result;
    bool have_root = false, have_mode = false, have_language = false;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--plan-only" && !result.plan_only) {
            result.plan_only = true;
            continue;
        }
        if (i + 1 >= argc) throw odh::FormatError("missing integrated game option value");
        const std::string value = argv[++i];
        if (key == "--game-root" && !have_root) {
            result.game_root = value;
            have_root = true;
        } else if (key == "--mode" && !have_mode) {
            result.mode = parse_number(value, 7, "neighborhood mode");
            have_mode = true;
        } else if (key == "--language" && !have_language) {
            result.language = parse_number(value, 20, "language code");
            have_language = true;
        } else if (key == "--house" && !result.house) {
            result.house = parse_number(value, 98, "house number");
        } else if (key == "--resident-id" && !result.resident_id) {
            result.resident_id = parse_number(value, 32767, "resident ID");
        } else {
            throw odh::FormatError("unknown or duplicate integrated game option");
        }
    }
    if (!have_mode || result.mode == 0 || result.mode == 6 || result.language == 0 ||
        (result.house && *result.house == 0) ||
        (result.resident_id && *result.resident_id == 0) ||
        (result.plan_only && !result.house))
        throw odh::FormatError(
            "Usage: odh-person [--game-root DIR] --mode 1..5,7 [--language 1..20] "
            "[--house 1..98] [--resident-id 1..32767] [--plan-only]");
#ifdef ODH_HAVE_LINUX_USER_CONFIG
    odh_host::LinuxUserConfig saved;
    std::string config_error;
    if (!odh_host::load_linux_user_config(saved, &config_error)) {
        if (!have_root)
            throw odh::FormatError("cannot read user settings; pass --game-root DIR to replace them: " + config_error);
        saved = odh_host::LinuxUserConfig{};
    }
    if (!have_root && !saved.data_root.empty()) {
        result.game_root = saved.data_root;
        have_root = true;
    }
    if (!have_language) result.language = static_cast<unsigned>(saved.language_code);
#endif
    if (!have_root)
        throw odh::FormatError("game data path is not set; pass --game-root DIR on first launch");
    return result;
}

bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& component : relative)
        if (component == "..") return false;
    return true;
}

std::filesystem::path require_directory(const std::filesystem::path& root,
                                        const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_directory(path, error) || error)
        throw odh::FormatError("required game directory is missing: " + path.string());
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !path_is_within(root, canonical))
        throw odh::FormatError("game directory resolves outside the selected installation: " + path.string());
    return canonical;
}

std::filesystem::path require_file(const std::filesystem::path& root,
                                   const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error || !std::filesystem::is_regular_file(status))
        throw odh::FormatError("required game file is missing or is not a regular file: " + path.string());
    auto canonical = std::filesystem::canonical(path, error);
    if (error) throw odh::FormatError("cannot resolve required game file: " + path.string());
    if (!path_is_within(root, canonical))
        throw odh::FormatError("game file resolves outside the selected installation: " + path.string());
    return canonical;
}

std::vector<std::filesystem::path> find_files(const std::filesystem::path& root,
        const std::string& exact_name, const std::set<std::string>& extensions,
        std::size_t maximum) {
    std::vector<std::filesystem::path> result;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(root, error), end;
    if (error) throw odh::FormatError("cannot scan installed game resources: " + root.string());
    for (; it != end; it.increment(error)) {
        if (error) throw odh::FormatError("error scanning installed game resources: " + root.string());
        const auto& path = it->path();
        if (it->is_symlink(error)) {
            if (error) throw odh::FormatError("cannot inspect installed game resource");
            if (it->is_directory(error)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(error)) {
            if (error) throw odh::FormatError("cannot inspect installed game resource");
            continue;
        }
        bool match = !exact_name.empty() && path.filename() == exact_name;
        if (!match && !extensions.empty()) {
            auto ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            match = extensions.count(ext) != 0;
        }
        if (!match) continue;
        if (result.size() >= maximum) throw odh::FormatError("installed game resource count exceeds limit");
        result.push_back(std::filesystem::canonical(path, error));
        if (error) throw odh::FormatError("cannot resolve installed game resource path");
    }
    std::sort(result.begin(), result.end());
    return result;
}

void append(std::vector<std::string>& target, std::initializer_list<std::string> values);

std::vector<std::string> build_runtime_arguments(const Options& options,
        const std::filesystem::path& game, unsigned house,
        const odh::SessionEntry& session, const odh::SessionEntryMember& resident) {
    const auto userdata = require_directory(game, game / "UserData");
    const auto gamedata = require_directory(game, game / "GameData");
    const auto global_far = require_file(game, gamedata / "Global/Global.far");
    const auto objects_far = require_file(game, gamedata / "Objects/Objects.far");
    const auto animation_far = require_file(game, gamedata / "Animation/Animation.far");
    const auto textures_far = require_file(game, gamedata / "Textures/Textures.far");
    const auto floors = require_file(game, gamedata / "floors.iff");
    const auto walls = require_file(game, gamedata / "walls.iff");
    const auto zoning = require_file(game, userdata / "LotZoning.iff");
    const auto neighborhood = require_file(game, userdata / "Neighborhood.iff");
    const auto house_file = require_file(game, session.house_file);
    const auto shared = std::vector<std::filesystem::path>{
        require_file(game, game / "ExpansionShared/ExpansionShared.far")};

    const auto characters = require_directory(game, userdata / "Characters");
    std::vector<std::filesystem::path> character_files;
    for (const auto& member : session.members) {
        const auto asset_path = std::filesystem::path(member.character_asset);
        if (asset_path.filename() != asset_path || asset_path.empty())
            throw odh::FormatError("character catalog returned an invalid asset name");
        const auto character = require_file(game, characters / asset_path);
        if (character.parent_path() != characters)
            throw odh::FormatError("character resource escaped UserData/Characters");
        if (std::find(character_files.begin(), character_files.end(), character) == character_files.end())
            character_files.push_back(character);
    }
    const auto selected_character = require_file(game, characters / resident.character_asset);
    if (std::find(character_files.begin(), character_files.end(), selected_character) == character_files.end())
        throw odh::FormatError("selected resident character resource is absent from the resolved family");
    const auto architecture = find_files(gamedata, "", {".flr", ".wll"}, 4096);

    std::vector<std::string> args{"odh-person", "--avatar-file", selected_character.string(),
        std::to_string(resident.object_definition_id), "--semiglobal", global_far.string(),
        "PersonGlobals.iff", "--global", global_far.string(), "Global.iff",
        "--lot", house_file.string(), std::to_string(house), "--zoning", zoning.string(),
        "--neighborhood", neighborhood.string(), "--activate-family", "--language",
        std::to_string(options.language), "--object-definitions", objects_far.string()};
    for (const auto& character : character_files)
        append(args, {"--object-definition-file", character.string()});
    append(args, {"--animation-source", animation_far.string()});
    for (const auto& archive : shared) append(args, {"--animation-source", archive.string()});
    append(args, {"--saved-world-main", "--render-window", "--render-saved-guid",
        std::to_string(resident.guid), "--render-animation-far", animation_far.string(),
        "--render-textures", textures_far.string(), "--render-skeleton",
        "adult-skeleton.cmx.bcf", "adult", "--render-floors", floors.string(),
        "--render-walls", walls.string(), "--render-level", "1", "--render-wall-mode",
        "up", "--render-terrain-theme", "0"});
    for (const auto& resource : architecture)
        append(args, {"--architecture-resource", resource.string()});
    return args;
}

void print_plan(const Options& options, unsigned house,
                const odh::SessionEntry& session, const odh::SessionEntryMember& resident,
                const std::vector<std::string>& args) {
    std::cout << "{\"status\":\"ready\",\"mode\":" << options.mode
        << ",\"house\":" << house << ",\"family_chunk_id\":" << session.family_chunk_id
        << ",\"resident_guid\":" << resident.guid << ",\"resident_neighbor_id\":"
        << resident.neighbor_id << ",\"resident_asset\":" << json_bytes(resident.character_asset)
        << ",\"resident_count\":" << session.members.size() << ",\"command\":[";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << json_bytes(args[i]);
    }
    std::cout << "]}\n";
}

void append(std::vector<std::string>& target, std::initializer_list<std::string> values) {
    target.insert(target.end(), values.begin(), values.end());
}

} // namespace

int odh_linux_game_entry(int argc, char** argv) {
    try {
        auto options = parse_options(argc, argv);
        std::error_code error;
        const auto game = std::filesystem::canonical(options.game_root, error);
        if (error || !std::filesystem::is_directory(game))
            throw odh::FormatError("game root is not an accessible directory: " + options.game_root.string());
        options.game_root = game;
        const auto userdata = require_directory(game, game / "UserData");
#ifdef ODH_HAVE_X11
        const auto graphics = require_file(game, game / "UIGraphics/UIGraphics.far");
#endif

#ifdef ODH_HAVE_X11
        std::optional<odh_host::PreparedNeighborhoodMap> map;
        if (!options.house) map = odh_host::prepare_neighborhood_map(userdata, graphics, options.mode);
        std::unique_ptr<odh_host::LinuxWindow> window;
        if (!options.plan_only)
            window = std::make_unique<odh_host::LinuxWindow>(map ? 800 : 320, map ? 600 : 240,
                "OpenDollhouse");
#else
        if (!options.plan_only)
            throw odh::FormatError("interactive game entry requires a build with X11 support");
        if (!options.house)
            throw odh::FormatError("--house is required for --plan-only without X11 support");
#endif

        unsigned house = options.house.value_or(0);
#ifdef ODH_HAVE_X11
        if (map) {
            const auto selected = odh_host::show_neighborhood_picker(*window, *map);
            if (!selected) {
                std::cout << "{\"status\":\"cancelled\",\"stage\":\"neighborhood\"}\n";
                return 0;
            }
            house = *selected;
        }
#endif

        const auto session = odh::load_session_entry(userdata, options.mode,
            static_cast<std::uint16_t>(house), static_cast<std::uint8_t>(options.language));
        const odh::SessionEntryMember* resident = &session.members.front();
        if (options.resident_id) {
            const auto found = std::find_if(session.members.begin(), session.members.end(),
                [&](const auto& member) { return member.neighbor_id == *options.resident_id; });
            if (found == session.members.end()) throw odh::FormatError("resident is not in the selected family");
            resident = &*found;
        }
        const auto arguments = build_runtime_arguments(options, game, house, session, *resident);
        if (options.plan_only) {
            print_plan(options, house, session, *resident, arguments);
            return 0;
        }
#ifdef ODH_HAVE_LINUX_USER_CONFIG
        odh_host::LinuxUserConfig saved;
        saved.language_code = static_cast<int>(options.language);
        saved.data_root = game;
        std::string config_error;
        if (!odh_host::save_linux_user_config(saved, &config_error))
            throw odh::FormatError("cannot save user settings: " + config_error);
#endif
        std::vector<char*> pointers;
        pointers.reserve(arguments.size());
        for (const auto& argument : arguments) pointers.push_back(const_cast<char*>(argument.c_str()));
#ifdef ODH_HAVE_X11
        return odh_person_run(static_cast<int>(pointers.size()), pointers.data(), window.get());
#else
        return odh_person_run(static_cast<int>(pointers.size()), pointers.data(), nullptr);
#endif
    } catch (const std::exception& error) {
        std::cerr << "odh-person: " << error.what() << '\n';
        return 2;
    }
}

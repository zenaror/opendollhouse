/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// Prepare the same disposable indexes that the handheld builds consume. The
// destination becomes visible only when the indexes and their stamp are complete.
#include "app/App.h"
#include "app/buy/UICatalog.h"
#include "content/ContentManager.h"
#include "files/FilePool.h"
#include "files/LoadProgress.h"
#include "files/SafeFile.h"
#include "files/formats/iff/chunks/STR.h"
#include "language_picker.hpp"
#include "vitaboy/content/TS1AvatarProvider.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "Usage: %s GAME_FOLDER NEW_OUTPUT_FOLDER [LANGUAGE]\n"
            "Use the game's language tag, such as en-US or pt-BR (default en-US).\n", argv[0]);
        return 2;
    }
    const auto language = odh_host::parse_game_language(argc == 4 ? argv[3] : "en-US");
    if (!language) {
        std::fprintf(stderr, "Unknown game language: %s\n", argv[3]);
        return 2;
    }
    fs::path staging;
    bool ownsStaging = false;
    try {
        const fs::path root = fs::canonical(argv[1]);
        if (!fs::is_regular_file(root / "GameData/Objects/Objects.far"))
            throw std::runtime_error("GameData/Objects/Objects.far is missing from the game folder");
        const auto output = fs::weakly_canonical(fs::absolute(argv[2]));
        const auto relative = output.lexically_relative(root);
        if (!relative.empty() && *relative.begin() != "..")
            throw std::runtime_error("Choose an output folder outside the game installation");
        if (fs::exists(fs::symlink_status(output)))
            throw std::runtime_error("The output folder already exists. Choose a new folder");
        staging = output;
        staging += ".partial";
        ownsStaging = fs::create_directory(staging);
        if (!ownsStaging) throw std::runtime_error("The preparation folder already exists: " + staging.string());

        namespace chunks = FSO::Files::Formats::IFF::Chunks;
        chunks::STR::DefaultLangCode = static_cast<chunks::STRLangCode>(*language);
        FSO::Files::LoadProgress::StepSink = [](const std::string& step) { std::printf("%s\n", step.c_str()); };
        const std::string data = root.generic_string(), cache = staging.generic_string();
        FSO::Content::Content::Init(data, cache);
        auto* objects = FSO::Content::Content::Get()->WorldObjects.get();
        const auto mainArchive = data + "/GameData/Objects/Objects.far";
        if (!objects || objects->ArchiveObjectCount == 0 ||
            std::none_of(objects->Archives.begin(), objects->Archives.end(),
                         [&](const auto& entry) { return entry.Path == mainArchive; }))
            throw std::runtime_error("Could not read the base game object archive");
        simapp::UICatalog::Get().Init(cache);
        FSO::Content::TS1::TS1AvatarContent::SetBasePath(data);
        auto* avatars = FSO::Content::TS1::TS1AvatarContent::Get();
        if (!avatars->EnsureInit()) throw std::runtime_error("Could not index avatar assets");
        avatars->BCF.AllSuitHosts();
        FSO::Files::InstallDirCache::Save();
        for (const char* name : {"objects.cache", "catalog.cache", "avatars.cache", "bcfnames.cache", "dirs.cache"}) {
            if (!fs::is_regular_file(staging / name) || !fs::file_size(staging / name))
                throw std::runtime_error(std::string("Missing index: ") + name);
        }
        const auto stamp = std::to_string(simapp::App::CacheVersion()) + " " + std::to_string(*language) + "\n";
        if (!FSO::Files::SafeFile::WriteText(cache + "/cache_version.txt", stamp))
            throw std::runtime_error("Could not finish the cache version stamp");
        fs::rename(staging, output);
        ownsStaging = false;
        std::printf("Prepared indexes for %zu objects in %zu archives. Copy the files from %s "
            "into the handheld cache folder for this same installation.\n",
            size_t(objects->ArchiveObjectCount), objects->Archives.size(), output.string().c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Cache preparation failed: %s\n", e.what());
        if (ownsStaging) { std::error_code ec; fs::remove_all(staging, ec); }
        return 1;
    }
    return 0;
}

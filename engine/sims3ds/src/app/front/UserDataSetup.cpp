/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — see UserDataSetup.h. TS1(sims): new code.
#include "files/SafeFile.h" // FIX(sims) SAVE
#include "content/TS1FamilyImport.h"
#include "content/TS1NeighbourProvider.h"
#include <set>
#include "files/FilePool.h"
#include "files/LoadProgress.h"
#include "app/front/UserDataSetup.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
// directory listing: POSIX hosts, and the 3DS when the build says devkitARM's libsysbase opendir is there
// (src/platform/3ds/Makefile passes -DST_HAVE_DIRENT); plain arm-none-eabi newlib has none (probing instead).
#if defined(ST_HAVE_DIRENT)
#include <dirent.h>
#define ST_USE_DIRENT 1
#elif !defined(__3DS__) && defined(__has_include)
#if __has_include(<dirent.h>)
#include <dirent.h>
#define ST_USE_DIRENT 1
#endif
#endif

namespace simapp { namespace front {

static const char* MARKER = "sims_setup.txt";
static const char* BOOTSTRAP = "sims_bootstrap.txt";
static const char* NEW_PENDING = "OpenDollhouse setup v2: new neighbourhood; family templates pending.\n";
static const char* NEW_COMPLETE = "OpenDollhouse setup v2: new neighbourhood; complete.\n";
static std::string ReadText(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    char text[256]; size_t n = fread(text, 1, sizeof text, f); fclose(f);
    return std::string(text, n);
}
bool UserDataSetup::PreparationPending(int index) const {
    const auto dir = NeighbourhoodDir(index);
    const auto intent = ReadText(dir + "/" + BOOTSTRAP);
    return !FileExists(dir + "/" + MARKER) &&
        (intent == "OpenDollhouse bootstrap v2: new\n" || intent == "OpenDollhouse bootstrap v2: import\n");
}
bool UserDataSetup::TemplateFamiliesPending(int index) const {
    return ReadText(NeighbourhoodDir(index) + "/" + MARKER) == NEW_PENDING;
}
bool UserDataSetup::CompleteTemplateFamilies(int index) const {
    return FSO::Files::SafeFile::WriteText(NeighbourhoodDir(index) + "/" + MARKER, NEW_COMPLETE);
}

std::string UserDataSetup::FolderName(int index) {
    return index <= 0 ? std::string("UserData") : "UserData" + std::to_string(index + 1);
}

bool UserDataSetup::FileExists(const std::string& p) {
    struct stat info;
    return stat(p.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool UserDataSetup::DirExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && (st.st_mode & S_IFDIR);
}

void UserDataSetup::MakeDirs(const std::string& p) {
    for (size_t i = 1; i <= p.size(); i++) {
        if (i == p.size() || p[i] == '/') {
            std::string sub = p.substr(0, i);
            if (!sub.empty() && sub.back() != ':' && !DirExists(sub)) {
#ifdef _WIN32
                ::_mkdir(sub.c_str());
#else
                mkdir(sub.c_str(), 0777);
#endif
            }
        }
    }
}

std::vector<std::string> UserDataSetup::ListFiles(const std::string& dir, const std::string& probePattern) {
    std::vector<std::string> r;
#ifdef ST_USE_DIRENT
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            if (FileExists(dir + "/" + n)) r.push_back(n);
        }
        closedir(d);
        std::sort(r.begin(), r.end());
        return r;
    }
#endif
    // no directory listing: probe "<pattern with %d>" names (e.g. "User%05d.iff"), stop after a long gap
    if (!probePattern.empty()) {
        int misses = 0;
        for (int i = 0; i < 2000 && misses < 64; i++) {
            char name[64];
            snprintf(name, sizeof name, probePattern.c_str(), i);
            if (FileExists(dir + "/" + name)) { r.push_back(name); misses = 0; }
            else misses++;
        }
    }
    return r;
}

void UserDataSetup::Configure(const std::string& dataRoot, const std::string& saveRoot) {
    _Data = dataRoot;
    _Save = saveRoot;
    _Jobs.clear();
    _FailedGroups.clear();
    _Next = _TotalBytes = _DoneBytes = 0;
}

static size_t FileSize(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return 0;
    return (size_t)st.st_size;
}

void UserDataSetup::Add(const std::string& src, const std::string& dst, const std::string& group) {
    if (!FileExists(src)) return;
    Job j;
    j.Src = src; j.Dst = dst; j.Size = FileSize(src); j.Group = group.empty() ? _PlanningDir : group;
    _Jobs.push_back(j);
    _TotalBytes += j.Size;
}

// Copy the complete save tree, including photo albums and arbitrary supported
// metadata. Only the destination service may create its native setup authority.
void UserDataSetup::PlanCopyTree(const std::string& source, const std::string& destination, const std::string& group) {
    bool opened = false;
    const auto entries = FSO::Files::ListDirectory(source, &opened);
    if (opened) {
        for (const auto& e : entries) {
            if (destination == group && (e.Name.compare(0, 14, "sims_setup.txt") == 0 || e.Name.compare(0, 18, "sims_bootstrap.txt") == 0)) continue;
            const auto dst = destination + "/" + e.Name;
            if (e.IsDir) { MakeDirs(dst); PlanCopyTree(source + "/" + e.Name, dst, group); }
            else if (!FileExists(dst)) Add(source + "/" + e.Name, dst, group);
        }
    } else {
#ifdef ST_USE_DIRENT
        // Failure on a host that can enumerate is not an empty save folder.
        _FailedGroups.insert(group);
        FSO::Files::LoadProgress::Step("WARNING: could not list " + source);
        return;
#endif
        // Hosts without directory enumeration retain the historic known-name
        // fallback. Linux uses the complete tree above.
        for (const auto& f : ListFiles(source, "User%05d.iff"))
            if (!FileExists(destination + "/" + f)) Add(source + "/" + f, destination + "/" + f, group);
    }
}

bool UserDataSetup::HasTemplates() const { return DirExists(_Data + "/TemplateUserData"); }

void UserDataSetup::Plan(int index, const std::string& source, bool fresh) {
    std::string dst = NeighbourhoodDir(index);
    if (!FSO::Content::TS1::IsPrivateNeighbourhood(_Data, _Save, dst)) return;
    if (FileExists(dst + "/" + MARKER) || !FileExists(source + "/Neighborhood.iff")) return;
    _FailedGroups.erase(dst);
    // Only this service's durable in-progress record permits resuming a copy.
    // An unmarked existing folder can be a deliberately edited original save.
    const std::string intent = fresh ? "OpenDollhouse bootstrap v2: new\n" : "OpenDollhouse bootstrap v2: import\n";
    if (DirExists(dst)) {
        if (ReadText(dst + "/" + BOOTSTRAP) != intent) return;
    } else {
        MakeDirs(dst);
        if (!FSO::Files::SafeFile::WriteText(dst + "/" + BOOTSTRAP, intent)) return;
    }
    MakeDirs(dst + "/Houses");
    MakeDirs(dst + "/Characters");
    _PlanningDir = dst;
    auto keep = [&](const std::string& d) { return !FileExists(d); }; // never overwrite the player's files
    const bool userLocations = false; // fresh bootstrap owns its in-progress metadata
    const bool userStreets = false;
    auto finish = [&]() {
        Job m; m.Dst = dst + "/" + MARKER; m.Marker = true; m.Group = dst;
        m.Text = fresh ? NEW_PENDING : "OpenDollhouse setup v2: imported existing neighbourhood.\n";
        _Jobs.push_back(m);
    };
    // Existing saves are copied as saves. Missing characters/houses are not
    // invitations to restore defaults from a different neighbourhood.
    if (!fresh) {
        PlanCopyTree(source, dst, dst);
        // Directory enumeration may already have scheduled these files. The
        // known-name fallback is still needed on no-dirent hosts, but must not
        // copy and durably commit the same destination a second time.
        std::set<std::string> queued;
        for(const auto& job:_Jobs)if(!job.Marker)queued.insert(job.Dst);
        const auto keepImport=[&](const std::string& path){return keep(path)&&queued.insert(path).second;};
        // Known-name probes preserve no-dirent hosts' previous save service.
        for (const auto& f : { "Neighborhood.iff", "LotZoning.iff", "LotLocations.iff", "StreetNames.iff" })
            if (keepImport(dst + "/" + f)) Add(source + "/" + f, dst + "/" + f, dst);
        for (const auto& f : ListFiles(source + "/Characters", "User%05d.iff"))
            if (keepImport(dst + "/Characters/" + f)) Add(source + "/Characters/" + f, dst + "/Characters/" + f, dst);
        for (int h = 0; h < 100; ++h) {
            char n[32]; snprintf(n, sizeof n, "/Houses/House%02d.iff", h);
            if (keepImport(dst + n)) Add(source + n, dst + n, dst);
        }
        finish(); return;
    }
    // the neighbourhood itself
    if (keep(dst + "/Neighborhood.iff")) Add(source + "/Neighborhood.iff", dst + "/Neighborhood.iff");
    if (keep(dst + "/LotZoning.iff")) Add(source + "/LotZoning.iff", dst + "/LotZoning.iff");
    for (int h = 0; h < 100; h++) {
        char n[32]; snprintf(n, sizeof n, "/Houses/House%02d.iff", h);
        if (keep(dst + n)) Add(source + n, dst + n);
    }
    for (auto& f : ListFiles(source + "/Characters", "User%05d.iff"))
        if (keep(dst + "/Characters/" + f)) Add(source + "/Characters/" + f, dst + "/Characters/" + f);
    // the expansion packs' template lots (installer). Later templates carry the more complete zoning /
    // street / lot location tables (Magic Town's lists every lot), so they replace the earlier ones.
    struct Tpl { const char* Dir; int First, Last; const char* Desc; };
    static const Tpl tpls[] = {
        { "TemplateCommunity", 50, 80, "NeighborhoodDesc.iff" },
        { "TemplateDowntown", 21, 30, "DTDesc.iff" },
        { "TemplateVacation", 40, 49, "VIDesc.iff" },
        { "TemplateStudiotown", 81, 89, "STDesc.iff" },
        { "TemplateMagicTown", 90, 99, "MTDesc.iff" },
    };
    bool userZoning = false; // already in the save area: the player's (rezoned) lots
    // FIX(sims): the Old Town homes of the Unleashed families (TemplateFamilyUnleashed/*.FAM: 56 63 69 76 77) are
    // not copied from TemplateCommunity: Maxis saved those with other GUIDs for the family members (14 missing
    // objects each). NhoodModel::Switch installs the families with their own houses (TS1FamilyImport::InstallTemplates)
    // the first time the neighbourhood is opened; until then the provider falls back to the template copy.
    std::set<int> famHouses;
    {
        std::string fd = _Data + "/TemplateFamilyUnleashed";
        auto fams = ListFiles(fd);
        if (fams.empty()) fams = { "Burb_11.FAM", "Charming_13.FAM", "Goth Sr_12.FAM", "Hick_10.FAM", "Kat_6.FAM", "Strays_4000.FAM" };
        for (auto& f : fams) {
            FSO::Content::TS1::TS1FamFile fam;
            if (FileExists(fd + "/" + f) && FSO::Content::TS1::TS1FamilyImport::Read(fd + "/" + f, fam) && fam.HouseNumber > 0) famHouses.insert(fam.HouseNumber);
        }
    }
    for (auto& t : tpls) {
        std::string td = _Data + "/" + t.Dir;
        if (!DirExists(td) && std::string(t.Dir) == "TemplateMagicTown") td = _Data + "/TemplateMagictown";
        if (!DirExists(td)) continue;
        for (int h = t.First; h <= t.Last; h++) {
            char n[32]; snprintf(n, sizeof n, "House%02d.iff", h);
            if (famHouses.count(h)) continue;
            if (keep(dst + "/Houses/" + n)) Add(td + "/" + n, dst + "/Houses/" + n);
        }
        if (keep(dst + "/Houses/" + t.Desc)) Add(td + "/" + t.Desc, dst + "/Houses/" + t.Desc);
        for (const char* meta : { "LotZoning.iff", "LotLocations.iff", "StreetNames.iff" }) {
            if ((std::string(meta) == "LotZoning.iff" && userZoning) ||
                (std::string(meta) == "LotLocations.iff" && userLocations) ||
                (std::string(meta) == "StreetNames.iff" && userStreets)) continue;
            Add(td + "/" + meta, dst + "/" + meta);
        }
    }
    std::string npcs = _Data + "/TemplateNPCs";
    for (auto& f : ListFiles(npcs)) {
        if (f.size() < 4) continue;
        std::string ext = f.substr(f.size() - 4);
        for (auto& c : ext) c = (char)tolower((unsigned char)c);
        if (ext == ".iff" && keep(dst + "/Characters/" + f)) Add(npcs + "/" + f, dst + "/Characters/" + f);
    }
    finish();
}

void UserDataSetup::PlanAll() {
    for (int i = 0; i < MaxNeighbourhoods; i++) {
        const auto dst = NeighbourhoodDir(i);
        const auto pending = ReadText(dst + "/" + BOOTSTRAP);
        if (pending == "OpenDollhouse bootstrap v2: new\n") Plan(i, _Data + "/TemplateUserData", true);
        else if (FileExists(_Data + "/" + FolderName(i) + "/Neighborhood.iff")) Plan(i, _Data + "/" + FolderName(i), false);
    }
}

void UserDataSetup::PlanNew(int index) {
    if (FileExists(_Data + "/TemplateUserData/Neighborhood.iff")) Plan(index, _Data + "/TemplateUserData", true);
}

bool UserDataSetup::Step(size_t budget, std::string* status) {
    size_t done = 0;
    std::vector<uint8_t> buf(64 * 1024);
    while (_Next < _Jobs.size() && done < budget) {
        Job& j = _Jobs[_Next++];
        if (status) *status = j.Dst;
        if (j.Marker) {
            if (!_FailedGroups.count(j.Group) && FSO::Files::SafeFile::WriteText(j.Dst, j.Text))
                remove((j.Group + "/" + BOOTSTRAP).c_str());
            continue;
        }
        FILE* in = fopen(j.Src.c_str(), "rb");
        if (!in) { _FailedGroups.insert(j.Group); continue; }
        // FIX(sims) SAVE: copied to <dst>.new and renamed when complete. Before, a copy cut by a power-off / HOME close
        // left a truncated file that the next start kept for good ("never overwrite the player's files").
        FILE* out = FSO::Files::SafeFile::BeginWrite(j.Dst);
        if (!out) { _FailedGroups.insert(j.Group); fclose(in); FSO::Files::LoadProgress::Step("WARNING: could not write " + j.Dst); continue; }
        size_t n;
        bool ok = true;
        while ((n = fread(buf.data(), 1, buf.size(), in)) > 0) {
            FSO::Files::FilePool::Stats.BytesRead += n; // TS1(sims): counted for the boot log / SD estimate
            if (fwrite(buf.data(), 1, n, out) != n) { ok = false; break; }
        }
        if (ferror(in)) ok = false;
        fclose(in);
        if (!FSO::Files::SafeFile::EndWrite(j.Dst, out, nullptr, ok)) // TS1(sims): SD card full / write error
            { _FailedGroups.insert(j.Group); FSO::Files::LoadProgress::Step("WARNING: could not write " + j.Dst + " (SD card full?)"); }
        done += j.Size;
        _DoneBytes += j.Size;
    }
    return _Next < _Jobs.size();
}

float UserDataSetup::Progress() const {
    if (_Jobs.empty()) return 1.0f;
    if (_TotalBytes == 0) return (float)_Next / (float)_Jobs.size();
    return std::min(1.0f, (float)_DoneBytes / (float)_TotalBytes);
}

std::vector<int> UserDataSetup::Available() const {
    std::vector<int> r;
    for (int i = 0; i < MaxNeighbourhoods; i++) {
        const auto dir = NeighbourhoodDir(i);
        if (!FSO::Content::TS1::IsPrivateNeighbourhood(_Data, _Save, dir)) {
            _Broken.erase(i); // A directory outside the save tree cannot be offered for repair.
            continue;
        }
        std::string nh = dir + "/Neighborhood.iff";
        if (PreparationPending(i)) { _Broken.insert(i); continue; }
        if (FileExists(nh)) {
            if (!_Broken.count(i)) r.push_back(i); // (damaged contents: NhoodModel::Switch validates and marks it)
            continue;
        }
        // FIX(sims) SAVE: set up (marker) but no Neighborhood.iff = an interrupted save (the user's v6 card): restore
        // the newest complete .new / .tmp / .bak, once per run. Healthy folders cost nothing extra here.
        if (_Checked.count(i) || !DirExists(NeighbourhoodDir(i))) continue;
        _Checked.insert(i);
        auto res = FSO::Files::SafeFile::Recover(nh, FSO::Files::SafeFile::IffValidator("NBRS"));
        if (res == FSO::Files::SafeFile::Result::Restored) { _Broken.erase(i); r.push_back(i); }
        else {
            _Broken.insert(i);
            FSO::Files::LoadProgress::Step("WARNING: " + FolderName(i) + "/Neighborhood.iff is missing and has no backup");
        }
    }
    return r;
}

std::vector<int> UserDataSetup::Broken() const {
    Available();
    return std::vector<int>(_Broken.begin(), _Broken.end());
}

bool UserDataSetup::CanRestore(int index) const {
    return FSO::Content::TS1::IsPrivateNeighbourhood(_Data, _Save, NeighbourhoodDir(index)) &&
        (FileExists(_Data + "/" + FolderName(index) + "/Neighborhood.iff") || FileExists(_Data + "/TemplateUserData/Neighborhood.iff"));
}

bool UserDataSetup::RestoreFromInstall(int index, std::string* error, std::string* log) {
    auto say = [&](const std::string& l) { printf("[save] %s\n", l.c_str()); fflush(stdout); if (log) *log += l + "\n"; };
    if (!CanRestore(index)) { if (error) *error = "The game's original of this neighborhood was not found."; return false; }
    std::string dir = NeighbourhoodDir(index);
    FSO::Files::FilePool::CloseAll(); // no handle may stay open inside the folder (the 3DS refuses to rename it)
    if (DirExists(dir)) {
        std::string aside;
        for (int n = 0; n < 100; n++) {
            aside = dir + ".damaged" + (n ? std::to_string(n + 1) : std::string());
            if (!DirExists(aside) && !FileExists(aside)) break;
        }
        if (rename(dir.c_str(), aside.c_str()) == 0) say("restore: moved " + dir + " to " + aside);
        else {
            // cannot move the folder: move the neighbourhood file and the marker aside, the copy then fills in only
            // what is missing (the player's houses / characters stay)
            int e = errno;
            say("restore: cannot rename " + dir + " (" + std::to_string(e) + " " + strerror(e) + "): replacing Neighborhood.iff only");
            std::string nh = dir + "/Neighborhood.iff";
            if (FileExists(nh)) { remove((nh + ".damaged").c_str()); rename(nh.c_str(), (nh + ".damaged").c_str()); }
            remove((dir + "/" + MARKER).c_str());
            // Preserve the legacy family-install receipt; it grants no bootstrap authority.
        }
    }
    size_t jobs = _Jobs.size();
    // Explicit restore retains the displaced folder, and imports the selected
    // install save as-is. If rename failed, only replace Neighborhood.iff.
    if (DirExists(dir)) {
        Add(_Data + "/" + FolderName(index) + "/Neighborhood.iff", dir + "/Neighborhood.iff", dir);
        if (_Jobs.size() == jobs) Add(_Data + "/TemplateUserData/Neighborhood.iff", dir + "/Neighborhood.iff", dir);
    } else if (FileExists(_Data + "/" + FolderName(index) + "/Neighborhood.iff"))
        Plan(index, _Data + "/" + FolderName(index), false);
    else PlanNew(index);
    if (_Jobs.size() == jobs) { if (error) *error = "Nothing to copy for " + dir; return false; }
    while (Step(4u << 20)) {}
    _Checked.erase(index);
    _Broken.erase(index);
    bool ok = FileExists(dir + "/Neighborhood.iff");
    say(std::string("restore: ") + FolderName(index) + (ok ? " copied from the original game" : " FAILED (SD card full?)"));
    if (!ok && error) *error = "Could not copy the neighborhood (SD card full?).";
    return ok;
}

}}

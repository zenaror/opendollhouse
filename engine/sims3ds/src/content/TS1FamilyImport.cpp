/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// TS1(sims): see TS1FamilyImport.h.
#include "content/TS1FamilyImport.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "content/WorldContentFiles.h"
#include "files/formats/iff/chunks/NBRS.h"
#include "core/io.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace FSO { namespace Content { namespace TS1 {

using FSO::Files::Formats::IFF::Chunks::NBRS;
using FSO::Files::Formats::IFF::Chunks::Neighbour;

static std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static int32_t LE32(const std::vector<uint8_t>& d, size_t at) { return at + 4 <= d.size() ? (int32_t)(d[at] | (d[at + 1] << 8) | (d[at + 2] << 16) | ((uint32_t)d[at + 3] << 24)) : 0; }
static void SetLE32(std::vector<uint8_t>& d, size_t at, int32_t v) { if (at + 4 <= d.size()) { d[at] = (uint8_t)v; d[at + 1] = (uint8_t)(v >> 8); d[at + 2] = (uint8_t)(v >> 16); d[at + 3] = (uint8_t)(v >> 24); } }
static bool Exists(const std::string& p) { struct stat info; return stat(p.c_str(), &info) == 0 && S_ISREG(info.st_mode); }
static std::string Pad2(int32_t n) { char b[16]; snprintf(b, sizeof b, "%02d", n); return b; }

static bool ParseNBRS(const std::vector<uint8_t>& data, NBRS& out) {
    st::Reader r(data, st::Endian::Little);
    try { out.Read(nullptr, r); } catch (...) { return false; }
    return true;
}

// A consumed installer receipt authorizes only the exact installed FAM. It
// does not authorize restoring deleted/moved families or replacing edited lots.
static bool TrustedInstallerFamily(const TS1FamFile& fam, const std::string& root) {
    if (root.empty()) return false;
    auto slash = fam.Path.find_last_of("/\\");
    auto name = fam.Path.substr(slash == std::string::npos ? 0 : slash + 1);
    if (Lower(name).size() >= 8 && Lower(name).substr(name.size() - 8) == ".applied") name.resize(name.size() - 8);
    if (Lower(name).size() < 4 || Lower(name).substr(name.size() - 4) != ".fam") return false;
    std::vector<uint8_t> actual, installed;
    return RawIff::ReadFile(fam.Path, actual) &&
        RawIff::ReadFile(root + "/TemplateFamilyUnleashed/" + name, installed) && actual == installed;
}

static bool PublishInstallerHouse(const std::string& userDir, const TS1FamFile& fam,
        int32_t house, const std::string& root, bool allowMissing, TS1FamilyImport::Result& result) {
    if (house <= 0 || fam.HouseChunks.empty()) return true;
    const auto name = "House" + Pad2(house) + ".iff";
    const auto path = userDir + "/Houses/" + name;
    struct stat info;
#if defined(__linux__)
    const auto inspect = [](const std::string& p, struct stat& st) { return lstat(p.c_str(), &st); };
#else
    const auto inspect = [](const std::string& p, struct stat& st) { return stat(p.c_str(), &st); };
#endif
    if (inspect(path, info) != 0) {
        if (errno != ENOENT) { result.Error = "cannot inspect " + path; return false; }
        if (!allowMissing) return true; // absence after a consumed receipt is the player's state
        if (!RawIff::WriteFileAtomic(path, RawIff::Build(fam.HouseChunks))) {
            result.Error = "cannot write " + path; return false;
        }
        result.Files.push_back(path); return true;
    }
    if (!S_ISREG(info.st_mode)) { result.Error = "family house is not a local file: " + path; return false; }
    if (house != fam.HouseNumber || !TrustedInstallerFamily(fam, root)) return true;
    std::vector<uint8_t> current;
    if (!RawIff::ReadFile(path, current)) { result.Error = "cannot read " + path; return false; }
    const auto desired = RawIff::Build(fam.HouseChunks);
    if (current == desired) return true;
    bool untouched = false;
    for (const auto& source : {root + "/TemplateCommunity/" + name, root + "/TemplateUserData/Houses/" + name}) {
        std::vector<uint8_t> bytes;
        if (RawIff::ReadFile(source, bytes) && bytes == current) untouched = true;
    }
    if (!untouched) return true;
    // Keep a permanent, independently readable preimage. SafeFile's rotating
    // .bak belongs to its atomic write and is removed after successful commit.
    const auto backup = path + ".pre-family-template";
    if (inspect(backup, info) == 0) {
        std::vector<uint8_t> bytes;
        if (!S_ISREG(info.st_mode) || !RawIff::ReadFile(backup, bytes) || bytes != current) {
            result.Error = "conflicting family house preimage: " + backup; return false;
        }
    } else {
        if (errno != ENOENT || !RawIff::WriteFileAtomic(backup, current)) {
            result.Error = "cannot preserve " + backup; return false;
        }
    }
    std::vector<uint8_t> preserved, reread;
    if (!RawIff::ReadFile(backup, preserved) || preserved != current ||
        !RawIff::ReadFile(path, reread) || reread != current) {
        result.Error = "family house changed before installer repair: " + path; return false;
    }
    if (!RawIff::WriteFileAtomic(path, desired)) { result.Error = "cannot write " + path; return false; }
    result.Files.push_back(backup); result.Files.push_back(path); return true;
}

// A receipt is evidence of work already committed, never a queue to replay.
// Verify the exact current household before repairing only its untouched lot.
static TS1FamilyImport::Result RepairAppliedHouse(const std::string& userDir,
        const TS1FamFile& fam, const std::string& root) {
    TS1FamilyImport::Result result; result.Ok = result.Skipped = true;
    if (fam.HouseNumber <= 0 || !TrustedInstallerFamily(fam, root)) return result;
    std::vector<uint8_t> bytes; std::vector<RawIffChunk> chunks;
    if (!RawIff::ReadFile(userDir + "/Neighborhood.iff", bytes) || !RawIff::Parse(bytes, chunks)) {
        result.Ok = false; result.Error = "cannot read neighbourhood for family house repair"; return result;
    }
    NBRS nbrs; bool haveNbrs = false;
    for (const auto& chunk : chunks) if (chunk.Type == "NBRS" && !haveNbrs) haveNbrs = ParseNBRS(chunk.Data, nbrs);
    if (!haveNbrs) { result.Ok = false; result.Error = "bad NBRS for family house repair"; return result; }
    std::set<uint32_t> expected;
    for (const auto& member : fam.Members) {
        if (!nbrs.DefaultNeighbourByGUID.count(member.GUID)) return result;
        expected.insert(member.GUID);
    }
    bool exact = false;
    for (const auto& chunk : chunks) {
        if (chunk.Type != "FAMI" || LE32(chunk.Data, 12) != fam.HouseNumber ||
            LE32(chunk.Data, 36) != int32_t(expected.size()) || chunk.Data.size() < 40 + 4 * expected.size()) continue;
        std::set<uint32_t> actual;
        for (size_t i = 0; i < expected.size(); ++i) actual.insert(uint32_t(LE32(chunk.Data, 40 + 4 * i)));
        if (actual == expected) { exact = true; result.FamilyID = int16_t(chunk.ID); break; }
    }
    if (!exact) return result;
    // A roster alone is insufficient: all imported person definitions must
    // still exist. Never recreate or reset a character from a consumed receipt.
    for (auto guid : expected) {
        const auto matches = std::count_if(nbrs.Entries.begin(), nbrs.Entries.end(), [guid](const std::unique_ptr<Neighbour>& n) {
            return n->Unknown1 == 1 && n->GUID == guid;
        });
        if (matches != 1) return result;
        const auto* neighbour = nbrs.NeighbourByID.at(nbrs.DefaultNeighbourByGUID.at(guid));
        if (neighbour->Name.empty() || neighbour->Name.find_first_of("/\\") != std::string::npos) return result;
        const auto file = WorldContentFiles::Find(userDir + "/Characters", neighbour->Name + ".iff");
        std::vector<uint8_t> data; std::vector<RawIffChunk> character;
        if (file.empty() || !RawIff::ReadFile(file, data) || !RawIff::Parse(data, character)) return result;
        size_t definitions = 0;
        for (const auto& chunk : character) if (chunk.Type == "OBJD" && chunk.Data.size() >= 32 &&
            uint32_t(LE32(chunk.Data, 28)) == guid) ++definitions;
        if (definitions != 1) return result;
    }
    result.HouseNumber = fam.HouseNumber;
    result.Ok = PublishInstallerHouse(userDir, fam, fam.HouseNumber, root, false, result);
    result.Skipped = result.Files.empty(); return result;
}

bool TS1FamilyImport::Read(const std::string& path, TS1FamFile& fam, std::string* err) {
    std::vector<uint8_t> bytes;
    std::vector<RawIffChunk> chunks;
    if (!RawIff::ReadFile(path, bytes) || !RawIff::Parse(bytes, chunks)) { if (err) *err = "cannot read " + path; return false; }
    fam = TS1FamFile();
    fam.Path = path;
    const RawIffChunk* fami = nullptr;
    for (auto& c : chunks) if (c.Type == "FAMI") { fami = &c; break; }
    if (fami == nullptr || fami->Data.size() < 40) { if (err) *err = "no FAMI in " + path; return false; }
    fam.FamilyID = (int16_t)fami->ID;
    fam.FAMIData = fami->Data;
    fam.HouseNumber = LE32(fami->Data, 12);
    fam.Budget = LE32(fami->Data, 20);
    int32_t n = LE32(fami->Data, 36);
    for (int32_t i = 0; i < n && 40 + 4 * (size_t)i + 4 <= fami->Data.size(); i++) fam.GUIDs.push_back((uint32_t)LE32(fami->Data, 40 + 4 * (size_t)i));
    NBRS nbrs;
    bool haveNbrs = false;
    for (auto& c : chunks) {
        if (c.Type == "FAMs" && c.ID == fami->ID) { auto v = RawIff::ReadSTRValues(c.Data); if (!v.empty()) fam.Name = v[0]; }
        if ((c.Type == "FAMs" || c.Type == "FAMh") && c.ID == fami->ID) fam.FamilyChunks.push_back(c);
        if (c.Type == "NBRS" && !haveNbrs) haveNbrs = ParseNBRS(c.Data, nbrs);
    }
    if (!haveNbrs) { if (err) *err = "no NBRS in " + path; return false; }
    for (auto& e : nbrs.Entries) if (e->Unknown1 == 1) fam.OldNeighbourGUIDs.push_back({ e->NeighbourID, e->GUID });
    for (auto guid : fam.GUIDs) {
        auto it = nbrs.DefaultNeighbourByGUID.find(guid);
        if (it == nbrs.DefaultNeighbourByGUID.end()) continue;
        auto ne = nbrs.NeighbourByID[it->second];
        TS1FamFile::Member m;
        m.GUID = guid;
        m.OldNeighbourID = ne->NeighbourID;
        m.UserName = Lower(ne->Name);
        m.PersonMode = ne->PersonMode;
        m.PersonData = ne->PersonData;
        for (auto& kv : ne->Relationships) m.Relationships.push_back(kv);
        std::sort(m.Relationships.begin(), m.Relationships.end(), [](const std::pair<int32_t, std::vector<int16_t>>& a, const std::pair<int32_t, std::vector<int16_t>>& b) { return a.first < b.first; });
        uint16_t id = (uint16_t)ne->NeighbourID;
        for (auto& c : chunks) {
            if (c.Type == "uChr" && c.ID == id) m.BodyStrings = RawIff::ReadSTRValues(c.Data);
            if (c.Type == "BMP_" && c.ID == id) m.Thumbnail = c;
            if (c.Type == "CTSS" && c.ID == (uint16_t)(2000 + id)) m.CTSS = c;
        }
        // the plain CTSS #id: name, bio with the "Filename:" line
        if (auto c = RawIff::Find(chunks, "CTSS", id)) {
            auto v = RawIff::ReadSTRValues(c->Data);
            if (v.size() > 0) m.FirstName = v[0];
            if (v.size() > 1) { m.Bio = v[1]; auto f = m.Bio.find("\r\n\nFilename:"); if (f != std::string::npos) m.Bio = m.Bio.substr(0, f + 2); }
            if (m.CTSS.Type.empty()) { m.CTSS = *c; }
        }
        if (m.BodyStrings.empty()) continue;
        fam.Members.push_back(std::move(m));
    }
    if (fam.HouseNumber > 0) {
        static const std::set<std::string> family = { "FAMI", "FAMs", "FAMh", "EXPi", "NBRS", "uChr", "XXXX", "rsmp" };
        for (auto& c : chunks) {
            if (family.count(c.Type)) continue;
            if (c.Type == "CTSS" && c.ID != 0) continue;       // the members' names (the house has none of its own)
            if (c.Type == "BMP_" && c.ID < 512) continue;      // the members' thumbnails
            fam.HouseChunks.push_back(c);
        }
    }
    return !fam.Members.empty();
}

TS1FamilyImport::Result TS1FamilyImport::Import(const std::string& userDir, const TS1FamFile& fam, const Options& opts) {
    Result res;
    auto content = Content::Get();
    auto prov = content ? content->Neighborhood.get() : nullptr;
    std::string nbhPath = userDir + "/Neighborhood.iff";
    std::vector<uint8_t> nbhBytes;
    std::vector<RawIffChunk> nbh;
    if (!RawIff::ReadFile(nbhPath, nbhBytes) || !RawIff::Parse(nbhBytes, nbh)) { res.Error = "cannot read " + nbhPath; return res; }
    RawIffChunk* nbrsChunk = nullptr;
    NBRS nbrs;
    std::set<int> famIDs;
    std::set<int32_t> occupied;
    int32_t maxFamilyNumber = 0;
    for (auto& c : nbh) {
        if (c.Type == "NBRS" && nbrsChunk == nullptr) { nbrsChunk = &c; if (!ParseNBRS(c.Data, nbrs)) { res.Error = "bad NBRS"; return res; } }
        if (c.Type == "FAMI") {
            famIDs.insert(c.ID);
            maxFamilyNumber = std::max(maxFamilyNumber, LE32(c.Data, 16));
            if (LE32(c.Data, 12) > 0 && LE32(c.Data, 36) > 0) occupied.insert(LE32(c.Data, 12));
        }
    }
    if (nbrsChunk == nullptr) { res.Error = "Neighborhood.iff has no NBRS"; return res; }
    auto writeMissingHouse = [&](int32_t house) {
        if (house <= 0 || !opts.WriteHouse || fam.HouseChunks.empty()) return true;
        return PublishInstallerHouse(userDir, fam, house,
            opts.BootstrapRecovery ? opts.TemplateRoot : std::string(), true, res);
    };
    bool anyPresent = false, allPresent = true;
    for (const auto& m : fam.Members) {
        const bool present = nbrs.DefaultNeighbourByGUID.count(m.GUID) != 0;
        anyPresent = anyPresent || present; allPresent = allPresent && present;
    }
    if (anyPresent) {
        if (opts.BootstrapRecovery) {
            // A pending new bootstrap may have committed this family before a
            // previous version failed to write its house. Verify the complete
            // family identity before repairing that missing file.
            bool matched = false;
            for (const auto& c : nbh) {
                if (c.Type != "FAMI" || LE32(c.Data, 36) != (int32_t)fam.Members.size()) continue;
                std::set<uint32_t> guids;
                for (size_t i = 0; i < fam.Members.size(); ++i) guids.insert((uint32_t)LE32(c.Data, 40 + 4 * i));
                bool same = allPresent;
                for (const auto& m : fam.Members) same = same && guids.count(m.GUID) != 0;
                if (!same) continue;
                const int32_t house = LE32(c.Data, 12);
                if (house > 0 && house != fam.HouseNumber) { res.Error = "bootstrap family moved to a different house"; return res; }
                if (!writeMissingHouse(house)) return res;
                res.FamilyID = (int16_t)c.ID; res.HouseNumber = house; matched = true; break;
            }
            if (!matched) { res.Error = "incomplete or conflicting template family identity"; return res; }
        }
        res.Skipped = true; res.Ok = true; res.Error = "already in the neighbourhood"; return res;
    }

    // ---- family id / house
    int16_t famID = fam.FamilyID;
    if (famID <= 0 || famIDs.count(famID)) { famID = 1; while (famIDs.count(famID)) famID++; }
    res.FamilyID = famID;
    int32_t house = 0;
    if (opts.MoveIntoHouse && fam.HouseNumber > 0 && !occupied.count(fam.HouseNumber)) house = fam.HouseNumber;
    res.HouseNumber = house;

    // ---- neighbour ids: lowest free per member
    std::set<int16_t> used;
    for (auto& kv : nbrs.NeighbourByID) used.insert(kv.first);
    std::map<int32_t, int16_t> idMap; // old neighbour id -> new
    for (auto& m : fam.Members) { int16_t id = 1; while (used.count(id)) id++; used.insert(id); idMap[m.OldNeighbourID] = id; }
    for (auto& og : fam.OldNeighbourGUIDs) // others: the same Sim (by GUID) in this neighbourhood
        if (!idMap.count(og.first)) { auto it = nbrs.DefaultNeighbourByGUID.find(og.second); if (it != nbrs.DefaultNeighbourByGUID.end()) idMap[og.first] = it->second; }

    // ---- character files
    std::set<std::string> names;
    for (auto& e : nbrs.Entries) if (e->Unknown1 == 1) names.insert(Lower(e->Name));
    std::set<uint32_t> guidsUsed;
    for (auto& e : nbrs.Entries) if (e->Unknown1 == 1) guidsUsed.insert(e->GUID);
    for (auto& m : fam.Members) {
        TS1CharacterFiles::NewCharacter nc;
        nc.GUID = m.GUID;
        if (!opts.BootstrapRecovery && (guidsUsed.count(nc.GUID) || (content && content->WorldObjects && content->WorldObjects->Entries.count(nc.GUID))))
            nc.GUID = TS1CharacterFiles::NewGUID(nullptr, guidsUsed);
        guidsUsed.insert(nc.GUID);
        auto kind = m.BodyStrings[0];
        nc.TemplateGUID = (kind == "dog") ? TS1CharacterFiles::TEMPLATE_DOG_GUID : (kind == "kat" || kind == "cat") ? TS1CharacterFiles::TEMPLATE_CAT_GUID
                                                                               : TS1CharacterFiles::TEMPLATE_PERSON_GUID;
        nc.FirstName = m.FirstName;
        nc.Bio = m.Bio;
        nc.BodyStrings = m.BodyStrings;
        if (!m.CTSS.Type.empty()) { RawIffChunk c = m.CTSS; c.ID = 2000; c.Label = ""; nc.Extra.push_back(c); }
        if (!m.Thumbnail.Type.empty()) { RawIffChunk b = m.Thumbnail; b.ID = 2004; b.Label = "thumbnail"; nc.Extra.push_back(b); }
        int preferred = -1;
        if (m.UserName.size() == 9 && m.UserName.compare(0, 4, "user") == 0) preferred = atoi(m.UserName.c_str() + 4);
        std::string userName, path, err;
        if (opts.BootstrapRecovery) {
            // A failed pre-commit attempt can leave characters, including in
            // the current process's object index. Reuse only the exact output
            // this template would generate; never remap GUIDs embedded in its house.
            for (const auto& candidate : WorldContentFiles::FilesWithExt(userDir + "/Characters", ".iff", false)) {
                std::vector<uint8_t> bytes;
                std::vector<RawIffChunk> chunks;
                if (!RawIff::ReadFile(candidate, bytes) || !RawIff::Parse(bytes, chunks)) continue;
                bool matchingGUID = false;
                for (const auto& chunk : chunks)
                    if (chunk.Type == "OBJD" && (uint32_t)LE32(chunk.Data, 28) == m.GUID) matchingGUID = true;
                if (!matchingGUID) continue;
                auto slash = candidate.find_last_of("/\\");
                std::string name = Lower(candidate.substr(slash == std::string::npos ? 0 : slash + 1));
                name = name.substr(0, name.size() - 4);
                std::vector<uint8_t> templateBytes;
                std::vector<RawIffChunk> templateChunks;
                if (!TS1CharacterFiles::LoadObjectIff(nc.TemplateGUID, templateBytes) || !RawIff::Parse(templateBytes, templateChunks) ||
                    RawIff::Build(TS1CharacterFiles::Clone(templateChunks, nc, name)) != bytes || names.count(name)) {
                    res.Error = "conflicting bootstrap character identity"; return res;
                }
                if (!path.empty()) { res.Error = "duplicate bootstrap character GUID"; return res; }
                path = candidate; userName = name;
            }
            if (path.empty() && content && content->WorldObjects && content->WorldObjects->Entries.count(nc.GUID)) {
                res.Error = "template GUID conflicts with an existing object"; return res;
            }
        }
        if (path.empty()) {
            userName = TS1CharacterFiles::NextUserName(userDir + "/Characters", names, preferred);
            path = TS1CharacterFiles::WriteCharacter(userDir, nc, userName, &err);
        }
        names.insert(userName);
        if (path.empty()) { res.Error = err; return res; }
        res.Files.push_back(path);
        res.GUIDs.push_back(nc.GUID);
        // the neighbour record
        auto n = new Neighbour();
        n->Unknown1 = 1;
        n->Version = 4;
        n->Name = userName;
        n->PersonMode = m.PersonMode;
        n->PersonData = m.PersonData;
        n->PersonData.resize(88, 0);
        n->PersonData[61] = famID; // 'family number'
        n->NeighbourID = idMap[m.OldNeighbourID];
        n->GUID = nc.GUID;
        n->UnknownNegOne = -1;
        for (auto& r : m.Relationships) { auto it = idMap.find(r.first); if (it != idMap.end()) n->Relationships[it->second] = r.second; }
        nbrs.Entries.emplace_back(n);
        nbrs.NeighbourByID[n->NeighbourID] = n;
        nbrs.DefaultNeighbourByGUID[n->GUID] = n->NeighbourID;
        res.NeighbourIDs.push_back(n->NeighbourID);
    }
    { st::Writer w(st::Endian::Little); nbrs.Write(nullptr, w); nbrsChunk->Data = w.ToArray(); }

    // ---- FAMI (the .FAM's own record, patched), FAMs, FAMh
    RawIffChunk fc; fc.Type = "FAMI"; fc.ID = (uint16_t)famID; fc.Data = fam.FAMIData;
    SetLE32(fc.Data, 12, house);
    SetLE32(fc.Data, 16, maxFamilyNumber + 1);
    for (size_t i = 0; i < res.GUIDs.size(); i++) SetLE32(fc.Data, 40 + 4 * i, (int32_t)res.GUIDs[i]);
    std::vector<RawIffChunk> add = { fc };
    for (auto c : fam.FamilyChunks) { c.ID = (uint16_t)famID; add.push_back(c); }
    for (auto& c : add) {
        size_t at = nbh.size();
        for (size_t i = 0; i < nbh.size(); i++) if (nbh[i].Type == c.Type) at = i + 1;
        if (at == nbh.size()) for (size_t i = 0; i < nbh.size(); i++) if (nbh[i].Type == "rsmp") { at = i; break; }
        nbh.insert(nbh.begin() + (ptrdiff_t)at, c);
    }
    // Publish family membership only after the required house is durable.
    // On retry, an existing house remains untouched.
    if (!writeMissingHouse(house)) return res;
    if (!RawIff::WriteFileAtomic(nbhPath, RawIff::Build(nbh))) { res.Error = "cannot write " + nbhPath; return res; }

    res.Ok = true;
    return res;
}

std::vector<TS1FamilyImport::Result> TS1FamilyImport::ApplyPendingPatches(const std::string& userDir, const std::string& installRoot) {
    std::vector<Result> out;
    const auto dir = userDir + "/Patch";
    struct stat info;
    // The caller admits only a private save. Its queue must remain inside it;
    // never rename a file through a symlink back into commercial installation.
#if defined(__linux__)
    const auto inspect = [](const std::string& path, struct stat& st) { return lstat(path.c_str(), &st); };
#else
    const auto inspect = [](const std::string& path, struct stat& st) { return stat(path.c_str(), &st); };
#endif
    if (inspect(dir, info) != 0) {
        if (errno != ENOENT) { Result r; r.Error = "cannot inspect family patch queue " + dir; out.push_back(r); }
        return out;
    }
    if (!S_ISDIR(info.st_mode)) { Result r; r.Error = "family patch queue is not a local directory: " + dir; out.push_back(r); return out; }
    std::vector<std::string> files;
#ifdef ST_WCF_DIRENT
    DIR* queue = opendir(dir.c_str());
    if (!queue) { Result r; r.Error = "cannot open family patch queue " + dir; out.push_back(r); return out; }
    bool listed = true;
    for (;;) {
        errno = 0; const auto* entry = readdir(queue);
        if (!entry) { listed = errno == 0; break; }
        const std::string name = entry->d_name;
        if (name != "." && name != "..") files.push_back(name);
    }
    if (closedir(queue) != 0) listed = false;
    if (!listed) { Result r; r.Error = "cannot list complete family patch queue " + dir; out.push_back(r); return out; }
#else
    Result unavailable; unavailable.Error = "this platform cannot enumerate the pending family patch queue";
    out.push_back(unavailable); return out;
#endif
    std::sort(files.begin(), files.end());
    for (const auto& filename : files) {
        const auto lower = Lower(filename);
        const bool applied = lower.size() >= 12 && lower.substr(lower.size() - 12) == ".fam.applied";
        if (!applied && (lower.size() < 4 || lower.substr(lower.size() - 4) != ".fam")) continue;
        if (applied && installRoot.empty()) continue;
        Result result;
        const auto path = dir + "/" + filename;
        const auto receipt = path + ".applied";
        if (filename.find_first_of("/\\") != std::string::npos ||
            inspect(path, info) != 0 || !S_ISREG(info.st_mode)) {
            result.Error = "family patch is not a local file: " + path; out.push_back(result); break;
        }
        if (!applied && (inspect(receipt, info) == 0 || errno != ENOENT)) {
            result.Error = "family patch receipt already exists or cannot be inspected: " + receipt;
            out.push_back(result); break;
        }
        TS1FamFile family; family.Path = path;
        if (applied && !TrustedInstallerFamily(family, installRoot)) continue;
        if (!Read(path, family, &result.Error)) {
            if (result.Error.empty()) result.Error = "invalid family patch " + path;
            out.push_back(result); break;
        }
        if (applied) {
            result = RepairAppliedHouse(userDir, family, installRoot);
            if (!result.Ok || !result.Skipped) out.push_back(result);
            if (!result.Ok) break;
            continue;
        }
        Options options; options.TemplateRoot = installRoot;
        // The queue is explicit installer authority. Preserve the house's
        // member GUIDs and permit an interrupted commit to finish its house.
        options.BootstrapRecovery = true;
        result = Import(userDir, family, options);
        if (result.Ok && rename(path.c_str(), receipt.c_str()) != 0) {
            result.Ok = false; result.Error = "family patch committed but queue receipt failed: " + path;
        }
        out.push_back(result);
        if (!result.Ok) break;
    }
    return out;
}

std::vector<TS1FamilyImport::Result> TS1FamilyImport::InstallTemplates(const std::string& userDir, const std::string& installRoot, bool bootstrapRecovery) {
    std::vector<Result> out;
    auto dir = installRoot + "/TemplateFamilyUnleashed";
    auto files = WorldContentFiles::List(dir);
    if (files.empty()) // builds without dirent: the six families the Unleashed installer ships
        for (auto n : { "Burb_11.FAM", "Charming_13.FAM", "Goth Sr_12.FAM", "Hick_10.FAM", "Kat_6.FAM", "Strays_4000.FAM" })
            if (Exists(dir + "/" + n)) files.push_back(n);
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        if (Lower(f).size() < 4 || Lower(f).substr(Lower(f).size() - 4) != ".fam") continue;
        TS1FamFile fam;
        Result r;
        if (!Read(dir + "/" + f, fam, &r.Error)) { out.push_back(r); continue; }
        Options options; options.BootstrapRecovery = bootstrapRecovery; options.TemplateRoot = installRoot;
        out.push_back(Import(userDir, fam, options));
    }
    return out;
}

}}}

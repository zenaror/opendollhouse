/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims app — see UICatalog.h (FreeSims UI/Controls/Catalog/UICatalog.cs + FreeSO TS1ObjectProvider catalogue)
#include "files/SafeFile.h" // FIX(sims) SAVE
#include "app/buy/UICatalog.h"
#ifndef __3DS__
#include "app/ui/OriginalTextEncoding.h"
#include "app/ui/OriginalPCUI.h"
#include "content/TS1PersonalText.h"
#include "files/formats/iff/chunks/OBJD.h"
#endif
#include "app/Font.h"
#include "app/ui/UIGraphics.h"
#include "content/ContentManager.h"
#include "content/WorldWallProvider.h"
#include "content/WorldFloorProvider.h"
#include "content/model/WallFloor.h"
#include "files/FAR1/FAR1Archive.h"
#include "files/FilePool.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/BMP.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "files/formats/iff/chunks/SPR.h"
#include "files/formats/iff/chunks/SPR2.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

namespace simapp {

using FSO::Content::Content;

// UI order -> OBJD bit (see the header)
const uint16_t UICatalog::FunctionBits[8] = { 0x01, 0x02, 0x20, 0x08, 0x04, 0x10, 0x80, 0x40 };
const uint16_t UICatalog::RoomBits[8] = { 0x08, 0x20, 0x02, 0x80, 0x01, 0x04, 0x10, 0x40 };

UICatalog& UICatalog::Get() {
    static UICatalog instance;
    return instance;
}


// TS1(sims): the catalogue scan only needs the OBJD chunks: read them through the file's resource map
// (IffFile::ReadFromFile "OBJD", ~20 KB per file instead of the whole file: ~680 MB with every expansion pack) and
// lay them out as a minimal IFF image (64-byte header + OBJD chunks) for ScanIff.
static bool ElementFile(const UICatalogElement& e, std::string& path, int64_t& base, int64_t& len) {
    auto content = Content::Get();
    if (content == nullptr) return false;
    auto objs = content->WorldObjects.get();
    base = 0;
    if (e.FarIndex >= 0 && e.FarIndex < (int)objs->Archives.size()) {
        auto& arch = objs->Archives[(size_t)e.FarIndex];
        auto it = arch.EntriesByName.find(FSO::Content::ToLowerInvariant(e.File));
        if (it == arch.EntriesByName.end()) return false;
        path = arch.Path; base = it->second.DataOffset; len = it->second.DataLength;
        return true;
    }
    path = e.File;
    len = FSO::Files::FilePool::Size(path);
    return len >= 0;
}

static bool ReadObjdChunks(const UICatalogElement& e, std::vector<uint8_t>& out) {
    std::string path;
    int64_t base = 0, len = 0;
    if (!ElementFile(e, path, base, len)) return false;
    FSO::Files::Formats::IFF::IffFile iff;
    try { iff.ReadFromFile(path, base, (uint32_t)len, true, "OBJD"); } catch (...) { return false; }
    out.assign(64, 0);
    for (auto* c : iff.SilentListAll()) {
        if (c->ChunkType != "OBJD" || c->ChunkProcessed) continue;
        uint32_t size = (uint32_t)c->ChunkData.size() + 76;
        uint8_t h[76] = { 'O', 'B', 'J', 'D', (uint8_t)(size >> 24), (uint8_t)(size >> 16), (uint8_t)(size >> 8), (uint8_t)size,
                          (uint8_t)(c->ChunkID >> 8), (uint8_t)c->ChunkID, (uint8_t)(c->ChunkFlags >> 8), (uint8_t)c->ChunkFlags };
        memcpy(h + 12, c->ChunkLabel.data(), std::min<size_t>(64, c->ChunkLabel.size()));
        out.insert(out.end(), h, h + 76);
        out.insert(out.end(), c->ChunkData.begin(), c->ChunkData.end());
    }
    return out.size() > 64;
}

static inline uint32_t BE32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline uint16_t BE16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

// TS1(sims): raw IFF walk (header 64 bytes, chunk header type/size/id/flags/label = 76 bytes) so the scan
// does not build an IffFile per object.
static void ScanIff(const std::vector<uint8_t>& d, int32_t farIndex, const std::string& file, std::vector<UICatalogElement>& out,
                    const std::set<uint32_t>& known, std::set<uint32_t>& seen) {
    size_t p = 64;
    while (p + 76 <= d.size()) {
        uint32_t size = BE32(&d[p + 4]);
        if (size < 76 || p + size > d.size()) break;
        if (memcmp(&d[p], "OBJD", 4) == 0) {
            const uint8_t* b = &d[p + 76];
            size_t blen = size - 76;
            if (blen >= 4 + 2 * 68) {
                size_t n = (blen - 4) / 2;
                auto f = [&](size_t i) -> uint16_t { return i < n ? (uint16_t)(b[4 + 2 * i] | (b[5 + 2 * i] << 8)) : (uint16_t)0; };
                uint32_t guid = (uint32_t)f(12) | ((uint32_t)f(13) << 16);
                uint16_t numGraphics = f(2), masterID = f(8), disabled = f(14), price = f(16);
                int16_t subIndex = (int16_t)f(9);
                uint16_t room = f(37), func = f(38), ctss = f(39), bmt = f(67);
                bool multiTile = masterID != 0;
                //does this object appear in the catalog? (FreeSO TS1ObjectProvider)
                if ((func > 0 || (bmt > 0 && bmt < 0xFFFF)) && disabled == 0 && (multiTile || numGraphics > 0)
                    && (masterID == 0 || subIndex == -1) && known.count(guid) && !seen.count(guid)) {
                    UICatalogElement e;
                    e.GUID = guid;
                    e.Price = price;
                    char label[65]; memcpy(label, &d[p + 12], 64); label[64] = 0;
                    e.Name = label;
                    e.FunctionFlags = func; e.RoomFlags = room; e.BuildModeType = (bmt == 0xFFFF) ? 0 : bmt;
                    e.Subsort = f(92); e.CommunitySubsort = f(97);
                    e.DowntownSubsort = f(93); e.VacationSubsort = f(95);
                    e.StudioSubsort = f(101); e.MagicSubsort = f(102);
                    e.CatalogStringsID = ctss;
                    e.InitialDepreciation = (int16_t)f(33); e.DepreciationLimit = (int16_t)f(36);
                    for (int i = 0; i < 7; i++) e.Ratings[i] = (int16_t)f(80 + (size_t)i);
                    e.SkillFlags = f(87);
                    e.FarIndex = farIndex; e.File = file;
                    out.push_back(e);
                    seen.insert(guid);
                }
            }
        }
        p += size;
    }
}

void UICatalog::Scan(bool standalone) {
    auto content = Content::Get();
    if (!content) return;
    auto objs = content->WorldObjects.get();
    std::map<std::pair<int32_t, std::string>, std::set<uint32_t>> files;
    auto collect = [&](const auto& entries) {
        for (const auto& kv : entries) {
            const auto& ref = kv.second;
            if ((ref.FarIndex < 0) != standalone || ref.Person) continue;
            const auto file = standalone ? ref.FileName : ref.FileName + ".iff";
            files[{ref.FarIndex, file}].insert((uint32_t)kv.first);
        }
    };
    collect(objs->Entries);
    if (!standalone) collect(objs->ReplacedArchiveEntries);
    std::set<uint32_t> seen;
    std::vector<uint8_t> bytes;
    for (const auto& file : files) {
        UICatalogElement probe;
        probe.FarIndex = file.first.first;
        probe.File = file.first.second;
        bytes.clear();
        if (ReadObjdChunks(probe, bytes))
            ScanIff(bytes, probe.FarIndex, probe.File, _Objects, file.second, seen);
    }
}

static const char* CACHE_MAGIC = "STCAT4";

bool UICatalog::LoadCache(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    auto content = Content::Get();
    char magic[16] = { 0 }; unsigned known = 0, count = 0;
    bool ok = fscanf(fp, "%15s %u %u\n", magic, &known, &count) == 3 && strcmp(magic, CACHE_MAGIC) == 0
              && known == (unsigned)content->WorldObjects->ArchiveObjectCount && count <= known;
    if (ok) {
        char line[1024];
        for (unsigned i = 0; i < count && fgets(line, sizeof line, fp); i++) {
            UICatalogElement e;
            unsigned guid, price, func, room, bmt, sub, csub, dsub, vsub, ssub, msub, ctss; int far_, idep, dlim;
            int r[7]; unsigned skill;
            char file[256] = { 0 }, name[128] = { 0 };
            // tab separated: the label and file name may contain spaces
            int n = sscanf(line, "%x\t%u\t%x\t%x\t%u\t%x\t%x\t%x\t%x\t%x\t%x\t%u\t%d\t%d\t%d\t%d %d %d %d %d %d %d\t%x\t%255[^\t]\t%127[^\n]",
                           &guid, &price, &func, &room, &bmt, &sub, &csub, &dsub, &vsub, &ssub, &msub, &ctss, &far_, &idep, &dlim,
                           &r[0], &r[1], &r[2], &r[3], &r[4], &r[5], &r[6], &skill, file, name);
            if (n != 25) { ok = false; break; }
            e.GUID = guid; e.Price = price; e.FunctionFlags = (uint16_t)func; e.RoomFlags = (uint16_t)room;
            e.BuildModeType = (uint16_t)bmt; e.Subsort = (uint16_t)sub; e.CommunitySubsort = (uint16_t)csub;
            e.DowntownSubsort = (uint16_t)dsub; e.VacationSubsort = (uint16_t)vsub;
            e.StudioSubsort = (uint16_t)ssub; e.MagicSubsort = (uint16_t)msub;
            e.CatalogStringsID = (uint16_t)ctss; e.FarIndex = far_; e.InitialDepreciation = (int16_t)idep;
            e.DepreciationLimit = (int16_t)dlim;
            for (int k = 0; k < 7; k++) e.Ratings[k] = (int16_t)r[k];
            e.SkillFlags = (uint16_t)skill;
            e.File = file; e.Name = name;
            _Objects.push_back(e);
        }
    }
    if (_Objects.size() != count) ok = false; // A truncated cache cannot hide the rest of a catalog.
    fclose(fp);
    if (!ok) _Objects.clear();
    return ok;
}

void UICatalog::SaveCache(const std::string& path) {
    FILE* fp = FSO::Files::SafeFile::BeginWrite(path); // FIX(sims) SAVE: a cut cache must not be read as a short catalogue
    if (!fp) return;
    auto content = Content::Get();
    fprintf(fp, "%s %u %u\n", CACHE_MAGIC, (unsigned)content->WorldObjects->ArchiveObjectCount, (unsigned)_Objects.size());
    for (auto& e : _Objects) {
        std::string name = e.Name;
        for (auto& ch : name) if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
        fprintf(fp, "%X\t%u\t%X\t%X\t%u\t%X\t%X\t%X\t%X\t%X\t%X\t%u\t%d\t%d\t%d\t%d %d %d %d %d %d %d\t%X\t%s\t%s\n",
                e.GUID, e.Price, e.FunctionFlags, e.RoomFlags, e.BuildModeType, e.Subsort, e.CommunitySubsort,
                e.DowntownSubsort, e.VacationSubsort, e.StudioSubsort, e.MagicSubsort,
                e.CatalogStringsID, e.FarIndex, e.InitialDepreciation, e.DepreciationLimit,
                e.Ratings[0], e.Ratings[1], e.Ratings[2], e.Ratings[3], e.Ratings[4], e.Ratings[5], e.Ratings[6],
                e.SkillFlags, e.File.c_str(), name.empty() ? "?" : name.c_str());
    }
    FSO::Files::SafeFile::EndWrite(path, fp);
}

void UICatalog::AddSpecials() {
    _Specials.clear();
    auto content = Content::Get();
    // AddWallpapers (C# category 8)
    for (auto& kv : content->WorldWalls->Entries) {
        auto& wall = kv.second;
        if (wall.ID == 0 || wall.Price < 0) continue;
        UICatalogElement e;
        e.Special = UICatalogElement::SpecialType::Wallpaper;
        e.ResID = wall.ID; e.Price = (uint32_t)wall.Price; e.Name = wall.Name;
        _Specials.push_back(e);
    }
    // AddFloors (C# category 9; ids >= 65534 are pool/water tiles, category 5)
    for (auto& kv : content->WorldFloors->Entries) {
        auto& floor = kv.second;
        if (floor.ID == 0 || floor.ID >= 65534 || floor.Price < 0) continue;
        UICatalogElement e;
        e.Special = UICatalogElement::SpecialType::Floor;
        e.ResID = floor.ID; e.Price = (uint32_t)floor.Price; e.Name = floor.Name;
        _Specials.push_back(e);
    }
    // AddWallStyles (C# category 7): wall, picket fence, iron fence, privacy fence, banisters
    for (int i = 0; i < 5; i++) {
        auto style = content->WorldWalls->GetWallStyle(FSO::Content::WorldWallProvider::WallStyleIDs[i]);
        if (style == nullptr) continue;
        UICatalogElement e;
        e.Special = UICatalogElement::SpecialType::WallStyle;
        e.ResID = FSO::Content::WorldWallProvider::WallStyleIDs[i];
        e.Pattern = FSO::Content::WorldWallProvider::WallStylePatterns[i];
        e.Price = (uint32_t)std::max<int>(0, style->Price); e.Name = style->Name;
        _Specials.push_back(e);
    }
}

void UICatalog::Init(const std::string& cacheDir) {
    if (_Ready) return;
    std::string path = cacheDir + "/catalog.cache";
    if (!LoadCache(path)) {
        Scan(false);
        SaveCache(path);
        printf("[catalog] scanned %zu catalogue objects -> %s\n", _Objects.size(), path.c_str());
    }
    const auto& entries = Content::Get()->WorldObjects->Entries;
    _Objects.erase(std::remove_if(_Objects.begin(), _Objects.end(), [&](const auto& item) {
        const auto winner = entries.find(item.GUID);
        return winner == entries.end() || winner->second.FarIndex < 0;
    }), _Objects.end());
    Scan(true);
    AddSpecials();
    _Ready = true;
}

// C# CatalogSorter: by price
void UICatalog::Sort(std::vector<const UICatalogElement*>& v) {
    std::stable_sort(v.begin(), v.end(), [](const UICatalogElement* a, const UICatalogElement* b) { return a->Price < b->Price; });
}

std::vector<const UICatalogElement*> UICatalog::ByFunction(int index, int subsort) const {
    std::vector<const UICatalogElement*> r;
    if (index < 0 || index > 7) return r;
    for (auto& e : _Objects) {
        if (!(e.FunctionFlags & FunctionBits[index])) continue;
        uint16_t sub = EffectiveSubsort(e);
        if (sub == 0) continue; // FIX(sims): not sold on this kind of lot
        if (subsort >= 0) {
            bool other = (sub & 0x0F) == 0;
            if (subsort == 4 ? !other : !(sub & (1 << subsort))) continue;
        }
        r.push_back(&e);
    }
    Sort(r);
    return r;
}

std::vector<const UICatalogElement*> UICatalog::ByRoom(int index) const {
    std::vector<const UICatalogElement*> r;
    if (index < 0 || index > 7) return r;
    for (auto& e : _Objects) if ((e.RoomFlags & RoomBits[index]) && e.FunctionFlags && EffectiveSubsort(e) != 0) r.push_back(&e); // FIX(sims)
    Sort(r);
    return r;
}

std::vector<const UICatalogElement*> UICatalog::ByPublicCategory(int index) const {
    std::vector<const UICatalogElement*> result;
    if(Type==CatalogType::Residential || index<0 || index>4)return result;
    // Four authored destination groups, then Other (bit7), matching the
    // reference provider's public catalog. Bit4 is not the fifth button.
    const auto mask = index == 4 ? 0x80u : (1u << index);
    for(const auto& item:_Objects)
        if(item.FunctionFlags && (EffectiveSubsort(item)&mask))result.push_back(&item);
    Sort(result);return result;
}

std::vector<const UICatalogElement*> UICatalog::ByBuildType(int type) const {
    std::vector<const UICatalogElement*> r;
    for (auto& e : _Objects) if (e.FunctionFlags == 0 && e.BuildModeType == type) r.push_back(&e);
    Sort(r);
    return r;
}

std::vector<const UICatalogElement*> UICatalog::Wallpapers() const {
    std::vector<const UICatalogElement*> r;
    for (auto& e : _Specials) if (e.Special == UICatalogElement::SpecialType::Wallpaper) r.push_back(&e);
    Sort(r);
    return r;
}

std::vector<const UICatalogElement*> UICatalog::Floors() const {
    std::vector<const UICatalogElement*> r;
    for (auto& e : _Specials) if (e.Special == UICatalogElement::SpecialType::Floor) r.push_back(&e);
    Sort(r);
    return r;
}

std::vector<const UICatalogElement*> UICatalog::WallStyles() const {
    std::vector<const UICatalogElement*> r;
    for (auto& e : _Specials) if (e.Special == UICatalogElement::SpecialType::WallStyle) r.push_back(&e);
    return r; // C# order (wall, fences, banisters)
}

const UICatalogElement* UICatalog::ByGUID(uint32_t guid) const {
    for (auto& e : _Objects) if (e.GUID == guid) return &e;
    return nullptr;
}

// ---------------------------------------------------------------- details

static void IconFromSPR(FSO::Files::Formats::IFF::Chunks::SPR* spr, int frame, Canvas& out) {
    if (spr == nullptr || frame >= (int)spr->Frames.size()) return;
    auto& f = spr->Frames[(size_t)frame];
    auto data = f->ExpandColors(); // FIX(sims): palettised frames
    if (f->Width <= 0 || data.size() < (size_t)f->Width * f->Height) return;
    out.Resize(f->Width, f->Height);
    for (size_t i = 0; i < out.Px.size(); i++) out.Px[i] = data[i].PackedValue();
}

static void IconFromSPR2(FSO::Files::Formats::IFF::Chunks::SPR2* spr, Canvas& out) {
    if (spr == nullptr || spr->Frames.empty()) return;
    auto& f = spr->Frames[0];
    f->DecodeIfRequired();
    if (f->Width <= 0 || !f->HasColor() || f->PalData.size() + f->PixelData.size() < (size_t)f->Width * f->Height) return;
    out.Resize(f->Width, f->Height);
    for (size_t i = 0; i < out.Px.size(); i++) out.Px[i] = f->ColorAt(i).PackedValue(); // FIX(sims): palettised frames
}

static bool DecodeCatalogArtwork(FSO::Files::Formats::IFF::Chunks::BMP* bmp, Canvas& normal, Canvas& active, bool requireStrip = false) {
    normal = Canvas(); active = Canvas();
    if (!bmp || !UIGraphics::DecodeBMP(bmp->GetData(), normal) || normal.Px.empty()) return false;
    // A mislabeled catalog reference may use another authored Catalog chunk,
    // but only its established normal/active button strip can be recovered.
    if (requireStrip && (normal.Height > 48 ||
        (normal.Width != 2 * normal.Height && normal.Width != 4 * normal.Height))) {
        normal = Canvas(); return false;
    }
    // catalogue icons also key on their corner colour (App IconFor)
    uint32_t keyc = normal.Px[0] & 0x00FFFFFFu;
    if (normal.Px[0] >> 24) for (auto& p : normal.Px) if ((p & 0x00FFFFFFu) == keyc) p = 0;
    // FIX(sims) FB1: a TS1 catalogue BMP_ holds the button frames side by side (74x37 = normal | active;
    // wider than 2:1 = four frames). FreeSO UICatalogItem draws one frame (Width/2 or Width/4, the active one
    // when selected / hovered); the whole strip was drawn, so every item showed its picture twice.
    const Canvas& all = normal;
    int frames = (all.Height > 0 && all.Width / all.Height > 2) ? 4 : (all.Height > 48 || all.Width == all.Height) ? 1 : 2;
    if (frames > 1) {
        int fw = all.Width / frames;
        Canvas f0(fw, all.Height), f1(fw, all.Height);
        for (int y = 0; y < all.Height; y++)
            for (int x = 0; x < fw; x++) {
                f0.Px[(size_t)y * fw + x] = all.Px[(size_t)y * all.Width + x];
                f1.Px[(size_t)y * fw + x] = all.Px[(size_t)y * all.Width + fw + x];
            }
        active = std::move(f1);
        normal = std::move(f0);
    }
    return true;
}

static bool IsCatalogLabel(const std::string& label) {
    const auto first = label.find_first_not_of(" \t\r\n");
    const auto last = label.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || last - first + 1 != 7) return false;
    static const char name[] = "catalog";
    for (size_t i = 0; i < 7; ++i) {
        const unsigned char ch = static_cast<unsigned char>(label[first + i]);
        if ((ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch) != name[i]) return false;
    }
    return true;
}

static bool DecodeUniqueAuthoredCatalog(FSO::Files::Formats::IFF::IffFile* resource,
        Canvas& normal, Canvas& active) {
    if (!resource) return false;
    FSO::Files::Formats::IFF::IffChunk* candidate = nullptr;
    // Use the selected/PIFF-patched resource, never a second bare FAR read or
    // another object's artwork. Multiple authored candidates remain ambiguous.
    for (auto* chunk : resource->SilentListAll()) {
        if (!chunk || chunk->ChunkType != "BMP_" || !IsCatalogLabel(chunk->ChunkLabel)) continue;
        if (candidate) return false;
        candidate = chunk;
    }
    if (!candidate) return false;
    try {
        return DecodeCatalogArtwork(resource->Get<FSO::Files::Formats::IFF::Chunks::BMP>(candidate->ChunkID),
            normal, active, true);
    } catch (...) { return false; }
}

#ifndef __3DS__
static void PreparedCatalogIcon(std::uint32_t guid, Canvas& icon) {
    const auto result=OriginalPCUI::BuildCatalogThumbnail(guid);
    if(result.status!=odh_host::CatalogObjectThumbnailStatus::ready)return;
    const auto& image=result.image;
    if(!image.width||!image.height||image.pixels.size()!=std::size_t(image.width)*image.height)return;
    // Retain only a small grid image in the existing bounded Details cache.
    // Preserve transparent pixels and the source aspect ratio; the popup owns
    // its separate full-size result, not this per-item grid presentation.
    const unsigned side=std::max(image.width,image.height);
    const unsigned target=std::min(37U,side); // Never enlarge a small authored sprite.
    const unsigned w=std::max(1U,image.width*target/side),h=std::max(1U,image.height*target/side);
    icon.Resize(int(w),int(h));
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const unsigned sx=std::min(image.width-1,((2*x+1)*image.width)/(2*w));
        const unsigned sy=std::min(image.height-1,((2*y+1)*image.height)/(2*h));
        const auto& p=image.pixels[std::size_t(sy)*image.width+sx];
        icon.Px[std::size_t(y)*w+x]=RGBA(p[0],p[1],p[2],p[3]);
    }
}
#endif

const UICatalog::Details& UICatalog::GetDetails(const UICatalogElement& e) {
    uint64_t key = (e.Special == UICatalogElement::SpecialType::None) ? (uint64_t)e.GUID : (((uint64_t)e.Special << 32) | e.ResID);
    auto it = _Details.find(key);
    if (it != _Details.end()) return it->second;
    Details& d = _Details[key];
    _DetailOrder.push_back(key);
    d.Loaded = true;
    d.Name = e.Name;
#ifndef __3DS__
    bool catalog_text_resolved=false;
#endif
    auto content = Content::Get();
    using namespace FSO::Files::Formats::IFF;
    switch (e.Special) {
        case UICatalogElement::SpecialType::None: {
#ifndef __3DS__
            // The provider owns override/PIFF precedence and lazy bytes. Reading
            // a new bare FAR slice here would bypass its selected patch set.
            auto selected=content->WorldObjects->Get(e.GUID,true);
            if(!selected||!selected->OBJ||!selected->Resource)break;
            const auto strings=selected->OBJ->CatalogStringsID;
            Chunks::CTSS* ctss=nullptr;
            try { ctss=selected->Resource->Get<Chunks::CTSS>(strings); } catch (...) { ctss=nullptr; }
#else
            // TS1(sims): portable catalog reads only its resource map + CTSS/BMP.
            std::string path;
            int64_t base = 0, len = 0;
            if (!ElementFile(e, path, base, len)) break;
            IffFile iff;
            try { iff.ReadFromFile(path, base, (uint32_t)len, true); } catch (...) { break; }
            Chunks::CTSS* ctss = nullptr;
            try { ctss = iff.Get<Chunks::CTSS>(e.CatalogStringsID); } catch (...) { ctss = nullptr; }
#endif
            if (ctss != nullptr) {
#ifndef __3DS__
                const auto text=FSO::Content::TS1::ResolvePersonalText(ctss);
                if(!text.Ambiguous&&(text.SourceLanguage||text.UTF8)){
                    const auto name=text.Value(0);
                    if(!name.empty())d.NameUTF8=original::PersonalToUTF8(name,text.SourceLanguage,text.UTF8);
                    else d.NameUTF8=original::LegacyToUTF8(d.Name);
                    d.DescriptionUTF8=original::PersonalToUTF8(text.Value(1),text.SourceLanguage,text.UTF8);
                    // Preserve source encoding/language before the compatibility
                    // Latin-1 fields are prepared; UTF-8 formats stay UTF-8.
                    d.Name=d.NameUTF8;d.Description=d.DescriptionUTF8;
                    catalog_text_resolved=true;
                }
#else
                auto n = ctss->GetString(0), desc = ctss->GetString(1);
                if (!n.empty()) d.Name = n;
                d.Description = desc;
#endif
            }
            Chunks::BMP* bmp = nullptr;
#ifndef __3DS__
            try { bmp = selected->Resource->Get<Chunks::BMP>(strings); } catch (...) { bmp = nullptr; }
#else
            try { bmp = iff.Get<Chunks::BMP>(e.CatalogStringsID); } catch (...) { bmp = nullptr; }
#endif
            if (bmp) DecodeCatalogArtwork(bmp, d.Icon, d.IconActive);
#ifndef __3DS__
            else if(!DecodeUniqueAuthoredCatalog(selected->Resource->MainIff(), d.Icon, d.IconActive))
                PreparedCatalogIcon(e.GUID,d.Icon);
#else
            else DecodeUniqueAuthoredCatalog(&iff, d.Icon, d.IconActive);
#endif
            break;
        }
        case UICatalogElement::SpecialType::Wallpaper: {
            auto it2 = content->WorldWalls->Entries.find(e.ResID);
            if (it2 != content->WorldWalls->Entries.end()) d.Description = it2->second.Description;
            auto globals = content->WorldWalls->Globals();
            auto catalog = it2 != content->WorldWalls->Entries.end() && it2->second.FileName == "global" && globals
                ? globals->Get<Chunks::BMP>(uint16_t(512 + e.ResID)) : nullptr;
            if (!DecodeCatalogArtwork(catalog, d.Icon, d.IconActive)) {
                // Custom patterns without authored catalog art retain the existing
                // medium-wall thumbnail. Never treat the two-frame strip as one icon.
                auto wall = content->WorldWalls->Get(e.ResID, true);
                if (wall != nullptr) IconFromSPR(wall->Medium ? wall->Medium : wall->Far, 2, d.Icon);
            }
            break;
        }
        case UICatalogElement::SpecialType::Floor: {
            auto it2 = content->WorldFloors->Entries.find(e.ResID);
            if (it2 != content->WorldFloors->Entries.end()) d.Description = it2->second.Description;
            auto globals = content->WorldFloors->Globals();
            auto catalog = it2 != content->WorldFloors->Entries.end() && it2->second.FileName == "global" && globals
                ? globals->Get<Chunks::BMP>(e.ResID) : nullptr;
            if (!DecodeCatalogArtwork(catalog, d.Icon, d.IconActive)) {
                auto floor = content->WorldFloors->Get(e.ResID, true);
                if (floor != nullptr) IconFromSPR2(floor->Near ? floor->Near : floor->Medium, d.Icon);
            }
            break;
        }
        case UICatalogElement::SpecialType::WallStyle: {
            auto style = content->WorldWalls->GetWallStyle(e.ResID);
            if (style != nullptr) {
                d.Description = style->Description;
                // TS1(sims): the catalogue picture of a wall style is walls.iff BMP_ <style id> ("Catalog"), the
                // wallpapers' are BMP_ 512 + id; the walls-up sprite is the fallback
                auto wg = content->WorldWalls->Globals();
                auto bmp = wg ? wg->Get<Chunks::BMP>(e.ResID) : nullptr;
                if (!DecodeCatalogArtwork(bmp, d.Icon, d.IconActive)) {
                    IconFromSPR(style->WallsUpMedium ? style->WallsUpMedium : style->WallsUpFar, 0, d.Icon);
                }
            }
            break;
        }
    }
#ifndef __3DS__
    if(!catalog_text_resolved){
        d.NameUTF8 = original::LegacyToUTF8(d.Name);
        d.DescriptionUTF8 = original::LegacyToUTF8(d.Description);
    }
#else
    d.NameUTF8 = d.Name;
    d.DescriptionUTF8 = d.Description;
#endif
    while (!d.NameUTF8.empty() && (unsigned char)d.NameUTF8.back() <= 32) d.NameUTF8.pop_back();
    d.Name = ToLatin1(d.Name);
    // FIX(sims) REL: some catalogue names end in a line break ("Precarious Designs D.I.Y. Bookshelf\r\n"), drawn as "??"
    while (!d.Name.empty() && (unsigned char)d.Name.back() <= 32) d.Name.pop_back();
    d.Description = ToLatin1(d.Description);
    return d;
}

void UICatalog::TrimDetails(size_t keep) {
    while (_DetailOrder.size() > keep) {
        _Details.erase(_DetailOrder.front());
        _DetailOrder.erase(_DetailOrder.begin());
    }
}

}

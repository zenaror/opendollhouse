/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// TS1(sims): complete Neighborhood.iff writer; see the header.
#include "content/TS1NeighbourhoodWriter.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/IffFieldEncode.h"
#include "files/formats/iff/IffFieldEncodeWriter.h"
#include "files/formats/iff/chunks/NBRS.h"
#include "files/formats/iff/chunks/NGBH.h"
#include "files/formats/iff/chunks/TTAT.h"
#include "files/formats/iff/chunks/FAMI.h"
#include "core/io.h"
#include "files/LoadProgress.h"
#include <algorithm>
#include <climits>
#include <map>
#include <set>

using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::Content::TS1::TS1NeighborhoodProvider;

namespace {

void Put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }
void Put32(std::vector<uint8_t>& o, uint32_t v) { for (int k = 0; k < 4; k++) o.push_back((uint8_t)(v >> (8 * k))); }
uint32_t LE32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t LE16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/// NBRS payload kept lossless: per slot the original bytes, and for valid neighbours the parsed fields.
struct NBRSRaw {
    std::vector<uint8_t> Header; // pad, version, "SRBN"
    struct Rel { int32_t KeyCount = 1; int32_t Key = 0; std::vector<int32_t> Values; };
    struct Slot {
        bool Valid = false;
        std::vector<uint8_t> Prefix;      // unknown1 .. person mode, as saved
        int32_t Version = 0, PersonMode = 0;
        std::vector<int16_t> PersonData;  // all of them (0xA0 or 0x200 bytes)
        int16_t NeighbourID = 0;
        uint32_t GUID = 0;
        int32_t NegOne = -1;
        std::vector<Rel> Rels;
        std::vector<uint8_t> Raw;
    };
    std::vector<Slot> Slots;
    std::vector<uint8_t> Tail;

    bool Parse(const std::vector<uint8_t>& d) {
        if (d.size() < 16) return false;
        Header.assign(d.begin(), d.begin() + 12);
        uint32_t count = LE32(d.data() + 12);
        size_t p = 16;
        auto need = [&](size_t n) { if (p + n > d.size()) throw std::runtime_error("NBRS truncated"); };
        try {
            for (uint32_t i = 0; i < count && p + 4 <= d.size(); i++) {
                Slot s;
                size_t start = p;
                int32_t u1 = (int32_t)LE32(d.data() + p); p += 4;
                if (u1 != 1) { s.Raw.assign(d.begin() + (long)start, d.begin() + (long)p); Slots.push_back(s); continue; }
                s.Valid = true;
                need(4); s.Version = (int32_t)LE32(d.data() + p); p += 4;
                if (s.Version == 0xA) p += 4;
                size_t ns = p;
                while (p < d.size() && d[p] != 0) p++;
                size_t nameLen = p - ns;
                p++;
                if (nameLen % 2 == 0) p++;
                p += 4; // mystery zero
                need(4); s.PersonMode = (int32_t)LE32(d.data() + p); p += 4;
                s.Prefix.assign(d.begin() + (long)start, d.begin() + (long)p);
                if (s.PersonMode > 0) {
                    int size = (s.Version == 0x4) ? 0xa0 : 0x200;
                    need((size_t)size);
                    for (int k = 0; k < size; k += 2) { s.PersonData.push_back((int16_t)LE16(d.data() + p)); p += 2; }
                }
                need(14);
                s.NeighbourID = (int16_t)LE16(d.data() + p); p += 2;
                s.GUID = LE32(d.data() + p); p += 4;
                s.NegOne = (int32_t)LE32(d.data() + p); p += 4;
                int32_t entries = (int32_t)LE32(d.data() + p); p += 4;
                for (int k = 0; k < entries; k++) {
                    Rel r;
                    need(12);
                    r.KeyCount = (int32_t)LE32(d.data() + p); p += 4;
                    r.Key = (int32_t)LE32(d.data() + p); p += 4;
                    int32_t vc = (int32_t)LE32(d.data() + p); p += 4;
                    need((size_t)std::max<int32_t>(0, vc) * 4);
                    for (int j = 0; j < vc; j++) { r.Values.push_back((int32_t)LE32(d.data() + p)); p += 4; }
                    s.Rels.push_back(r);
                }
                s.Raw.assign(d.begin() + (long)start, d.begin() + (long)p);
                Slots.push_back(std::move(s));
            }
        } catch (...) { return false; }
        Tail.assign(d.begin() + (long)std::min(p, d.size()), d.end());
        return true;
    }
    static std::vector<uint8_t> Encode(const Slot& s) {
        std::vector<uint8_t> o(s.Prefix);
        for (auto v : s.PersonData) Put16(o, (uint16_t)v);
        Put16(o, (uint16_t)s.NeighbourID);
        Put32(o, s.GUID);
        Put32(o, (uint32_t)s.NegOne);
        Put32(o, (uint32_t)s.Rels.size());
        for (auto& r : s.Rels) {
            Put32(o, (uint32_t)r.KeyCount); Put32(o, (uint32_t)r.Key); Put32(o, (uint32_t)r.Values.size());
            for (auto v : r.Values) Put32(o, (uint32_t)v);
        }
        return o;
    }
    /// A record for a neighbour the file does not have yet (same layout as Neighbour::Save, game pad bytes).
    static Slot FromNeighbour(const Neighbour& n, int32_t fileVersion) {
        Slot s;
        s.Valid = true;
        s.Version = n.Version;
        auto& o = s.Prefix;
        Put32(o, 1);
        Put32(o, (uint32_t)n.Version);
        if (n.Version == 0xA) Put32(o, (uint32_t)n.Unknown3);
        for (char c : n.Name) o.push_back((uint8_t)c);
        o.push_back(0);
        if (n.Name.size() % 2 == 0) o.push_back(0xA3);
        Put32(o, (uint32_t)n.MysteryZero);
        s.PersonMode = n.PersonMode != 0 ? n.PersonMode : (n.PersonData.empty() ? 0 : 5);
        Put32(o, (uint32_t)s.PersonMode);
        (void)fileVersion;
        if (s.PersonMode > 0) {
            int size = (n.Version == 0x4) ? 0xa0 : 0x200;
            s.PersonData.assign((size_t)size / 2, 0);
            for (size_t i = 0; i < n.PersonData.size() && i < s.PersonData.size(); i++) s.PersonData[i] = n.PersonData[i];
        }
        s.NeighbourID = n.NeighbourID;
        s.GUID = n.GUID;
        s.NegOne = n.UnknownNegOne;
        const auto keys=n.OrderedRelationshipKeys();
        for (auto k : keys) { Rel r; r.Key = k; r.Values.assign(n.Relationships.at(k).begin(), n.Relationships.at(k).end()); s.Rels.push_back(r); }
        s.Raw = Encode(s);
        return s;
    }
    // DVD serializer 00423e3b uses the outer NBRS version to decide whether
    // each occupied slot has one version word or the 0xA / 9 pair. Keeping
    // a legacy header around one expanded person record shifts every field
    // after that pair. Promote the complete roster without losing expansion
    // person data, identities, relationships, free slots, or the opaque tail.
    int NormalizeWireVersion(std::string* error) {
        if (Header.size() != 12) { if (error) *error = "Invalid NBRS header"; return -1; }
        bool modern = LE32(Header.data() + 4) >= 0x49;
        for (const auto& slot : Slots) if (slot.Valid && slot.Version == 0xA) modern = true;
        if (!modern) return 0;
        // Refuse an unknown layout instead of guessing how to expand it.
        for (const auto& slot : Slots) if (slot.Valid && slot.Version != 4 && slot.Version != 0xA) {
            if (error) *error = "Cannot promote an unknown NBRS record version";
            return -1;
        }
        int changed = 0;
        if (LE32(Header.data() + 4) < 0x49) {
            Header[4] = 0x49; Header[5] = Header[6] = Header[7] = 0;
            ++changed;
        }
        for (auto& slot : Slots) if (slot.Valid && slot.Version == 4) {
            if (slot.Prefix.size() < 8) { if (error) *error = "Invalid NBRS record prefix"; return -1; }
            std::vector<uint8_t> prefix(slot.Prefix.begin(), slot.Prefix.begin() + 4);
            Put32(prefix, 0xA); Put32(prefix, 9);
            prefix.insert(prefix.end(), slot.Prefix.begin() + 8, slot.Prefix.end());
            slot.Prefix = std::move(prefix);
            slot.Version = 0xA;
            if (slot.PersonMode > 0) slot.PersonData.resize(0x200 / 2, 0);
            slot.Raw = Encode(slot);
            ++changed;
        }
        return changed;
    }
    std::vector<uint8_t> Write() const {
        std::vector<uint8_t> o(Header);
        Put32(o, (uint32_t)Slots.size());
        for (auto& s : Slots) o.insert(o.end(), s.Raw.begin(), s.Raw.end());
        o.insert(o.end(), Tail.begin(), Tail.end());
        return o;
    }
};

int UpdateNBRS(IffRawFile& file, NBRS* nbrs, std::vector<std::string>* log, std::string* error) {
    auto c = file.Find("NBRS", 1);
    if (!c || nbrs == nullptr) return 0;
    NBRSRaw nb;
    if (!nb.Parse(c->Data)) { if (log) log->push_back("NBRS not parsed: kept as saved"); return 0; }
    int changed = 0, added = 0;
    std::set<int16_t> seen;
    for (auto& s : nb.Slots) {
        if (!s.Valid) continue;
        seen.insert(s.NeighbourID);
        auto it = nbrs->NeighbourByID.find(s.NeighbourID);
        if (it == nbrs->NeighbourByID.end() || it->second == nullptr) continue;
        Neighbour* n = it->second;
        NBRSRaw::Slot ns = s;
        // FIX(sims) QA2: a version 4 record upgraded in memory (person data 80..87 set: Superstar fame) is written in the
        // newer layout (version 0xA, the int 9 after it, 0x200 bytes of person data), keeping its other bytes.
        if (s.Version == 0x4 && n->Version == 0xA && s.PersonMode > 0 && ns.Prefix.size() >= 8) {
            std::vector<uint8_t> pre(ns.Prefix.begin(), ns.Prefix.begin() + 4);
            Put32(pre, 0xAu);
            Put32(pre, (uint32_t)n->Unknown3);
            pre.insert(pre.end(), ns.Prefix.begin() + 8, ns.Prefix.end());
            ns.Prefix = pre;
            ns.Version = 0xA;
            ns.PersonData.resize(0x200 / 2, 0);
        }
        if (s.PersonMode > 0) for (size_t i = 0; i < n->PersonData.size() && i < ns.PersonData.size(); i++) ns.PersonData[i] = n->PersonData[i];
        ns.GUID = n->GUID;
        // relationships: values of saved keys updated in place, new keys appended in their recorded creation order, dropped keys removed
        std::vector<NBRSRaw::Rel> rels;
        std::set<int32_t> done;
        for (auto& r : s.Rels) {
            auto rit = n->Relationships.find(r.Key);
            if (rit == n->Relationships.end()) continue;
            auto nr = r;
            nr.Values.assign(rit->second.begin(), rit->second.end());
            rels.push_back(nr);
            done.insert(r.Key);
        }
        std::vector<int32_t> rest;
        for (auto key : n->OrderedRelationshipKeys()) if (!done.count(key)) rest.push_back(key);
        for (auto k : rest) { NBRSRaw::Rel r; r.Key = k; r.Values.assign(n->Relationships[k].begin(), n->Relationships[k].end()); rels.push_back(r); }
        ns.Rels = rels;
        auto bytes = NBRSRaw::Encode(ns);
        if (bytes != s.Raw) ++changed;
        ns.Raw = std::move(bytes);
        s = std::move(ns);
    }
    // neighbours created at run time: TS1 keeps neighbour id N in slot N-1
    int32_t fileVersion = (int32_t)LE32(nb.Header.data() + 4);
    for (auto& kv : nbrs->NeighbourByID) {
        if (kv.second == nullptr || seen.count(kv.first) || kv.first <= 0 || kv.second->Unknown1 != 1) continue;
        size_t slot = (size_t)kv.first - 1;
        while (nb.Slots.size() <= slot) { NBRSRaw::Slot e; Put32(e.Raw, 0); nb.Slots.push_back(e); }
        if (nb.Slots[slot].Valid) continue;
        nb.Slots[slot] = NBRSRaw::FromNeighbour(*kv.second, fileVersion);
        added++;
    }
    const int promoted = nb.NormalizeWireVersion(error);
    if (promoted < 0) return -1;
    if (changed + added + promoted == 0) return 0;
    c->Data = nb.Write();
    if (log) log->push_back("NBRS: " + std::to_string(changed) + " neighbour records changed, " + std::to_string(added) + " added");
    return changed + added + promoted;
}

int UpdateNGBH(IffRawFile& file, NGBH* ngbh, std::vector<std::string>* log, std::string* error) {
    auto c = file.Find("NGBH", 1);
    if (!c || ngbh == nullptr) return 0;
    const auto& d = c->Data;
    auto reject = [&](const char* reason) {
        if (error) *error = std::string("NGBH not saved: ") + reason;
        return -1;
    };
    if (d.size() < 44) return reject("truncated neighbourhood header");
    const uint32_t version = LE32(d.data() + 4);
    // The native reader also accepts inventory appended by older native saves
    // under a legacy header. Parse that body before minimally normalizing it.
    const bool savedInventory = version >= 0x40 || d.size() > 44;
    const bool hasInventory = savedInventory || !ngbh->InventoryByID.empty();

    // Locate the original suffix before changing anything. It contains opaque
    // expansion state; preserve all its bytes, including lengths unknown here.
    size_t p = 44;
    std::vector<std::pair<int16_t, uint32_t>> order;
    std::set<int16_t> seen;
    if (savedInventory) {
        if (d.size() - p < 4) return reject("missing inventory count");
        const int32_t count = static_cast<int32_t>(LE32(d.data() + p)); p += 4;
        if (count < 0 || static_cast<size_t>(count) > (d.size() - p) / 10)
            return reject("invalid inventory record count");
        for (int32_t i = 0; i < count; ++i) {
            if (d.size() - p < 10) return reject("truncated inventory record");
            const uint32_t marker = LE32(d.data() + p); p += 4;
            const int16_t id = static_cast<int16_t>(LE16(d.data() + p)); p += 2;
            const int32_t n = static_cast<int32_t>(LE32(d.data() + p)); p += 4;
            if (n < 0 || static_cast<size_t>(n) > (d.size() - p) / 10)
                return reject("invalid inventory item count");
            p += static_cast<size_t>(n) * 10;
            order.emplace_back(id, marker);
            seen.insert(id);
        }
    }
    std::vector<uint8_t> o(d.begin(), d.begin() + 12);
    if (hasInventory && version < 0x40) {
        // Original inventory starts at version0x40; do not add later flags.
        o[4] = 0x40; o[5] = 0; o[6] = 0; o[7] = 0;
    }
    for (int i = 0; i < 16; i++) Put16(o, static_cast<uint16_t>(ngbh->NeighborhoodData[i]));
    if (hasInventory) {
        // Saved owner order first, then newly created owners ascending.
        std::vector<int16_t> rest;
        for (const auto& kv : ngbh->InventoryByID) if (!seen.count(kv.first)) rest.push_back(kv.first);
        std::sort(rest.begin(), rest.end());
        for (auto id : rest) order.emplace_back(id, 1u);
        std::vector<std::pair<int16_t, uint32_t>> present;
        for (const auto& row : order) if (ngbh->InventoryByID.count(row.first)) present.push_back(row);
        if (present.size() > static_cast<size_t>(INT32_MAX)) return reject("too many inventory owners");
        Put32(o, static_cast<uint32_t>(present.size()));
        for (const auto& row : present) {
            const auto id = row.first;
            const auto& inv = ngbh->InventoryByID.at(id);
            if (inv.size() > static_cast<size_t>(INT32_MAX)) return reject("too many inventory items");
            Put32(o, row.second);
            Put16(o, static_cast<uint16_t>(id));
            Put32(o, static_cast<uint32_t>(inv.size()));
            for (const auto& it : inv) { Put32(o, static_cast<uint32_t>(it.Type)); Put32(o, it.GUID); Put16(o, it.Count); }
        }
    }
    o.insert(o.end(), d.begin() + p, d.end());
    if (o == d) return 0;
    c->Data = std::move(o);
    if (log) log->push_back("NGBH: neighbourhood data / inventories written; original suffix preserved");
    return 1;
}

int UpdateTATT(IffRawFile& file, TATT* tatt, std::vector<std::string>* log) {
    auto c = file.Find("TATT", 1);
    if (!c || tatt == nullptr || c->Data.size() < 13) return 0;
    auto& d = c->Data;
    // saved order and values
    std::vector<std::pair<uint32_t, std::vector<int16_t>>> saved;
    uint8_t compression = d[12];
    try {
        st::Reader io(d, st::Endian::Little);
        io.Seek(st::SeekOrigin::Begin, 13);
        std::unique_ptr<IOProxy> iop;
        if (compression != 1) iop.reset(new TTABNormal(&io)); else iop.reset(new IffFieldEncode(&io));
        int32_t total = iop->ReadInt32();
        for (int i = 0; i < total; i++) {
            uint32_t guid = (uint32_t)iop->ReadInt32();
            int32_t count = iop->ReadInt32();
            std::vector<int16_t> v((size_t)std::max<int32_t>(0, count));
            for (auto& x : v) x = iop->ReadInt16();
            saved.push_back(std::make_pair(guid, v));
        }
    } catch (...) { saved.clear(); }
    bool same = saved.size() == tatt->TypeAttributesByGUID.size();
    for (auto& kv : saved) {
        auto it = tatt->TypeAttributesByGUID.find(kv.first);
        if (it == tatt->TypeAttributesByGUID.end() || it->second != kv.second) { same = false; break; }
    }
    if (same) return 0;
    std::vector<uint32_t> order;
    for (auto& kv : saved) if (tatt->TypeAttributesByGUID.count(kv.first)) order.push_back(kv.first);
    std::vector<uint32_t> rest;
    for (auto& kv : tatt->TypeAttributesByGUID) if (std::find(order.begin(), order.end(), kv.first) == order.end()) rest.push_back(kv.first);
    std::sort(rest.begin(), rest.end());
    order.insert(order.end(), rest.begin(), rest.end());
    std::vector<uint8_t> o(d.begin(), d.begin() + 13);
    if (compression == 1) {
        IffFieldEncodeWriter w(1); // the stream starts 1 byte after the 12-byte header (no strings: parity unused)
        w.WriteInt32((int32_t)order.size());
        for (auto g : order) {
            auto& v = tatt->TypeAttributesByGUID[g];
            w.WriteInt32((int32_t)g);
            w.WriteInt32((int32_t)v.size());
            for (auto x : v) w.WriteInt16(x);
        }
        o.insert(o.end(), w.Data.begin(), w.Data.end());
    } else {
        Put32(o, (uint32_t)order.size());
        for (auto g : order) {
            auto& v = tatt->TypeAttributesByGUID[g];
            Put32(o, g); Put32(o, (uint32_t)v.size());
            for (auto x : v) Put16(o, (uint16_t)x);
        }
    }
    d = o;
    if (log) log->push_back("TATT: " + std::to_string(order.size()) + " type attribute sets written");
    return 1;
}

int UpdateFAMI(IffRawFile& file, TS1NeighborhoodProvider* prov, const std::vector<int16_t>& deleted, std::vector<std::string>* log, FAMI* overrideFamily = nullptr) {
    int changed = 0;
    for (auto id : deleted) {
        if (file.Find("FAMI", (uint16_t)id)) { file.Remove("FAMI", (uint16_t)id); changed++; }
        if (file.Find("FAMs", (uint16_t)id)) file.Remove("FAMs", (uint16_t)id);
    }
    for (auto fami : prov->ListFamilies()) {
        if (fami == nullptr) continue;
        if (overrideFamily) {
            if (fami->ChunkID != overrideFamily->ChunkID) continue;
            fami = overrideFamily;
        }
        if (std::find(deleted.begin(), deleted.end(), (int16_t)fami->ChunkID) != deleted.end()) continue;
        auto c = file.Find("FAMI", fami->ChunkID);
        std::vector<uint8_t> o;
        if (c && c->Data.size() >= 40) {
            auto& d = c->Data;
            uint32_t n = LE32(d.data() + 36);
            size_t after = 40 + (size_t)n * 4;
            o.assign(d.begin(), d.begin() + 12); // pad, version (kept), "IMAF"
            Put32(o, (uint32_t)fami->HouseNumber); Put32(o, (uint32_t)fami->FamilyNumber); Put32(o, (uint32_t)fami->Budget);
            Put32(o, (uint32_t)fami->NetWorth); Put32(o, (uint32_t)fami->FamilyFriends); Put32(o, (uint32_t)fami->Unknown);
            Put32(o, (uint32_t)fami->FamilyGUIDs.size());
            for (auto g : fami->FamilyGUIDs) Put32(o, g);
            if (after <= d.size()) o.insert(o.end(), d.begin() + (long)after, d.end());
            if (o == d) continue;
            d = o;
        } else {
            // FIX(sims): a new family takes the version of the file's other families (FAMI::Write: 7 if unknown)
            if (fami->Version == 0 || fami->Version >= 9) {
                for (auto other : prov->ListFamilies()) {
                    if (other == nullptr || other == fami) continue;
                    auto oc = file.Find("FAMI", other->ChunkID);
                    if (oc && oc->Data.size() >= 8) { fami->Version = LE32(oc->Data.data() + 4); break; }
                }
            }
            st::Writer w(st::Endian::Little);
            fami->Write(prov->MainResource.get(), w);
            file.Set("FAMI", fami->ChunkID, w.ToArray(), fami->ChunkLabel);
        }
        changed++;
    }
    if (changed && log) log->push_back("FAMI: " + std::to_string(changed) + " family records written");
    return changed;
}

// FIX(sims): Makin' Magic family spells (TS1GameState::FamilySpells: the Magic Controller's attributes 0..5 per
// family, see VMTS1Travel::LoadFamilySpells). Where the original game keeps them is not known (Sims.exe is
// SafeDisc-encrypted; no Makin' Magic save with spells in the data; FreeSO leaves Generic Sims Call 43 empty). They are
// written as a private chunk "FSPL" (id = family number; u32 version 1, u16 count, count x s16) which the original's
// IFF loader skips like any unknown chunk type; the TS1 chunks stay untouched.
int UpdateFamilySpells(IffRawFile& file, TS1NeighborhoodProvider* prov, const std::vector<int16_t>& deleted, std::vector<std::string>* log, FAMI* overrideFamily = nullptr) {
    int changed = 0;
    for (auto id : deleted) if (file.Find("FSPL", (uint16_t)id)) { file.Remove("FSPL", (uint16_t)id); changed++; }
    for (auto& kv : prov->GameState.FamilySpells) {
        if (kv.first <= 0 || std::find(deleted.begin(), deleted.end(), kv.first) != deleted.end()) continue;
        std::vector<uint8_t> o;
        Put32(o, 1);
        Put16(o, (uint16_t)kv.second.size());
        for (auto v : kv.second) Put16(o, (uint16_t)v);
        auto c = file.Find("FSPL", (uint16_t)kv.first);
        if (c && c->Data == o) continue;
        file.Set("FSPL", (uint16_t)kv.first, o, "Family Spells");
        changed++;
    }
    if (changed && log) log->push_back("FSPL: " + std::to_string(changed) + " family spell sets written");
    return changed;
}

// ODHV stores portable visit identity, not VM actions (M1 permits action restart).
int UpdateVisits(IffRawFile& file, TS1NeighborhoodProvider* prov, const std::vector<int16_t>& deleted) {
    auto& gs = prov->GameState;
    std::vector<uint16_t> remove;
    for (const auto& c : file.Chunks) if (c.Type=="ODHV") remove.push_back(c.ID);
    for (auto id:remove) file.Remove("ODHV",id);
    for (const auto& kv:gs.SavedVisits) {
        if (std::find(deleted.begin(),deleted.end(),kv.first)!=deleted.end() || !prov->GetFamily(kv.first)) continue;
        const auto& v=kv.second;
        std::vector<uint8_t> o; Put32(o,1); Put16(o,v.Home); Put16(o,v.House); Put16(o,v.Transit);
        Put32(o,v.Caller); Put32(o,v.Day); Put32(o,v.Month); Put32(o,v.Year); Put32(o,v.Generation); Put16(o,(uint16_t)v.Party.size());
        for(auto guid:v.Party) Put32(o,guid);
        file.Set("ODHV",(uint16_t)kv.first,o,"OpenDollhouse Saved Visit");
    }
    return !remove.empty() || !gs.SavedVisits.empty();
}

} // namespace

namespace FSO { namespace Content { namespace TS1 {

void TS1NeighbourhoodWriter::LoadFamilySpells(TS1NeighborhoodProvider* prov, const std::string& path) {
    if (prov == nullptr) return;
    IffRawFile file;
    if (!file.Load(path, nullptr)) return;
    std::map<int16_t,TS1SavedVisit> visits;
    for (const auto& c : file.Chunks) {
        if(c.Type!="ODHV" || c.Data.size()<32 || LE32(c.Data.data())!=1) {
            if(c.Type=="ODHV") throw std::runtime_error("Unsupported or corrupt saved visit record");
            continue;
        }
        const auto* d=c.Data.data(); size_t count=LE16(d+30);
        auto family=prov->GetFamily((int16_t)c.ID);
        if(!family || count==0 || count>8 || c.Data.size()!=32+4*count) throw std::runtime_error("Invalid saved visit party");
        TS1SavedVisit v; v.Home=(int16_t)LE16(d+4); v.House=(int16_t)LE16(d+6);v.Transit=(int16_t)LE16(d+8);
        v.Caller=LE32(d+10);v.Day=(int32_t)LE32(d+14);v.Month=(int32_t)LE32(d+18);v.Year=(int32_t)LE32(d+22);v.Generation=LE32(d+26);
        if(v.Home!=family->HouseNumber || v.House<=0 || v.House==v.Home || !v.Generation) throw std::runtime_error("Invalid saved visit destination");
        bool valid=true;
        for(size_t i=0;i<count;i++) { auto guid=LE32(d+32+i*4);
            if(std::find(family->FamilyGUIDs.begin(),family->FamilyGUIDs.end(),guid)==family->FamilyGUIDs.end() ||
               std::find(v.Party.begin(),v.Party.end(),guid)!=v.Party.end()) {valid=false;break;}
            v.Party.push_back(guid);
        }
        if(!valid || std::find(v.Party.begin(),v.Party.end(),v.Caller)==v.Party.end()) throw std::runtime_error("Invalid saved visit member identity");
        visits[(int16_t)c.ID]=v;
    }
    prov->GameState.SavedVisits=std::move(visits);
    for (auto& c : file.Chunks) {
        if (c.Type != "FSPL" || c.Data.size() < 6 || LE32(c.Data.data()) != 1) continue;
        size_t n = LE16(c.Data.data() + 4);
        std::vector<int16_t> v;
        for (size_t i = 0; i < n && 6 + i * 2 + 2 <= c.Data.size(); i++) v.push_back((int16_t)LE16(c.Data.data() + 6 + i * 2));
        prov->GameState.FamilySpells[(int16_t)c.ID] = v;
    }
}

static bool WriteNeighbourhood(TS1NeighborhoodProvider* prov, const std::string& path,
        const std::vector<int16_t>& deletedFamilies, bool requireInventory, std::string* error, std::vector<std::string>* log) {
    if (prov == nullptr || prov->MainResource == nullptr) { if (error) *error = "no neighbourhood"; return false; }
    // ADDED(sims) save progress (files/LoadProgress.h): the app's "Saving..." overlay follows the stages
    auto progress = [](size_t done) { FSO::Files::LoadProgress::Progress("save: neighbourhood", done, 7); };
    progress(0);
    IffRawFile file;
    std::string err;
    if (!file.Load(path, &err)) { if (error) *error = err; return false; }
    if (requireInventory && (!prov->Neighborhood || !file.Find("NGBH", 1))) {
        if (error) *error = "Cannot save the purchase without a neighbourhood inventory";
        return false;
    }
    progress(1);
    int changed = 0;
    const int nbrsChanged = UpdateNBRS(file, prov->Neighbors, log, error);
    if (nbrsChanged < 0) return false;
    changed += nbrsChanged;
    progress(2);
    const int ngbhChanged = UpdateNGBH(file, prov->Neighborhood, log, error);
    if (ngbhChanged < 0) return false;
    changed += ngbhChanged;
    progress(3);
    changed += UpdateTATT(file, prov->TypeAttributes, log);
    progress(4);
    changed += UpdateFAMI(file, prov, deletedFamilies, log);
    changed += UpdateVisits(file, prov, deletedFamilies);
    changed += UpdateFamilySpells(file, prov, deletedFamilies, log); // FIX(sims)
    progress(5);
    if (changed == 0) { if (log) log->push_back("neighbourhood unchanged"); return true; }
    return file.Save(path, error);
}

bool TS1NeighbourhoodWriter::PrepareEviction(TS1NeighborhoodProvider* prov, int16_t house,
        std::vector<uint8_t>& bytes, std::string* error) {
    bytes.clear();
    auto reject = [&](const char* text) { if (error) *error = text; return false; };
    if (!prov || !prov->HasPrivateNeighbourhood() || prov->PreparingPersonTemplate)
        return reject("The converter needs a private neighbourhood");
    const auto family = prov->GetFamilyForHouse(house);
    if (!family || prov->GameState.SavedVisits.count(int16_t(family->ChunkID)))
        return reject("The family is missing or travelling");
    const int64_t value = int64_t(family->Budget) + family->NetWorth;
    if (family->NetWorth < 0 || value < INT32_MIN || value > INT32_MAX)
        return reject("The family property value cannot be refunded safely");
    IffRawFile file;
    if (!file.Load(prov->UserPath + "/Neighborhood.iff", error)) return false;
    auto chunk = file.Find("FAMI", family->ChunkID);
    if (!chunk || chunk->Data.size() < 40 || LE32(chunk->Data.data()+36) > (chunk->Data.size()-40)/4)
        return reject("The family record is truncated");
    const auto& saved = chunk->Data;
    if (int32_t(LE32(saved.data()+12)) != house ||
        int32_t(LE32(saved.data()+20)) != family->Budget ||
        int32_t(LE32(saved.data()+24)) != family->NetWorth ||
        LE32(saved.data()+36) != family->FamilyGUIDs.size())
        return reject("The saved family changed before eviction preparation");
    for (size_t i=0;i<family->FamilyGUIDs.size();++i)
        if (LE32(saved.data()+40+i*4) != family->FamilyGUIDs[i])
            return reject("The saved family membership changed before eviction preparation");
    FAMI planned = *family;
    // Eviction changes ownership and refund only. Non-transactional cache
    // metadata must never replace the saved number/friends/unknown words.
    planned.FamilyNumber = int32_t(LE32(saved.data()+16));
    planned.FamilyFriends = int32_t(LE32(saved.data()+28));
    planned.Unknown = int32_t(LE32(saved.data()+32));
    planned.HouseNumber = 0; planned.Budget = int32_t(value); planned.NetWorth = 0;
    // Serialize this override alone: other families and every other chunk stay exact.
    UpdateFAMI(file, prov, {}, nullptr, &planned);
    bytes = file.Write();
    return true;
}

bool TS1NeighbourhoodWriter::SaveRoster(TS1NeighborhoodProvider* prov,const std::string& path,std::string* error) {
    if(prov&&prov->PreparingPersonTemplate){if(error)*error="Template preparation cannot save a neighbourhood";return false;}
    if(!prov||!prov->Neighbors||path!=prov->UserPath+"/Neighborhood.iff"||!prov->HasPrivateNeighbourhood()){
        if(error)*error="The roster destination is outside the writable neighbourhood";return false;
    }
    IffRawFile file;if(!file.Load(path,error))return false;
    auto raw=file.Find("NBRS",1);NBRSRaw checked;
    if(!raw||!checked.Parse(raw->Data)){if(error)*error="The roster could not be parsed losslessly";return false;}
    auto reject=[&](const char* reason){if(error)*error=reason;return false;};
    if(checked.Slots.size()!=LE32(raw->Data.data()+12)||checked.Write()!=raw->Data)
        return reject("The saved roster is truncated or cannot round-trip exactly");
    std::set<int16_t> ids;std::set<uint32_t> guids;
    for(const auto& slot:checked.Slots)if(slot.Valid){
        if(slot.NeighbourID<=0||!ids.insert(slot.NeighbourID).second)
            return reject("The saved roster has ambiguous neighbour IDs");
        guids.insert(slot.GUID);
    }
    bool changed=false;
    for(const auto& entry:prov->Neighbors->Entries){if(!entry||entry->Unknown1!=1)continue;const auto& n=*entry;
        if(ids.count(n.NeighbourID)){
            const auto found=std::find_if(checked.Slots.begin(),checked.Slots.end(),[&](const auto& x){
                return x.Valid&&x.NeighbourID==n.NeighbourID&&x.GUID==n.GUID;});
            if(found==checked.Slots.end())return reject("Existing neighbour identity changed during roster registration");
            continue;
        }
        if(n.NeighbourID<=0||n.Unknown1!=1||n.PersonMode!=9||n.PersonData.size()!=88||guids.count(n.GUID))
            return reject("An added neighbour does not satisfy the template contract");
        const size_t position=size_t(n.NeighbourID)-1;
        while(checked.Slots.size()<=position){NBRSRaw::Slot empty;Put32(empty.Raw,0);checked.Slots.push_back(empty);}
        if(checked.Slots[position].Valid||checked.Slots[position].Raw!=std::vector<uint8_t>({0,0,0,0}))
            return reject("The new neighbour ID collides with a saved slot");
        checked.Slots[position]=NBRSRaw::FromNeighbour(n,int32_t(LE32(checked.Header.data()+4)));
        ids.insert(n.NeighbourID);guids.insert(n.GUID);changed=true;
    }
    if(!changed)return true;
    if(checked.NormalizeWireVersion(error)<0)return false;
    const auto bytes=checked.Write();NBRSRaw verify;
    if(!verify.Parse(bytes)||verify.Slots.size()!=checked.Slots.size()||verify.Write()!=bytes)
        return reject("The appended roster failed its publication check");
    raw->Data=bytes;return file.Save(path,error);
}

bool TS1NeighbourhoodWriter::Save(TS1NeighborhoodProvider* prov, const std::string& path,
        const std::vector<int16_t>& deletedFamilies, std::string* error, std::vector<std::string>* log) {
    if(prov&&prov->PreparingPersonTemplate){if(error)*error="Template preparation cannot save a neighbourhood";return false;}
    if (!WriteNeighbourhood(prov, path, deletedFamilies, false, error, log)) return false;
    FSO::Files::LoadProgress::Progress("save: neighbourhood", 7, 7);
    return true;
}

bool TS1NeighbourhoodWriter::SavePurchase(TS1NeighborhoodProvider* prov, const std::string& path,
        const std::vector<int16_t>& deletedFamilies, bool requireInventory, std::string* error) {
    if(prov&&prov->PreparingPersonTemplate){if(error)*error="Template preparation cannot save a neighbourhood";return false;}
    if (!prov || path != prov->UserPath + "/Neighborhood.iff" || !prov->HasPrivateNeighbourhood()) {
        if (error) *error = "The purchase destination is outside the writable neighbourhood.";
        return false;
    }
    return WriteNeighbourhood(prov, path, deletedFamilies, requireInventory, error, nullptr);
}

}}}

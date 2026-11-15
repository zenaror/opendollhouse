/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "files/formats/iff/chunks/ODHN.h"
#include "simantics/VM.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "content/TS1PersonalText.h"
#include "simantics/entities/VMMultitileGroup.h"
#include <algorithm>
#include <map>
#include <set>

namespace FSO { namespace SimAntics { namespace Utils {
class VMTS1ObjectNames {
    using Names = FSO::Files::Formats::IFF::Chunks::ODHN;
    using Group = FSO::SimAntics::Entities::VMMultitileGroup;
    static Names::Entry Identity(Group* group) {
        Names::Entry e;
        VMEntity* anchor = nullptr;
        for (auto part : group->Objects) {
            if (!part || part->Dead || part->ObjectID <= 0 || !part->Object || !part->Object->OBJ)
                throw std::runtime_error("ODHN: incomplete live group");
            e.Parts.push_back({part->ObjectID, part->Object->OBJ->GUID});
            if (!anchor || part->ObjectID < anchor->ObjectID) anchor = part;
        }
        if (!anchor) throw std::runtime_error("ODHN: empty group");
        // BaseObject may change when dynamic parts move/combine. The lowest
        // persistent part ID remains an unambiguous instance anchor regardless
        // of vector order, zero offset, or MyLeadObject flags.
        e.AnchorID = anchor->ObjectID;
        e.MasterGUID = anchor->MasterDefinition ? anchor->MasterDefinition->GUID : anchor->Object->OBJ->GUID;
        std::sort(e.Parts.begin(), e.Parts.end(), [](const Names::Part& a, const Names::Part& b) { return a.ID < b.ID; });
        return e;
    }
public:
    static Names Capture(const std::vector<VMEntity*>& saved, uint32_t objectSignature) {
        Names names;
        names.ObjectSignature = objectSignature;
        std::set<VMEntity*> persisted(saved.begin(), saved.end());
        std::set<Group*> seen;
        for (auto entity : saved) {
            auto group = entity->MultitileGroup;
            if (!group || !seen.insert(group).second) continue;
            // Global controllers are recreated by the activator instead of using
            // their saved instance IDs; internal groups also have special loading.
            // They are not player-named persistent lot instances.
            bool ordinary = true;
            for (auto part : group->Objects) {
                if (!part || !persisted.count(part) || !part->Object || !part->Object->OBJ ||
                    part->Object->OBJ->Global != 0 || part->Object->OBJ->ObjectType == FSO::Files::Formats::IFF::Chunks::OBJDType::Internal)
                    ordinary = false;
            }
            if (!ordinary) {
                if (!group->Name.empty()) throw std::runtime_error("ODHN: named group cannot be matched after loading");
                continue;
            }
            auto e = Identity(group);
            e.Name = group->Name; // Raw value, including empty; never Name() fallback.
            auto anchor = *std::find_if(group->Objects.begin(), group->Objects.end(),
                [&](VMEntity* part) { return part->ObjectID == e.AnchorID; });
            if (dynamic_cast<VMAvatar*>(anchor)) {
                auto personal = Content::TS1::ResolvePersonalText(anchor->Object->Resource->Get<Files::Formats::IFF::Chunks::CTSS>(anchor->Object->OBJ->CatalogStringsID));
                e.CanonicalPersonalName = !personal.Value(0).empty() && e.Name == personal.Value(0);
            }
            names.Entries.push_back(std::move(e));
        }
        std::sort(names.Entries.begin(), names.Entries.end(), [](const Names::Entry& a, const Names::Entry& b) { return a.AnchorID < b.AnchorID; });
        Names::Validate(names.Entries, names.FormatVersion);
        return names;
    }
    static void Restore(VM* vm, const Names& names, uint32_t objectSignature) {
        Names::Validate(names.Entries, names.FormatVersion);
        if (names.ObjectSignature != objectSignature) throw std::runtime_error("ODHN: names belong to a different object snapshot");
        std::vector<std::pair<Group*, std::string>> pending;
        std::set<Group*> seen;
        for (const auto& e : names.Entries) {
            auto anchor = vm->GetObjectById(e.AnchorID);
            if (!anchor || !anchor->MultitileGroup) throw std::runtime_error("ODHN: saved object instance is missing");
            auto group = anchor->MultitileGroup;
            auto actual = Identity(group);
            if (actual.AnchorID != e.AnchorID || actual.MasterGUID != e.MasterGUID || actual.Parts != e.Parts || !seen.insert(group).second)
                throw std::runtime_error("ODHN: saved object group identity does not match");
            std::string restored = e.Name;
            auto avatar = dynamic_cast<VMAvatar*>(anchor);
            auto ctss = avatar ? anchor->Object->Resource->Get<Files::Formats::IFF::Chunks::CTSS>(anchor->Object->OBJ->CatalogStringsID) : nullptr;
            bool canonical = e.CanonicalPersonalName;
            // Version 1 had no provenance bit. Recognize only an exact personal
            // name from its own catalog; arbitrary dynamic names remain literal.
            if (names.FormatVersion == 1 && ctss && !e.Name.empty())
                for (const auto& language : ctss->LanguageSets)
                    if (!language.Strings.empty() && language.Strings[0].Value == e.Name) canonical = true;
            if (canonical) {
                if (!avatar || !ctss) throw std::runtime_error("ODHN: personal name has no character catalog");
                auto personal = Content::TS1::ResolvePersonalText(ctss);
                if (personal.Ambiguous || personal.Value(0).empty()) throw std::runtime_error("ODHN: personal name cannot be resolved");
                restored = personal.Value(0);
            }
            pending.push_back({group, std::move(restored)});
        }
        // All groups have been checked: no partially restored names on failure.
        for (auto& p : pending) p.first->Name.swap(p.second);
    }
};
}}}

/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Engine.VMDialogHandler
#include "simantics/engine/VMDialogHandler.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMMemory.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "simantics/model/VMDialogInfo.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include <cctype>
#include <cstdio>
#include "content/ContentManager.h"
#include "content/TS1JobProvider.h"
#include "content/TS1NeighbourProvider.h"
#include "content/TS1PersonalText.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "files/formats/iff/chunks/BMP.h"
#include "files/formats/iff/IffFile.h"
#include <stdexcept>
#include <algorithm>
namespace FSO { namespace SimAntics { namespace Engine {
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::SimAntics::Primitives;
using Scopes::VMVariableScope;
using Utils::VMMemory;

// FIX(sims): FreeSO's token set (TS1 strings use $Job:/$JobDesc:/$JobOffer:/$TimeLocal:/$Neighbor/... ; FreeSims only knew 13,
// so e.g. the promotion letter read "You have been promoted! (to Job:4:5) JobDesc:4:5 You now work from TimeLoc...").
const char* const VMDialogHandler::valid[26] = {
    "Object", "Me", "TempXL:", "Temp:", "$", "Attribute:", "DynamicStringLocal:", "Local:", "TimeLocal:", "NameLocal:",
    "FixedLocal:", "DynamicObjectName", "MoneyXL:", "JobOffer:", "Job:", "JobDesc:", "Param:", "Neighbor", "\r\n", "ListObject",
    "CatalogLocal:", "DateLocal:", "ObjectLocal:", "\\n",
    "TokenNameLocal:", // FIX(sims): TS1 expansion token menus (not in FreeSO)
    "NeighborLocal:"   // FIX(sims) QA3: Superstar 'CT - Promotion Dialog' ("Congratulations $NeighborLocal:2!")
};

// STR.GetString returns null for missing entries in C#; mirror that with TryGetString.
static bool StrGet(STR* s, int32_t index, std::string& out) {
    if (s == nullptr) return false;
    return s->TryGetString(index, STR::DefaultLangCode, out);
}

void VMDialogHandler::ShowDialog(VMStackFrame* context, VMDialogOperand* operand, STR* source) {
    Model::VMDialogInfo info;
    std::string s;
    info.Block = (operand->Flags & VMDialogFlags::Continue) == 0;
    info.Caller = context->Caller;
    info.Icon = context->StackObject;
    info.Operand = *operand;
    const int iconMode = (static_cast<int>(operand->Flags) >> 1) & 7;
    if (iconMode == 3 && source && source->ChunkParent) {
        // Original indexed illustrations use BMP_ 5000 + the icon operand in
        // the same private/global/semiglobal resource as the dialog strings.
        if (auto bmp = source->ChunkParent->Get<BMP>(5000 + operand->IconNameStringID)) {
            constexpr size_t MaxDialogBitmapBytes = 4 * 1024 * 1024;
            if (bmp->GetData().size() <= MaxDialogBitmapBytes)
                info.IndexedIconBMP = bmp->GetData();
        }
    }
    info.HasTextParts = true;
    const auto text = [&](int id, Model::VMDialogInfo::TextField field) {
        bool ok = StrGet(source, id, s);
        info.TextParts[field] = ParseDialogText(context, s, source, ok);
        if (info.TextParts[field].LegacyOnly) info.HasTextParts = false;
        return info.TextParts[field].LegacyBytes();
    };
    info.Message = text(std::max(0, operand->MessageStringID - 1), Model::VMDialogInfo::MessageText);
    if (operand->TitleStringID) info.Title = text(operand->TitleStringID - 1, Model::VMDialogInfo::TitleText);
    if (operand->IconNameStringID) info.IconName = text(operand->IconNameStringID - 1, Model::VMDialogInfo::IconText);
    if (operand->YesStringID) { info.Yes = text(operand->YesStringID - 1, Model::VMDialogInfo::YesText); info.HasYes = true; }
    if (operand->NoStringID) { info.No = text(operand->NoStringID - 1, Model::VMDialogInfo::NoText); info.HasNo = true; }
    if (operand->CancelStringID) { info.Cancel = text(operand->CancelStringID - 1, Model::VMDialogInfo::CancelText); info.HasCancel = true; }
    info.DialogID = (context->CodeOwner->GUID << 32) | ((uint64_t)context->Routine->ID << 16) | context->InstructionPointer;
    context->VM()->SignalDialog(info);
}

bool VMDialogHandler::CommandSubstrValid(const std::string& command) {
    for (int i = 0; i < 26; i++) {
        std::string v = valid[i];
        if (command.size() <= v.size() && command == v.substr(0, command.size())) return true;
    }
    return false;
}

namespace {
struct FormatException : std::runtime_error { FormatException() : std::runtime_error("Input string was not in a correct format.") {} };
// C# short.Parse: FormatException on empty/invalid, OverflowException (not caught by the parser) on overflow.
int16_t ShortParse(const std::string& num) {
    if (num.empty()) throw FormatException();
    long v = std::stol(num);
    if (v > 32767 || v < -32768) throw std::overflow_error("Value was either too large or too small for an Int16.");
    return (int16_t)v;
}
char CharAt(const std::string& s, int i) {
    if (i < 0 || i >= (int)s.size()) throw std::out_of_range("Index was outside the bounds of the array.");
    return s[i];
}
// C# int.ToString("##,#0")
std::string GroupThousands(int32_t v) {
    bool neg = v < 0;
    std::string digits = std::to_string(neg ? -(int64_t)v : (int64_t)v);
    std::string out;
    int count = 0;
    for (int i = (int)digits.size() - 1; i >= 0; i--) {
        out.insert(out.begin(), digits[i]);
        if (++count % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return neg ? "-" + out : out;
}
}

namespace {
uint8_t ResourceLanguage(const STR* source) {
    if (!source) return 1;
    auto selected = source->GetLanguageSet(STRLangCode::Default);
    for (size_t i = 0; i < source->LanguageSets.size(); ++i)
        if (selected == &source->LanguageSets[i]) return uint8_t(i + 1);
    return 1;
}
bool ResourceUTF8(const STR* source) { return source && (source->SourceFormat == -1 || source->SourceFormat == -4); }
void AppendResource(Model::VMDialogText& output, const std::string& bytes, const STR* resource) {
    output.Append(bytes, ResourceLanguage(resource), ResourceUTF8(resource));
}
void AppendOpaque(Model::VMDialogText& output, const std::string& bytes) {
    output += bytes;
    output.LegacyOnly = true;
}
void AppendEntity(Model::VMDialogText& output, VMEntity* entity, const std::string& bytes) {
    if (auto avatar = dynamic_cast<VMAvatar*>(entity)) {
        const auto personal = Content::TS1::ResolvePersonalText(avatar->Object->Resource->Get<CTSS>(avatar->Object->OBJ->CatalogStringsID));
        if (!personal.Ambiguous && !personal.Value(0).empty() && personal.Value(0) == bytes) {
            output.Append(personal.Value(0), personal.SourceLanguage, personal.UTF8);
            return;
        }
    }
    AppendOpaque(output, bytes);
}
void AppendEntity(Model::VMDialogText& output, VMEntity* entity) { AppendEntity(output, entity, entity->ToString()); }
}
std::string VMDialogHandler::ParseDialogString(VMStackFrame* context, const std::string& input, STR* source, bool hasInput) {
    return ParseDialogText(context, input, source, hasInput).LegacyBytes();
}
Model::VMDialogText VMDialogHandler::ParseDialogText(VMStackFrame* context, const std::string& input, STR* source, bool hasInput) {
    int state = 0;
    std::string command;
    Model::VMDialogText output(ResourceLanguage(source), ResourceUTF8(source));

    if (!hasInput) { output += "Missing String!!!"; return output; }

    for (int i = 0; i < (int)input.size(); i++) {
        if (state == 0) {
            if (input[i] == '$') {
                state = 1; //start parsing string
                command.clear();
            } else {
                output += input[i];
            }
        } else {
            command += input[i];
            if (i == (int)input.size() - 1 || !CommandSubstrValid(command)) {
                if (i != (int)input.size() - 1 || std::isdigit((unsigned char)input[i])) {
                    command.erase(command.size() - 1, 1);
                    i--;
                }

                auto cmdString = command;
                int16_t values[3] = { 0, 0, 0 };
                if (cmdString.size() > 1 && cmdString[cmdString.size() - 1] == ':') {
                    try {
                        if (cmdString == "DynamicStringLocal:" || cmdString == "TimeLocal:" || cmdString == "JobOffer:" || cmdString == "Job:" || cmdString == "JobDesc:" || cmdString == "DateLocal:") { // FIX(sims): FreeSO
                            values[1] = -1;
                            values[2] = -1;
                            for (int j = 0; j < 3; j++) {
                                char next = CharAt(input, ++i);
                                std::string num = "";
                                while (std::isdigit((unsigned char)next)) {
                                    num += next;
                                    next = (++i == (int)input.size()) ? '!' : input[i];
                                }
                                if (num == "") {
                                    values[j] = -1;
                                    if (j == 1) values[2] = -1;
                                    break;
                                }
                                values[j] = ShortParse(num);
                                if (i == (int)input.size() || next != ':') break;
                            }
                        } else {
                            char next = CharAt(input, ++i);
                            std::string num = "";
                            while (std::isdigit((unsigned char)next)) {
                                num += next;
                                next = (++i == (int)input.size()) ? '!' : input[i];
                            }
                            values[0] = ShortParse(num);
                        }
                        i--;
                    } catch (FormatException&) {
                    }
                }
                try {
                    if (cmdString == "Object" || cmdString == "DynamicObjectName") {
                        if (context->StackObject == nullptr) throw std::runtime_error("null");
                        // FIX(sims): FreeSO: the group name, else the catalogue name (CTSS 0), else the label
                        auto stackObj = context->StackObject;
                        std::string nm;
                        const STR* nameSource = nullptr;
                        if (stackObj->MultitileGroup != nullptr && stackObj->MultitileGroup->Name != "") nm = stackObj->MultitileGroup->Name;
                        else {
                            auto obj = stackObj->MasterDefinition ? stackObj->MasterDefinition : stackObj->Object->OBJ;
                            auto strings = stackObj->Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::CTSS>(obj->CatalogStringsID);
                            if (strings != nullptr && strings->Length() > 0) { nm = strings->GetString(0); nameSource = strings; }
                            if (nm.empty()) nm = stackObj->ToString();
                        }
                        if (dynamic_cast<VMAvatar*>(stackObj)) AppendEntity(output, stackObj, nm);
                        else if (nameSource) AppendResource(output, nm, nameSource);
                        else AppendOpaque(output, nm);
                    } else if (cmdString == "Me") {
                        AppendEntity(output, context->Caller);
                    } else if (cmdString == "TempXL:") {
                        output += std::to_string(VMMemory::GetBigVariable(context, VMVariableScope::TempXL, values[0]));
                    } else if (cmdString == "MoneyXL:") {
                        output += "$" + GroupThousands(VMMemory::GetBigVariable(context, VMVariableScope::TempXL, values[0]));
                    } else if (cmdString == "Temp:") {
                        output += std::to_string(VMMemory::GetBigVariable(context, VMVariableScope::Temps, values[0]));
                    } else if (cmdString == "$") {
                        output += "$"; i--;
                    } else if (cmdString == "Attribute:") {
                        output += std::to_string(VMMemory::GetBigVariable(context, VMVariableScope::MyObjectAttributes, values[0]));
                    } else if (cmdString == "DynamicStringLocal:") {
                        STR* res = nullptr;
                        bool skip = false;
                        if (values[2] != -1 && values[1] != -1) {
                            VMEntity* obj = context->VM()->GetObjectById((int16_t)context->Locals.at(values[2]));
                            if (obj == nullptr) skip = true;
                            else {
                                uint16_t tableID = (uint16_t)context->Locals.at(values[1]);
                                {//local
                                    if (obj->SemiGlobal != nullptr) res = obj->SemiGlobal->Get<STR>(tableID);
                                    if (res == nullptr) res = obj->Object->Resource->Get<STR>(tableID);
                                    if (res == nullptr) res = context->Global()->Resource->Get<STR>(tableID);
                                }
                            }
                        } else if (values[1] != -1) {
                            //global table
                            uint16_t tableID = (uint16_t)context->Locals.at(values[1]);
                            res = context->Global()->Resource->Get<STR>(tableID);
                        } else {
                            res = source;
                        }

                        if (!skip) {
                            uint16_t index = (uint16_t)context->Locals.at(values[0]);
                            if (res != nullptr) {
                                std::string str;
                                bool ok = StrGet(res, index, str);
                                output += ParseDialogText(context, str, res, ok); // recursive command parsing!
                                // this is needed for the crafting table.
                                // though it is also, completely insane?
                            }
                        }
                    } else if (cmdString == "Local:") {
                        output += std::to_string(VMMemory::GetBigVariable(context, VMVariableScope::Local, values[0]));
                    } else if (cmdString == "Param:") {
                        output += std::to_string(VMMemory::GetBigVariable(context, VMVariableScope::Parameters, values[0]));
                    } else if (cmdString == "NameLocal:") { // FIX(sims): FreeSO (was the literal "(NameLocal)")
                        auto o = context->VM()->GetObjectById(VMMemory::GetVariable(context, VMVariableScope::Local, values[0]));
                        if (o == nullptr) throw std::runtime_error("null");
                        AppendEntity(output, o);
                    } else if (cmdString == "FixedLocal:") { // FIX(sims): FreeSO tokens from here on
                        char b[32]; snprintf(b, sizeof b, "%.2f", VMMemory::GetBigVariable(context, VMVariableScope::Local, values[0]) / 100.0f);
                        output += b;
                    } else if (cmdString == "TimeLocal:") {
                        int32_t hours = VMMemory::GetBigVariable(context, VMVariableScope::Local, values[0]);
                        int32_t mins = (values[1] == -1) ? 0 : VMMemory::GetBigVariable(context, VMVariableScope::Local, values[1]);
                        const char* suffix = (hours > 11) ? "pm" : "am";
                        if (hours > 12) hours -= 12;
                        char b[32]; snprintf(b, sizeof b, "%d:%02d%s", hours, mins, suffix);
                        output += b;
                    } else if (cmdString == "ObjectLocal:") {
                        auto o = context->VM()->GetObjectById(VMMemory::GetVariable(context, VMVariableScope::Local, values[0]));
                        if (o != nullptr) AppendEntity(output, o);
                    } else if (cmdString == "JobOffer:") {
                        auto c = FSO::Content::Content::Get();
                        if (c && c->Jobs) AppendOpaque(output, c->Jobs->JobOffer((int16_t)VMMemory::GetBigVariable(context, VMVariableScope::Local, values[0]),
                                                                       VMMemory::GetBigVariable(context, VMVariableScope::Local, values[1])));
                    } else if (cmdString == "Job:" || cmdString == "JobDesc:") {
                        int32_t level = VMMemory::GetBigVariable(context, VMVariableScope::Local, values[1]);
                        auto c = FSO::Content::Content::Get();
                        auto jobStr = (c && c->Jobs) ? c->Jobs->JobStrings((int16_t)VMMemory::GetBigVariable(context, VMVariableScope::Local, values[0])) : nullptr;
                        std::string js;
                        if (jobStr != nullptr && StrGet(jobStr, level * 3 + ((cmdString == "JobDesc:") ? 3 : 4), js)) AppendResource(output, js, jobStr);
                    } else if (cmdString == "Neighbor" || cmdString == "NeighborLocal:") {
                        auto c = FSO::Content::Content::Get();
                        // The neighbour ID is not a live object ID. Resolve its exact
                        // character identity, then use the common personal-text policy.
                        std::string nm = "Unknown";
                        uint8_t language = 1; bool utf8 = false;
                        if (c && c->Neighborhood && c->WorldObjects) {
                            int16_t nid = (cmdString == "Neighbor") ? context->StackObjectID()
                                : (int16_t)VMMemory::GetBigVariable(context, VMVariableScope::Local, values[0]);
                            auto n = c->Neighborhood->GetNeighborByID(nid);
                            auto gobj = n && n->Unknown1 == 1 && n->GUID ? c->WorldObjects->Get(n->GUID, true) : nullptr;
                            if (gobj && gobj->OBJ && gobj->Resource && n->GUID == gobj->OBJ->GUID) {
                                const auto personal = Content::TS1::ResolvePersonalText(gobj->Resource->Get<CTSS>(gobj->OBJ->CatalogStringsID));
                                if (!personal.Ambiguous && (personal.SourceLanguage || personal.UTF8) && !personal.Value(0).empty()) {
                                    nm = personal.Value(0); language = personal.SourceLanguage; utf8 = personal.UTF8;
                                }
                            }
                        }
                        output.Append(nm, language, utf8);
                    } else if (cmdString == "ListObject") {
                        if (context->StackObject != nullptr) {
                            std::string bytes; for (auto x : context->StackObject->MyList) bytes += (char)x;
                            if (!bytes.empty()) AppendOpaque(output, bytes);
                        }
                    } else if (cmdString == "CatalogLocal:") {
                        auto catObj = context->VM()->GetObjectById(VMMemory::GetVariable(context, VMVariableScope::Local, values[0]));
                        if (catObj != nullptr) {
                            auto ctss = catObj->Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::CTSS>(catObj->Object->OBJ->CatalogStringsID);
                            if (ctss != nullptr && ctss->Length() > 1) AppendResource(output, ctss->GetString(1), ctss);
                        }
                    } else if (cmdString == "DateLocal:") {
                        static const char* months[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
                        int d = context->Locals.at(values[0]), m = context->Locals.at(values[1]), y = context->Locals.at(values[2]);
                        char b[64]; snprintf(b, sizeof b, "%s %d, %d", (m >= 1 && m <= 12) ? months[m - 1] : "?", d, y);
                        output += b;
                    } else if (cmdString == "TokenNameLocal:") {
                        // FIX(sims): "$TokenNameLocal:N" (Makin' Magic EverAfter Crafter / cauldron 'Add...' menus): the
                        // catalogue name of the caller's inventory token at the index in local N (Manage Inventory 'find
                        // token' writes that index). FreeSO lacks the token, so the menu read "TokenNameLocal:1".
                        auto c = FSO::Content::Content::Get();
                        auto av = dynamic_cast<VMAvatar*>(context->Caller);
                        auto inv = (c && c->Neighborhood && av) ? c->Neighborhood->GetInventoryByNID(av->GetPersonData(Model::VMPersonDataVariable::NeighborId)) : nullptr;
                        int idx = VMMemory::GetVariable(context, VMVariableScope::Local, values[0]);
                        if (inv != nullptr && idx >= 0 && idx < (int)inv->size()) {
                            auto gobj = c->WorldObjects->Get((*inv)[idx].GUID, true);
                            std::string nm;
                            const STR* nameSource = nullptr;
                            if (gobj != nullptr) {
                                auto ctss = gobj->Resource->Get<FSO::Files::Formats::IFF::Chunks::CTSS>(gobj->OBJ->CatalogStringsID);
                                if (ctss != nullptr && ctss->Length() > 0) { nm = ctss->GetString(0); nameSource = ctss; }
                                if (nm.empty()) nm = gobj->OBJ->ChunkLabel;
                            }
                            if (nameSource) AppendResource(output, nm, nameSource);
                            else AppendOpaque(output, nm);
                        }
                    } else if (cmdString == "\\n") {
                        output += "\n";
                    } else if (cmdString.empty()) {
                        // FIX(sims) QA2: a '$' that starts no token is a literal (pie "Call Cab.../Go Downtown - $50" read
                        // "Go Downtown - 50"; "Go On Vacation - 500")
                        output += "$";
                    } else {
                        output += cmdString;
                    }
                } catch (std::exception&) {
                    //something went wrong. just skip command
                }
                state = 0;
            }
        }
    }
    output.ExpandParagraphs();
    return output;
}
}}}

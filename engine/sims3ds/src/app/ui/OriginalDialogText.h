// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "app/ui/OriginalTextEncoding.h"
#include "simantics/model/VMDialogText.h"
#include "simantics/model/VMDialogInfo.h"
#include "app/LotSession.h"
namespace simapp { namespace original {
inline std::string DialogTextUTF8(const FSO::SimAntics::Model::VMDialogText& text) {
    std::string result;
    for (const auto& part : text.Parts) result += PersonalToUTF8(part.Bytes, part.Language, part.UTF8);
    return result;
}
inline void ApplyDialogTextUTF8(const FSO::SimAntics::Model::VMDialogInfo& info, PendingDialog& dialog) {
    if (!info.HasTextParts) return;
    using Info = FSO::SimAntics::Model::VMDialogInfo;
    dialog.Title = DialogTextUTF8(info.TextParts[Info::TitleText]);
    dialog.Message = DialogTextUTF8(info.TextParts[Info::MessageText]);
    dialog.Yes = DialogTextUTF8(info.TextParts[Info::YesText]);
    dialog.No = DialogTextUTF8(info.TextParts[Info::NoText]);
    dialog.Cancel = DialogTextUTF8(info.TextParts[Info::CancelText]);
    dialog.TextUTF8 = true;
}
inline std::string DialogStringUTF8(const std::string& bytes, bool utf8) {
    return utf8 ? bytes : LegacyToUTF8(bytes);
}
}}

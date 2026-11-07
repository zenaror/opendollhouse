// SPDX-License-Identifier: MPL-2.0
#include "original_cursors.hpp"
#include <algorithm>
#include <fstream>

namespace odh_host {
namespace {
constexpr std::array<const char*,original_cursor_count> filenames{{
    "ADDPTS.cur","Arrow.cur","BULLDOZE.cur","DEADEXPL.cur","DOWN.cur","DOWNLEFT.cur","DOWNRIGHT.cur","EXPLORE.cur","HELP.CUR","HOURGLASS.cur","LEFT.cur",
    "LiveNothing.cur","LiveNothingPet.cur","LiveObjectAvail.cur","LiveObjectUnavail.cur","LivePerson.cur","LivePet.cur","MOVEIN.cur","NODROP.CUR","Pan.cur","RIGHT.cur",
    "RemovePts.cur","Rezone.cur","Rezone2.cur","SimsDelete.cur","SimsFlip.cur","SimsLevel.cur","SimsLower.cur","SimsMove.cur","SimsPlace.cur","SimsRaise.cur",
    "SimsRotate.cur","SimsRotateNE.cur","SimsRotateNW.cur","SimsRotateSE.cur","SimsRotateSW.cur","UP.cur","UPLEFT.cur","UPRIGHT.cur",
    "balloon.cur","edit.cur","finger.cur","hand.cur","hole.cur","limo.cur","ship.cur","taxi.cur","tram.cur","trolleybus.cur","van.cur","water.cur"}};
std::string fold(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;}
bool within(const std::filesystem::path& root,const std::filesystem::path& path){
    const auto relative=path.lexically_relative(root);
    if(relative.empty()||relative.is_absolute())return false;
    for(const auto& part:relative)if(part=="..")return false;
    return true;
}
}
const char* original_cursor_filename(OriginalCursorAsset asset){
    const auto i=static_cast<std::size_t>(asset);
    if(i>=filenames.size())throw odh::FormatError("unknown original cursor asset identity");
    return filenames[i];
}
OriginalCursors::OriginalCursors(const std::filesystem::path& game_root,OriginalCursorLimits limits){
    const auto root=std::filesystem::canonical(game_root);
    const auto directory=std::filesystem::canonical(root/"UIGraphics/Shared/cursors");
    if(!within(root,directory))throw odh::FormatError("original cursor directory escapes installation");
    std::array<std::optional<std::filesystem::path>,original_cursor_count> paths;
    std::uint32_t count=0;
    for(const auto& item:std::filesystem::directory_iterator(directory)){
        if(++count>limits.directory_entries)throw odh::FormatError("original cursor directory exceeds entry budget");
        const auto name=fold(item.path().filename().string());
        for(std::size_t i=0;i<filenames.size();++i)if(name==fold(filenames[i])){
            if(paths[i])throw odh::FormatError("original cursor filename is ambiguous");
            const auto selected=std::filesystem::canonical(item.path());
            if(selected.parent_path()!=directory||!std::filesystem::is_regular_file(selected))
                throw odh::FormatError("original cursor asset is not an in-directory regular file");
            paths[i]=selected;
        }
    }
    std::uint64_t pixels=0;
    for(std::size_t i=0;i<paths.size();++i){
        if(!paths[i])throw odh::FormatError(std::string("original cursor asset missing: ")+filenames[i]);
        auto budget=limits.cursor;budget.pixels=static_cast<std::uint32_t>(std::min<std::uint64_t>(budget.pixels,limits.pixels-pixels));
        std::ifstream input(*paths[i],std::ios::binary);
        if(!input)throw odh::FormatError("cannot open original cursor asset");
        const odh::View view(input,std::filesystem::file_size(*paths[i]));
        if(view.size()>budget.bytes)throw odh::FormatError("original cursor exceeds byte budget");
        if(i==static_cast<std::size_t>(OriginalCursorAsset::MOVEIN)&&view.size()>=6&&view.le16(0)==0&&view.le16(2)==1){
            // Recognition only, not icon decoding. ICO directory words4/6 are
            // planes/bit depth, never cursor hotspot coordinates. No native
            // cursor or synthetic hotspot may be produced from this resource.
            const auto images=view.le16(4);
            if(!images||images>budget.images||6+std::uint64_t(images)*16>view.size())
                throw odh::FormatError("MOVEIN icon directory is truncated or over budget");
            status_[i]=OriginalCursorStatus::unsupported_icon_without_hotspot;
            continue;
        }
        cursors_[i]=odh::read_windows_cur(view,budget);
        for(const auto& image:cursors_[i].images)pixels+=std::uint64_t(image.width)*image.height;
    }
}
const odh::WindowsCursor& OriginalCursors::at(OriginalCursorAsset asset)const{
    const auto i=static_cast<std::size_t>(asset);
    if(i>=cursors_.size())throw odh::FormatError("unknown original cursor asset identity");
    return cursors_[i];
}
OriginalCursorStatus OriginalCursors::status(OriginalCursorAsset asset)const{
    const auto i=static_cast<std::size_t>(asset);
    if(i>=status_.size())throw odh::FormatError("unknown original cursor asset identity");
    return status_[i];
}
const char* OriginalCursors::diagnostic(OriginalCursorAsset asset)const{
    return status(asset)==OriginalCursorStatus::ready?"ready":
        "ICO image is unavailable as cursor: no authenticated hotspot";
}
}

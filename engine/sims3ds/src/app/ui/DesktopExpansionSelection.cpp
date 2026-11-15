/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __3DS__
#include "app/ui/DesktopExpansionPanels.h"
#include <algorithm>
#include <map>
namespace simapp { namespace original {
void DesktopInventorySelection::Sort(DesktopInventorySort sort) {
    if(sort_==sort)return;
    sort_=sort;page_=0;selected_.reset();popup_open_=false;popup.Close();
}
std::vector<std::size_t> DesktopInventorySelection::VisibleItems(const DesktopInventorySnapshot& snapshot)const {
    std::vector<std::size_t> indices;
    if(!snapshot.has_selected_person||!snapshot.items)return indices;
    // Original0051e3b5 enumerates Gifts in type0,4,5 order. Each
    // occurrence appends the cached GUID tile to the active vector; later
    // occurrences overwrite that same tile's count (never sum the records).
    const int types[]{sort_==DesktopInventorySort::gifts?0:sort_==DesktopInventorySort::magic?7:8,4,5};
    const int groups=sort_==DesktopInventorySort::gifts?3:1;
    std::map<std::uint32_t,std::size_t> final_by_guid;
    for(int group=0;group<groups;++group)for(std::size_t i=0;i<snapshot.items->size();++i)
        if((*snapshot.items)[i].type==types[group]){
            indices.push_back(i);final_by_guid[(*snapshot.items)[i].guid]=i;
        }
    // Original0051ead0 compares the tile GUID using signed x86 SETL.
    // XOR the sign bit for a portable equivalent without implementation-defined
    // uint32->int32 conversion. Stable ties preserve the source identity.
    std::stable_sort(indices.begin(),indices.end(),[&](std::size_t a,std::size_t b){
        return ((*snapshot.items)[a].guid^0x80000000u)<((*snapshot.items)[b].guid^0x80000000u);
    });
    // Keep the original occurrence count for page occupancy, but every
    // alias selects the final source record supplying the shared tile count.
    // Snapshot raw records remain untouched, including earlier aliases.
    for(auto& index:indices)index=(*snapshot.items)[final_by_guid.at((*snapshot.items)[index].guid)].source_index;
    return indices;
}
void DesktopInventorySelection::Reconcile(const DesktopInventorySnapshot& snapshot,std::size_t page_size) {
    if(neighbor_!=snapshot.neighbor_id){neighbor_=snapshot.neighbor_id;page_=0;selected_.reset();popup_open_=false;popup.Close();}
    std::vector<std::array<std::uint64_t,4>> signature;
    if(snapshot.has_selected_person&&snapshot.items)for(const auto& item:*snapshot.items)
        signature.push_back({item.source_index,std::uint32_t(item.type),item.guid,item.count});
    // Source indices may be reused after removal; never let a popup silently
    // acquire the next item's identity. Both pinned Simitone views reset their
    // scroll on raw inventory changes; selection is equally invalidated here.
    if(signature!=source_signature_){source_signature_=std::move(signature);page_=0;selected_.reset();popup_open_=false;popup.Close();}
    const auto visible=VisibleItems(snapshot);
    if(selected_&&std::find(visible.begin(),visible.end(),*selected_)==visible.end()){
        selected_.reset();popup_open_=false;popup.Close();
    }
    const auto pages=page_size?(visible.size()+page_size-1)/page_size:0;
    page_=pages?std::min(page_,pages-1):0;
}
bool DesktopInventorySelection::Page(int delta,const DesktopInventorySnapshot& snapshot,std::size_t page_size) {
    Reconcile(snapshot,page_size);
    const auto count=VisibleItems(snapshot).size();
    const auto pages=page_size?(count+page_size-1)/page_size:0;
    const auto before=page_;
    if(delta<0){const auto amount=std::size_t(-std::int64_t(delta));page_=amount>page_?0:page_-amount;}
    else if(delta>0&&pages){const auto amount=std::size_t(delta);page_+=std::min(amount,pages-1-page_);}
    if(page_!=before){popup_open_=false;popup.Close();}
    return page_!=before;
}
bool DesktopInventorySelection::Select(std::size_t source_index,const DesktopInventorySnapshot& snapshot) {
    const auto visible=VisibleItems(snapshot);
    if(std::find(visible.begin(),visible.end(),source_index)==visible.end())return false;
    if(selected_==source_index)popup_open_=!popup_open_;
    else{selected_=source_index;popup_open_=true;}
    return true;
}
}}
#endif

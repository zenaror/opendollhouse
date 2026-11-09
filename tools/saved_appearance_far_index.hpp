// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/formats.hpp"
#include <algorithm>
#include <numeric>
#include <string_view>

namespace odh_host::appearance_detail {
// Exactly the legacy fold(name) equality: ASCII A..Z only. Do NOT normalize
// basename, separators, locale, accents or Unicode here. No name allocations.
inline unsigned char folded_byte(unsigned char c) noexcept {
    return c>='A'&&c<='Z'?static_cast<unsigned char>(c+('a'-'A')):c;
}
inline int compare_full_names(std::string_view a,std::string_view b) noexcept {
    const auto count=std::min(a.size(),b.size());
    for(std::size_t n=0;n<count;++n){
        const auto left=folded_byte(static_cast<unsigned char>(a[n]));
        const auto right=folded_byte(static_cast<unsigned char>(b[n]));
        if(left<right)return -1;
        if(left>right)return 1;
    }
    return a.size()<b.size()?-1:a.size()>b.size()?1:0;
}
// Entries remain immutable and in original FAR/provider order. Only their
// indices are sorted. Logical auxiliary storage: sizeof(size_t)*entry_count,
// bounded by Archive's existing8192-entry parser limit; no copied name keys.
inline std::vector<std::size_t> index_full_names(const std::vector<odh::FarEntry>& entries) {
    std::vector<std::size_t> order(entries.size());
    std::iota(order.begin(),order.end(),std::size_t{0});
    std::sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){
        return compare_full_names(entries[a].name,entries[b].name)<0;
    });
    return order;
}
// The order must be built from these immutable entries. Ambiguous keys are
// retained, not rejected at construction: unrelated unique lookups still work.
inline const odh::FarEntry& select_full_name(const std::vector<odh::FarEntry>& entries,
        const std::vector<std::size_t>& order,const std::string& name) {
    const auto at=std::lower_bound(order.begin(),order.end(),std::string_view{name},
        [&](std::size_t index,std::string_view key){return compare_full_names(entries[index].name,key)<0;});
    if(at==order.end()||compare_full_names(entries[*at].name,name)!=0)
        throw odh::FormatError("saved appearance missing FAR entry: "+name);
    const auto next=at+1;
    if(next!=order.end()&&compare_full_names(entries[*next].name,name)==0)
        throw odh::FormatError("saved appearance ambiguous FAR entry: "+name);
    return entries[*at];
}
}

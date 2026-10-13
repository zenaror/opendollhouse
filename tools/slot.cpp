#include "odh/formats.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
std::uint16_t number(const std::string& s) {
    if (s.empty() || s[0] < '0' || s[0] > '9') throw odh::FormatError("invalid numeric selector");
    std::size_t end=0;
    const bool hex=s.size()>2 && s[0]=='0' && (s[1]=='x' || s[1]=='X');
    const auto value=std::stoull(s,&end,hex?16:10);
    if (end!=s.size() || value>65535) throw odh::FormatError("selector outside uint16 range");
    return static_cast<std::uint16_t>(value);
}
template<class T> void optional(const std::optional<T>& value) {
    if (value) std::cout<<*value; else std::cout<<"null";
}
void record(const odh::SlotRecord& r,std::size_t index) {
    std::cout<<"{\"record_index\":"<<index<<",\"type\":"<<r.type<<",\"offset_bits\":["
        <<r.offset_bits[0]<<','<<r.offset_bits[1]<<','<<r.offset_bits[2]<<"],\"standing\":"<<r.standing
        <<",\"sitting\":"<<r.sitting<<",\"ground\":"<<r.ground<<",\"flags\":"<<r.flags
        <<",\"snap_target_slot\":"<<r.snap_target_slot<<",\"proximity_raw\":";
    if (r.proximity) std::cout<<'['<<(*r.proximity)[0]<<','<<(*r.proximity)[1]<<','<<(*r.proximity)[2]<<']';
    else std::cout<<"null";
    std::cout<<",\"max_size\":"; optional(r.max_size);
    std::cout<<",\"unknown_i10\":"; optional(r.unknown_i10);
    std::cout<<",\"gradient_bits\":"; optional(r.gradient_bits);
    std::cout<<",\"height\":"; optional(r.height);
    std::cout<<",\"facing\":"; optional(r.facing);
    std::cout<<",\"resolution\":"; optional(r.resolution);
    std::cout<<'}';
}
}
int main(int argc,char** argv) {
    if (argc!=4 && argc!=6) {
        std::cerr<<"Usage: odh-slot ARCHIVE.far ENTRY.iff CHUNK_ID [TYPE INDEX_WITHIN_TYPE]\n"
                   "Read-only raw SLOT fields; selectors decimal or 0x-prefixed.\n";
        return 2;
    }
    try {
        const auto id=number(argv[3]);
        std::optional<std::uint16_t> type,index;
        if (argc==6) { type=number(argv[4]); index=number(argv[5]); }
        std::ifstream stream(argv[1],std::ios::binary);
        if (!stream) throw odh::FormatError("cannot open archive");
        const odh::View archive(stream,std::filesystem::file_size(argv[1]));
        std::optional<odh::FarEntry> selected;
        for (const auto& entry : odh::far1_index(archive)) {
            if (entry.name!=argv[2]) continue;
            if (selected) throw odh::FormatError("ambiguous archive entry");
            selected=entry;
        }
        if (!selected) throw odh::FormatError("exact archive entry not found");
        std::optional<odh::SlotTable> table;
        odh::iff_chunks(archive.subview(selected->offset,selected->length),[&](const odh::Chunk& c) {
            if (c.type!="SLOT" || c.id!=id) return;
            if (table) throw odh::FormatError("ambiguous SLOT chunk ID");
            table=odh::read_slot(c.payload);
        });
        if (!table) throw odh::FormatError("SLOT chunk not found");
        const auto* item=type ? odh::slot_of_type(*table,*type,*index) : nullptr;
        if (type && !item) throw odh::FormatError("slot type/index not found");
        std::cout<<"{\"evidence\":\"decoded_fields_only\",\"chunk_id\":"<<id
            <<",\"version\":"<<table->version<<",\"reserved\":"<<table->reserved
            <<",\"declared_count\":"<<table->records.size()<<",\"remaining_bytes\":"<<table->remaining_bytes;
        if (type) std::cout<<",\"selection\":{\"type\":"<<*type<<",\"index_within_type\":"<<*index<<'}';
        std::cout<<",\"records\":[";
        if (item) record(*item,static_cast<std::size_t>(item-table->records.data()));
        else for (std::size_t n=0;n<table->records.size();++n) {
            if (n) std::cout<<',';
            record(table->records[n],n);
        }
        std::cout<<"]}\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<"odh-slot: "<<e.what()<<'\n'; return 2; }
}

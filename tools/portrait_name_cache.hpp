// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/sha256.hpp"
#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <map>

// The private cache contains only declarations. Live FAR selection, limits,
// duplicate handling and the mesh/texture data remain the provider's job.
namespace odh_host::preview_detail {
using PortraitNames = std::vector<std::pair<char,std::string>>;
class PortraitNameCache {
    using Digest = std::array<std::uint8_t,32>;
    static constexpr std::size_t MaxBytes = 2 * 1024 * 1024, MaxRecords = 8192;
    static constexpr char Magic[] = "ODHPN001";
    std::filesystem::path path_;
    std::vector<std::uint8_t> records_;
    std::map<Digest,std::size_t> offsets_;
    bool dirty_ = false;
    std::size_t hits_ = 0, misses_ = 0;

    static std::uint32_t u32(const std::uint8_t* p) {
        return p[0] | std::uint32_t(p[1])<<8 | std::uint32_t(p[2])<<16 | std::uint32_t(p[3])<<24;
    }
    static void append32(std::vector<std::uint8_t>& out,std::uint32_t n) {
        for(unsigned shift=0;shift<32;shift+=8) out.push_back(static_cast<std::uint8_t>(n>>shift));
    }
    void load() {
        std::ifstream input(path_, std::ios::binary | std::ios::ate);
        const auto size = input.tellg();
        if (size < 40 || size > static_cast<std::streamoff>(MaxBytes + 40)) return;
        input.seekg(0);
        char magic[8];
        Digest expected;
        input.read(magic,8);
        input.read(reinterpret_cast<char*>(expected.data()),expected.size());
        if (!input || !std::equal(magic,magic+8,Magic)) return;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size)-40);
        input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if (!input || odh::sha256(bytes.data(),bytes.size())!=expected) return;

        std::map<Digest,std::size_t> offsets;
        for(std::size_t at=0;at<bytes.size();) {
            if(bytes.size()-at<36 || offsets.size()>=MaxRecords) return;
            Digest key;
            std::copy_n(bytes.data()+at,key.size(),key.begin());
            at+=key.size();
            const auto len=u32(bytes.data()+at);
            if(len>bytes.size()-at-4 || !offsets.emplace(key,at).second) return;
            at+=4+len;
        }
        records_=std::move(bytes);
        offsets_=std::move(offsets);
    }
public:
    explicit PortraitNameCache(const std::filesystem::path& path):path_(path) {
        if(!path_.empty()) load();
    }

    template<class Parse> PortraitNames read(const odh::View& source,char kind,Parse parse) {
        // Large custom containers still use the normal bounded streaming parser.
        if(source.size()>256*1024) return parse(source);
        const auto size=static_cast<std::size_t>(source.size());
        std::vector<std::uint8_t> bytes;
        const auto* data=source.memory_data();
        if(!data && size){
            bytes.resize(size);
            source.read(0,bytes.data(),size);
            data=bytes.data();
        }
        const odh::View memory(data,size);
        if(path_.empty()) return parse(memory);

        // FAT timestamp/inode precision varies. Check exact selected payload
        // bytes, including for same-length replacements with preserved times.
        odh::Sha256 hash;
        hash.update(&kind,1);
        hash.update(data,size);
        const auto key=hash.finish();
        const auto found=offsets_.find(key);
        if(found!=offsets_.end()) {
            auto at=found->second;
            const auto end=at+4+u32(records_.data()+at);
            at+=4;
            PortraitNames names;
            while(at<end && end-at>=3 && names.size()<8192) {
                const auto type=static_cast<char>(records_[at]);
                const std::size_t length=records_[at+1] | (unsigned(records_[at+2])<<8);
                if ((type!='S' && type!='A' && type!='C') || !length || length>4096 || length>end-at-3) break;
                names.emplace_back(type,std::string(reinterpret_cast<const char*>(records_.data()+at+3),length));
                at+=3+length;
            }
            if(at==end) {++hits_; return names;}
            return parse(memory);
        }
        ++misses_;
        auto names=parse(memory);
        std::size_t length=0;
        for(const auto& item:names) {
            if(item.second.empty() || item.second.size()>4096) return names;
            length+=3+item.second.size();
        }
        if(names.size()>8192 || offsets_.size()>=MaxRecords || length+36>MaxBytes-records_.size()) return names;
        records_.insert(records_.end(),key.begin(),key.end());
        offsets_.emplace(key,records_.size());
        append32(records_,static_cast<std::uint32_t>(length));
        for(const auto& item:names) {
            records_.push_back(static_cast<std::uint8_t>(item.first));
            records_.push_back(static_cast<std::uint8_t>(item.second.size()));
            records_.push_back(static_cast<std::uint8_t>(item.second.size()>>8));
            records_.insert(records_.end(),item.second.begin(),item.second.end());
        }
        dirty_=true;
        return names;
    }
    void save() const {
        if(path_.empty()) return;
        std::printf("[cold-ui] stage=portrait-name-cache hits=%zu misses=%zu records=%zu bytes=%zu\n",
                    hits_,misses_,offsets_.size(),records_.size());
        if(!dirty_) return;
        const auto temp=path_.string()+".new";
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        if(!out) return;
        const auto digest=odh::sha256(records_.data(),records_.size());
        out.write(Magic,8);
        out.write(reinterpret_cast<const char*>(digest.data()),digest.size());
        out.write(reinterpret_cast<const char*>(records_.data()),records_.size());
        out.close();
        if(out) {
            if(std::rename(temp.c_str(),path_.string().c_str())==0) return;
            // Some FAT implementations cannot replace an existing file by rename.
            // This index is disposable and a missing/partial cache always reparses.
            if(errno==EEXIST && std::remove(path_.string().c_str())==0 &&
               std::rename(temp.c_str(),path_.string().c_str())==0) return;
        }
        std::remove(temp.c_str());
    }
};
}

#pragma once
#include "odh/formats.hpp"
#include "odh/host_filesystem.hpp"
#include <filesystem>
#include <fstream>
#include <array>
#include <algorithm>
#include <limits>
#include <memory>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdio>

struct ResourceIoCounters {
    std::atomic<std::uint64_t> opens{0},reads{0},bytes{0},manifest_reads{0},manifest_hits{0};
};
inline ResourceIoCounters& resource_io_counters(){static ResourceIoCounters value;return value;}
class ColdResourceStage {
public:
    explicit ColdResourceStage(const char* stage):stage_(stage),started_(std::chrono::steady_clock::now()),
        opens_(resource_io_counters().opens),reads_(resource_io_counters().reads),bytes_(resource_io_counters().bytes),
        manifests_(resource_io_counters().manifest_reads),hits_(resource_io_counters().manifest_hits){}
    ~ColdResourceStage(){
        const auto& io=resource_io_counters();
        std::printf("[cold-ui] stage=%s ms=%.3f opens=%llu reads=%llu bytes=%llu manifest_reads=%llu manifest_hits=%llu\n",
            stage_,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started_).count(),
            static_cast<unsigned long long>(io.opens-opens_),static_cast<unsigned long long>(io.reads-reads_),
            static_cast<unsigned long long>(io.bytes-bytes_),static_cast<unsigned long long>(io.manifest_reads-manifests_),
            static_cast<unsigned long long>(io.manifest_hits-hits_));
    }
private:
    const char* stage_;
    std::chrono::steady_clock::time_point started_;
    std::uint64_t opens_,reads_,bytes_,manifests_,hits_;
};
// View performs many absolute seeks while decoding scalar fields. Standard
// filebuf discards its read buffer on those seeks on several portable runtimes.
// Keep a bounded read window across seeks within that window instead. Each
// owner retains its own stream; no View or mutable cursor crosses owners.
class BufferedResourceFile : private std::streambuf, public std::istream {
public:
    explicit BufferedResourceFile(const std::string& path)
        : std::istream(static_cast<std::streambuf*>(this)), file_(path,std::ios::binary) {
        setg(window_.data(),window_.data(),window_.data());
        ++resource_io_counters().opens;
        if(!file_)setstate(std::ios::failbit);
    }
    std::uint64_t physical_reads() const {return reads_;}
    std::uint64_t physical_bytes() const {return bytes_;}
private:
    using traits_type=std::streambuf::traits_type;
    using int_type=std::streambuf::int_type;
    using pos_type=std::streambuf::pos_type;
    using off_type=std::streambuf::off_type;
    std::ifstream file_;
    std::vector<char> window_=std::vector<char>(64*1024);
    std::streamoff start_=0;
    std::uint64_t reads_=0,bytes_=0;
    int_type underflow() override {
        if(gptr()<egptr())return traits_type::to_int_type(*gptr());
        start_+=egptr()-eback();
        file_.clear();file_.seekg(start_);
        if(!file_)return traits_type::eof();
        file_.read(window_.data(),window_.size());
        const auto count=file_.gcount();++reads_;bytes_+=static_cast<std::uint64_t>(count);
        ++resource_io_counters().reads;resource_io_counters().bytes+=static_cast<std::uint64_t>(count);
        setg(window_.data(),window_.data(),window_.data()+count);
        return count?traits_type::to_int_type(*gptr()):traits_type::eof();
    }
    pos_type seekpos(pos_type position,std::ios_base::openmode which) override {
        const auto offset=static_cast<off_type>(position);
        if(!(which&std::ios::in)||offset<0)return pos_type(off_type(-1));
        const auto extent=egptr()-eback();
        if(offset>=start_&&offset-start_<=extent)
            setg(eback(),eback()+static_cast<std::ptrdiff_t>(offset-start_),egptr());
        else {start_=offset;setg(window_.data(),window_.data(),window_.data());}
        return position;
    }
    pos_type seekoff(off_type offset,std::ios_base::seekdir direction,std::ios_base::openmode which) override {
        off_type base=0;
        if(direction==std::ios::cur)base=start_+(gptr()-eback());
        else if(direction==std::ios::end){
            file_.clear();file_.seekg(0,std::ios::end);
            if(!file_)return pos_type(off_type(-1));
            base=static_cast<off_type>(file_.tellg());
        }else if(direction!=std::ios::beg)return pos_type(off_type(-1));
        if(base<0||(offset>0&&base>std::numeric_limits<off_type>::max()-offset)||
           (offset<0&&offset < -base))return pos_type(off_type(-1));
        offset+=base;
        return seekpos(pos_type(offset),which);
    }
};

// Immutable manifests can be reused while an appearance/renderer owns them.
// Mutable input cursors remain per consumer. Weak registration is bounded and
// keyed by physical root identity plus source size and modification time.
inline std::shared_ptr<const std::vector<odh::FarEntry>> shared_far_manifest(
        const std::string& path,const odh::View& view,const odh::Limits& limits={}) {
    struct Cached {
        std::uintmax_t size=0;
        std::filesystem::file_time_type modified{};
        std::weak_ptr<const std::vector<odh::FarEntry>> entries;
    };
    static std::map<std::string,Cached> cache;
    static std::mutex mutex;
    std::error_code error;
    const auto canonical=odh::hostfs::canonical(path,error);
    const auto identity=error?std::string{}:canonical.string();
    const auto size=std::filesystem::file_size(path,error);
    const bool sized=!error;
    const auto modified=std::filesystem::last_write_time(path,error);
    const bool cacheable=!identity.empty()&&sized&&!error;
    std::lock_guard<std::mutex> guard(mutex);
    for(auto it=cache.begin();it!=cache.end();)if(it->second.entries.expired())it=cache.erase(it);else ++it;
    auto found=cache.find(identity);
    if(cacheable&&found!=cache.end()&&found->second.size==size&&found->second.modified==modified){
        if(auto entries=found->second.entries.lock()){
            if(entries->size()>limits.far_entries)throw odh::FormatError("FAR entry count exceeds configured limit");
            std::uint64_t names=0;
            for(const auto& entry:*entries){
                if(entry.name.size()>limits.far_name_length)throw odh::FormatError("FAR name exceeds supported length");
                names+=entry.name.size();
            }
            if(names>limits.far_name_bytes)throw odh::FormatError("FAR cumulative name bytes exceed configured limit");
            ++resource_io_counters().manifest_hits;return entries;
        }
    }
    ++resource_io_counters().manifest_reads;
    auto entries=std::make_shared<const std::vector<odh::FarEntry>>(odh::far1_index(view,limits));
    if(cacheable&&(found!=cache.end()||cache.size()<256))cache[identity]={size,modified,entries};
    return entries;
}

class FarEntryNotFound : public odh::FormatError {
public:
    using odh::FormatError::FormatError;
};

template<class F> void with_iff_file(const std::string& path,F read) {
    BufferedResourceFile stream(path);if(!stream)throw odh::FormatError("cannot open IFF file");
    read(odh::View(stream,std::filesystem::file_size(path)));
}
template<class F> void with_far_entry(const std::string& path,const std::string& name,F read,
                                      const odh::Limits& limits = {}) {
    BufferedResourceFile stream(path);if(!stream)throw odh::FormatError("cannot open archive");
    const odh::View archive(stream,std::filesystem::file_size(path));std::optional<odh::FarEntry> selected;
    // Retain the owner throughout iteration: dereferencing a temporary
    // shared_ptr does not extend the pointee's lifetime in a C++17 range-for.
    const auto entries=shared_far_manifest(path,archive,limits);
    for(const auto& entry:*entries){if(entry.name!=name)continue;
        if(selected)throw odh::FormatError("ambiguous archive entry");
        selected=entry;}
    if(!selected)throw FarEntryNotFound("exact archive entry not found");
    read(archive.subview(selected->offset,selected->length));
}

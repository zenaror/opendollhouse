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
#include <cerrno>
#include <cstring>
#include <vector>
#include <sys/stat.h>

struct ResourceIoCounters {
    std::atomic<std::uint64_t> opens{0},reads{0},seeks{0},bytes{0},manifest_reads{0},manifest_hits{0};
    std::atomic<std::uint64_t> open_failures{0},reopens{0},evictions{0},live_handles{0},peak_handles{0},owners{0},weak_identity_opens{0};
};
// Readers can be destroyed by other translation units during process shutdown.
inline ResourceIoCounters& resource_io_counters(){static auto* value=new ResourceIoCounters;return *value;}
class ColdResourceStage {
public:
    explicit ColdResourceStage(const char* stage):stage_(stage),started_(std::chrono::steady_clock::now()),
        opens_(resource_io_counters().opens),reads_(resource_io_counters().reads),seeks_(resource_io_counters().seeks),bytes_(resource_io_counters().bytes),
        manifests_(resource_io_counters().manifest_reads),hits_(resource_io_counters().manifest_hits),
        failures_(resource_io_counters().open_failures),reopens_(resource_io_counters().reopens),evictions_(resource_io_counters().evictions){}
    ~ColdResourceStage(){
        const auto& io=resource_io_counters();
        std::printf("[cold-ui] stage=%s ms=%.3f opens=%llu reads=%llu seeks=%llu bytes=%llu manifest_reads=%llu manifest_hits=%llu open_failures=%llu reopens=%llu evictions=%llu resource_live=%llu resource_peak=%llu resource_cap=4 identity_weak_opens=%llu\n",
            stage_,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started_).count(),
            static_cast<unsigned long long>(io.opens-opens_),static_cast<unsigned long long>(io.reads-reads_),
            static_cast<unsigned long long>(io.seeks-seeks_),static_cast<unsigned long long>(io.bytes-bytes_),static_cast<unsigned long long>(io.manifest_reads-manifests_),
            static_cast<unsigned long long>(io.manifest_hits-hits_),
            static_cast<unsigned long long>(io.open_failures-failures_),static_cast<unsigned long long>(io.reopens-reopens_),
            static_cast<unsigned long long>(io.evictions-evictions_),static_cast<unsigned long long>(io.live_handles.load()),
            static_cast<unsigned long long>(io.peak_handles.load()),static_cast<unsigned long long>(io.weak_identity_opens.load()));
    }
private:
    const char* stage_;
    std::chrono::steady_clock::time_point started_;
    std::uint64_t opens_,reads_,seeks_,bytes_,manifests_,hits_,failures_,reopens_,evictions_;
};
// Each owner keeps its own window and logical cursor. Only backing handles
// are evicted; a process-lifetime registry bounds this service independently
// of the engine's FilePool. Resource files must remain immutable while owned.
// Reopen rejects detectable changes. Devices without inode/subsecond metadata
// can only check size and available timestamps, not every same-size rewrite.
class BufferedResourceFile : private std::streambuf, public std::istream {
public:
    static constexpr std::size_t HandleLimit=4;
    enum class Window { Full, Metadata };
    explicit BufferedResourceFile(const std::string& path,Window window=Window::Full)
        :std::istream(static_cast<std::streambuf*>(this)),path_(path),
         window_(window==Window::Metadata ? 16*1024 : 64*1024),far_metadata_(window==Window::Metadata) {
        setg(window_.data(),window_.data(),window_.data());
        auto& handles=pool();std::lock_guard<std::mutex> lock(handles.mutex);
        if(!acquire_locked(true))setstate(std::ios::failbit);
        ++resource_io_counters().owners;
    }
    ~BufferedResourceFile() override {
        auto& handles=pool();std::lock_guard<std::mutex> lock(handles.mutex);
        close_locked();--resource_io_counters().owners;
    }
    BufferedResourceFile(const BufferedResourceFile&)=delete;
    BufferedResourceFile& operator=(const BufferedResourceFile&)=delete;
    BufferedResourceFile(BufferedResourceFile&&)=delete;
    BufferedResourceFile& operator=(BufferedResourceFile&&)=delete;
    std::uint64_t size() const {return static_cast<std::uint64_t>(identity_.size);}
    std::uint64_t physical_reads() const {return reads_;}
    std::uint64_t physical_bytes() const {return bytes_;}
    const std::string& diagnostic() const {return diagnostic_;}
    // Read one coalesced metadata extent without disturbing this consumer's
    // logical cursor or cached bytes. Reopens use the usual identity checks.
    void read_range(std::uint32_t offset, std::uint8_t* out, std::size_t size) {
        auto& handles=pool();std::lock_guard<std::mutex> lock(handles.mutex);
        const auto count=read_backing_locked(offset,reinterpret_cast<char*>(out),size);
        if(count<0 || static_cast<std::size_t>(count)!=size)
            throw odh::FormatError("short metadata extent in " + path_);
    }
private:
    using traits_type=std::streambuf::traits_type;
    using int_type=traits_type::int_type;
    using pos_type=traits_type::pos_type;
    using off_type=traits_type::off_type;
    struct Identity {
        std::uintmax_t device=0,inode=0;
        std::intmax_t size=0,modified=0,subsecond=0;
        bool operator==(const Identity& other) const {
            return device==other.device&&inode==other.inode&&size==other.size&&
                modified==other.modified&&subsecond==other.subsecond;
        }
    };
    struct Handles {std::mutex mutex;std::vector<BufferedResourceFile*> open;std::uint64_t clock=0;};
    static Handles& pool(){static auto* handles=new Handles;return *handles;}
    std::string path_,diagnostic_;
    std::ifstream file_;
    std::vector<char> window_;
    Identity identity_;
    std::streamoff start_=0, next_backing_offset_=0;
    std::uint64_t reads_=0,bytes_=0,last_use_=0;
    bool far_metadata_=false, failed_=false;
    bool identity(Identity& out,int& error) const {
#if defined(_WIN32)
        struct _stat64 info{};errno=0;const auto result=::_stat64(path_.c_str(),&info);error=errno;
        if(result!=0)return false;
        if((info.st_mode&_S_IFMT)!=_S_IFREG){error=EINVAL;return false;}
#else
        struct stat info{};errno=0;const auto result=::stat(path_.c_str(),&info);error=errno;
        if(result!=0)return false;
        if(!S_ISREG(info.st_mode)){error=EINVAL;return false;}
#endif
        out.device=static_cast<std::uintmax_t>(info.st_dev);out.inode=static_cast<std::uintmax_t>(info.st_ino);
        out.size=static_cast<std::intmax_t>(info.st_size);out.modified=static_cast<std::intmax_t>(info.st_mtime);
#if defined(__APPLE__)
        out.subsecond=info.st_mtimespec.tv_nsec;
#elif defined(__linux__)
        out.subsecond=info.st_mtim.tv_nsec;
#endif
        return true;
    }
    void close_locked(){
        if(file_.is_open())file_.close();
        auto& open=pool().open;const auto found=std::find(open.begin(),open.end(),this);
        if(found!=open.end()){open.erase(found);--resource_io_counters().live_handles;}
    }
    bool failure_locked(const char* operation,int error,const char* reason){
        failed_=true;setg(window_.data(),window_.data(),window_.data());
        diagnostic_=std::string(operation)+" '"+path_+"': "+reason+
            " (errno="+std::to_string(error)+", "+(error?std::strerror(error):"unavailable")+")";
        const auto& io=resource_io_counters();
        std::fprintf(stderr,"[resource-file] %s attempts=%llu failures=%llu live=%llu peak=%llu cap=%zu owners=%llu\n",
            diagnostic_.c_str(),static_cast<unsigned long long>(io.opens.load()),
            static_cast<unsigned long long>(io.open_failures.load()),static_cast<unsigned long long>(io.live_handles.load()),
            static_cast<unsigned long long>(io.peak_handles.load()),HandleLimit,static_cast<unsigned long long>(io.owners.load()));
        return false;
    }
    // Caller holds the registry lock through all backing stream operations.
    bool acquire_locked(bool initial=false){
        if(failed_)return false;
        auto& handles=pool();
        if(file_.is_open()){last_use_=++handles.clock;return true;}
        Identity before;int metadata_error=0;const bool known=identity(before,metadata_error);
        if(!initial&&(!known||!(before==identity_)))
            return failure_locked("reopen-identity",metadata_error,known?"source changed":"cannot verify source identity");
        // Close before opening, including initial admission and failed attempts.
        if(handles.open.size()>=HandleLimit){
            auto* victim=*std::min_element(handles.open.begin(),handles.open.end(),
                [](const auto* a,const auto* b){return a->last_use_<b->last_use_;});
            victim->close_locked();++resource_io_counters().evictions;
        }
        errno=0;file_.clear();file_.open(path_,std::ios::binary);const int open_error=errno;
        ++resource_io_counters().opens;if(!initial)++resource_io_counters().reopens;
        if(!file_){++resource_io_counters().open_failures;return failure_locked(initial?"initial-open":"reopen",open_error,"cannot open source");}
        // Include successfully opened handles in peak telemetry even if the
        // following identity admission rejects and closes the source.
        last_use_=++handles.clock;handles.open.push_back(this);
        const auto live=++resource_io_counters().live_handles;
        if(live>resource_io_counters().peak_handles.load())resource_io_counters().peak_handles=live;
        Identity after;int after_error=0;const bool verified=identity(after,after_error);
        if(!known||!verified||!(before==after)){
            close_locked();return failure_locked(initial?"initial-identity":"reopen-identity",known?after_error:metadata_error,
                !known||!verified?"cannot verify source identity":"source changed during open");
        }
        next_backing_offset_=0;
        if(initial)identity_=after;
        if(!after.inode)++resource_io_counters().weak_identity_opens;
        return true;
    }
    std::streamsize read_backing_locked(std::streamoff offset,char* out,std::size_t size) {
        if(!acquire_locked())return -1;
        errno=0;file_.clear();
        if(offset!=next_backing_offset_){file_.seekg(offset);++resource_io_counters().seeks;}
        const int seek_error=errno;
        if(!file_){close_locked();failure_locked("seek",seek_error,"cannot seek source");return -1;}
        errno=0;file_.read(out,static_cast<std::streamsize>(size));const int read_error=errno;
        const auto count=file_.gcount();++reads_;bytes_+=static_cast<std::uint64_t>(count);
        ++resource_io_counters().reads;resource_io_counters().bytes+=static_cast<std::uint64_t>(count);
        if(file_.bad()||(file_.fail()&&!file_.eof())){
            close_locked();failure_locked("read",read_error,"cannot read source");return -1;
        }
        next_backing_offset_=offset+count;
        return count;
    }
    int_type underflow() override {
        if(failed_)return traits_type::eof();
        if(gptr()<egptr())return traits_type::to_int_type(*gptr());
        auto& handles=pool();std::lock_guard<std::mutex> lock(handles.mutex);
        start_+=egptr()-eback();
        // The FAR header points to the trailing manifest; adjacent payload
        // bytes have no use on the first metadata read.
        const auto size=far_metadata_ && !reads_ && start_==0 ? 16u : window_.size();
        const auto count=read_backing_locked(start_,window_.data(),size);
        if(count<0)return traits_type::eof();
        setg(window_.data(),window_.data(),window_.data()+count);
        return count?traits_type::to_int_type(*gptr()):traits_type::eof();
    }
    pos_type seekpos(pos_type position,std::ios_base::openmode which) override {
        const auto offset=static_cast<off_type>(position);
        if(failed_||!(which&std::ios::in)||offset<0)return pos_type(off_type(-1));
        const auto extent=egptr()-eback();
        if(offset>=start_&&offset-start_<=extent)
            setg(eback(),eback()+static_cast<std::ptrdiff_t>(offset-start_),egptr());
        else {start_=offset;setg(window_.data(),window_.data(),window_.data());}
        return position;
    }
    pos_type seekoff(off_type offset,std::ios_base::seekdir direction,std::ios_base::openmode which) override {
        off_type base=0;
        if(failed_)return pos_type(off_type(-1));
        if(direction==std::ios::cur)base=start_+(gptr()-eback());
        else if(direction==std::ios::end){
            auto& handles=pool();std::lock_guard<std::mutex> lock(handles.mutex);
            if(!acquire_locked())return pos_type(off_type(-1));
            errno=0;file_.clear();file_.seekg(0,std::ios::end);const auto end=file_.tellg();const int error=errno;
            if(end==pos_type(off_type(-1))){close_locked();failure_locked("end-seek",error,"cannot inspect source size");return pos_type(off_type(-1));}
            base=static_cast<off_type>(end);
            next_backing_offset_=base;
            ++resource_io_counters().seeks;
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

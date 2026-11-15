/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "files/SafeFile.h"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <functional>
#include <stdexcept>
#ifndef __3DS__
#include "odh/host_filesystem.hpp"
#endif
namespace FSO { namespace Files {
// Exactly three converter destinations inside an authenticated private UserData.
// Uses SafeFile durability; physical power-loss guarantees depend on that writer/filesystem.
class LotRezoneTransaction {
public:
    struct Change { unsigned Slot; bool Existed=false; std::vector<uint8_t> Before, After; };
    using Fault=std::function<bool(unsigned)>; // optional isolated fault fixture
    static std::string Relative(unsigned slot,int house){
        if(slot==1)return "Neighborhood.iff";if(slot==3)return "LotZoning.iff";
        char n[40];std::snprintf(n,sizeof n,"Houses/House%02d.iff",house);return slot==2?n:std::string();
    }
    static bool Read(const std::string& path,std::vector<uint8_t>& out,bool& exists,std::string& error){
        out.clear();errno=0;FILE* f=std::fopen(path.c_str(),"rb");
        if(!f){exists=false;if(errno==ENOENT)return true;error="Cannot read converter file: "+path;return false;}
        exists=true;if(std::fseek(f,0,SEEK_END)||std::ftell(f)<0){std::fclose(f);error="Cannot measure converter file";return false;}
        long n=std::ftell(f);std::rewind(f);if(n>128*1024*1024){std::fclose(f);error="Converter file exceeds journal bound";return false;}
        out.resize(n);bool ok=out.empty()||std::fread(out.data(),1,out.size(),f)==out.size();if(std::fclose(f))ok=false;
        if(!ok)error="Cannot read complete converter file";return ok;
    }
    static bool Capture(const std::string& root,int house,unsigned slot,const std::vector<uint8_t>& after,Change& c,std::string& error){
        if(!Safe(root,Relative(slot,house),error))return false;c.Slot=slot;c.After=after;
        return Read(root+"/"+Relative(slot,house),c.Before,c.Existed,error);
    }
    static bool Recover(const std::string& root,std::string& error,bool* wasCommitted=nullptr){
        if(wasCommitted)*wasCommitted=false;
        if(!Safe(root,".odh-rezone-journal",error)||!Safe(root,".odh-rezone-commit",error))return false;
        auto path=root+"/.odh-rezone-journal",marker=root+"/.odh-rezone-commit";
        auto valid=[](const auto& b){try{int h;std::vector<Change> c;Decode(b,h,c);return true;}catch(...){return false;}};
        if(SafeFile::HasLeftover(path)&&SafeFile::Recover(path,valid)==SafeFile::Result::Damaged){error="Damaged converter journal retained";return false;}
        std::vector<uint8_t> journal,committed;bool exists=false,marked=false;if(!Read(path,journal,exists,error))return false;
        if(!exists)return Remove(marker,error)&&Remove(marker+".new",error)&&Remove(marker+".bak",error);
        int house=0;std::vector<Change> changes;try{Decode(journal,house,changes);}catch(const std::exception& e){error=e.what();return false;}
        for(const auto& c:changes)if(!Safe(root,Relative(c.Slot,house),error))return false;
        const auto expected=Marker(journal);
        if(SafeFile::HasLeftover(marker)&&SafeFile::Recover(marker,[&](const auto& b){return b==expected;})==SafeFile::Result::Damaged){error="Damaged converter commit marker retained";return false;}
        if(!Read(marker,committed,marked,error))return false;
        if(marked&&committed!=expected){error="Converter commit marker mismatch";return false;}
        // Inspect every destination first. Recognize interrupted SafeFile rename
        // states; unknown user edits must not be overwritten by a rollback.
        for(const auto& c:changes){
            auto dest=root+"/"+Relative(c.Slot,house);
            std::vector<uint8_t> now;bool present=false;if(!Read(dest,now,present,error))return false;
            if(present && now!=c.After && (!c.Existed || now!=c.Before)){
                error="Converter recovery found unrelated changed files; journal retained";return false;
            }
            if(SafeFile::HasLeftover(dest)){
                SafeFile::Recover(dest,[&](const auto& b){return b==c.After||(c.Existed&&b==c.Before);});
                if(!Read(dest,now,present,error))return false;
                if(SafeFile::HasLeftover(dest)){error="Unrecognized converter file leftovers retained";return false;}
            }
            bool old=present==c.Existed&&(!present||now==c.Before),next=present&&now==c.After;
            if(marked?!next:(!old&&!next)){error="Converter recovery found unrelated changed files; journal retained";return false;}
        }
        if(wasCommitted)*wasCommitted=marked;
        if(!marked)for(const auto& c:changes){
            auto dest=root+"/"+Relative(c.Slot,house);
            if(c.Existed){if(!SafeFile::Write(dest,c.Before,&error))return false;}
            else {SafeFile::ReleaseHandles(dest);if(!Remove(dest,error))return false;}
        }
        return Remove(path,error)&&Remove(path+".new",error)&&Remove(path+".bak",error)&&Remove(marker,error)&&Remove(marker+".new",error)&&Remove(marker+".bak",error);
    }
    static bool Commit(const std::string& root,int house,const std::vector<Change>& changes,std::string& error,const Fault& fault={}){
        if(!Recover(root,error))return false;std::vector<uint8_t> journal;
        try{journal=Encode(house,changes);int h;std::vector<Change> c;Decode(journal,h,c);}catch(const std::exception& e){error=e.what();return false;}
        for(const auto& c:changes){
            if(!Safe(root,Relative(c.Slot,house),error))return false;std::vector<uint8_t> now;bool exists=false;
            if(!Read(root+"/"+Relative(c.Slot,house),now,exists,error))return false;
            if(exists!=c.Existed||(exists&&now!=c.Before)){error="Lot changed during converter preparation";return false;}
        }
        if(!SafeFile::Write(root+"/.odh-rezone-journal",journal,&error))return false;
        unsigned step=0;
        for(const auto& c:changes)if((fault&&fault(++step))||!SafeFile::Write(root+"/"+Relative(c.Slot,house),c.After,&error))return Failed(root,error);
        if((fault&&fault(++step))||!SafeFile::Write(root+"/.odh-rezone-commit",Marker(journal),&error))return Failed(root,error);
        // The durable marker commits. Cleanup failure must not leave the caller
        // using old in-memory ownership after the files have already committed.
        std::string cleanup;Recover(root,cleanup);return true;
    }
private:
    static bool Failed(const std::string& root,std::string& error){
        std::string recovery;bool committed=false;bool ok=Recover(root,recovery,&committed);
        if(committed)return true; // marker was durably prepared before a rename error
        if(!ok)error+="; rollback: "+recovery;
        else if(error.empty())error="Converter interrupted; previous state restored";
        return false;
    }
    static bool Safe(const std::string& root,const std::string& relative,std::string& error){
        if(root.empty()||relative.empty()){error="Invalid converter destination";return false;}
#ifndef __3DS__
        std::error_code ec;auto base=odh::hostfs::canonical(std::filesystem::path(root),ec);if(ec){error="Cannot resolve converter save root";return false;}
        for(const auto* suffix:{"", ".new", ".bak", ".tmp"}){
            auto target=odh::hostfs::weakly_canonical(base/(relative+suffix),ec);
            if(ec){error="Cannot resolve converter destination";return false;}
            const auto b=base.generic_string(),t=target.generic_string();
            if(t.size()<=b.size()||t.compare(0,b.size(),b)||t[b.size()]!='/'){
                error="Converter destination escapes private saves";return false;
            }
        }
#endif
        return true;
    }
    static bool Remove(const std::string& path,std::string& error){errno=0;if(!std::remove(path.c_str())||errno==ENOENT)return true;error="Cannot remove converter recovery file";return false;}
    static void Put(std::vector<uint8_t>& b,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
    // Corruption checksum, not an authorization signature. Destination authority
    // comes from the private-root check and the fixed slot list below.
    static uint64_t Hash(const std::vector<uint8_t>& b,size_t n){uint64_t h=1469598103934665603ULL;for(size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ULL;}return h;}
    static std::vector<uint8_t> Marker(const std::vector<uint8_t>& b){std::vector<uint8_t> r={'O','D','H','C',1};Put(r,Hash(b,b.size()),8);return r;}
    static std::vector<uint8_t> Encode(int house,const std::vector<Change>& changes){std::vector<uint8_t>b={'O','D','H','Z',1};Put(b,house,2);Put(b,changes.size(),1);for(const auto& c:changes){Put(b,c.Slot,1);Put(b,c.Existed,1);Put(b,c.Before.size(),4);Put(b,c.After.size(),4);b.insert(b.end(),c.Before.begin(),c.Before.end());b.insert(b.end(),c.After.begin(),c.After.end());}Put(b,Hash(b,b.size()),8);return b;}
    static void Decode(const std::vector<uint8_t>& b,int& house,std::vector<Change>& changes){
        if(b.size()<16||b.size()>128*1024*1024||std::memcmp(b.data(),"ODHZ\1",5))throw std::runtime_error("Invalid converter journal header");
        size_t p=5;auto get=[&](unsigned n){if(p+n>b.size())throw std::runtime_error("Truncated converter journal");uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(b[p++])<<(8*i);return v;};
        house=int(get(2));auto count=get(1);if(house<1||house>80||(house>=21&&house<=49)||count<2||count>3)throw std::runtime_error("Invalid converter journal scope");
        unsigned seen=0;changes.clear();
        for(unsigned i=0;i<count;++i){Change c;c.Slot=unsigned(get(1));auto exists=get(1);c.Existed=exists==1;auto before=get(4),after=get(4);
            if(c.Slot<1||c.Slot>3||(seen&(1U<<c.Slot))||exists>1||before>64*1024*1024||after>64*1024*1024||p+before+after+8>b.size()||(!exists&&before))throw std::runtime_error("Invalid converter journal entry");
            seen|=1U<<c.Slot;c.Before.assign(b.begin()+p,b.begin()+p+before);p+=before;c.After.assign(b.begin()+p,b.begin()+p+after);p+=after;
            if((c.Existed&&!SafeFile::IffComplete(c.Before))||!SafeFile::IffComplete(c.After))throw std::runtime_error("Invalid converter IFF preimage");changes.push_back(std::move(c));}
        if((seen&12)!=12||p+8!=b.size()||get(8)!=Hash(b,b.size()-8))throw std::runtime_error("Converter journal checksum/layout mismatch");
    }
};
}}

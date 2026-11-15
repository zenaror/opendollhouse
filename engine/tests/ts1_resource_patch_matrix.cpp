/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. https://mozilla.org/MPL/2.0/ */
// Resource-free service fixtures. Build with the complete matching engine and
// headers. The argument is a NEW disposable output directory; no game assets
// are read, and these fixtures make no gameplay or save-continuity claim.
#include "content/ContentManager.h"
#include "content/TS1ExpansionContent.h"
#include "content/TS1JobProvider.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/PIFFRegistry.h"
#include "files/formats/iff/chunks/PIFF.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "files/formats/iff/chunks/STR.h"
#include "files/formats/iff/chunks/SPR2.h"
#include "core/io.h"
#include "files/FilePool.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <sys/stat.h>
namespace fs=std::filesystem;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::Content::Content;
using FSO::Content::TS1::TS1ExpansionContent;
using Bytes=std::vector<uint8_t>;
void require(bool b,const std::string& s){if(!b)throw std::runtime_error(s);}
void bytes(const fs::path& p,const Bytes& b){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());require(bool(f),"write fixture "+p.string());}
Bytes read(const fs::path& p){Bytes b;require(st::ReadAllBytes(p.string(),b),"read fixture");return b;}
void text(const fs::path&p,const std::string&s){bytes(p,Bytes(s.begin(),s.end()));}
IffRawFile raw(){IffRawFile f;f.Header=Bytes(64);std::string h="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";std::copy(h.begin(),h.end(),f.Header.begin());f.HadRsmp=true;f.RsmpIndex=0;return f;}
Bytes strings(const std::vector<std::string>& s){st::Writer w(st::Endian::Little);w.WriteInt16(-3);w.WriteUInt16(s.size());for(auto&x:s){w.WriteByte(1);w.WriteNullTerminatedString(x);w.WriteByte(0);}return w.ToArray();}
Bytes obj(uint32_t guid,bool person=false,bool global=false){OBJD o;o.GUID=guid;o.ObjectType=person?OBJDType::Person:OBJDType::Normal;o.Global=global;o.SubIndex=-1;st::Writer w(st::Endian::Little);o.Write(nullptr,w);return w.ToArray();}
Bytes career(const std::string& n){st::Writer w(st::Endian::Little);w.WriteUInt32(0);w.WriteUInt32(1);w.WriteUInt32(0);w.WriteByte(1);w.WriteNullTerminatedString(n);if(n.size()%2)w.WriteByte(0);w.WriteByte(0);return w.ToArray();}
PIFFEntry replace(const std::string&t,uint16_t id,const Bytes&a,const Bytes&b,uint16_t target=0){PIFFEntry e;e.Type=t;e.ChunkID=id;e.NewChunkID=target?target:id;e.NewDataSize=b.size();e.ChunkLabel="selected";e.ChunkFlags=0x1234;e.Comment="entry comment";PIFFPatch rm;rm.Size=a.size();rm.Mode=PIFFPatchMode::Remove;PIFFPatch add;add.Size=b.size();add.Mode=PIFFPatchMode::Add;add.Data=b;e.Patches={rm,add};return e;}
PIFFEntry remove(const std::string&t,uint16_t id){PIFFEntry e;e.Type=t;e.ChunkID=id;e.EntryType=PIFFEntryType::Remove;return e;}
Bytes piffBytes(const std::string&source,const std::vector<PIFFEntry>& entries,int version){PIFF p;p.SourceIff=source;p.Comment="file comment";p.Entries=entries;st::Writer w(st::Endian::Little);
 if(version==2)p.Write(nullptr,w);else{w.WriteUInt16(version);w.WriteVariableLengthPascalString(source);w.WriteUInt16(entries.size());for(auto&e:entries){w.WriteCString(e.Type,4);w.WriteUInt16(e.ChunkID);w.WriteByte((uint8_t)e.EntryType);if(e.EntryType==PIFFEntryType::Patch){w.WriteVariableLengthPascalString(e.ChunkLabel);w.WriteUInt16(e.ChunkFlags);if(version)w.WriteUInt16(e.NewChunkID);w.WriteUInt32(e.NewDataSize);w.WriteUInt32(e.Patches.size());uint32_t last=0;for(auto&d:e.Patches){w.WriteVarLen(d.Offset-last);last=d.Offset;w.WriteVarLen(d.Size);w.WriteByte((uint8_t)d.Mode);if(d.Mode==PIFFPatchMode::Add)w.WriteBytes(d.Data);}}}}
 return w.ToArray();}
void patch(const fs::path&p,const std::string&source,const std::vector<PIFFEntry>&e,int version=2,const std::vector<IffRawFile::Chunk>&donors={}){auto f=raw();f.Set("PIFF",256,piffBytes(source,e,version));for(auto&d:donors)f.Set(d.Type,d.ID,d.Data,d.Label,d.Flags);bytes(p,f.Write());}
IffRawFile::Chunk donor(const std::string&t,uint16_t id,const Bytes&b){IffRawFile::Chunk c;c.Type=t;c.ID=id;c.Data=b;c.Label="donor";return c;}
void far(const fs::path&p,const std::vector<std::pair<std::string,Bytes>>&files){st::Writer w(st::Endian::Little);w.WriteCString("FAR!byAZ",8);w.WriteUInt32(1);uint32_t table=16;for(auto&f:files)table+=f.second.size();w.WriteUInt32(table);for(auto&f:files)w.WriteBytes(f.second);w.WriteUInt32(files.size());uint32_t off=16;for(auto&f:files){w.WriteUInt32(f.second.size());w.WriteUInt32(f.second.size());w.WriteUInt32(off);w.WriteUInt32(f.first.size());w.WriteBytes(Bytes(f.first.begin(),f.first.end()));off+=f.second.size();}bytes(p,w.ToArray());}
Bytes boundedSprite(uint16_t width,uint16_t height,uint8_t index=1){
 st::Writer body(st::Endian::Little);body.WriteUInt16(width);body.WriteUInt16(height);body.WriteUInt32(3);
 body.WriteUInt16(0);body.WriteUInt16(0);body.WriteUInt16(0);body.WriteUInt16(0);
 body.WriteUInt16(6);body.WriteUInt16(0x2001);body.WriteByte(128);body.WriteByte(index);body.WriteUInt16(0xA000);
 st::Writer sprite(st::Endian::Little);sprite.WriteUInt32(1001);sprite.WriteUInt32(0);sprite.WriteUInt32(1);
 sprite.WriteUInt32(1001);sprite.WriteUInt32(body.ToArray().size());sprite.WriteBytes(body.ToArray());return sprite.ToArray();
}
template<class F>void rejects(F f,const std::string&message){bool rejected=false;try{f();}catch(const std::exception&e){rejected=true;std::cout<<"REJECT "<<message<<" "<<e.what()<<"\n";}require(rejected,message+" was accepted");}
int main(int argc,char**argv){try{require(argc==2||argc==3,"usage: matrix NEW_PRIVATE_DIRECTORY [CASE_FILTER]");fs::path root=fs::absolute(argv[1]);require(!fs::exists(root),"output must be new");fs::create_directories(root);int cases=0;auto test=[&](const std::string&n,const std::function<void()>&f){if(argc==3&&n.find(argv[2])==std::string::npos)return;f();++cases;std::cout<<"PASS "<<n<<"\n";};
 const auto base=strings({"base","10","base description"}), selected=strings({"selected","23","selected description"});
 for(int version:{0,1,2})test("wire-v"+std::to_string(version)+"-patch-remove-add-comments",[&]{auto dir=root/("v"+std::to_string(version));auto e=replace("STR#",0,base,selected);PIFFEntry add;add.Type="STR#";add.ChunkID=7;add.EntryType=PIFFEntryType::Add;patch(dir/"a.piff","CASE.IFF",{e,remove("STR#",2),add},version,{donor("STR#",7,strings({"added"}))});PIFFRegistry::Init(dir.string());auto f=raw();f.Set("STR#",0,base);f.Set("STR#",2,base);IffFile a;a.Read(f.Write());a.SetFilename("folder/case.iff");require(a.Get<STR>(0)->GetString(0)=="selected","delta not consumed");require(!a.Get<STR>(2),"remove");require(a.Get<STR>(7)->GetString(0)=="added","add");auto pp=PIFFRegistry::GetPIFFs("case.iff")->front()->Get<PIFF>(256);require(pp->Version==version,"version");if(version==2)require(pp->Comment=="file comment"&&pp->Entries[0].Comment=="entry comment","comments");a.SetFilename("CASE.IFF");rejects([&]{a.SetFilename("else.iff");},"rebind");});
 test("independent-clones-deferred-move-flags-label",[&]{auto dir=root/"clone";patch(dir/"a.piff","clone.iff",{replace("STR#",1,base,selected,3) },2,{donor("STR#",7,base)});PIFFRegistry::Init(dir.string());auto f=raw();f.Set("STR#",1,base);f.Set("STR#",3,strings({"swap"}));bytes(root/"clone.iff",f.Write());auto a=IffFile::OpenReadOnly((root/"clone.iff").string());auto b=IffFile::OpenReadOnly((root/"clone.iff").string());require(a->Get<STR>(3)->SourceOffset==-1,"source invalidation");require(a->Get<STR>(3)->GetString(0)=="selected"&&a->Get<STR>(1)->GetString(0)=="swap","move swap");require(a->Get<STR>(3)->ChunkFlags==0x1234&&a->Get<STR>(3)->ChunkLabel=="selected","metadata");auto ap=a->Get<STR>(7),bp=b->Get<STR>(7);require(ap!=bp&&ap->ChunkParent==a.get()&&bp->ChunkParent==b.get(),"clone ownership");ap->SetString(0,"mutated");require(bp->GetString(0)=="base","shared clone data");auto d=PIFFRegistry::GetPIFFs("clone.iff")->front();require(d->Get<STR>(7)->GetString(0)=="base","donor mutated");});
 test("late-binding-rejected",[&]{PIFFRegistry::Init("");auto f=raw();f.Set("STR#",0,base);IffFile a;a.Read(f.Write());a.Get<STR>(0);auto dir=root/"late";patch(dir/"a.piff","late.iff",{replace("STR#",0,base,selected)});PIFFRegistry::Init(dir.string());rejects([&]{a.SetFilename("late.iff");},"late bind");});
 test("normal-user-composition-root-relative-otf-reinit",[&]{auto dir=root/"User"/"selected-root";patch(dir/"A-normal.piff","case.iff",{replace("STR#",0,base,strings({"earlier normal"}))});patch(dir/"a-normal.piff","case.iff",{replace("STR#",0,base,strings({"normal"}))});patch(dir/"User/a-user.piff","case.iff",{replace("STR#",0,base,strings({"user"}))});patch(dir/"User/b-user.piff","case.iff",{replace("STR#",0,strings({"user"}),selected)});text(dir/"A/CASE.OTF","<T i=\"1\"><K i=\"2\" v=\"11\"/></T>");text(dir/"z/CASE.OTF","<T i=\"1\"><K i=\"2\" v=\"33\"/></T>");PIFFRegistry::Init(dir.string());auto ps=PIFFRegistry::GetPIFFs("CASE.IFF");require(ps&&ps->size()==2,"User suppression relative root");auto f=raw();f.Set("STR#",0,base);IffFile a;a.Read(f.Write());a.SetFilename("case.iff");require(a.Get<STR>(0)->GetString(0)=="selected","User compose");require(PIFFRegistry::GetOTFRewrite("case.otf").find("/z/CASE.OTF")!=std::string::npos,"OTF normalization/ordering");PIFFRegistry::Init(dir.string());require(PIFFRegistry::GetPIFFs("case.iff")->size()==2,"repeat init");PIFFRegistry::Init("");require(!PIFFRegistry::GetPIFFs("case.iff")&&PIFFRegistry::GetOTFRewrite("case.otf").empty(),"clear selection");});
 test("explicit-root-subdir-stat-malformed-errors-visible",[&]{rejects([&]{PIFFRegistry::Init((root/"missing").string());},"missing directory");auto dir=root/"unreadable";fs::create_directories(dir);chmod(dir.c_str(),0000);try{rejects([&]{PIFFRegistry::Init(dir.string());},"unreadable root");}catch(...){chmod(dir.c_str(),0700);throw;}chmod(dir.c_str(),0700);fs::create_directories(dir/"nested");chmod((dir/"nested").c_str(),0000);try{rejects([&]{PIFFRegistry::Init(dir.string());},"unreadable subdirectory");}catch(...){chmod((dir/"nested").c_str(),0700);throw;}chmod((dir/"nested").c_str(),0700);fs::create_symlink(root/"missing",dir/"broken");rejects([&]{PIFFRegistry::Init(dir.string());},"stat failure");fs::remove(dir/"broken");text(dir/"bad.piff","not an IFF");rejects([&]{PIFFRegistry::Init(dir.string());},"malformed IFF");fs::remove(dir/"bad.piff");auto f=raw();auto b=piffBytes("x.iff",{},2);b[0]=3;f.Set("PIFF",256,b);bytes(dir/"bad.piff",f.Write());rejects([&]{PIFFRegistry::Init(dir.string());},"unsupported version");});
 test("extended-normal-compose-and-unknown-type-mode",[&]{auto dir=root/"normal";patch(dir/"a.piff","case.iff",{replace("STR#",0,base,strings({"first"}))});patch(dir/"z.piff","case.iff",{replace("STR#",0,strings({"first"}),selected)});PIFFRegistry::Init(dir.string());auto f=raw();f.Set("STR#",0,base);IffFile a;a.Read(f.Write());a.SetFilename("case.iff");require(a.Get<STR>(0)->GetString(0)=="selected","normal ordering");PIFFEntry bad;bad.Type="STR#";bad.EntryType=static_cast<PIFFEntryType>(3);patch(root/"badtype/a.piff","case.iff",{bad});rejects([&]{PIFFRegistry::Init((root/"badtype").string());},"unknown entry type");auto mode=replace("STR#",0,base,selected);mode.Patches[0].Mode=static_cast<PIFFPatchMode>(7);patch(root/"badmode/a.piff","case.iff",{mode});rejects([&]{PIFFRegistry::Init((root/"badmode").string());},"unknown mode");});
 test("extended-deferred-SPR2-decode-drop-redecode-no-base-reread",[&]{auto sprite=[](int width){st::Writer w(st::Endian::Little);w.WriteUInt32(1001);w.WriteUInt32(0);w.WriteUInt32(1);w.WriteUInt32(1001);w.WriteUInt32(20);w.WriteUInt16(width);w.WriteUInt16(1);w.WriteUInt32(1);w.WriteUInt16(0);w.WriteUInt16(0);w.WriteUInt16(0);w.WriteUInt16(0);w.WriteUInt16(0x8001);w.WriteUInt16(0xA000);return w.ToArray();};auto before=sprite(1),after=sprite(2);auto f=raw();f.Set("SPR2",1,before);bytes(root/"sprite.iff",f.Write());PIFFRegistry::Init("");auto unpatched=IffFile::OpenReadOnly((root/"sprite.iff").string());auto& uf=*unpatched->Get<SPR2>(1)->Frames[0];uf.DecodeIfRequired();require(uf.Width==1&&uf.PalData.size()==1,"unpatched decode");require(uf.DropDecoded()>0,"unpatched drop");uf.DecodeIfRequired();require(uf.Width==1&&uf.PalData.size()==1,"unpatched redecode");unpatched.reset();patch(root/"sprite-patches/a.piff","sprite.iff",{replace("SPR2",1,before,after)});PIFFRegistry::Init((root/"sprite-patches").string());auto selectedFile=IffFile::OpenReadOnly((root/"sprite.iff").string());auto spr=selectedFile->Get<SPR2>(1);require(spr->SourceOffset<0,"patched source offset");auto& sf=*spr->Frames[0];sf.DecodeIfRequired();require(sf.Width==2&&sf.PalData.size()==2&&sf.SrcOffset<0,"selected decode");sf.DropDecoded();sf.DecodeIfRequired();require(sf.Width==2&&sf.PalData.size()==2,"selected drop/reaccess reread base");require(read(root/"sprite.iff")==f.Write(),"commercial fixture changed");});
 test("bounded-SPR2-patched-memory-oversize-before-pixel-buffers",[&]{
  const auto before=boundedSprite(1,1),oversized=boundedSprite(65535,65535);
  auto f=raw();f.Set("SPR2",1,before);bytes(root/"bounded-memory.iff",f.Write());
  patch(root/"bounded-memory-patches/a.piff","bounded-memory.iff",{replace("SPR2",1,before,oversized)});
  PIFFRegistry::Init((root/"bounded-memory-patches").string());
  auto resource=IffFile::OpenReadOnly((root/"bounded-memory.iff").string());auto sprite=resource->Get<SPR2>(1);
  auto& frame=*sprite->Frames[0];require(sprite->SourceOffset<0&&frame.SrcOffset<0&&frame.Width==0,"patched fixture must be cold in memory");
  const auto io=FSO::Files::FilePool::Stats.BytesRead;
  rejects([&]{frame.DecodeIfRequiredBounded(2048,2*1048576,64);},"oversized patched frame");
  rejects([&]{frame.DecodeIfRequiredBounded(2048,2*1048576,16);},"oversized encoded memory frame");
  require(frame.Width==0&&frame.Height==0&&frame.PixelData.empty()&&frame.PalData.empty()&&frame.AlphaData.empty()&&frame.ZBufferData.empty(),"rejected memory frame allocated decoded buffers");
  require(FSO::Files::FilePool::Stats.BytesRead==io,"memory frame unexpectedly reread disk");
  PIFFRegistry::Init("");
 });
 test("bounded-SPR2-disk-oversize-and-encoded-cap-before-read",[&]{
  PIFFRegistry::Init("");auto f=raw();f.Set("SPR2",1,boundedSprite(65535,65535));bytes(root/"bounded-disk.iff",f.Write());
  auto resource=IffFile::OpenReadOnly((root/"bounded-disk.iff").string());auto& frame=*resource->Get<SPR2>(1)->Frames[0];
  require(frame.SrcOffset>=0&&frame.Width==0,"disk fixture must retain real cold frame offset");
  const auto size=frame.SrcSize;frame.SrcSize=65;
  auto io=FSO::Files::FilePool::Stats.BytesRead;
  rejects([&]{frame.DecodeIfRequiredBounded(2048,2*1048576,64);},"encoded disk frame exceeds cap");
  require(FSO::Files::FilePool::Stats.BytesRead==io,"encoded cap checked after disk read");frame.SrcSize=size;
  rejects([&]{frame.DecodeIfRequiredBounded(2048,2*1048576,64);},"oversized disk frame");
  require(FSO::Files::FilePool::Stats.BytesRead-io==size,"bounded frame must read only its capped encoded extent");
  require(frame.Width==0&&frame.Height==0&&frame.PixelData.empty()&&frame.PalData.empty()&&frame.AlphaData.empty()&&frame.ZBufferData.empty(),"rejected disk frame allocated decoded buffers");
 });
 test("bounded-SPR2-valid-selected-memory-disk-cold-warm-equivalence",[&]{
  PIFFRegistry::Init("");const auto before=boundedSprite(1,1,0),selected=boundedSprite(1,1,1);
  st::Writer palette(st::Endian::Little);palette.WriteUInt32(0);palette.WriteUInt32(2);palette.WriteBytes(Bytes(8));
  for(const auto value:{0,0,0,31,127,223})palette.WriteByte(uint8_t(value));
  auto fixture=[&](const Bytes& sprite){auto f=raw();f.Set("SPR2",1,sprite);f.Set("PALT",0,palette.ToArray());return f;};
  bytes(root/"bounded-valid-disk.iff",fixture(selected).Write());
  auto disk=IffFile::OpenReadOnly((root/"bounded-valid-disk.iff").string());auto& df=*disk->Get<SPR2>(1)->Frames[0];
  require(df.Width==0&&df.SrcOffset>=0,"valid disk must start cold");df.DecodeIfRequiredBounded(1,1,64);
  require(df.Width==1&&df.Height==1&&df.Flags==3&&df.ZBufferData.size()==1&&df.ZBufferData[0]==128&&df.ColorAt(0).R==31&&df.ColorAt(0).A==255,"valid native pixels/depth changed");
  const auto pixels=df.ExpandColors();const auto z=df.ZBufferData;
  auto io=FSO::Files::FilePool::Stats.BytesRead;df.DecodeIfRequiredBounded(1,1,64);
  require(FSO::Files::FilePool::Stats.BytesRead==io&&df.ExpandColors()==pixels&&df.ZBufferData==z,"warm bounded frame changed or reread");
  require(df.DropDecoded()>0,"valid disk drop must be real");df.DecodeIfRequiredBounded(1,1,64);
  require(df.ExpandColors()==pixels&&df.ZBufferData==z,"bounded disk reaccess differs");
  bytes(root/"bounded-valid-memory.iff",fixture(before).Write());
  patch(root/"bounded-valid-patches/a.piff","bounded-valid-memory.iff",{replace("SPR2",1,before,selected)});
  PIFFRegistry::Init((root/"bounded-valid-patches").string());
  auto memory=IffFile::OpenReadOnly((root/"bounded-valid-memory.iff").string());auto sprite=memory->Get<SPR2>(1);auto& mf=*sprite->Frames[0];
  require(sprite->SourceOffset<0&&mf.SrcOffset<0&&mf.Width==0,"selected memory frame must start cold");mf.DecodeIfRequiredBounded(1,1,64);
  require(mf.ExpandColors()==pixels&&mf.ZBufferData==z&&mf.Flags==df.Flags,"patched memory/disk bounded decode differs");
  io=FSO::Files::FilePool::Stats.BytesRead;mf.DecodeIfRequiredBounded(1,1,64);
  require(FSO::Files::FilePool::Stats.BytesRead==io&&mf.ExpandColors()==pixels&&mf.ZBufferData==z,"warm selected frame changed or reread base");
  PIFFRegistry::Init("");
 });
 test("extended-global-OTF-selected-consumer-unavailable-visible",[&]{auto data=root/"globals-data",dir=root/"globals-patches";auto f=raw();f.Set("STR#",0,base);far(data/"GameData/Global/Global.far",{{"global.iff",f.Write()}});patch(dir/"global.piff","global.iff",{replace("STR#",0,base,selected)});text(dir/"global.otf","<T i=\"1\"><K i=\"2\" v=\"33\"/></T>");auto c=Content::Get();c->SimsCompleteDir=data.string();c->BasePath=data.string();PIFFRegistry::Init(dir.string());c->WorldObjectGlobals->Init();auto g=c->WorldObjectGlobals->Get("global",true);require(g&&g->Resource->Iff->Get<STR>(0)->GetString(0)=="selected","global PIFF binding");require(g->Resource->Tuning->GetTable(1)->GetKey(2)->Value==33,"global OTF consumer");c->WorldObjectGlobals->Init();fs::remove(dir/"global.otf");rejects([&]{c->WorldObjectGlobals->Get("global",true);},"missing selected global OTF");});
 auto data=root/"data",cache=root/"cache",patches=root/"provider-patches";fs::create_directories(cache);auto a=raw();a.Set("OBJD",1,obj(100));auto b=raw();b.Set("OBJD",2,obj(100));b.Set("OBJD",4,obj(200,true,true));auto later=raw();later.Set("OBJD",9,obj(500));auto zeta=raw();zeta.Set("OBJD",8,obj(800));auto work=raw();work.Set("CARR",1,career("BaseCareer"));work.Set("STR#",1,strings({"Base career text"}));auto wall=raw();wall.Set("STR#",0,base);far(data/"GameData/Objects/Objects.far",{{"same.iff",a.Write()},{"zeta.iff",zeta.Write()}});far(data/"ExpansionPack/ExpansionPack.far",{{"same.iff",b.Write()},{"later.iff",later.Write()},{"work.iff",work.Write()},{"wall.wll",wall.Write()},{"floor.flr",wall.Write()}});
 patch(patches/"same.piff","same.iff",{remove("OBJD",1)},2,{donor("OBJD",3,obj(300)),donor("OBJD",5,obj(500)),donor("OBJD",6,obj(600))});patch(patches/"zeta.piff","zeta.iff",{},2,{donor("OBJD",6,obj(600))});patch(patches/"work.piff","work.iff",{remove("CARR",1),replace("STR#",1,strings({"Base career text"}),strings({"Selected career text"}))},2,{donor("CARR",2,career("SelectedCareer")),donor("STR#",2,strings({"Added career text"}))});patch(patches/"wall.piff","wall.wll",{replace("STR#",0,base,selected)});patch(patches/"floor.piff","floor.flr",{replace("STR#",0,base,selected)});text(patches/"same.otf","<T i=\"1\"><K i=\"2\" v=\"33\"/></T>");
 auto c=Content::Get();c->SimsCompleteDir=data.string();c->BasePath=data.string();c->CacheDir=cache.string();
 auto setup=[&](bool selectedPatches){PIFFRegistry::Init(selectedPatches?patches.string():"");c->WorldObjects->Init(false);};
 auto snapshot=[&]{std::string s;for(uint32_t id:{100,200,300,500,600,800}){auto i=c->WorldObjects->Entries.find(id);require(i!=c->WorldObjects->Entries.end(),"missing GUID "+std::to_string(id));auto o=c->WorldObjects->Get(id,true);require(o&&o->OBJ->GUID==id,"index/load mismatch");s+=std::to_string(id)+":"+std::to_string(i->second.FarIndex)+":"+i->second.FileName+";";}return s;};std::string cold;Bytes baseCache;
 test("OBJD-cold-all-FAR-duplicates-collisions-fallback-indices",[&]{setup(true);cold=snapshot();auto&p=*c->WorldObjects;require(p.Entries.at(100).FarIndex==1,"removed winner fallback");require(p.Entries.at(200).FarIndex==1&&p.PersonGUIDs.count(200),"later distinct GUID/person");require(p.Entries.at(500).FarIndex==0&&p.Entries.at(500).FileName=="same","earlier addition winner");require(p.Entries.at(600).FarIndex==0&&p.Entries.at(600).FileName=="same","two additions data-order");require(p.Get(uint32_t(300),true)->Resource->Tuning->GetTable(1)->GetKey(2)->Value==33,"object OTF consumer");baseCache=read(cache/"objects.cache");});
 test("OBJD-warm-equivalence-base-cache-not-polluted",[&]{setup(true);require(snapshot()==cold,"cold/warm difference");require(read(cache/"objects.cache")==baseCache,"overlay persisted");});
 test("career-full-map-CARR-STR-add-remove-archive-and-loose",[&]{FSO::Content::TS1::TS1JobProvider jobs(c);jobs.Init();require(!jobs.GetJob(1)&&jobs.GetJob(2)&&jobs.GetJob(2)->Name=="SelectedCareer","career add remove");require(jobs.JobStrings(1)->GetString(0)=="Selected career text"&&jobs.JobStrings(2)->GetString(0)=="Added career text","career strings");bytes(root/"work.iff",work.Write());IffFile loose((root/"work.iff").string());require(!loose.Get<CARR>(1)&&loose.Get<CARR>(2)->Name=="SelectedCareer"&&loose.Get<STR>(2)->GetString(0)=="Added career text","loose/archive mismatch");});
 for(auto ext:{".wll",".flr"})test(std::string("wallfloor-cold-warm-overlay-clear-")+ext,[&]{auto coldList=TS1ExpansionContent::ListArchiveItems(c,ext);require(coldList.size()==1&&coldList[0].Name=="selected"&&coldList[0].Price==23,"cold overlay");auto cacheBytes=read(cache/(std::string("wallfloor")+std::string(ext).substr(1)+".cache"));auto warm=TS1ExpansionContent::ListArchiveItems(c,ext);require(warm[0].Name=="selected","warm overlay");auto iff=TS1ExpansionContent::LoadArchiveIff(c,TS1ExpansionContent::MakeReference(warm[0]));require(iff->Get<STR>(0)->GetString(0)==warm[0].Name,"lazy selected resource");iff.reset();PIFFRegistry::Init("");auto unselected=TS1ExpansionContent::ListArchiveItems(c,ext);require(unselected[0].Name=="base"&&unselected[0].Price==10,"clear restores base");require(read(cache/(std::string("wallfloor")+std::string(ext).substr(1)+".cache"))==cacheBytes,"base cache polluted");PIFFRegistry::Init(patches.string());});
 test("loose-index-final-load-override-new-GUID",[&]{auto loose=raw();loose.Set("OBJD",1,obj(500));bytes(data/"Downloads/loose.iff",loose.Write());patch(patches/"loose.piff","loose.iff",{},2,{donor("OBJD",2,obj(900))});setup(true);require(c->WorldObjects->Get(uint32_t(900),true)&&c->WorldObjects->Get(uint32_t(500),true),"loose selected added GUID");require(c->WorldObjects->Entries.at(500).Source==FSO::Content::GameObjectSource::Standalone,"loose override");});
 test("final-object-OTF-unavailable-visible-and-unrelated-negative",[&]{setup(true);fs::remove(patches/"same.otf");rejects([&]{c->WorldObjects->Get(uint32_t(300),true);},"missing selected object OTF");auto f=raw();f.Set("STR#",0,base);IffFile unrelated;unrelated.Read(f.Write());unrelated.SetFilename("unrelated.iff");require(unrelated.Get<STR>(0)->GetString(0)=="base","unrelated resource patched");});
 PIFFRegistry::Init("");std::cout<<"MATRIX_COMPLETE cases="<<cases<<" fixtures-only\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}

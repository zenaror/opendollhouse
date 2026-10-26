#include "game_audio.hpp"
#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Sink final:odh::PcmSink {
    std::deque<std::int16_t> samples;
    std::uint64_t accepted=0,consumed=0;
    unsigned clears=0;
    bool heard_nonzero=false;
    odh::PcmSinkFormat format() const override{return {8000,2};}
    std::size_t queued_frames() const override{return samples.size()/2;}
    std::size_t queue(const std::int16_t* pcm,std::size_t frames) override{
        const auto count=std::min(frames,std::size_t{2}-queued_frames());
        for(std::size_t n=0;n<count*2;++n){samples.push_back(pcm[n]);heard_nonzero|=pcm[n]!=0;}
        accepted+=count;return count;
    }
    void consume(){consumed+=queued_frames();samples.clear();}
    void pause() override{}
    void start() override{}
    void clear() override{++clears;samples.clear();}
};
void write(const std::filesystem::path& path,const std::string& bytes){
    std::ofstream stream(path,std::ios::binary);stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    stream.close();require(bool(stream),"cannot write private synthetic sound fixture");
}
void put(std::string& bytes,std::uint32_t value,unsigned count){
    for(unsigned n=0;n<count;++n)bytes.push_back(static_cast<char>((value>>(8*n))&255));
}
void xa(const std::filesystem::path& path){
    std::string bytes("XAI\0",4);put(bytes,112,4);put(bytes,1,2);put(bytes,1,2);
    put(bytes,8000,4);put(bytes,16000,4);put(bytes,2,2);put(bytes,16,2);
    for(unsigned n=0;n<2;++n){bytes.push_back(0);bytes+=std::string(14,0x11);}
    write(path,bytes);
}
void group(const std::filesystem::path& root,const std::string& stem){
    // @none needs an explicit symbol; this is metadata, not an invented entry PC.
    write(root/(stem+".hsm"),"none 0\n");
    write(root/(stem+".hot"),"[TrackData]\n0x9=0x10\n0x102=0x15\n[EventMapping]\nfixture=1,7,0,0,0,0\n"
          "[Track]\n7=0,entry,1024,0,0,0,0,@none,9\n[Patch]\n9=0,fixture,\"fixture.xa\",0,0\n");
    std::string hit="HIT!";put(hit,1,4);put(hit,8,4);hit+="TRAX";
    // Original TS1-style Jump13 carries a literal DWORD branch ID 258.
    // Its low byte 2 must not be interpreted as caller argument register R2.
    hit.push_back(13);put(hit,258,4);
    // The pinned instance-group operation is legal before the first note.
    for(const unsigned char opcode:{69,0,2,5,11,12})hit.push_back(static_cast<char>(opcode));
    write(root/(stem+".hit"),hit);
}
std::string report(const odh_host::GameAudio& audio){std::ostringstream out;audio.emit_json(out);return out.str();}
std::uint64_t number(const std::string& json,const std::string& section,const std::string& key){
    auto position=json.find("\""+section+"\":{");
    require(position!=json.npos,"missing diagnostic section");
    position=json.find("\""+key+"\":",position);
    require(position!=json.npos,"missing diagnostic counter");
    position+=key.size()+3;return std::stoull(json.substr(position));
}
odh::SoundRequest request(const odh::World& world,std::int16_t id=1){
    odh::SoundRequest result{};result.name="fixture";result.source=id;result.pan=true;result.zoom=true;
    result.source_variables=odh::sound_source_variables(*world.find(id));return result;
}
void empty_resources(const odh_host::GameAudio& audio){
    const auto json=report(audio);
    const bool empty=number(json,"threads","active")==0&&number(json,"pcm","handles")==0&&
                     number(json,"pcm","bytes")==0&&audio.output_empty();
    if(!empty)throw std::runtime_error("finished/cancelled game audio retains resources or output: "+json);
    require(empty,"finished/cancelled game audio must release every resource and queued frame");
}
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("requires private fixture directory");
    const auto root=std::filesystem::path(argv[1]);
    const auto positive=root/"positive";std::filesystem::create_directory(positive);
    group(positive,"sound");xa(positive/"fixture.xa");
    write(positive/"unrelated.txt","deliberately invalid metadata, ignored by extension\n");
    odh::World world;require(world.create(1,odh::EntityKind::avatar,{0,66,0,0})&&
        world.create(2,odh::EntityKind::object,{1,0,0,0}),"create explicit sound owners");
    require(world.set_person_data(1,58,21)&&world.set_person_data(1,65,1)&&world.set_position(1,{16,32,1,0}),
        "initialize simulation state independently of audio");
    const auto before=world.snapshot();
    {
        Sink sink;odh_host::GameAudio audio(world,positive,sink);auto sound=request(world);
        require(audio.submit(sound)&&sink.samples.empty(),"discovered event must admit without executing notes during submit");
        audio.simulation_tick();
        require(sink.queued_frames()==2&&sink.heard_nonzero&&!audio.output_empty(),
                "first audio tick must decode XA and queue bounded nonzero PCM");
        const auto started=report(audio);
        require(number(started,"threads","active")==1&&number(started,"pcm","handles")==1,
                "active HIT owns the queued PCM note");
        const auto ticks=number(started,"threads","ticks");
        for(unsigned n=0;n<3;++n)audio.pump();
        const auto blocked=report(audio);
        require(number(blocked,"threads","ticks")==ticks&&number(blocked,"threads","completed")==0&&
                sink.queued_frames()==2,"PCM pumping cannot advance HIT or acknowledge unconsumed output");
        // The default transport renders a whole block, including the silent
        // tail after this short XA. The two-frame sink must explicitly consume
        // that entire rendered block, not just the 56 source frames.
        const auto drain_polls=(number(started,"pcm","rendered")+1)/2+8;
        for(std::uint64_t n=0;n<drain_polls&&(number(report(audio),"threads","active")||!audio.output_empty());++n){
            sink.consume();audio.simulation_tick();
        }
        empty_resources(audio);
        require(sink.consumed>=56&&number(report(audio),"threads","completed")==1&&
                number(report(audio),"threads","faulted")==0,
                "consumed synthetic XA must complete its HIT thread and release PCM");
        auto missing=sound;missing.name="absent_event";
        require(!audio.submit(missing),"unknown discovered event must reject without audio");
        for(const auto& name:std::vector<std::string>{{},std::string("fixture\0tail",12),std::string(257,'x')}){
            auto malformed=sound;malformed.name=name;require(!audio.submit(malformed),"malformed event must reject nonfatally");
        }
        auto loop=sound;loop.loop=true;require(!audio.submit(loop),"unsupported loop must reject nonfatally");
        empty_resources(audio);
        require(audio.submit(sound),"service remains usable after rejected requests");audio.simulation_tick();
        const auto generation=sound.source_variables->generation;
        audio.stop_owner(1,generation+1);
        require(number(report(audio),"threads","active")==1&&!sink.samples.empty(),
                "wrong generation must not stop the current owner's sound");
        audio.stop_owner(1,generation);empty_resources(audio);
        require(sink.samples.empty()&&number(report(audio),"threads","stopped")==1,
                "exact owner generation must cancel queued samples and release its note");
        require(audio.submit(sound)&&audio.submit(request(world,2)),"admit independent owners before clear");
        audio.simulation_tick();require(!sink.samples.empty(),"multiple owner output must reach fake sink");
        audio.clear();empty_resources(audio);audio.clear();empty_resources(audio);
        require(number(report(audio),"threads","clears")==2,"clear remains safe when already empty");
    }
    const auto after=world.snapshot();
    require(after.generation_sequence==before.generation_sequence&&after.spatial_revision==before.spatial_revision&&
            after.word_count==before.word_count&&after.entities.size()==before.entities.size(),
            "audio discovery, execution and cancellation must not advance simulation identity or spatial state");
    for(std::size_t n=0;n<before.entities.size();++n){
        const auto& a=before.entities[n];const auto& b=after.entities[n];
        require(a.generation==b.generation&&a.person_data==b.person_data&&a.attributes==b.attributes&&
                a.object_words==b.object_words&&a.position==b.position&&a.slots==b.slots&&a.motives==b.motives,
                "game audio must leave simulation banks and positions unchanged");
    }
    // GameAudio receives World and PcmSink only. World contains no gameplay RNG;
    // no audio chooser is allowed to require or mutate a VM/random-state provider.
    const auto ambiguous=root/"ambiguous";std::filesystem::create_directory(ambiguous);
    group(ambiguous,"first");group(ambiguous,"second");xa(ambiguous/"fixture.xa");
    {
        Sink sink;odh_host::GameAudio audio(world,ambiguous,sink);
        require(!audio.submit(request(world)),"duplicate event across complete groups must reject ambiguity");
        audio.simulation_tick();empty_resources(audio);
        require(sink.accepted==0&&report(audio).find("ambiguous across HOT groups")!=std::string::npos,
                "ambiguous discovery must explain rejection without choosing or playing a resource");
    }
    const auto linked=root/"linked";std::filesystem::create_directory(linked);
    for(const auto* name:{"sound.hsm","sound.hot","sound.hit","fixture.xa"})
        std::filesystem::create_symlink(positive/name,linked/name);
    {
        Sink sink;odh_host::GameAudio audio(world,linked,sink);
        require(!audio.submit(request(world)),"file symlinks must not import a sound group outside selected tree");
        audio.simulation_tick();empty_resources(audio);require(sink.accepted==0,"excluded symlinks must produce no audio");
    }
    std::cout<<checks<<" host discovery->HIT->XA->bounded fake PCM lifecycle checks passed (synthetic; no SDL/hardware)\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"GameAudio test failed: "<<error.what()<<'\n';return 1;}}

#include "odh/headline.hpp"
#include "odh/headline_snapshot_codec.hpp"
#include "odh/formats.hpp"
#include <iostream>
#include <limits>

namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F> bool rejects(F f){try{f();}catch(const odh::FormatError&){return true;}return false;}
odh::Instruction instruction(std::int16_t duration=2,std::uint8_t index=3,std::uint8_t group=5,
    std::uint16_t flags2=0,std::uint8_t flags=0) {
    const auto d=static_cast<std::uint16_t>(duration);
    return {41,254,255,{static_cast<std::uint8_t>(flags2),static_cast<std::uint8_t>(flags2>>8),index,group,
        static_cast<std::uint8_t>(d),static_cast<std::uint8_t>(d>>8),255,flags}};
}
}
int main(){try{
    odh::World world;
    check(world.create(1,odh::EntityKind::avatar,{})&&world.create(2,odh::EntityKind::object,{})&&
        world.create(3,odh::EntityKind::object,{}),"world fixture");
    odh::BalloonHeadlines headlines(world);
    auto handler=headlines.primitive();
    odh::VmFrame frame{odh::routine_key(4096),0,{1,1,2},{},{3}};
    odh::Ts1Temps temps{};
    const auto run=[&](const odh::Instruction& i){return handler(frame,i,temps,{nullptr,1}).disposition;};
    check(run(instruction())==odh::PrimitiveDisposition::goto_true,"generic caller accepted");
    auto state=headlines.snapshot();
    check(state.size()==1&&state[0].target==1&&state[0].target_generation==world.find(1)->generation&&
        state[0].duration==2&&state[0].anim==0&&state[0].index==3,"owned initial state");
    check(headlines.tick(2)&&headlines.snapshot()[0].duration==2,"unrelated tick cannot expire caller");
    check(headlines.tick(1)&&headlines.snapshot()[0].duration==1&&headlines.snapshot()[0].anim==1,
        "one entity Tick advances one time unit");
    const auto wire=odh::encode_balloon_headline_snapshot(headlines.snapshot());
    odh::BalloonHeadlines restored(world);
    check(restored.restore_snapshot(odh::decode_balloon_headline_snapshot(wire))&&
        odh::encode_balloon_headline_snapshot(restored.snapshot())==wire,"exact mid-duration roundtrip");
    check(headlines.tick(1)&&restored.tick(1)&&headlines.snapshot().empty()&&restored.snapshot().empty(),
        "original and restored expire at same next entity Tick");
    check(run(instruction(-1,4,7,1))==odh::PrimitiveDisposition::goto_true,"object StackObject target supported");
    state=headlines.snapshot();
    check(state[0].target==2&&state[0].icon_target==3&&state[0].icon_generation==world.find(3)->generation,
        "algorithmic index>=2 resolves Local selected by flags2");
    check(run(instruction(2,1,7,1))==odh::PrimitiveDisposition::goto_true&&
        headlines.snapshot()[0].icon_target==2,"algorithmic index<2 uses StackObject");
    check(run(instruction(2,3,7,3))==odh::PrimitiveDisposition::unsupported&&headlines.snapshot()[0].index==1,
        "bad local fails before replacing prior headline");
    frame.locals[0]=99;
    check(run(instruction(2,3,7,1))==odh::PrimitiveDisposition::goto_true&&
        headlines.snapshot()[0].icon_target==0,"unresolved icon remains valid null source result");
    temps[0]=32767;
    check(run(instruction(2,2,5,1,16))==odh::PrimitiveDisposition::goto_true&&
        headlines.snapshot()[0].index==1,"TEMP indexed arithmetic wraps signed8");
    check(run(instruction(2,128,5,1))==odh::PrimitiveDisposition::goto_true&&
        headlines.snapshot()[0].index==-128,"raw index is signed8");
    check(run(instruction(2,255,5,1))==odh::PrimitiveDisposition::goto_true&&headlines.snapshot().empty(),
        "indexminus1 clears target");
    check(run(instruction(2,3,255,1))==odh::PrimitiveDisposition::goto_true&&
        run(instruction(0,3,255,1))==odh::PrimitiveDisposition::goto_true&&headlines.snapshot().empty(),
        "null provider does not invent Money renderer protection");
    check(run(instruction(2,3,5,1,8))==odh::PrimitiveDisposition::goto_true&&
        headlines.snapshot()[0].duration==30,"loop durations multiply fifteen");
    check(run(instruction(-1,3,5,1,8))==odh::PrimitiveDisposition::goto_true&&
        headlines.snapshot()[0].duration==-1&&headlines.tick(2)&&headlines.snapshot()[0].duration==-1,
        "minus1 loops remains indefinitely displayed");
    state=headlines.snapshot();state[0].anim=std::numeric_limits<std::int32_t>::max();
    check(headlines.restore_snapshot(state)&&headlines.tick(2)&&
        headlines.snapshot()[0].anim==std::numeric_limits<std::int32_t>::min(),"Anim wraps signed32 without UB");
    const auto good=headlines.snapshot();auto bad=good;bad[0].target_generation++;
    check(!headlines.restore_snapshot(bad)&&headlines.snapshot()[0].anim==good[0].anim,
        "generation mismatch restore is atomic");
    bad=good;bad.push_back(good[0]);
    check(!headlines.restore_snapshot(bad)&&rejects([&]{odh::encode_balloon_headline_snapshot(bad);}),
        "duplicate states refused");
    bad=good;bad[0].duration=0;
    check(!headlines.restore_snapshot(bad),"expired state cannot restore");
    bad=good;bad[0].duration=-2;
    check(!headlines.restore_snapshot(bad),"negative duration must match operand semantics");
    bad=good;bad[0].icon_target=3;bad[0].icon_generation=world.find(3)->generation;
    check(!headlines.restore_snapshot(bad),"nonalgorithmic state cannot acquire fabricated icon");
    bad=good;bad[0].operand[3]=7;bad[0].icon_target=3;bad[0].icon_generation=world.find(3)->generation+1;
    check(headlines.restore_snapshot(bad)&&headlines.snapshot()[0].icon_generation==bad[0].icon_generation,
        "weak historical icon generation stays exact without rebinding a live replacement");
    check(headlines.restore_snapshot(good),"restore original headline before unrelated primitive checks");
    auto other=instruction();other.opcode=40;
    check(run(other)==odh::PrimitiveDisposition::unsupported&&headlines.snapshot()[0].anim==good[0].anim,
        "unrelated opcode has no headline effects");
    auto bytes=odh::encode_balloon_headline_snapshot(good);
    for(std::size_t n=0;n<bytes.size();++n){auto cut=bytes;cut.resize(n);
        check(rejects([&]{odh::decode_balloon_headline_snapshot(cut);}),"truncated wire refused");}
    auto changed=bytes;changed.push_back(0);
    check(rejects([&]{odh::decode_balloon_headline_snapshot(changed);}),"trailing wire refused");
    changed=bytes;changed[0]=2;check(rejects([&]{odh::decode_balloon_headline_snapshot(changed);}),"unknown version refused");
    changed=bytes;changed[4]=1;changed[5]=16;
    check(rejects([&]{odh::decode_balloon_headline_snapshot(changed);}),"count budget refused");
    frame.context.stack_object=99;
    check(run(instruction(0,3,5,1))==odh::PrimitiveDisposition::unsupported&&headlines.snapshot().size()==1,
        "missing target fails even for clear without touching existing state");
    check(world.remove(2)&&world.create(2,odh::EntityKind::object,{})&&!headlines.tick(2),
        "stale target cannot advance replacement entity");
    check(headlines.remove(2)&&headlines.snapshot().empty(),"host removal releases own state");
    check(restored.restore_snapshot({})&&odh::decode_balloon_headline_snapshot(
        odh::encode_balloon_headline_snapshot({})).empty(),"empty snapshot codec and replacement");
    std::cout<<checks<<" headline checks passed (synthetic fixtures)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

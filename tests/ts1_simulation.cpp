#include "odh/ts1_simulation.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
int checks=0;
void require(bool b,const char* msg) {++checks;if(!b) throw std::runtime_error(msg);}
odh::Ts1MotiveInputs input() {odh::Ts1MotiveInputs in;in.active_personality=666;in.outgoing_personality=500;return in;}
constexpr std::size_t energy=static_cast<std::size_t>(odh::Ts1Need::energy);
}
int main() {
try {
    // Simitone FreeSO f86bbd34 VMClock.cs:29-34,44-67; VMContext.cs:81-84.
    odh::Ts1Clock c;require(c.initialize(23,30,59,12,1997),"initialize reference calendar");
    for(int n=0;n<29;++n) require(c.tick(true),"partial minute tick");
    require(c.seconds()==58 && c.snapshot().minute==59,"minute fraction seconds");
    require(!c.tick(false) && c.snapshot().minute_fraction==29 && c.snapshot().ticks==29,"pause does not advance");
    require(c.tick(true) && c.snapshot().hour==0 && c.snapshot().minute==0 && c.snapshot().day==1 &&
            c.snapshot().month==1 && c.snapshot().year==1998,"minute/hour/day/month/year carry");
    require(c.initialize(12,2,4,6,2000),"initialize ordinary minute");
    for(int n=0;n<30;++n) c.tick(true);
    require(c.snapshot().minute==5 && c.snapshot().hour==12 && c.snapshot().day==2,"ordinary minute carry");
    require(!c.initialize(24,1,0,1,2000) && c.snapshot().minute==5,"invalid initialize atomic");
    auto cs=c.snapshot();cs.minute_fraction=31;
    require(!c.restore(cs) && c.snapshot().minute==5,"invalid fraction restore atomic");
    cs=c.snapshot();cs.ticks=std::numeric_limits<std::uint64_t>::max();require(c.restore(cs),"max ticks restore");
    require(!c.tick(true) && c.snapshot().minute_fraction==0,"tick overflow preserves state");
    c.initialize(23,30,59,12,std::numeric_limits<std::int32_t>::max());
    cs=c.snapshot();cs.minute_fraction=29;c.restore(cs);
    require(!c.tick(true) && c.snapshot().day==30 && c.snapshot().minute_fraction==29,"year overflow preserves state");
    odh::Ts1Clock resumed;require(resumed.restore(c.snapshot()),"clock snapshot restore");
    require(!resumed.tick(true) && resumed.snapshot().ticks==c.snapshot().ticks,"clock restored exhaustion deterministic");
    c.initialize(3,4,5,6,2000);for(int n=0;n<17;++n)c.tick(true);resumed.restore(c.snapshot());
    for(int n=0;n<120;++n){c.tick(true);resumed.tick(true);}
    require(c.snapshot().ticks==resumed.snapshot().ticks && c.snapshot().minute_fraction==resumed.snapshot().minute_fraction &&
            c.snapshot().minute==resumed.snapshot().minute,"clock continued snapshot deterministic");
    // VMTS1Activator.cs:88-93 and VMTS1ActivatorNew.cs:451-460, pinned f86bbd34.
    // House07 global6=22 becomes fraction660; first Tick advances one minute.
    require(c.initialize_simi(7,17,0,22,6,1997) && c.snapshot().minute_fraction==660 && c.seconds()==1320,"Simitone SIMI bootstrap carry quirk");
    require(c.tick(true) && c.snapshot().minute==1 && c.snapshot().minute_fraction==0,"first SIMI tick consumes one overflow only");
    require(!c.initialize_simi(7,17,0,60,6,1997),"malformed SIMI seconds rejected");
    require(c.global(0)==7 && c.global(1)==17 && c.global(4)==0 && c.global(5)==1 && c.global(6)==0 && c.global(7)==6 && c.global(8)==1997 && !c.global(9),"clock global mapping");
    require(c.initialize_simi(23,16,59,59,6,1997)&&c.seconds()==3540&&c.daylight_seconds_of_day()==58*60,
        "late bootstrap carries into the next daylight cycle without changing VM seconds");
    require(c.snapshot().hour==23&&c.snapshot().minute==59&&c.snapshot().day==16&&c.snapshot().ticks==0,
        "sampling daylight preserves the unadvanced simulation date and time");
    // VMTS1MotiveDecay.cs:29-38 order; Model/VMMotive.cs:9-20 bank indexes.
    const unsigned bank[]={7,6,8,9,5,15,14};
    for(unsigned i=0;i<7;++i)require(odh::ts1_motive_index(static_cast<odh::Ts1Need>(i))==bank[i],"explicit motive mapping");
    // VMTS1MotiveDecay.cs:47-58 gate/hidden/active changes;132-139 integer conversion.
    odh::Ts1MotiveDecay d;auto in=input();
    require(d.update(12,0,in).status==odh::Ts1MotiveStatus::unchanged,"initial zero bucket gate");
    require(d.update(12,1,in).status==odh::Ts1MotiveStatus::unchanged,"one-minute gate");
    auto r=d.update(12,2,in);
    require(r.status==odh::Ts1MotiveStatus::updated && d.snapshot().fractions==std::array<std::int16_t,7>{200,500,170,360,375,250,55},"default fractional rates");
    require(d.update(12,3,in).status==odh::Ts1MotiveStatus::unchanged,"same bucket no double decay");
    r=d.update(12,4,in);require(r.needs[1]==-1 && d.snapshot().fractions[1]==0,"fraction whole decrement");
    in.hidden=true;require(d.update(12,6,in).status==odh::Ts1MotiveStatus::unchanged && d.snapshot().last_two_minute_bucket==2,"hidden preserves gate");
    in.hidden=false;require(d.update(12,6,in).status==odh::Ts1MotiveStatus::updated,"unhidden same minute applies");
    odh::Ts1MotiveDecay active;in=input();in.needs.fill(80);in.active_changes.fill(true);
    r=active.update(12,2,in);
    require(active.snapshot().fractions==std::array<std::int16_t,7>{0,0,0,0,375,0,0} && r.mood==10,"active changes skip all except energy including mood sum");
    // VMTS1MotiveDecay.cs:60 reads Hunger during every iteration, including after Hunger's update.
    odh::Ts1MotiveDecay order;auto ds=order.snapshot();ds.fractions[0]=900;order.restore(ds);in=input();in.needs[0]=100;
    r=order.update(12,2,in);require(r.needs[0]==99 && order.snapshot().fractions[3]==419,"bladder uses updated hunger");
    // VMTS1MotiveDecay.cs:65-77,105-107: exact 666 split; social multiplier truncates to zero.
    for(auto personality:{665,666,667}) {
        odh::Ts1MotiveDecay x;in=input();in.active_personality=static_cast<std::int16_t>(personality);in.outgoing_personality=1000;
        x.update(12,2,in);require(x.snapshot().fractions[1]==(personality<666?600:personality>666?400:500) && x.snapshot().fractions[6]==55,"personality reference fixed-point boundaries");
    }
    // VMTS1MotiveDecay.cs:79-104: sleeping flag is captured before wake-hour mutation.
    odh::Ts1MotiveDecay wake;in=input();in.sleep_state=-1;in.needs[energy]=80;
    r=wake.update(7,2,in);require(r.sleep_state==0 && r.needs[energy]==80 && wake.snapshot().fractions[energy]==10 && wake.snapshot().fractions[5]==0,"wake at seven retains sleeping rates this update");
    in.sleep_state=r.sleep_state;r=wake.update(7,4,in);require(wake.snapshot().fractions[energy]==385 && wake.snapshot().fractions[5]==250,"following update awake rates");
    odh::Ts1MotiveDecay sleep;in=input();in.sleep_state=-1;
    r=sleep.update(6,2,in);require(r.needs[energy]==2 && sleep.snapshot().fractions[energy]==714 && sleep.snapshot().fractions[2]==80,"sleep energy negative fraction round-up");
    in.needs=r.needs;r=sleep.update(6,4,in);require(r.needs[energy]==3 && sleep.snapshot().fractions[energy]==428,"sleep fractional accumulation");
    odh::Ts1MotiveDecay exact;ds=exact.snapshot();ds.fractions[energy]=286;exact.restore(ds);in=input();in.sleep_state=-1;
    r=exact.update(6,2,in);require(r.needs[energy]==2 && exact.snapshot().fractions[energy]==1000,"exact negative thousand retains reference 1000 carry");
    odh::Ts1MotiveDecay exact_copy;require(exact_copy.restore(exact.snapshot()),"reference 1000 carry is saveable");
    odh::Ts1MotiveDecay nap;in=input();in.sleep_state=1;nap.update(6,2,in);require(nap.snapshot().fractions[energy]==0,"pre-seven non-bed sleep no drift");
    nap.update(8,4,in);require(nap.snapshot().fractions[energy]==10,"daytime non-bed sleep drift");
    // VMTS1MotiveDecay.cs:111-129 + VMAvatar.cs:970-976 default setter limits.
    odh::Ts1MotiveDecay low;in=input();in.needs.fill(-100);for(unsigned m=2;m<=20;m+=2){r=low.update(12,m,in);in.needs=r.needs;}
    require(r.needs==std::array<std::int16_t,7>{-100,-100,-100,-100,-100,-100,-100},"lower needs clamp");
    odh::Ts1MotiveDecay high;in=input();in.sleep_state=-1;in.needs[energy]=100;r=high.update(6,2,in);require(r.needs[energy]==100 && r.mood==12,"upper setter clamps while reference mood uses local intermediate");
    odh::Ts1MotiveDecay copy;require(copy.restore(sleep.snapshot()),"motive snapshot restore");
    in=input();in.sleep_state=-1;in.needs.fill(0);
    for(unsigned m=6;m<60;m+=2){auto a=sleep.update(6,m,in);auto b=copy.update(6,m,in);require(a.needs==b.needs && a.mood==b.mood && sleep.snapshot().fractions==copy.snapshot().fractions,"continued motive snapshot deterministic");in.needs=a.needs;}
    require(copy.update(7,0,in).status==odh::Ts1MotiveStatus::updated,"hour wrap bucket runs");
    ds=copy.snapshot();ds.fractions[0]=1001;require(copy.restore(ds),"signed source carry may exceed the normalized post-update interval");ds=copy.snapshot();ds.last_two_minute_bucket=30;require(!copy.restore(ds),"invalid gate rejected");
    const auto previous=copy.snapshot();in.active_personality.reset();require(copy.update(7,2,in).status==odh::Ts1MotiveStatus::unsupported && copy.snapshot().fractions==previous.fractions,"missing personality unsupported atomic");
    in=input();in.needs[0]=101;require(copy.update(7,2,in).status==odh::Ts1MotiveStatus::updated,"existing signed overfill is valid source input");
    // SleepState is a signed motive word; only exact -1 has the bed Energy rate.
    for(const auto state:{-32768,-100,-40,-2,-1,0,1,2,100,32767})for(const unsigned hour:{6U,8U}){
        odh::Ts1MotiveDecay actual;auto values=input();values.sleep_state=static_cast<std::int16_t>(state);
        const auto result=actual.update(hour,2,values);const bool sleeping=state!=0;
        require(result.status==odh::Ts1MotiveStatus::updated&&result.sleep_state==state&&!result.sleep_state_written&&
            actual.snapshot().fractions[2]==(sleeping?80:170)&&actual.snapshot().fractions[3]==(sleeping?210:360)&&
            actual.snapshot().fractions[5]==(sleeping?0:250),"every signed nonzero SleepState preserves source sleeping rates");
        require(actual.snapshot().fractions[energy]==(state==-1?714:sleeping?(hour>=7?10:0):375)&&
            result.needs[energy]==(state==-1?2:0)&&result.needs_written[energy]==(state==-1)&&result.mood_written,
            "exact minus-one Energy is separate from all other signed sleeping states");
        odh::Ts1MotiveDecay waking;values.needs[energy]=80;
        const auto woke=waking.update(7,2,values);
        require(woke.sleep_state==(sleeping?0:state)&&woke.sleep_state_written==sleeping&&
            waking.snapshot().fractions[energy]==(sleeping?10:375)&&waking.snapshot().fractions[5]==(sleeping?0:250),
            "wake hour writes zero but retains the captured sleeping branch for this entire update");
    }
    {
        odh::Ts1MotiveDecay actual;auto values=input();values.active_personality.reset();values.outgoing_personality.reset();
        values.hidden=true;const auto before=actual.snapshot();const auto hidden=actual.update(6,2,values);
        require(hidden.status==odh::Ts1MotiveStatus::unchanged&&!hidden.mood_written&&
            actual.snapshot().fractions==before.fractions&&actual.snapshot().last_two_minute_bucket==before.last_two_minute_bucket,
            "Hidden gates before any optional personality access or decay mutation");
        values.hidden=false;require(actual.update(6,0,values).status==odh::Ts1MotiveStatus::unchanged,
            "same-minute gate does not demand unused personality inputs");
        values.active_changes.fill(true);values.needs[energy]=80;
        const auto changed=actual.update(6,2,values);
        require(changed.status==odh::Ts1MotiveStatus::updated&&changed.mood==10&&actual.snapshot().fractions[energy]==375&&
            !changed.needs_written[energy]&&changed.mood_written,
            "active changes skip personality access and mood contributions, while Energy still advances");
        values.active_changes[static_cast<std::size_t>(odh::Ts1Need::comfort)]=false;
        const auto previous=actual.snapshot();
        require(actual.update(6,4,values).status==odh::Ts1MotiveStatus::unsupported&&
            actual.snapshot().last_two_minute_bucket==previous.last_two_minute_bucket&&actual.snapshot().fractions==previous.fractions,
            "a genuinely needed missing personality capability rejects atomically");
    }
    for(const auto personality:{-32768,32767}){
        odh::Ts1MotiveDecay actual;auto values=input();values.active_personality=static_cast<std::int16_t>(personality);
        values.outgoing_personality=static_cast<std::int16_t>(personality);
        require(actual.update(6,2,values).status==odh::Ts1MotiveStatus::updated&&
            actual.snapshot().fractions[1]==(personality<666?600:400)&&actual.snapshot().fractions[6]==55,
            "signed personality extremes are source comparison operands, not a restricted input profile");
    }
    {
        odh::Ts1MotiveDecay actual;auto values=input();values.sleep_state=-40;values.needs[energy]=-200;
        const auto result=actual.update(6,2,values);
        require(result.status==odh::Ts1MotiveStatus::updated&&result.needs[energy]==-200&&!result.needs_written[energy]&&result.mood==-25,
            "unchanged signed need is not passed through a setter or normalized prematurely");
        odh::Ts1MotiveDecay overfill;values=input();values.sleep_state=-1;values.needs[energy]=200;
        const auto filled=overfill.update(6,2,values);
        require(filled.needs[energy]==200&&filled.needs_written[energy]&&filled.mood==25,
            "actual Energy setter preserves old overfill limit while Mood uses the local202 intermediate");
        odh::Ts1MotiveDecay wrap;values.needs[energy]=32767;
        const auto wrapped=wrap.update(6,2,values);
        require(wrapped.needs[energy]==-100&&wrapped.needs_written[energy]&&wrapped.mood==-100,
            "source local short wraps before the real setter and Mood calculation");
    }
    for(const auto carry:{-32768,32767}){
        odh::Ts1MotiveDecay actual;auto saved=actual.snapshot();saved.fractions[energy]=static_cast<std::int16_t>(carry);
        require(actual.restore(saved),"full signed fraction word restores without repair");auto values=input();
        const auto result=actual.update(6,2,values);
        require(result.needs[energy]==33&&result.needs_written[energy]&&actual.snapshot().fractions[energy]==(carry<0?607:606),
            "fraction addition and negative carry normalization use source signed-short wrap");
        odh::Ts1MotiveDecay reopened;require(reopened.restore(actual.snapshot()),"normalized signed fraction snapshot reopens");
        values.needs=result.needs;values.mood=result.mood;
        const auto continued=actual.update(6,4,values),fresh=reopened.update(6,4,values);
        require(continued.needs==fresh.needs&&continued.mood==fresh.mood&&continued.needs_written==fresh.needs_written&&
            actual.snapshot().fractions==reopened.snapshot().fractions,"signed-fraction resumed execution preserves values and actual setter decisions");
    }
    {
        odh::Ts1MotiveDecay actual;auto values=input();values.needs[0]=32767;values.mood=5000;
        const auto result=actual.update(6,2,values);
        require(result.needs[0]==32767&&!result.needs_written[0]&&actual.snapshot().fractions[0]==198&&
            result.needs[3]==-20&&result.needs_written[3]&&actual.snapshot().fractions[3]==20&&result.mood==4093,
            "extreme Hunger rate casts before carry, affects later Bladder and respects existing Mood overfill");
    }
    std::cout<<checks<<" TS1 simulation checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

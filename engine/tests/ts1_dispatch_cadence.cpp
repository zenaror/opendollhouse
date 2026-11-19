// SPDX-License-Identifier: MPL-2.0
#include "common/TS1/TS1DispatchCadence.h"
#include <iostream>
#include <stdexcept>
using FSO::Common::TS1::DispatchCadence;
static void Need(bool v, const char* m) { if (!v) throw std::runtime_error(m); }
static unsigned Drain(DispatchCadence& clock) { unsigned n=0; while(clock.TakePass()) ++n; return n; }
int main() {
    try {
        Need(DispatchCadence::PeriodForRaw(-100)==172 && DispatchCadence::PeriodForRaw(0)==172,"signed low clamp/raw zero");
        Need(DispatchCadence::PeriodForRaw(760)==42,"normal GUI source period");
        Need(DispatchCadence::PeriodForRaw(924)==14,"fast GUI source period");
        Need(DispatchCadence::PeriodForRaw(994)==2,"ultra GUI source period");
        Need(DispatchCadence::PeriodForRaw(1000)==1 && DispatchCadence::PeriodForRaw(100000)==1,"high clamp");
        uint32_t previous=172;
        for(int raw=0;raw<=1000;++raw) {
            auto period=DispatchCadence::PeriodForRaw(raw);
            Need(period>=1 && period<=previous,"raw speed period not monotonic"); previous=period;
        }
        DispatchCadence clock;
        clock.BeginFrame(0); Need(Drain(clock)==1,"first owner opportunity");
        clock.BeginFrame(0); Need(Drain(clock)==0,"zero-time frame duplicated initial opportunity");
        clock.BeginFrame(41.5); Need(Drain(clock)==0,"early pass");
        clock.BeginFrame(.5); Need(Drain(clock)==1,"fractional frame time lost");
        clock.BeginFrame(84); Need(Drain(clock)==2,"ordinary catchup missing");
        clock.BeginFrame(420); Need(clock.TakePass(),"budget frame did not start");
        Need(clock.Backlog()==378 && clock.TokenBalance()==378,"budget interruption lost retained work");
        clock.BeginFrame(0); Need(Drain(clock)==9,"budget interruption could not resume");
        clock.BeginFrame(21); Need(Drain(clock)==0,"half period advanced");
        const auto balance=clock.TokenBalance();
        clock.ResetOwner(); clock.BeginFrame(0);
        Need(clock.Backlog()==42 && clock.TokenBalance()==balance,"clear pause reset token credit");
        Need(Drain(clock)==0,"unpause minted a token");
        clock.BeginFrame(21); Need(Drain(clock)==1,"retained token epoch lost");
        // Raw-rate publication resets owner timing, but not token credit/epoch.
        clock.SetRawSpeed(994); clock.BeginFrame(0);
        Need(clock.Period()==2 && Drain(clock)==0,"speed publication minted a token");
        clock.BeginFrame(2); Need(Drain(clock)==1,"speed publication lost token epoch");
        DispatchCadence fast; fast.SetRawSpeed(1000); fast.BeginFrame(0); Drain(fast);
        fast.BeginFrame(10000); Need(Drain(fast)==30,"source capacity30 not enforced");
        Need(fast.Backlog()==1970,"global elapsed clamp2000/backlog changed");
        fast.BeginFrame(0); Need(Drain(fast)==0,"token cap bypass on same timestamp");
        fast.BeginFrame(1); Need(Drain(fast)==1,"capped backlog did not resume");
        std::cout << "TS1 dispatch cadence: source periods, timing, token and reset rules passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

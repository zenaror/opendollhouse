/* SPDX-License-Identifier: MPL-2.0 */
// Pinned Alex FreeSO VMGenericTS1Call transit protocol. The controller values
// above 1 belong to caller/date trips, not an approximate family-size flag.
#include "simantics/engine/VMTS1Travel.h"
#include "simantics/engine/VMTS1MotiveEffects.h"
#include "simantics/engine/VMTS1PersonDataState.h"
#include <array>
#include <iostream>
#include <stdexcept>
using FSO::SimAntics::Engine::Utils::VMTS1Travel;
int main() {
    struct Destination { int16_t Lot; bool HouseholdDefault; };
    const Destination destinations[] = {
        {-1,false},{0,false},{21,false},{30,false}, // return and Downtown
        {39,false},{40,true},{49,true},{50,false}, // Vacation boundaries
        {57,false},{58,true},{59,false},{60,false},{61,true},{62,false},
        {69,false},{70,true},{75,true},{76,false}, // Old Town, not every residential lot
        {81,false},{89,false},{90,false},{91,false},{99,false} // Studio and Magic
    };
    int cases=0;
    for (auto d:destinations) for (int16_t mode:{int16_t(-1),int16_t(0),int16_t(1),int16_t(2),int16_t(75)}) {
        const auto actual=VMTS1Travel::TransitInfoForDestination(d.Lot,mode);
        const auto expected=mode<=0 && d.HouseholdDefault ? int16_t(1):mode;
        if(actual!=expected || VMTS1Travel::IsWholeFamilyTransit(actual)!=(expected==1))
            throw std::runtime_error("Destination changed an explicit party mode or lost its household default");
        ++cases;
    }
    std::cout<<"travel party protocol: "<<cases<<" destination/mode cases passed\n";
    using FSO::Files::Formats::IFF::Chunks::InventoryItem;
    std::vector<InventoryItem> inventory;
    auto token=[&](uint32_t slot,uint16_t type,int16_t value) {
        InventoryItem item;item.GUID=slot;item.Type=type;item.Count=uint16_t(value);inventory.push_back(item);
    };
    std::array<int16_t,7> motives{{-31,27,-67,44,-15,18,-9}};
    const auto saved=motives;
    int writes=0;
    auto apply=[&](uint32_t slot,int16_t value){motives.at(slot)=value;++writes;};
    // A saved visitor has only the clock/party metadata left; unrelated
    // inventory can even use a motive slot's GUID without being an effect.
    token(7,2,18);token(8,2,35);token(9,2,3);token(0,4,99);
    FSO::SimAntics::Engine::Utils::ApplyTS1MotiveEffects(inventory,apply);
    if(motives!=saved || writes || inventory.size()!=4) throw std::runtime_error("metadata-only arrival changed saved motives/inventory");
    token(2,2,0);token(3,2,-48);token(3,2,77);
    FSO::SimAntics::Engine::Utils::ApplyTS1MotiveEffects(inventory,apply);
    if(writes!=2 || motives[2]!=0 || motives[3]!=-48 || motives[0]!=saved[0] || motives[4]!=saved[4] || inventory.size()!=4)
        throw std::runtime_error("partial effects lost zero/signed/first-value semantics or touched unrelated state");
    const auto after=motives;
    FSO::SimAntics::Engine::Utils::ApplyTS1MotiveEffects(inventory,apply);
    if(motives!=after || writes!=2) throw std::runtime_error("arrival replay reapplied consumed effects");
    for(uint32_t i=0;i<7;++i)token(i,2,int16_t(10+i));
    FSO::SimAntics::Engine::Utils::ApplyTS1MotiveEffects(inventory,apply);
    for(uint32_t i=0;i<7;++i)if(motives[i]!=int16_t(10+i))throw std::runtime_error("complete arrival lost a motive effect");
    if(inventory.size()!=4 || writes!=9)throw std::runtime_error("complete arrival consumed travel metadata");
    std::cout<<"travel motive effects: metadata-only, partial/zero/signed, duplicates, replay and complete arrival passed\n";
    // Same import boundary used on real return. The canonical importer supplies
    // neighbour data; a stale house must not win over earned skills/fame, and a
    // caller-only trip must not replace the household left at home.
    std::array<int16_t,101> home{}, neighbour{};
    for(size_t i=0;i<home.size();++i) { home[i]=int16_t(100+i); neighbour[i]=int16_t(500+i); }
    home[11]=0; neighbour[11]=93;
    neighbour[80]=245; neighbour[81]=3; neighbour[82]=1;
    const auto originalHome=home;
    const std::vector<uint32_t> travellers{0xC07F6184};
    int imports=0;
    auto refresh=[&](uint32_t guid) {
        return FSO::SimAntics::Engine::Utils::RefreshReturningTS1Person(guid,travellers,
            [&](int field){return home.at(field);},
            [&](int field,int16_t value){home.at(field)=value;},
            [&](){++imports;home=neighbour;return true;});
    };
    if(refresh(0xC0C6298E) || imports || home!=originalHome)
        throw std::runtime_error("return imported neighbour state into a non-traveller");
    if(!refresh(0xC07F6184) || imports!=1 || home[11]!=93 || home[80]!=245 || home[81]!=3 || home[82]!=1)
        throw std::runtime_error("return lost canonical skill/fame progress");
    // Explicit representative execution fields, independent of the helper's
    // iteration, protect against accidentally importing a running route/queue.
    for(int field:{0,8,22,23,25,27,28,33,37,38,39,40,41,42,43,44,45,59,62,64,71,74,77,78})
        if(home[field]!=originalHome[field])throw std::runtime_error("return replaced local transient person state");
    constexpr int greet=int(FSO::SimAntics::Model::VMPersonDataVariable::GreetStatus);
    for(int16_t value:{int16_t(1),int16_t(2)}) {
        home[greet]=value;
        // Exercise the same return wrapper with the reset performed by the
        // canonical importer, rather than allowing a pre-matched source value.
        FSO::SimAntics::Engine::Utils::RefreshReturningTS1Person(0xC07F6184,travellers,
            [&](int field){return home.at(field);},
            [&](int field,int16_t v){home.at(field)=v;},
            [&](){home=neighbour;home[greet]=0;return true;});
        if(home[greet]!=value)throw std::runtime_error("durable return refresh reset the resident greeting");
    }
    std::cout<<"travel persistent person: caller-only selection, charisma/fame and local execution state passed\n";
}

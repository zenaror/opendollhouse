// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <chrono>

namespace FSO { namespace Common { namespace TS1 {
// DVD42385a. These are raw person-bank indices, not TSO field names.
template<class F> void VisitBirthProfileFields(int32_t mode,F&& write) {
    static constexpr int one[]={2,3,4,5,6,7,9,10,11,12,16,20,15,26,17,18,46,47,48,49,50,51,52,53,13,14,58,60,61};
    if(mode>=1)for(int index:one)write(index);
    if(mode>=2)for(int index:{56,57,63})write(index);
    if(mode>=3)write(65);
    if(mode>=4)for(int index:{67,68})write(index);
    if(mode>=5)write(69);
    if(mode>=6)write(74);
    if(mode>=7)for(int index:{54,55,75,76})write(index);
    if(mode>=8)for(int index:{80,81})write(index);
    if(mode>=9)write(82);
    if(mode>=10)for(int index:{83,84,85,86})write(index);
}
// DVD423cc8 /42597a, distinct from the broad4258ed table.
template<class F> void VisitBirthInterestFields(int32_t mode,F&& write) {
    if(mode<7)return;
    for(int index:{46,47,48,49,50,51,52,53,13,14,16,20,26,54,55})write(index);
}
struct PersonCRTStream {
    uint32_t State=1; // original CRT TLS initializer63e1c0
    explicit PersonCRTStream(uint32_t state=1):State(state){}
    uint32_t Next(){State=State*UINT32_C(0x343fd)+UINT32_C(0x269ec3);return(State>>16)&32767;}
};
// The original can seed this independent TLS stream with RDTSC619d00.
// Its prior GUI/CRT consumers are not modeled. Use a separate process/thread
// stream and host entropy once; never the lot's serialized owner LCG or OS rand.
inline PersonCRTStream& NaturalPersonCRTStream(){
    thread_local PersonCRTStream stream(uint32_t(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    return stream;
}
template<class Bank> bool LegacyInterestBank(const Bank& bank){
    for(int index=46;index<=53;++index)if(bank[index]>10)return false;
    return true;
}
// DVD459c08. The caller supplies the independent stream, enabling exact
// fixed-seed controls without altering the natural simulation RNG.
template<class Bank> void MigratePersonInterests(Bank& bank,PersonCRTStream& stream){
    auto fill=[&](const int* fields,int count,std::array<int,3> quotas){
        for(int i=0;i<count;++i){
            uint32_t category;
            do{category=stream.Next()%3;}while(!quotas[category]);
            --quotas[category];
            const uint32_t value=category==0?stream.Next()%4:category==1?stream.Next()%3+4:stream.Next()%4+7;
            bank[fields[i]]=int16_t(value*100);
        }
    };
    const int interests[]={46,47,48,49,50,51,52,53,54,55};
    const int other[]={13,14,16,20,26};
    fill(interests,10,{{4,3,3}});fill(other,5,{{1,2,2}});
}
}}}

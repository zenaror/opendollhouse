/* SPDX-License-Identifier: MPL-2.0 */
// New-person export contract shared by CAS and MakeNewCharacter; no asset/save inputs.
#include "content/TS1SimDesc.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace FSO::Content::TS1::CAS;
static void Require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){try{CASSimDesc sim;const std::array<int16_t,10> points{{0,1,3,4,6,7,9,10,5,2}};const std::array<int16_t,10> stored{{0,100,300,400,600,700,900,1000,500,200}};
 for(size_t i=0;i<points.size();++i)sim.Interests[i]=points[i];sim.Traits[TraitNeat]=10;sim.Traits[TraitOutgoing]=3;sim.Traits[TraitActive]=0;sim.Traits[TraitPlayful]=7;sim.Traits[TraitNice]=5;
 auto person=sim.PersonData(13);Require(person.size()==80,"classic new-person export width changed");for(size_t i=0;i<stored.size();++i)Require(person[46+i]==stored[i],"interest design points did not export in SimAntics hundredths");
 Require(person[7]==1000&&person[6]==300&&person[3]==0&&person[5]==700&&person[2]==500,"personality mapping/units regressed");Require(person[58]==27&&person[61]==13&&person[65]==1,"adult identity export regressed");Require(sim.Interests[1]==1&&sim.Interests[7]==10,"export changed design points");
 // These authored phone breakpoints must remain reachable from valid design points.
 Require(person[47]>=100&&person[49]>=400&&person[51]>=700,"new-person interests cannot reach authored conversation thresholds");
 sim.Age=CASAge::Child;sim.Gender=CASGender::Male;sim.Skin=CASSkin::Dark;auto child=sim.PersonData(21);Require(child[56]==-1&&child[57]==4&&child[58]==9&&child[60]==2&&child[61]==21&&child[65]==0,"child/school/skin identity export regressed");for(size_t i=0;i<stored.size();++i)Require(child[46+i]==stored[i],"child interest export units differ");
 CASSimDesc zero;auto empty=zero.PersonData(0);for(int i=46;i<56;++i)Require(empty[i]==0,"zero interest gained a value");std::cout<<"PASS interest endpoints/intermediates, personality identity, child/adult, immutable design input\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

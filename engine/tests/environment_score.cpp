// SPDX-License-Identifier: MPL-2.0
#include "common/TS1/TS1Environment.h"
#include <cmath>
#include <iostream>
#include <cmath>
#include <utility>
#include "simantics/VMClock.h"
#include <stdexcept>
using namespace FSO::Common::TS1;
static void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void Near(float value, float expected, const char* message) {
    Check(std::fabs(value - expected) < .00001f, message);
}
int main() {
    EnvironmentTunings tuning;
    EnvironmentInputs bedroom;
    bedroom.HalfTiles = bedroom.CoveredHalfTiles = 96;
    bedroom.WallCount = bedroom.CoveredWallCount = 34;
    bedroom.RoomImpact = -20;
    bedroom.WindowLight = 390;
    auto result = EvaluateEnvironment(bedroom, tuning, false, 0);
    Check(result.Score == -6.444444656f, "DVD bedroom night float score");
    Check(result.LightFactor == 0, "windows do not light a room at night");
    auto bathroom = bedroom;
    bathroom.HalfTiles = bathroom.CoveredHalfTiles = 40;
    bathroom.WallCount = bathroom.CoveredWallCount = 23;
    bathroom.RoomImpact = 0;
    bathroom.WindowLight = 260;
    bathroom.ArtificialLight = 75;
    result = EvaluateEnvironment(bathroom, tuning, false, 0);
    Check(result.Score == 10 && result.LightFactor == 1, "DVD lit bathroom night score");
    bathroom.ArtificialLight = 0;
    Near(EvaluateEnvironment(bathroom, tuning, false, 0).Score, -30, "unlit bathroom");
    bathroom.CoveredHalfTiles /= 2;
    bathroom.CoveredWallCount = 0;
    Near(EvaluateEnvironment(bathroom, tuning, false, 0).Score, -90, "unfinished floor and wall penalties");
    bathroom.HalfTiles = 41;
    bathroom.CoveredHalfTiles = 41;
    bathroom.CoveredWallCount = bathroom.WallCount;
    Near(EvaluateEnvironment(bathroom, tuning, false, 0).Score, -30, "integer half-tile division");
    EnvironmentInputs outdoors;
    outdoors.RoomImpact = -40;
    outdoors.GroundGoodCount = 15;
    outdoors.GoodObjectCount = 7;
    auto outside = EvaluateEnvironment(outdoors, tuning, true, 0);
    Check(outside.Score == -58, "outdoor impact and good object aggregation");
    Check(EvaluateOutdoorEnvironment(outside.Score, tuning, 0) == -58.0f / 3.0f, "outdoor day aggregate");
    Near(EvaluateOutdoorEnvironment(outside.Score, tuning, 2), -69.333335876f, "DVD outdoor night aggregate");
    Check(EnvironmentDaylight(0) == 1 && EnvironmentDaylight(1) == .5f &&
          EnvironmentDaylight(2) == 0 && EnvironmentDaylight(3) == .5f, "daylight periods");
    tuning.OutdoorDivisor = 2;
    tuning.OutdoorNight = -5;
    Check(EvaluateOutdoorEnvironment(outside.Score, tuning, 2) == -34, "named tuning overrides");
    FSO::SimAntics::VMClock clock;
    for (auto sample : {std::pair<int,int>{0,2},{5,2},{6,3},{7,0},{17,0},{18,1},{19,2},{23,2}}) {
        clock.Hours=sample.first;
        Check(clock.TimeOfDay()==sample.second,"original day-period boundary");
    }
    EnvironmentInputs edge;edge.HalfTiles=edge.CoveredHalfTiles=2;
    edge.WallCount=edge.CoveredWallCount=4;edge.ArtificialLight=1;
    EnvironmentTunings threshold;threshold.ObjectLightFactor=99;
    Check(EvaluateEnvironment(edge,threshold,false,0).LightFactor<1,"below upper light cutoff");
    threshold.ObjectLightFactor=std::nextafter(99.0f,100.0f);
    Check(EvaluateEnvironment(edge,threshold,false,0).LightFactor==1,"above upper light cutoff");
    threshold.ObjectLightFactor=1;
    Check(EvaluateEnvironment(edge,threshold,false,0).LightFactor==.01f,"exact lower light cutoff");
    threshold.ObjectLightFactor=std::nextafter(1.0f,0.0f);
    Check(EvaluateEnvironment(edge,threshold,false,0).LightFactor==0,"below lower light cutoff");
    auto full=[](uint16_t id){return uint32_t(id)|(uint32_t(id)<<16);};
    std::vector<uint32_t> map(49,full(1));
    map[3*7+3]=uint32_t(2)|(uint32_t(3)<<16);
    map[3*7+2]=full(2);map[3*7+4]=full(3);
    map[2*7+3]=full(4);map[4*7+3]=full(5);
    auto resolve=[&](int x,int y,uint8_t diagonal){
        return ResolveEnvironmentRoom(map,7,7,3,3,x,y,diagonal);};
    Check(resolve(1,1,16)==2 && resolve(14,14,16)==3,"diagonal16 opposite sides");
    Check(resolve(7,8,16)==2 && resolve(8,8,16)==3,"diagonal16 exact tie");
    Check(resolve(1,14,32)==2 && resolve(14,1,32)==3,"diagonal32 opposite sides");
    Check(resolve(7,8,32)==2 && resolve(8,8,32)==3,"diagonal32 exact tie");
    map[3*7+2]=map[3*7+3];map[3*7+4]=map[3*7+3];
    Check(resolve(1,1,16)==4 && resolve(14,14,16)==5,"diagonal16 second neighbor");
    Check(resolve(1,14,32)==5 && resolve(14,1,32)==4,"diagonal32 second neighbor");
    map[2*7+3]=map[4*7+3]=map[3*7+3];
    Check(resolve(8,8,16)==-5 && resolve(8,8,32)==-5,"both neighbors unresolved");
    for (auto border : {std::pair<int,int>{0,3},{6,3},{3,0},{3,6}})
        Check(ResolveEnvironmentRoom(map,7,7,border.first,border.second,8,8,0)==-5,
              "border has unresolved original object room");
    Check(ResolveEnvironmentRoom(map,7,7,1,1,8,8,0)==1,"ordinary cell lookup");
    std::cout << "Environment score contracts passed\n";
}

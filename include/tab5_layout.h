#pragma once
#include "tab5_graph.h"
namespace tablayout {
struct Rect {int x,y,w,h;bool contains(int px,int py)const{return px>=x&&px<x+w&&py>=y&&py<y+h;}};
constexpr Rect battery{772,10,100,52};
// Right column of the Loop card: uploader battery and CAGE/SAGE/IAGE.
constexpr Rect loopSide{1122,76,134,148};
constexpr Rect wifi{884,10,52,52},headerSound{948,10,52,52};
constexpr Rect forecast{1012,10,52,52},refresh{1076,10,52,52},night{1140,10,52,52},settings{1204,10,52,52};
constexpr Rect ranges[]={{510,252,64,44},{582,252,64,44},{654,252,64,44},{726,252,64,44}};
constexpr Rect earlier{838,252,56,44},later{906,252,56,44},live{978,252,258,44};
constexpr Rect back{1204,10,52,52},sound{24,112,296,78},units{336,112,296,78},clockFormat{648,112,296,78},rotation{960,112,296,78};
constexpr Rect dimmer{52,266,56,52},brighter{536,266,56,52};
constexpr Rect reconnect{688,270,252,48},wifiSetup{956,270,272,48},portal{1030,376,198,48};
}

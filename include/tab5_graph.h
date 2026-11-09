#pragma once
#include <stdint.h>
#include <algorithm>
#include <cmath>
namespace tabgraph {
constexpr int left=88, right=1232, top=320, bottom=598;
// 12h/24h views reserve this many pixels under the glucose plot for the SMB strip.
constexpr int insulinStrip=46;
inline int x(int64_t t,int64_t start,int64_t end) {
  return left+int(double(t-start)*(right-left)/double(end-start));
}
inline int y(float value,float low,float high,int floor=bottom) {
  return floor-int((std::max(low,std::min(high,value))-low)*(floor-top)/(high-low));
}
inline unsigned range(unsigned index) {const unsigned hours[]={3,6,12,24};return hours[index%4];}
}

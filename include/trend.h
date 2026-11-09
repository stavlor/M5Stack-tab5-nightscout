#pragma once
#include <string.h>
namespace trend {
struct Arrow {float dx=0,dy=0;unsigned count=0;};
inline Arrow arrow(const char *value) {
 if(!value)return {};
 if(!strcmp(value,"DoubleUp"))return {0,-1,2};
 if(!strcmp(value,"SingleUp"))return {0,-1,1};
 if(!strcmp(value,"FortyFiveUp"))return {0.7071068f,-0.7071068f,1};
 if(!strcmp(value,"Flat"))return {1,0,1};
 if(!strcmp(value,"FortyFiveDown"))return {0.7071068f,0.7071068f,1};
 if(!strcmp(value,"SingleDown"))return {0,1,1};
 if(!strcmp(value,"DoubleDown"))return {0,1,2};
 return {};
}
}

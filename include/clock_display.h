#pragma once
#include <time.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
namespace clockdisplay {
inline bool valid(int64_t t){return t>=1767225600LL && t<4102444800LL;}
inline void formatTime(const tm &local,char *clock,size_t size,bool twelveHour=false){
 if(twelveHour){int hour=local.tm_hour%12;if(!hour)hour=12;snprintf(clock,size,"%d:%02d %s",hour,local.tm_min,local.tm_hour<12?"AM":"PM");}
 else strftime(clock,size,"%H:%M",&local);
}
inline void format(const tm &local,bool haveTime,char *clock,size_t clockSize,char *date,size_t dateSize,bool twelveHour=false){
 if(!haveTime){snprintf(clock,clockSize,"--:--");snprintf(date,dateSize,"Waiting for time");return;}
 formatTime(local,clock,clockSize,twelveHour);strftime(date,dateSize,"%a %d %b",&local);
}
}

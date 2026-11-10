#pragma once
// Pump reservoir and battery from the AAPS devicestatus `pump` block, with
// Nightscout's default pump-plugin warning thresholds.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
namespace pumpstatus {
enum class Level {Unknown,Ok,Warn,Urgent};
constexpr float RES_WARN=10, RES_URGENT=5;          // units, Nightscout warnRes/urgentRes
constexpr float BATT_P_WARN=30, BATT_P_URGENT=20;   // percent
constexpr float BATT_V_WARN=1.35f, BATT_V_URGENT=1.30f; // volts
inline Level below(float value,float warn,float urgent) {
  if (!isfinite(value)) return Level::Unknown;
  return value<urgent?Level::Urgent:value<warn?Level::Warn:Level::Ok;
}
inline Level reservoir(float units) {return below(units,RES_WARN,RES_URGENT);}
inline Level battery(float percent,float volts) {
  return isfinite(percent)?below(percent,BATT_P_WARN,BATT_P_URGENT):below(volts,BATT_V_WARN,BATT_V_URGENT);
}
// "123U", "7.5U" under 10 U, the pump's own override text (e.g. "50+") + "U", or "--".
inline void reservoirText(float units,const char *override,char *out,size_t cap) {
  if (override && *override) {
    size_t n=strlen(override);
    snprintf(out,cap,(n && (override[n-1]=='U' || override[n-1]=='u'))?"%s":"%sU",override);
  }
  else if (!isfinite(units)) snprintf(out,cap,"--");
  else if (units<10) snprintf(out,cap,"%.1fU",units);
  else snprintf(out,cap,"%dU",int(lroundf(units)));
}
// "80%", "1.42V" or "--".
inline void batteryText(float percent,float volts,char *out,size_t cap) {
  if (isfinite(percent)) snprintf(out,cap,"%d%%",int(lroundf(percent)));
  else if (isfinite(volts)) snprintf(out,cap,"%.2fV",volts);
  else snprintf(out,cap,"--");
}
}

#pragma once
// Cannula / sensor / insulin age (CAGE / SAGE / IAGE) from Nightscout careportal
// treatments, using the event names AAPS and Nightscout share.
#include "model.h"
namespace model {
struct CareAges {int64_t cannula=0,sensor=0,insulin=0;};
inline void careFilter(DynamicJsonDocument &doc) {
  JsonObject f=doc.to<JsonArray>().createNestedObject();
  for (const char *k : {"eventType","date","mills","created_at","timestamp","isValid","isDeleted"}) f[k]=true;
}
// Latest event of each kind; Sensor Start and Sensor Change both restart SAGE.
inline bool careEvents(JsonVariantConst root,int64_t now,CareAges &out) {
  if (!root.is<JsonArrayConst>()) return false;
  CareAges next;
  for (JsonObjectConst o : root.as<JsonArrayConst>()) {
    if (deleted(o)) continue;
    const char *type=o["eventType"] | "";
    int64_t t=timestamp(o["date"]);
    if (!t) t=timestamp(o["mills"]);
    if (!t) t=timestamp(o["timestamp"]);
    if (!t) t=timestamp(o["created_at"]);
    if (!validTime(t,now)) continue;
    int64_t *slot=!strcmp(type,"Site Change")?&next.cannula:
      (!strcmp(type,"Sensor Start")||!strcmp(type,"Sensor Change"))?&next.sensor:
      !strcmp(type,"Insulin Change")?&next.insulin:nullptr;
    if (slot && t>*slot) *slot=t;
  }
  out=next;return true; // [] clears ages older than the query window
}
}
namespace careage {
enum class Level {Unknown,Ok,Info,Warn,Urgent};
struct Limits {unsigned info,warn,urgent;}; // hours
// CAGE/IAGE match Nightscout's defaults. SAGE is set for a 10-day sensor
// (Nightscout's own default assumes 7 days); adjust here for other sensors.
constexpr Limits CANNULA{44,48,72}, INSULIN{44,48,72}, SENSOR{216,228,240};
inline int64_t hours(int64_t t,int64_t now) {return now>t?(now-t)/3600:0;}
inline Level level(int64_t t,int64_t now,Limits l) {
  if (!t || !now) return Level::Unknown;
  int64_t h=hours(t,now);
  return h>=l.urgent?Level::Urgent:h>=l.warn?Level::Warn:h>=l.info?Level::Info:Level::Ok;
}
// "17h" under two days, otherwise "3d 4h"; "--" when unknown.
inline void format(int64_t t,int64_t now,char *out,size_t cap) {
  if (!t || !now) {snprintf(out,cap,"--");return;}
  int64_t h=hours(t,now);
  if (h<48) snprintf(out,cap,"%lldh",(long long)h);
  else snprintf(out,cap,"%lldd %lldh",(long long)(h/24),(long long)(h%24));
}
}

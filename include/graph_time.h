#pragma once
// Local-clock alignment for graph ticks and 12h/24h SMB buckets. Pure functions
// over time_t so they are host-testable with any POSIX TZ.
#include <stdint.h>
#include <stddef.h>
#include <time.h>
namespace graphtime {
// Seconds east of UTC for local time at t (handles DST via localtime_r).
inline int64_t localOffset(int64_t t) {
  time_t at=time_t(t);tm local{},utc{};localtime_r(&at,&local);gmtime_r(&at,&utc);
  int64_t d=int64_t(local.tm_hour-utc.tm_hour)*3600+int64_t(local.tm_min-utc.tm_min)*60+(local.tm_sec-utc.tm_sec);
  int day=local.tm_yday-utc.tm_yday;
  if(day==1||day<-1)d+=86400;else if(day==-1||day>1)d-=86400;
  return d;
}
inline int64_t floorDiv(int64_t a,int64_t b){int64_t q=a/b;return (a%b!=0&&((a<0)!=(b<0)))?q-1:q;}
// Latest local-clock boundary of `step` seconds at or before t.
inline int64_t alignDown(int64_t t,int64_t step){int64_t off=localOffset(t);return floorDiv(t+off,step)*step-off;}
// Earliest local-clock boundary of `step` seconds at or after t.
inline int64_t alignUp(int64_t t,int64_t step){int64_t d=alignDown(t,step);return d<t?alignDown(t+step,step):d;}
// Axis tick spacing for a view of `hours` (forecast extension excluded).
inline int64_t tickStep(unsigned hours){return hours<=1?900:hours<=3?3600:hours<=6?7200:hours<=12?3*3600:6*3600;}
// SMB strip bucket: about 12 per view (5 min at 1h, 15 min at 3h, 30 min at 6h, 1 h at 12h, 2 h at 24h).
inline int64_t bucketSize(unsigned hours){int64_t size=int64_t(hours)*3600/12;return size<300?300:size;}
struct Bucket {int64_t from=0,to=0;float units=0;unsigned count=0;};
struct Buckets {
  static constexpr size_t CAPACITY=32;
  Bucket items[CAPACITY];
  size_t size=0;
  void build(int64_t start,int64_t end,int64_t step) {
    size=0;if(end<=start||step<=0)return;
    for(int64_t at=alignDown(start,step);at<end&&size<CAPACITY;) {
      int64_t next=alignUp(at+1,step);if(next<=at)next=at+step;
      Bucket &b=items[size++];b=Bucket();b.from=at;b.to=next;at=next;
    }
  }
  bool add(int64_t t,float units) {
    for(size_t i=0;i<size;++i)if(t>=items[i].from&&t<items[i].to){items[i].units+=units;++items[i].count;return true;}
    return false;
  }
  const Bucket *find(int64_t t) const {for(size_t i=0;i<size;++i)if(t>=items[i].from&&t<items[i].to)return &items[i];return nullptr;}
  float maximum() const {float m=0;for(size_t i=0;i<size;++i)if(items[i].units>m)m=items[i].units;return m;}
};
}

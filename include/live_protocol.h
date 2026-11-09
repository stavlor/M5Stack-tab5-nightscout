#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace nslive {
constexpr unsigned Entries=1,Status=2,Treatments=4,All=7;
inline unsigned collection(const char *s){return !strcmp(s,"entries")?Entries:!strcmp(s,"devicestatus")?Status:!strcmp(s,"treatments")?Treatments:0;}
// First-event deadline does not move during a burst. Taking work never clears later events.
struct Pending {
 unsigned mask=0;uint32_t since=0;
 void add(unsigned bits,uint32_t now){if(!mask)since=now;mask|=bits;}
 unsigned take(uint32_t now){if(!mask || uint32_t(now-since)<1500)return 0;unsigned out=mask;mask=0;return out;}
};
// Incremental RFC6455 server frames, bounded message storage supplied by caller.
// Control frames may interrupt fragmented text without corrupting the message.
class Frames {
 char *data;size_t capacity,used=0;uint8_t header[10]={},control[126]={};
 size_t hn=0,need=2;uint64_t remaining=0,length=0;uint8_t opcode=0;bool fin=false,fragment=false,body=false;
public:
 Frames(char *buffer,size_t cap):data(buffer),capacity(cap){}
 void reset(){used=hn=0;need=2;remaining=length=0;fragment=body=false;}
 template<class Emit> bool feed(uint8_t b,Emit emit){
  if(!body){
   header[hn++]=b;if(hn<need)return true;
   if(need==2){
    fin=header[0]&128;opcode=header[0]&15;
    if((header[0]&112)||(header[1]&128))return false;
    unsigned n=header[1]&127;
    if(opcode!=0&&opcode!=1&&opcode!=8&&opcode!=9&&opcode!=10)return false;
    if(opcode>=8&&(!fin||n>125))return false;
    if(opcode==0&&!fragment)return false;
    if(opcode==1&&fragment)return false;
    if(n>=126){need=n==126?4:10;return true;}length=n;
   }else{
    length=0;for(size_t i=2;i<need;i++)length=(length<<8)|header[i];
    if((need==10&&(header[2]&128))||(need==4&&length<126)||(need==10&&length<65536))return false;
   }
   if(opcode<8){if(opcode==1)used=0;if(length>capacity-used)return false;}
   remaining=length;body=true;
   if(remaining)return true;
  }else{
   if(opcode<8)data[used++]=char(b);else control[size_t(length-remaining)]=b;
   if(--remaining)return true;
  }
  bool ok=true;
  if(opcode>=8)ok=emit(opcode,reinterpret_cast<char*>(control),size_t(length));
  else if(fin){data[used]=0;fragment=false;ok=emit(1,data,used);used=0;}
  else fragment=true;
  hn=0;need=2;body=false;return ok;
 }
};
}

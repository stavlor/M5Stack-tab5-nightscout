#pragma once
#include "live_protocol.h"
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
// A read-only Socket.IO4 / Engine.IO4 listener. Data always comes from verified REST snapshots.
class NightscoutLive {
 const uint8_t *trustBundle;SecureClient socket;
 char *buffer=nullptr;nslive::Frames frames;
 enum Phase{Closed,Engine,Namespace,Subscribe,Ready} phase=Closed;
 uint32_t lastPacket=0,opened=0,heartbeat=45000,nextAttempt=0;
 unsigned retry=0;String credential;bool polling=false;
 bool sendFrame(uint8_t op,const char *p,size_t n){
  if(n>4096)return false;
  uint8_t h[8]={uint8_t(128|op),uint8_t(128|(n<126?n:126))};size_t k=2;
  if(n>=126){h[k++]=uint8_t(n>>8);h[k++]=uint8_t(n);}
  uint8_t mask[4];esp_fill_random(mask,4);memcpy(h+k,mask,4);k+=4;
  if(socket.write(h,k)!=k)return false;
  uint8_t chunk[128];size_t at=0;
  while(at<n){size_t count=std::min(sizeof(chunk),n-at);for(size_t i=0;i<count;i++)chunk[i]=uint8_t(p[at+i])^mask[(at+i)%4];if(socket.write(chunk,count)!=count)return false;at+=count;}
  return true;
 }
 bool text(const String &s){return sendFrame(1,s.c_str(),s.length());}
 bool message(uint8_t op,char *p,size_t n){
  if(op==8)return false;if(op==9)return sendFrame(10,p,n);if(op==10)return true;
  if(!n)return false;lastPacket=millis();
  if(p[0]=='2'){p[0]='3';return sendFrame(1,p,n);}
  if(p[0]=='1')return false;
  if(phase==Engine&&p[0]=='0'){
   DynamicJsonDocument d(1024);if(deserializeJson(d,p+1,n-1))return false;
   unsigned interval=d["pingInterval"]|0u,timeout=d["pingTimeout"]|0u;
   if(!interval||!timeout||interval>120000||timeout>120000)return false;
   heartbeat=interval+timeout+2000;phase=Namespace;return text("40/storage,");
  }
  if(!strncmp(p,"41/storage",10)||!strncmp(p,"44/storage",10))return false;
  if(phase==Namespace&&!strncmp(p,"40/storage,",11)){
   DynamicJsonDocument d(2048);JsonArray a=d.to<JsonArray>();a.add("subscribe");JsonObject request=a.createNestedObject();request["accessToken"]=credential;
   JsonArray cols=request.createNestedArray("collections");cols.add("entries");cols.add("devicestatus");cols.add("treatments");
   String packet="42/storage,1";serializeJson(d,packet);credential="";phase=Subscribe;return text(packet);
  }
  if(phase==Subscribe&&!strncmp(p,"43/storage,1",12)){
   DynamicJsonDocument d(1024);if(deserializeJson(d,p+12,n-12))return false;
   unsigned mask=0;for(JsonVariantConst c:d[0]["collections"].as<JsonArrayConst>())mask|=nslive::collection(c.as<const char*>()?c.as<const char*>():"");
   if(!(d[0]["success"]|false)||mask!=nslive::All)return false;
   phase=Ready;retry=0;recovered=true;Serial.println("Live: subscribed to entries, devicestatus, treatments");return true;
  }
  if(phase==Ready&&!strncmp(p,"42/storage,",11)){
   const char *event=nullptr;char *payload=p+11;
   while(*payload==' ')++payload;if(*payload++!='[')return false;
   while(*payload==' ')++payload;
   for(const char *name:{"create","update","delete"}){
    size_t len=strlen(name);if(payload[0]=='"'&&!strncmp(payload+1,name,len)&&payload[len+1]=='"'){event=name;payload+=len+2;break;}
   }
   if(!event)return true;
   while(*payload==' ')++payload;if(*payload++!=',')return false;
   StaticJsonDocument<96> filter;filter["colName"]=true;DynamicJsonDocument d(256);
   if(deserializeJson(d,payload,size_t(p+n-payload),DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(32)))return false;
   unsigned mask=nslive::collection(d["colName"]|"");
   pending.add(mask?mask:nslive::All,millis());Serial.printf("Live: %s collection mask=%u\n",event,mask);
  }
  return true;
 }
public:
 nslive::Pending pending;bool recovered=false;
 explicit NightscoutLive(const uint8_t *bundle):trustBundle(bundle),socket(bundle,false),buffer(static_cast<char*>(heap_caps_malloc(65537,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT))),frames(buffer,65536){}
 bool online()const{return phase==Ready;}
 bool active()const{return phase!=Closed;}
 void close(bool resetRetry=false){socket.stop();phase=Closed;credential="";frames.reset();pending={};recovered=false;if(resetRetry){retry=0;nextAttempt=0;}}
 void fail(){close();unsigned seconds=std::min(5u<<std::min(retry++,6u),300u);nextAttempt=millis()+seconds*1000+(esp_random()%1000);Serial.printf("Live: polling fallback; retry in %us\n",seconds);}
 bool due()const{return !active()&&(!nextAttempt||int32_t(millis()-nextAttempt)>=0);}
 void poll(){
  if(!active()||polling)return;
  polling=true;struct Guard{bool &b;~Guard(){b=false;}} guard{polling};
  unsigned budget=8192;uint8_t chunk[512];
  while(budget){int available=socket.available();if(available<=0)break;size_t want=std::min(size_t(available),std::min(sizeof(chunk),size_t(budget)));int got=socket.read(chunk,want);if(got<=0)break;budget-=got;
   for(int i=0;i<got;i++)if(!frames.feed(chunk[i],[this](uint8_t op,char *p,size_t n){return message(op,p,n);})){fail();return;}
  }
  if(!socket.connected()||uint32_t(millis()-lastPacket)>heartbeat||(phase!=Ready&&uint32_t(millis()-opened)>15000))fail();
 }
 void connect(const char *url,const char *token){
  if(!buffer){nextAttempt=millis()+300000;return;}
  close();String origin=url;if(!origin.startsWith("https://")){fail();return;}origin.remove(0,8);
  int slash=origin.indexOf('/');String authority=slash<0?origin:origin.substring(0,slash),prefix=slash<0?"":origin.substring(slash);
  while(prefix.endsWith("/"))prefix.remove(prefix.length()-1);
  String host=authority;uint16_t port=443;int colon=authority.lastIndexOf(':');
  if(authority.startsWith("[")||authority.indexOf('@')>=0){fail();return;}
  if(colon>=0){host=authority.substring(0,colon);long number=authority.substring(colon+1).toInt();if(number<1||number>65535){fail();return;}port=number;}
  uint8_t nonce[16];esp_fill_random(nonce,sizeof(nonce));unsigned char encoded[32];size_t len=0;mbedtls_base64_encode(encoded,sizeof(encoded),&len,nonce,sizeof(nonce));String key(reinterpret_cast<char*>(encoded),len);
  String acceptInput=key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";uint8_t digest[20];mbedtls_sha1(reinterpret_cast<const uint8_t*>(acceptInput.c_str()),acceptInput.length(),digest);
  mbedtls_base64_encode(encoded,sizeof(encoded),&len,digest,sizeof(digest));String expected(reinterpret_cast<char*>(encoded),len);
  // The pinned Arduino core clears bundle_attach_cb inside stop_ssl_socket().
  // Re-arm the CA callback on every persistent-client reconnect, never insecure.
  socket.setCACertBundle(trustBundle,size_t(tab5BundleEnd-trustBundle));
  socket.setTimeout(1000);
  if(!socket.connect(host.c_str(),port)){Serial.printf("Live: TLS connect failed (%d)\n",socket.lastError(nullptr,0));fail();return;}
  String request="GET "+prefix+"/socket.io/?EIO=4&transport=websocket HTTP/1.1\r\nHost: "+authority+"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "+key+"\r\nSec-WebSocket-Version: 13\r\nUser-Agent: ESP32HTTPClient\r\n\r\n";
  if(socket.print(request)!=request.length()){fail();return;}
  String line;unsigned total=0;bool status=false,upgrade=false,connection=false,accept=false,done=false;uint32_t start=millis();
  while(uint32_t(millis()-start)<5000&&total<8192){
   if(socket.WiFiClientSecure::available()<=0){if(!socket.connected())break;vTaskDelay(1);continue;}
   char c=char(socket.read());++total;if(c!='\n'){if(c!='\r')line+=c;continue;}
   if(!status){status=line.startsWith("HTTP/1.1 101 ");if(!status)break;}
   else if(!line.length()){done=true;break;}
   else{int colon=line.indexOf(':');String name=line.substring(0,colon);name.toLowerCase();String value=line.substring(colon+1);value.trim();if(name=="sec-websocket-accept")accept=value==expected;value.toLowerCase();if(name=="upgrade")upgrade=value=="websocket";if(name=="connection")connection=value.indexOf("upgrade")>=0;}
   line="";
  }
  if(!done||!status||!upgrade||!connection||!accept){Serial.printf("Live: upgrade rejected done=%u status=%u upgrade=%u connection=%u accept=%u bytes=%u\n",done,status,upgrade,connection,accept,total);fail();return;}
  credential=token;phase=Engine;opened=lastPacket=millis();heartbeat=45000;Serial.println("Live: verified WebSocket connected");
 }
};

#pragma once
#include "tab5_layout.h"
#include "trend.h"
#include "wifi_signal.h"
#include "clock_display.h"
#include "smb_labels.h"
#include "graph_time.h"
#include "care_age.h"
static_assert(smblabels::CAPACITY>=model::MAX_TREATMENTS,"Every cached SMB must fit the label layout");
void saveDisplaySettings();
bool applyUiSettings(bool force);
void toggleGlucoseAlerts();
void updateGlucoseAlerts(int64_t);
bool nightMode=false,rendered=false,clock12=false,rotated180=false;
int64_t historyOffset=0;
String selection,uiNotice;
uint32_t noticeAt=0;
M5Canvas canvas(&M5.Display);
bool canvasReady=false;
namespace style {
constexpr uint16_t rgb(uint32_t v){return ((v>>8)&0xf800)|((v>>5)&0x07e0)|((v>>3)&0x001f);}
constexpr uint16_t bg=rgb(0x0B111B),card=rgb(0x111C2A),edge=rgb(0x243549),grid=rgb(0x243447);
constexpr uint16_t ink=rgb(0xEDF4FC),muted=rgb(0xA4B7CD),cyan=rgb(0x62D4ED),green=rgb(0x71D9AB);
constexpr uint16_t amber=rgb(0xFFD17A),red=rgb(0xFF7E83),selected=rgb(0x193849),band=rgb(0x112C2A);
}
enum class Type {Small,Body,Heading,Status,Metric,Hero};
void font(Type type) {
 canvas.setTextSize(1);
 switch(type){
 case Type::Small:canvas.setFont(&fonts::FreeSans9pt7b);break;
 case Type::Body:canvas.setFont(&fonts::FreeSans12pt7b);break;
 case Type::Heading:canvas.setFont(&fonts::FreeSansBold12pt7b);break;
 case Type::Status:canvas.setFont(&fonts::FreeSansBold18pt7b);break;
 case Type::Metric:canvas.setFont(&fonts::DejaVu40);break;
 case Type::Hero:canvas.setFont(&fonts::DejaVu72);break;
 }
}
void label(int x,int y,String value,Type type=Type::Body,uint16_t color=style::ink,int width=0,lgfx::textdatum_t datum=lgfx::textdatum_t::top_left) {
 font(type);
 if(width>0 && canvas.textWidth(value)>width){
   while(value.length() && canvas.textWidth(value+"...")>width)value.remove(value.length()-1);
   value+="...";
 }
 canvas.setTextDatum(datum);canvas.setTextColor(color);canvas.drawString(value,x,y);canvas.setTextDatum(lgfx::textdatum_t::top_left);
}
void line(int y,const String &s,uint16_t color=TFT_WHITE,int size=1) {
 M5.Display.setFont(&fonts::FreeSans12pt7b);M5.Display.setTextSize(1);M5.Display.setTextColor(color,TFT_BLACK);M5.Display.drawString(s,24,y);
}
void panel(int x,int y,int w,int h,uint16_t border=style::edge) {
 canvas.fillSmoothRoundRect(x,y,w,h,12,nightMode?style::rgb(0x080D15):style::card);
 canvas.drawRoundRect(x,y,w,h,12,border);
}
void button(tablayout::Rect r,const String &value,bool active=false) {
 canvas.fillSmoothRoundRect(r.x,r.y,r.w,r.h,9,active?style::selected:style::card);
 canvas.drawRoundRect(r.x,r.y,r.w,r.h,9,active?style::cyan:style::edge);
 label(r.x+r.w/2,r.y+r.h/2,value,Type::Body,active?style::cyan:style::ink,r.w-16,lgfx::textdatum_t::middle_center);
}
void stroke(int x0,int y0,int x1,int y1,uint16_t color,float radius=1.6f){canvas.drawWideLine(x0,y0,x1,y1,radius,color);}
enum class Icon {SoundOn,SoundOff,Forecast,Refresh,Sun,Moon,Settings,Back,Left,Right,Minus,Plus};
void icon(tablayout::Rect r,Icon kind,bool active=false) {
 const uint16_t fill=active?style::selected:style::card,c=active?style::cyan:style::ink;
 canvas.fillSmoothRoundRect(r.x,r.y,r.w,r.h,9,fill);canvas.drawRoundRect(r.x,r.y,r.w,r.h,9,active?style::cyan:style::edge);
 int x=r.x+r.w/2,y=r.y+r.h/2;
 if(kind==Icon::SoundOn||kind==Icon::SoundOff){
  uint16_t speaker=kind==Icon::SoundOn?c:style::muted;
  canvas.fillRect(x-14,y-5,7,10,speaker);
  canvas.fillTriangle(x-7,y-5,x+1,y-12,x+1,y+12,speaker);
  canvas.fillTriangle(x-7,y-5,x-7,y+5,x+1,y+12,speaker);
  if(kind==Icon::SoundOn){
   for(int radius: {8,14})for(int a=-50;a<50;a+=8){float p=a*0.0174533f,q=(a+8)*0.0174533f;stroke(x+2+int(cosf(p)*radius),y+int(sinf(p)*radius),x+2+int(cosf(q)*radius),y+int(sinf(q)*radius),c,1.2f);}
  }else stroke(x-16,y-16,x+16,y+16,style::amber,1.7f);
 }else if(kind==Icon::Forecast){
  stroke(x-14,y-13,x-14,y+13,c,1);stroke(x-14,y+13,x+15,y+13,c,1);
  stroke(x-10,y+6,x-3,y-1,c);stroke(x-3,y-1,x+3,y+3,c);stroke(x+3,y+3,x+9,y-8,c);
  stroke(x+9,y-8,x+12,y-12,style::cyan,1);canvas.fillCircle(x+15,y-15,2,style::cyan);
 }else if(kind==Icon::Refresh){
  for(int a=-55;a<235;a+=7){float p=a*0.0174533f,q=(a+7)*0.0174533f;stroke(x+int(cosf(p)*14),y+int(sinf(p)*14),x+int(cosf(q)*14),y+int(sinf(q)*14),c);}
  canvas.fillTriangle(x-14,y-16,x-3,y-13,x-12,y-4,c);
 }else if(kind==Icon::Sun){
  canvas.drawCircle(x,y,6,c);canvas.drawCircle(x,y,7,c);
  for(int a=0;a<360;a+=45){float p=a*0.0174533f;stroke(x+int(cosf(p)*11),y+int(sinf(p)*11),x+int(cosf(p)*16),y+int(sinf(p)*16),c,1.2f);}
 }else if(kind==Icon::Moon){canvas.fillSmoothCircle(x,y,15,c);canvas.fillSmoothCircle(x+7,y-5,13,fill);
 }else if(kind==Icon::Settings){
  canvas.drawCircle(x,y,10,c);canvas.drawCircle(x,y,11,c);canvas.drawCircle(x,y,4,c);
  for(int a=0;a<360;a+=45){float p=a*0.0174533f;stroke(x+int(cosf(p)*11),y+int(sinf(p)*11),x+int(cosf(p)*16),y+int(sinf(p)*16),c,2);}
 }else if(kind==Icon::Back||kind==Icon::Left){stroke(x+5,y-10,x-5,y,c);stroke(x-5,y,x+5,y+10,c);if(kind==Icon::Back)stroke(x-5,y,x+13,y,c);
 }else if(kind==Icon::Right){stroke(x-5,y-10,x+5,y,c);stroke(x+5,y,x-5,y+10,c);
 }else {stroke(x-10,y,x+10,y,c);if(kind==Icon::Plus)stroke(x,y-10,x,y+10,c);}
}
bool wifiSampled=false,wifiLinked=false;
uint32_t wifiSampleAt=0;
int wifiRssi=0;
unsigned wifiBars=0;
void drawWifiSignal() {
 bool connected=WiFi.status()==WL_CONNECTED;
 if(!wifiSampled||connected!=wifiLinked||uint32_t(millis()-wifiSampleAt)>=5000){
  wifiSampled=true;wifiSampleAt=millis();wifiLinked=connected;wifiRssi=connected?WiFi.RSSI():0;wifiBars=wifisignal::bars(connected,wifiRssi);
 }
 auto r=tablayout::wifi;int x=r.x+r.w/2,y=r.y+r.h/2;
 canvas.fillSmoothRoundRect(r.x,r.y,r.w,r.h,9,style::card);canvas.drawRoundRect(r.x,r.y,r.w,r.h,9,style::edge);
 uint16_t color=wifiBars>=3?style::green:style::amber;
 for(unsigned i=0;i<4;++i){int h=(i+1)*6;canvas.fillRoundRect(x-14+i*8,y+12-h,5,h,1,i<wifiBars?color:style::edge);}
 if(!wifiLinked){stroke(x-16,y-16,x+16,y+16,style::amber,1.7f);}
}
// Tab5 uses a 2S pack and INA226; M5Unified's percentage is voltage-based.
// Read on the UI task (same I2C owner as RTC), not in the network worker.
int batteryPercent=-1,batteryMillivolts=0,batteryMilliamps=0;
bool batteryCharging=false,batterySampled=false;
uint32_t batterySampleAt=0;
void drawBatteryPower(){
 if(!batterySampled||uint32_t(millis()-batterySampleAt)>=5000){
  batterySampled=true;batterySampleAt=millis();
  batteryMillivolts=M5.Power.getBatteryVoltage();batteryMilliamps=M5.Power.getBatteryCurrent();
  int level=M5.Power.getBatteryLevel();
  batteryPercent=batteryMillivolts>=6000&&batteryMillivolts<=8800&&level>=0&&level<=100?level:-1;
  batteryCharging=batteryPercent>=0&&batteryMilliamps>10&&M5.Power.isCharging()==m5::Power_Class::is_charging_t::is_charging;
  static bool logged=false;if(!logged){logged=true;Serial.printf("Power indicator: pack=%dmV current=%dmA estimate=%d charging=%u\n",batteryMillivolts,batteryMilliamps,batteryPercent,batteryCharging);}
 }
 auto r=tablayout::battery;
 canvas.fillSmoothRoundRect(r.x,r.y,r.w,r.h,9,style::card);canvas.drawRoundRect(r.x,r.y,r.w,r.h,9,style::edge);
 uint16_t color=batteryPercent<0?style::muted:batteryCharging?style::cyan:batteryPercent<=15?style::red:batteryPercent<=30?style::amber:style::green;
 int x=r.x+31,y=r.y+7;
 canvas.drawRoundRect(x,y,34,18,3,color);canvas.fillRoundRect(x+34,y+5,3,8,1,color);
 if(batteryPercent>=0){int fill=(batteryPercent*28+99)/100;if(fill)canvas.fillRect(x+3,y+3,fill,12,color);}
 else label(x+17,y+9,"?",Type::Small,color,0,lgfx::textdatum_t::middle_center);
 if(batteryCharging){canvas.fillTriangle(x-12,y,x-19,y+10,x-11,y+10,color);canvas.fillTriangle(x-11,y+7,x-18,y+18,x-18,y+7,color);}
 label(r.x+r.w/2,r.y+30,batteryPercent>=0?String("~")+String(batteryPercent)+"%":String("Power --"),Type::Small,color,r.w-8,lgfx::textdatum_t::top_center);
}
void beginFrame() {
 if(!canvasReady){canvas.setColorDepth(16);canvas.setPsram(true);if(!canvas.createSprite(1280,720)){line(30,"Display buffer allocation failed",TFT_RED);for(;;)delay(1000);}canvasReady=true;}
 canvas.fillScreen(nightMode?TFT_BLACK:style::bg);canvas.setTextDatum(lgfx::textdatum_t::top_left);
}
void endFrame(){canvas.pushSprite(0,0);}
void notice(const String &value){uiNotice=value;noticeAt=millis();}
String glucose(float value){return std::isfinite(value)?String(mmol?value/18.0182f:value,mmol?1:0):String("--");}
void drawTrend(const char *direction,int cx,int cy,uint16_t color) {
 auto a=trend::arrow(direction);if(!a.count){label(cx,cy,"?",Type::Status,color,0,lgfx::textdatum_t::middle_center);return;}
 for(unsigned k=0;k<a.count;++k){
  float center=cx+(a.count==2?(k?11.f:-11.f):0.f),nx=-a.dy,ny=a.dx;
  float tx=center+a.dx*24,ty=cy+a.dy*24,bx=tx-a.dx*15,by=ty-a.dy*15,sx=center-a.dx*24,sy=cy-a.dy*24;
  auto p=[](float v){return int(std::lround(v));};
  canvas.fillTriangle(p(sx+nx*3),p(sy+ny*3),p(sx-nx*3),p(sy-ny*3),p(bx+nx*3),p(by+ny*3),color);
  canvas.fillTriangle(p(sx-nx*3),p(sy-ny*3),p(bx+nx*3),p(by+ny*3),p(bx-nx*3),p(by-ny*3),color);
  float head=a.count==2?8.f:11.f;canvas.fillTriangle(p(tx),p(ty),p(bx+nx*head),p(by+ny*head),p(bx-nx*head),p(by-ny*head),color);
 }
}
void legend(int x,const String &value,uint16_t color){stroke(x,651,x+16,651,color,1.5f);label(x+24,640,value,Type::Small,style::muted);}
void markerLegend(int x,const String &value,uint16_t color){canvas.fillTriangle(x+8,645,x+2,656,x+14,656,color);label(x+24,640,value,Type::Small,style::muted);}
void drawHeaderClock() {
 time_t at=time(nullptr);tm local={};bool valid=clockdisplay::valid(at);
 if(valid)localtime_r(&at,&local);
 char clock[12]={},date[32]={};clockdisplay::format(local,valid,clock,sizeof(clock),date,sizeof(date),clock12);
 label(640,9,clock,Type::Status,valid?style::ink:style::muted,220,lgfx::textdatum_t::top_center);
 label(640,45,date,Type::Small,style::muted,250,lgfx::textdatum_t::top_center);
}
String clockText(int64_t t){time_t tt=time_t(t);tm local={};localtime_r(&tt,&local);char v[16];clockdisplay::formatTime(local,v,sizeof(v),clock12);return String(v);}
String unitsText(float units){
 String v=amount(units,units<1?2:1);while(v.indexOf('.')>=0&&v.endsWith("0"))v.remove(v.length()-1);if(v.endsWith("."))v.remove(v.length()-1);return v+"U";
}
// SMB doses summed into fixed local-clock buckets (about 12 per view). Triangles keep exact times.
void buildSmbBuckets(graphtime::Buckets &out,int64_t start,int64_t end){
 out.build(start,end,graphtime::bucketSize(graphHours.load()));
 for(size_t i=0;i<ui->treatments.count;++i){const auto &e=ui->treatments.events[i];if(e.t>=start&&e.t<=end&&smblabels::needsLabel(e.smb,e.insulin))out.add(e.t,e.insulin);}
}
void drawSmbStrip(const graphtime::Buckets &b,int64_t start,int64_t end,int plotBottom){
 canvas.drawFastHLine(tabgraph::left,plotBottom,tabgraph::right-tabgraph::left,style::edge);
 label(76,plotBottom+26,"SMB",Type::Small,style::muted,64,lgfx::textdatum_t::middle_right);
 const float peak=b.maximum();if(peak<=0)return;
 for(size_t i=0;i<b.size;++i){
  const auto &k=b.items[i];if(!k.count)continue;
  int x0=std::max(tabgraph::left,tabgraph::x(k.from,start,end)),x1=std::min(tabgraph::right,tabgraph::x(k.to,start,end));
  if(x1-x0<12)continue;
  int h=std::max(3,int(std::lround(12.f*k.units/peak)));
  canvas.fillRoundRect(x0+3,tabgraph::bottom-h,x1-x0-6,h,2,style::rgb(0x2A7F90));
  label((x0+x1)/2,plotBottom+26,unitsText(k.units),Type::Small,style::cyan,x1-x0-4,lgfx::textdatum_t::middle_center);
 }
}
bool uploaderFresh(const model::Status &s,int64_t now){return std::isfinite(s.uploaderBattery)&&model::fresh(s.uploaderTime,now,uiSettings.apsStale);}
uint16_t careColor(careage::Level level){return level==careage::Level::Urgent?style::red:level==careage::Level::Warn?style::amber:level==careage::Level::Unknown?style::muted:style::ink;}
// Loop card right column: AAPS phone battery, then cannula / sensor / insulin age.
void drawLoopSide(const model::Status &s,const model::CareAges &care,int64_t now){
 const int x=1134,right=1244;
 canvas.drawFastVLine(1122,90,120,style::edge);
 const bool fresh=uploaderFresh(s,now);const int pct=fresh?int(std::lround(s.uploaderBattery)):-1;
 const uint16_t c=!fresh?style::muted:s.uploaderCharging?style::cyan:pct<=15?style::red:pct<=30?style::amber:style::green;
 canvas.drawRoundRect(x,86,14,24,3,c);canvas.drawFastHLine(x+4,88,6,c);
 if(pct>=0){int h=(pct*16+50)/100;if(h>0)canvas.fillRect(x+3,107-h,8,h,c);}
 label(x+22,98,pct>=0?String(pct)+"%":String("--"),Type::Small,c,56,lgfx::textdatum_t::middle_left);
 if(fresh&&s.uploaderCharging){canvas.fillTriangle(right-4,86,right-11,99,right-5,99,c);canvas.fillTriangle(right-6,96,right,96,right-8,110,c);}
 struct Row{const char *name;int64_t t;careage::Limits limits;};
 const Row rows[]={{"CAGE",care.cannula,careage::CANNULA},{"SAGE",care.sensor,careage::SENSOR},{"IAGE",care.insulin,careage::INSULIN}};
 for(size_t i=0;i<3;++i){
  const int y=124+int(i)*28;char value[16];careage::format(rows[i].t,now,value,sizeof(value));
  label(x,y,rows[i].name,Type::Small,style::muted);
  label(right,y,value,Type::Small,careColor(careage::level(rows[i].t,now,rows[i].limits)),70,lgfx::textdatum_t::top_right);
 }
}
String loopSideDetail(){
 const int64_t now=displayNow();const auto &s=ui->status;const auto &care=ui->care;char cage[16],sage[16],iage[16];
 careage::format(care.cannula,now,cage,sizeof(cage));careage::format(care.sensor,now,sage,sizeof(sage));careage::format(care.insulin,now,iage,sizeof(iage));
 String phone=uploaderFresh(s,now)?String(int(std::lround(s.uploaderBattery)))+"%"+(s.uploaderCharging?" charging":"")+" ("+age(s.uploaderTime,now)+" ago)":String("unavailable");
 return String("Phone ")+phone+" / Cannula "+cage+" / Sensor "+sage+" / Insulin "+iage;
}
void draw() {
 beginFrame();const int64_t now=displayNow();const auto &h=ui->history;const auto &s=ui->status;
 bool fresh=h.count&&model::fresh(h.p[h.count-1].t,now,uiSettings.sgvStale),loopFresh=model::fresh(s.loopTime,now,uiSettings.apsStale);
 float latest=h.count?h.p[h.count-1].sgv:NAN;
 uint16_t c=!fresh?style::amber:latest<uiSettings.low?style::red:latest>uiSettings.high?style::amber:style::green;
 label(24,14,"Nightscout",Type::Status);label(244,27,"Glucose dashboard",Type::Small,style::muted);
 drawHeaderClock();drawBatteryPower();drawWifiSignal();icon(tablayout::headerSound,glucoseAlerts.isEnabled()?Icon::SoundOn:Icon::SoundOff,glucoseAlerts.isEnabled());
 icon(tablayout::forecast,Icon::Forecast,showForecast);icon(tablayout::refresh,Icon::Refresh);icon(tablayout::night,nightMode?Icon::Moon:Icon::Sun,nightMode);icon(tablayout::settings,Icon::Settings);
 panel(24,76,430,148,!fresh||latest<uiSettings.low||latest>uiSettings.high?c:style::edge);
 panel(470,76,230,148);panel(716,76,230,148);panel(962,76,294,148);
 String heading=!h.count?"GLUCOSE":!fresh?"GLUCOSE / STALE":latest<uiSettings.low?"GLUCOSE / LOW":latest>uiSettings.high?"GLUCOSE / HIGH":"GLUCOSE";
 label(44,88,heading,Type::Small,!fresh?c:style::muted);
 label(40,112,glucose(latest),Type::Hero,c);label(230,166,mmol?"mmol/L":"mg/dL",Type::Small,style::muted);
 if(h.count)drawTrend(h.p[h.count-1].direction,392,153,fresh?c:style::muted);
 label(44,198,h.count?String(fresh?"Updated ":"Last reading ")+age(h.p[h.count-1].t,now)+" ago":"Waiting for data",Type::Small,style::muted,185);
 if(h.count>1){float delta=latest-h.p[h.count-2].sgv;label(245,198,String("Delta ")+(delta>0?"+":"")+glucose(delta),Type::Small,style::muted,190);}
 auto metric=[&](const model::Metric &m,unsigned digits){return model::fresh(m.t,now,uiSettings.apsStale)?amount(m.value,digits):String("--");};
 label(490,90,"IOB",Type::Body,style::muted);label(490,130,metric(s.iob,2),Type::Metric,style::ink,160);label(660,153,"U",Type::Small,style::muted);
 String basal=model::fresh(s.basal.t,now,uiSettings.apsStale)&&(!s.tempUntil||now<=s.tempUntil)?"Basal "+amount(s.basal.value)+" U/h":"Basal unavailable";
 label(490,198,basal,Type::Small,style::muted,190);
 label(736,90,"COB",Type::Body,style::muted);label(736,130,metric(s.cob,0),Type::Metric,style::ink,160);label(906,153,"g",Type::Small,style::muted);
 label(736,198,String("Pump: ")+s.pump,Type::Small,style::muted,190);
 label(982,90,"LOOP",Type::Body,style::muted);label(982,128,loopFresh?"Recent":"Stale",Type::Status,loopFresh?style::green:style::amber,136);
 label(982,176,s.loopTime?String("Updated ")+age(s.loopTime,now)+" ago":"Waiting for loop data",Type::Small,style::muted,136);
 label(982,201,s.activity,Type::Small,style::muted,136);
 drawLoopSide(s,ui->care,now);
 panel(24,240,1232,426);label(44,258,"Glucose history",Type::Heading);label(260,264,String(graphHours.load())+" h / "+(historyOffset?"earlier":"live"),Type::Small,style::muted,220);
 for(int i=0;i<4;++i)button(tablayout::ranges[i],String(tabgraph::range(i))+"h",graphHours.load()==tabgraph::range(i));
 icon(tablayout::earlier,Icon::Left);icon(tablayout::later,Icon::Right);button(tablayout::live,historyOffset?"Return to live":"Live",!historyOffset);
 int64_t end=now-historyOffset+(showForecast&&!historyOffset?3600:0),start=now-historyOffset-int64_t(graphHours.load())*3600;
 // Scale to what this view actually shows: visible readings plus any forecast lines drawn.
 const bool forecastShown=showForecast&&loopFresh&&model::fresh(s.forecast.t,now,uiSettings.apsStale);
 graphscale::Range range;
 for(size_t i=0;i<h.count;++i)if(h.p[i].t>=start&&h.p[i].t<=end)range.add(h.p[i].sgv);
 if(forecastShown)for(size_t k=0;k<model::PRED_LINES;++k)for(size_t i=0;i<s.forecast.count[k];++i){int64_t at=s.forecast.t+int64_t(i)*300;if(at>=start&&at<=end)range.add(s.forecast.bg[k][i]);}
 const auto bounds=range.bounds();const float low=bounds.low,high=bounds.high;
 const int plotBottom=tabgraph::bottom-tabgraph::insulinStrip;
 auto px=[&](int64_t t){return tabgraph::x(t,start,end);};auto py=[&](float v){return tabgraph::y(v,low,high,plotBottom);};
 int bandTop=py(uiSettings.high),bandBottom=py(uiSettings.low);
 canvas.fillRect(tabgraph::left,bandTop,tabgraph::right-tabgraph::left,bandBottom-bandTop,nightMode?style::rgb(0x081B17):style::band);
 const float unit=mmol?18.0182f:1.f,gridStep=graphscale::gridStep((high-low)/unit)*unit;
 for(float v=std::ceil(low/gridStep-0.001f)*gridStep;v<=high+0.01f;v+=gridStep){int y=py(v);canvas.drawFastHLine(tabgraph::left,y,tabgraph::right-tabgraph::left,style::grid);label(76,y,glucose(v),Type::Small,style::muted,64,lgfx::textdatum_t::middle_right);}
 // Round local-clock ticks (e.g. every 3 h at 12h, 6 h at 24h) instead of arbitrary fractions of the window.
 const int64_t tickStep=graphtime::tickStep(graphHours.load());
 for(int64_t at=graphtime::alignUp(start,tickStep);at<=end;at=graphtime::alignUp(at+1,tickStep)){int x=px(at);canvas.drawFastVLine(x,tabgraph::top,tabgraph::bottom-tabgraph::top,style::grid);label(std::max(112,std::min(x,1208)),608,clockText(at),Type::Small,style::muted,0,lgfx::textdatum_t::top_center);}
 for(size_t i=0;i<h.count;++i){const auto &p=h.p[i];if(p.t<start||p.t>end)continue;uint16_t point=p.sgv<uiSettings.low?style::red:p.sgv>uiSettings.high?style::amber:style::green;if(!fresh)point=style::muted;
  if(i&&p.t-h.p[i-1].t<=600&&h.p[i-1].t>=start)stroke(px(h.p[i-1].t),py(h.p[i-1].sgv),px(p.t),py(p.sgv),point,1.5f);
  canvas.fillSmoothCircle(px(p.t),py(p.sgv),3,point);
 }
 const uint16_t colors[]={style::cyan,style::amber,style::rgb(0xDDA6FF),style::rgb(0x86A9FF)};
 if(forecastShown)for(size_t k=0;k<model::PRED_LINES;++k)for(size_t i=1;i<s.forecast.count[k];++i){int64_t at=s.forecast.t+i*300,prev=at-300;if(prev<start||at>end||!std::isfinite(s.forecast.bg[k][i-1])||!std::isfinite(s.forecast.bg[k][i]))continue;stroke(px(prev),py(s.forecast.bg[k][i-1]),px(at),py(s.forecast.bg[k][i]),colors[k],0.8f);}
 static graphtime::Buckets smbBuckets;buildSmbBuckets(smbBuckets,start,end);
 for(size_t i=0;i<ui->treatments.count;++i){
  const auto &e=ui->treatments.events[i];if(e.t<start||e.t>end)continue;int x=px(e.t);
  uint16_t marker=e.smb?style::cyan:e.carbs>0?style::amber:colors[2];
  canvas.fillTriangle(x,plotBottom+3,x-5,plotBottom+13,x+5,plotBottom+13,marker);
 }
 font(Type::Small);
 drawSmbStrip(smbBuckets,start,end,plotBottom);
 if(uiNotice.length()&&uint32_t(millis()-noticeAt)<4000)label(44,640,uiNotice,Type::Small,style::cyan,1180);
 else if(selection.length())label(44,640,selection,Type::Small,style::cyan,1180);
 else {if(showForecast){legend(44,"IOB forecast",colors[0]);legend(208,"COB",colors[1]);legend(310,"UAM",colors[2]);legend(420,"ZT",colors[3]);}else label(44,640,"Forecast hidden",Type::Small,style::muted);markerLegend(570,"Carbs",style::amber);markerLegend(690,"Insulin",colors[2]);markerLegend(812,"SMB",style::cyan);label(1232,640,"Tap to inspect / swipe to move",Type::Small,style::muted,340,lgfx::textdatum_t::top_right);}
 String status=liveOnline.load()?"Nightscout connected / Live updates":liveUpdatesEnabled.load()&&uiSettings.v3?"Nightscout connected / Polling (live reconnecting)":"Nightscout connected / Polling";uint16_t statusColor=style::muted;
 if(WiFi.status()!=WL_CONNECTED){status="Wi-Fi disconnected";statusColor=style::amber;}
 else if(ui->glucoseError[0] && (strcmp(ui->glucoseError,"SGV history limited") || !h.count || h.p[0].t>start+600)){status=!strcmp(ui->glucoseError,"SGV history limited")?String("Earlier glucose history unavailable"):String("Glucose: ")+ui->glucoseError;statusColor=style::amber;}
 else if(ui->statusError[0]){status=String("Loop data: ")+ui->statusError;statusColor=style::amber;}
 else if(ui->treatmentError[0]){status=String("Treatments: ")+ui->treatmentError;statusColor=style::amber;}
 if(ui->treatments.limited){status+=" / Treatment history truncated";statusColor=style::amber;}
 canvas.fillCircle(32,695,4,statusColor);label(46,683,status,Type::Small,statusColor,1190);
 M5.Display.setBrightness(nightMode?8:uiSettings.brightness);endFrame();
}
void drawAudioSettings(){
 beginFrame();label(24,16,audioPickerAlert<alertaudio::AlertCount?String("Sound: ")+alertaudio::alertName(audioPickerAlert):String("Alert sounds"),Type::Status);icon(tablayout::back,Icon::Back);
 if(audioPickerAlert<alertaudio::AlertCount){
  label(24,88,"Choose a sound. Intensity applies to Acoustic / Electronic variants.",Type::Body,style::muted,1232);
  for(unsigned i=0;i<alertaudio::ChoiceCount;i++){
   int x=24+int(i%4)*312,y=142+int(i/4)*96;bool active=audioSettings.choice[audioPickerAlert]==i;
   button({x,y,296,76},"",active);String name=alertaudio::choiceName(i);int split=name.indexOf(" / ");
   if(split<0)label(x+148,y+38,name,Type::Body,active?style::cyan:style::ink,280,lgfx::textdatum_t::middle_center);
   else{label(x+148,y+13,name.substring(0,split),Type::Body,active?style::cyan:style::ink,280,lgfx::textdatum_t::top_center);label(x+148,y+45,name.substring(split+3),Type::Small,style::muted,280,lgfx::textdatum_t::top_center);}
  }
  label(24,650,"Low soon / Urgent low are sound names here; they do not add new alarm triggers.",Type::Small,style::muted,1232);
 }else{
  button({864,10,316,52},"Speaker check");
  label(24,92,"Intensity",Type::Heading);
  for(unsigned i=0;i<3;i++)button({336+int(i)*312,78,296,64},alertaudio::intensityName(i),audioSettings.intensity==i);
  for(unsigned i=0;i<4;i++){
   int y=170+i*96;label(24,y+21,alertaudio::alertName(i),Type::Heading,style::ink,280);
   button({336,y,656,70},alertaudio::choiceName(audioSettings.choice[i]));button({1012,y,244,70},"Preview");
  }
  label(24,562,"Eight extra tones have one recording each; intensity does not change those tones.",Type::Small,style::muted,1232);
  button({24,604,256,62},glucoseAlerts.isEnabled()?"Sound: On":"Sound: Muted",glucoseAlerts.isEnabled());
  button({336,604,100,62},"-");label(640,621,String("Volume ")+String((alerts::alarmVolume(uiSettings.volume)*100+127)/255)+"%"+(alerts::alarmVolume(uiSettings.volume)<=alerts::MIN_ALERT_VOLUME?" (min)":""),Type::Body,style::ink,300,lgfx::textdatum_t::top_center);button({844,604,100,62},"+");button({1012,604,244,62},"Stop preview");
 }
 if(*settingsMessage)label(24,690,settingsMessage,Type::Small,style::amber,1232);endFrame();
}
void audioSettingsTouch(int x,int y){
 using tablayout::Rect;
 if(tablayout::back.contains(x,y)){
  if(previewSoundActive){M5.Speaker.stop();previewSoundActive=false;}
  if(audioPickerAlert<alertaudio::AlertCount)audioPickerAlert=alertaudio::AlertCount;else audioSettingsOpen=false;return;
 }
 if(audioPickerAlert<alertaudio::AlertCount){
  for(unsigned i=0;i<alertaudio::ChoiceCount;i++)if(Rect{24+int(i%4)*312,142+int(i/4)*96,296,76}.contains(x,y)){
   auto next=audioSettings;next.choice[audioPickerAlert]=i;
   if(saveAudioSettings(next)){audioPickerAlert=alertaudio::AlertCount;settingsMessage[0]=0;}else strlcpy(settingsMessage,"Could not save sound",sizeof(settingsMessage));return;
  }return;
 }
 if(Rect{864,10,316,52}.contains(x,y)){
  if(!glucoseAlerts.isEnabled()){strlcpy(settingsMessage,"Enable sound first",sizeof(settingsMessage));return;}
  if(M5.Speaker.isPlaying()&&!previewSoundActive){strlcpy(settingsMessage,"An active alert takes priority",sizeof(settingsMessage));return;}
  M5.Speaker.stop();previewSoundActive=prepareAlertSpeaker()&&M5.Speaker.tone(1000,400);logSpeakerStatus();strlcpy(settingsMessage,previewSoundActive?"Playing speaker check":"Speaker check failed",sizeof(settingsMessage));return;
 }
 for(unsigned i=0;i<3;i++)if(Rect{336+int(i)*312,78,296,64}.contains(x,y)){
  auto next=audioSettings;next.intensity=i;if(saveAudioSettings(next))settingsMessage[0]=0;else strlcpy(settingsMessage,"Could not save intensity",sizeof(settingsMessage));return;
 }
 for(unsigned i=0;i<4;i++){
  int row=170+i*96;
  if(Rect{336,row,656,70}.contains(x,y)){audioPickerAlert=i;return;}
  if(Rect{1012,row,244,70}.contains(x,y)){
   if(!glucoseAlerts.isEnabled()){strlcpy(settingsMessage,"Enable sound to preview",sizeof(settingsMessage));return;}
   if(M5.Speaker.isPlaying()&&!previewSoundActive){strlcpy(settingsMessage,"An active alert takes priority over previews",sizeof(settingsMessage));return;}
   if(previewSoundActive)M5.Speaker.stop();previewSoundActive=playAlertClip(i);
   strlcpy(settingsMessage,previewSoundActive?"Playing preview":"Could not start preview",sizeof(settingsMessage));return;
  }
 }
 if(Rect{24,604,256,62}.contains(x,y)){toggleGlucoseAlerts();return;}
 if(Rect{1012,604,244,62}.contains(x,y)){if(previewSoundActive){M5.Speaker.stop();previewSoundActive=false;}settingsMessage[0]=0;return;}
 if(Rect{336,604,100,62}.contains(x,y)||Rect{844,604,100,62}.contains(x,y)){
  runtimecfg::Settings next;uint32_t rev;currentSettings(next,rev);unsigned effective=alerts::alarmVolume(next.volume);next.volume=x<500?std::max(int(alerts::MIN_ALERT_VOLUME),int(effective)-16):std::min(255u,effective+16);storeSettings(next,rev,settingsMessage,sizeof(settingsMessage));applyUiSettings(true);
 }
}
void drawSettings() {
 if(audioSettingsOpen){drawAudioSettings();return;}
 M5.Display.setBrightness(std::max(20u,uiSettings.brightness));beginFrame();label(24,16,"Settings",Type::Status);button({252,10,384,52},"Alert sounds");icon(tablayout::back,Icon::Back);button({672,10,508,52},liveUpdatesEnabled.load()?"Live updates: On":"Live updates: Off",liveUpdatesEnabled.load());
 button(tablayout::sound,String("Sound alerts: ")+(glucoseAlerts.isEnabled()?"On":"Off"),glucoseAlerts.isEnabled());button(tablayout::units,mmol?"Units: mmol/L":"Units: mg/dL");button(tablayout::clockFormat,clock12?"Clock: 12 hour":"Clock: 24 hour");button(tablayout::rotation,rotated180?"Rotation: 180 deg":"Rotation: 0 deg",rotated180);
 panel(24,210,596,128);label(52,228,"Display brightness",Type::Heading);label(590,228,String((uiSettings.brightness*100+127)/255)+"%",Type::Body,style::muted,100,lgfx::textdatum_t::top_right);
 icon(tablayout::dimmer,Icon::Minus);icon(tablayout::brighter,Icon::Plus);canvas.fillRoundRect(136,288,372,5,2,style::edge);canvas.fillRoundRect(136,288,std::max(4u,uiSettings.brightness*372/255),5,2,style::cyan);
 panel(660,210,596,128);label(688,228,"Wi-Fi",Type::Heading);button(tablayout::reconnect,"Reconnect");button(tablayout::wifiSetup,"Setup hotspot");
 panel(24,360,1232,248);label(52,382,"LAN configuration portal",Type::Heading);button(tablayout::portal,lanRequested.load()?"Disable":"Enable",lanRequested.load());label(52,424,"Off after restart. Use only on your local network.",Type::Body,style::muted,1150);
 int state=portalState.load();
 if(state>=1&&state<=5){
  label(52,468,String("Setup Wi-Fi: ")+portalName,Type::Body,style::cyan,1150);label(52,510,String("Password: ")+portalPassword,Type::Body,style::ink,1150);label(52,561,"Open http://192.168.4.1 on your phone. Back cancels setup.",Type::Small,style::muted,1150);
 }else if(lanRequested.load()){
  label(52,476,lanStatus.state==1?String(lanStatus.address):String("Waiting for Wi-Fi..."),Type::Heading,style::cyan,760);label(920,465,"Access code",Type::Small,style::muted);label(920,503,lanStatus.code,Type::Metric,style::ink,300);
 }else label(52,486,"Enable to edit Nightscout, Wi-Fi, alerts and time settings.",Type::Body,style::muted,1150);
 label(24,642,"Glucose units, thresholds and freshness follow your saved settings.",Type::Body,style::muted,1220);if(*settingsMessage)label(24,690,settingsMessage,Type::Small,style::amber,1220);endFrame();
}
void tabTouch(int x,int y) {
 using namespace tablayout;
 if(settingsOpen){
  if(audioSettingsOpen){audioSettingsTouch(x,y);return;}
  if(Rect{252,10,384,52}.contains(x,y)){audioSettingsOpen=true;audioPickerAlert=alertaudio::AlertCount;settingsMessage[0]=0;return;}
  if(back.contains(x,y)){settingsOpen=false;portalCancel.store(true);return;}
  if(Rect{672,10,508,52}.contains(x,y)){
   bool next=!liveUpdatesEnabled.load();Preferences saved;bool ok=false;
   if(saved.begin("ns-tab5",false)){ok=saved.putBool("live-updates",next)==1;saved.end();}
   if(ok){liveUpdatesEnabled.store(next);settingsMessage[0]=0;xTaskNotifyGive(networkTask);}
   else strlcpy(settingsMessage,"Could not save live updates",sizeof(settingsMessage));
  }
  else if(sound.contains(x,y))toggleGlucoseAlerts();
  else if(units.contains(x,y)){mmol=!mmol;saveDisplaySettings();}
  else if(clockFormat.contains(x,y)){
   Preferences saved;bool ok=false;
   if(saved.begin("ns-tab5",false)){ok=saved.putBool("clock-12",!clock12)==1;saved.end();}
   if(ok){clock12=!clock12;settingsMessage[0]=0;}
   else strlcpy(settingsMessage,"Could not save clock format",sizeof(settingsMessage));
  }
  else if(rotation.contains(x,y)){
   Preferences saved;bool ok=false;
   if(saved.begin("ns-tab5",false)){ok=saved.putBool("rotate-180",!rotated180)==1;saved.end();}
   if(ok){
    rotated180=!rotated180;
    // M5Unified obtains touch coordinates through this display's rotation transform.
    M5.Display.setRotation(rotated180?3:1);rendered=false;settingsMessage[0]=0;
   }else strlcpy(settingsMessage,"Could not save rotation",sizeof(settingsMessage));
  }
  else if(dimmer.contains(x,y)||brighter.contains(x,y)){runtimecfg::Settings next;uint32_t rev;currentSettings(next,rev);next.brightness=dimmer.contains(x,y)?std::max(10,int(next.brightness)-20):std::min(255u,next.brightness+20);storeSettings(next,rev,settingsMessage,sizeof(settingsMessage));}
  else if(portal.contains(x,y)){lanRequested.store(!lanRequested.load());lanStatus=LanStatus{};}
  else if(wifiSetup.contains(x,y)){portalCancel.store(false);portalState.store(1);portalRequested.store(true);xTaskNotifyGive(networkTask);}
  else if(reconnect.contains(x,y)){reconnectRequested.store(true);xTaskNotifyGive(networkTask);}
  return;
 }
 if(loopSide.contains(x,y)){notice(loopSideDetail());return;}
 if(battery.contains(x,y)){
  notice(batteryPercent<0?String("Battery reading unavailable / external input is not measured"):String("Battery ~")+String(batteryPercent)+"% / "+String(batteryMillivolts/1000.f,2)+" V / "+(batteryCharging?"Charging":batteryMilliamps < -10?"Discharging":"Idle")+" (voltage estimate)");return;
 }
 if(wifi.contains(x,y)){notice(!wifiLinked?String("Wi-Fi disconnected"):wifiBars?String("Wi-Fi: ")+wifisignal::quality(wifiBars)+" / "+String(wifiRssi)+" dBm":String("Wi-Fi signal unavailable"));return;}
 if(headerSound.contains(x,y)){toggleGlucoseAlerts();notice(glucoseAlerts.isEnabled()?"Sound alerts on":"Sound alerts muted");return;}
 if(settings.contains(x,y)){settingsOpen=true;return;}
 if(forecast.contains(x,y)){showForecast=!showForecast;saveDisplaySettings();notice(showForecast?"Forecast shown":"Forecast hidden");return;}
 if(refresh.contains(x,y)){xTaskNotifyGive(networkTask);notice("Refreshing Nightscout...");return;}
 if(night.contains(x,y)){nightMode=!nightMode;Preferences tab;if(tab.begin("ns-tab5",false)){tab.putBool("night",nightMode);tab.end();}notice(nightMode?"Night mode":"Day mode");return;}
 for(int i=0;i<4;++i)if(ranges[i].contains(x,y)){graphHours.store(tabgraph::range(i));historyOffset=std::min(historyOffset,int64_t(86400-graphHours.load()*3600));saveDisplaySettings();selection="";return;}
 if(live.contains(x,y)){historyOffset=0;selection="";return;}
 if(earlier.contains(x,y)){historyOffset=std::min(int64_t(86400-graphHours.load()*3600),historyOffset+3600);selection="";return;}
 if(later.contains(x,y)){historyOffset=std::max(int64_t(0),historyOffset-3600);selection="";return;}
 if(y>=tabgraph::top&&y<=tabgraph::bottom&&x>=tabgraph::left&&x<=tabgraph::right){
  int64_t now=displayNow(),start=now-historyOffset-int64_t(graphHours.load())*3600,end=now-historyOffset+(showForecast&&!historyOffset?3600:0);
  int64_t at=start+(end-start)*(x-tabgraph::left)/(tabgraph::right-tabgraph::left),best=INT64_MAX;selection="No glucose data at this time";
  if(y>=tabgraph::bottom-tabgraph::insulinStrip){
   static graphtime::Buckets touched;buildSmbBuckets(touched,start,end);const auto *k=touched.find(at);
   if(!k)selection="No SMB bucket here";
   else{String span=clockText(k->from)+" - "+clockText(k->to);selection=k->count?String("SMB ")+span+": "+String(k->count)+(k->count==1?" dose / ":" doses / ")+unitsText(k->units):String("No SMBs ")+span;}
   return;
  }
  for(size_t i=0;i<ui->history.count;++i){auto &p=ui->history.p[i];int64_t d=llabs(p.t-at);if(d<best){best=d;selection=glucose(p.sgv)+(mmol?" mmol/L / ":" mg/dL / ")+age(p.t,now)+" ago";}}
  for(size_t i=0;i<ui->treatments.count;++i){auto &e=ui->treatments.events[i];if(llabs(e.t-at)<(end-start)/40){selection=String(e.smb?"SMB: ":"Treatment: ")+amount(e.insulin)+" U / "+amount(e.carbs,0)+" g / "+age(e.t,now)+" ago";break;}}
 }
}

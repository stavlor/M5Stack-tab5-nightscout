#pragma once
#include <string>
#include "live_protocol.h"
#include "audio_settings.h"
#include "audio_catalog.h"
#include "tab5_graph.h"
#include "trend.h"
#include "wifi_signal.h"
#include "clock_display.h"
#include "model.h"
#include "body_buffer.h"
#include "treatments.h"
#include "alerts.h"
#include "power_state.h"
#include "ns_api.h"
#include "smb_labels.h"
#include "graph_scale.h"
#include "graph_time.h"
#include "care_age.h"
#include "pump_status.h"
#include "quick_settings.h"
#include "runtime_settings.h"
#include "tls_diagnostics.h"
#include "tls_anchor_policy.h"
// Runs on the same ESP32 framework and ArduinoJson version as production.
inline int runParserTests() {
  unsigned passed=0,failed=0;
  auto check=[&](bool b,const char *name){Serial.printf("%s %s\n",b?"PASS":"FAIL",name); b?++passed:++failed;};
  DynamicJsonDocument doc(16384);
  const int64_t now=1793102400; // 2026-10-27T12:00:00Z
  static model::History h;
  model::Status s;
  deserializeJson(doc,R"([{"sgv":110,"date":1793102400000,"direction":"Flat"},{"sgv":105,"date":1793102100000},{"sgv":9,"date":1793102399000},{"sgv":"120","date":1793102399000},{"sgv":120,"date":1793103000000},{"sgv":110,"date":1793102400000}])");
  check(model::entries(doc.as<JsonVariantConst>(),now,h) && h.count==2 && h.p[0].sgv==105,"SGV validation, ordering, dedup, future rejection");
  deserializeJson(doc,R"({"date":"2026-10-27T14:00:00.123+02:00","bad":"2026-02-30T12:00:00Z"})");
  check(model::timestamp(doc["date"])==now && !model::timestamp(doc["bad"]),"ISO offset/fraction and calendar validation");
  check(model::fresh(now-600,now,600) && !model::fresh(now-601,now,600),"Stale boundary");
  deserializeJson(doc,R"([{"created_at":"2026-10-27T12:00:00Z","device":"openaps://test","openaps":{"suggested":{"timestamp":"2026-10-27T11:59:00Z","IOB":0,"COB":0},"iob":[{"time":"2026-10-27T11:58:00Z","iob":1.2},{"time":"2026-10-27T12:05:00Z","iob":9}],"enacted":{"timestamp":"2026-10-27T11:59:30Z","received":true}},"pump":{"status":{"timestamp":"2026-10-27T11:58:00Z","status":"normal"},"extended":{"BaseBasalRate":0.8,"TempBasalAbsoluteRate":0,"TempBasalRemaining":10}}}])");
  check(model::statuses(doc.as<JsonVariantConst>(),"openaps://test",now,s) && s.iob.value==0 && s.cob.value==0,"Array IOB fallback and valid zeros");
  check(s.basal.value==0 && s.tempUntil==now+600 && !strcmp(s.basalKind,"Temp"),"Zero temp basal and expiry");
  check(!strcmp(s.activity,"Enacted") && s.loopTime==now-30,"Enact timestamp");
  check(model::statuses(doc.as<JsonVariantConst>(),"other",now,s) && s.uploaded==0,"Uploader isolation");
  deserializeJson(doc,R"([{"created_at":"2026-10-27T12:00:00Z","openaps":{"iob":{"time":"2026-10-27T11:00:00Z","iob":1}},"pump":{"status":{"timestamp":"2026-10-27T11:00:00Z"},"extended":{"BaseBasalRate":1}}}])");
  model::statuses(doc.as<JsonVariantConst>(),"",now,s);
  check(!model::fresh(s.iob.t,now,600) && !model::fresh(s.basal.t,now,600),"Fresh upload cannot revive old metrics");
  deserializeJson(doc,"[]"); model::entries(doc.as<JsonVariantConst>(),now,h); model::statuses(doc.as<JsonVariantConst>(),"",now,s);
  check(!h.count && !s.uploaded,"Empty responses clear cache");
  deserializeJson(doc,"{\"error\":\"unauthorized\"}");
  check(!model::entries(doc.as<JsonVariantConst>(),now,h) && !model::statuses(doc.as<JsonVariantConst>(),"",now,s),"Wrong root rejected");
  check(bool(deserializeJson(doc,"[{broken")),"Malformed JSON rejected");
  DynamicJsonDocument filter(2048);
  model::makeFilter(filter,true);
  auto e=deserializeJson(doc,R"([{"sgv":100,"date":1793102400000,"direction":"Flat","noise":{"anything":[1,2,3]}}])",DeserializationOption::Filter(filter));
  check(!e && model::entries(doc.as<JsonVariantConst>(),now,h) && h.count==1 && doc[0]["noise"].isNull(),"Production glucose JSON filter");
  model::makeFilter(filter,false);
  e=deserializeJson(doc,R"([{"created_at":"2026-10-27T12:00:00Z","openaps":{"suggested":{"timestamp":"2026-10-27T12:00:00Z","COB":0,"predBGs":{"IOB":[1,2,3]}},"iob":{"time":"2026-10-27T12:00:00Z","iob":1.5}},"pump":{"status":{"timestamp":"2026-10-27T12:00:00Z","status":"normal"},"extended":{"BaseBasalRate":0.9}}}])",DeserializationOption::Filter(filter));
  check(!e && model::statuses(doc.as<JsonVariantConst>(),"",now,s) && s.iob.value==1.5f && s.cob.value==0 && s.basal.value==0.9f && doc[0]["openaps"]["suggested"]["predBGs"]["IOB"].size()==3,"Production AAPS filter preserves object IOB and predictions");
  {
    BodyBuffer b(8);
    check(b.append(reinterpret_cast<const uint8_t*>("abc"),3)==3 &&
      b.append(reinterpret_cast<const uint8_t*>("defgh"),5)==5 &&
      b.length==8 && !strcmp(b.data,"abcdefgh"),"Decoded body supports multiple chunks and exact bound");
    check(b.append(reinterpret_cast<const uint8_t*>("x"),1)==0 && b.overflow &&
      b.length==8 && b.append(reinterpret_cast<const uint8_t*>("x"),1)==0,
      "Oversized body is rejected and cannot resume after a retry");
  }
  {
    BodyBuffer b(8,[](size_t)->void*{return nullptr;});
    check(!b.data && b.append(reinterpret_cast<const uint8_t*>("x"),1)==0,
      "Allocation failure does not masquerade as oversized response");
  }
  static model::Treatments treatments;
  model::Forecast predictions;
  deserializeJson(doc,R"([{"_id":"smb","date":1793102340000,"insulin":0.12,"isSMB":true},{"_id":"meal","created_at":"2026-10-27T11:58:00Z","carbs":12.5,"insulin":1.25,"type":"NORMAL"},{"_id":"other","date":1793102300000,"insulin":0.1,"eventType":"Correction Bolus"}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==3 && treatments.events[0].carbs==12.5f && treatments.events[2].smb && !treatments.events[1].smb,"SMB flag, carbs/bolus coexistence and no correction-bolus guessing");
  deserializeJson(doc,R"([{"date":1793102340000,"insulin":0.2,"type":"SMB"},{"date":1793102340000,"carbs":10,"isValid":false},{"date":1793102340000,"carbs":10,"isDeleted":true},{"date":1793102340000,"carbs":"10"},{"date":1793102340000,"carbs":-1},{"date":1793102500000,"carbs":20},{"date":1793002340000,"carbs":20},{"date":1793102340000,"insulin":2,"eventType":"Combo Bolus"}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==1 && treatments.events[0].smb,"SMB type; deleted, invalid, future, out-of-window and extended treatments excluded");
  deserializeJson(doc,R"([{"_id":"same","date":1793102340000,"carbs":10},{"_id":"same","date":1793102340000,"carbs":10},{"_id":"distinct","date":1793102340000,"carbs":10}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==2,"Deduplicate treatment IDs without merging distinct events at the same time");
  deserializeJson(doc,"[]");model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(!treatments.count,"Empty treatment response clears markers");
  deserializeJson(doc,"{}");
  check(!model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments),"Non-array treatment response rejected");
  model::treatmentFilter(filter);
  e=deserializeJson(doc,R"([{"date":1793102340000,"insulin":0.2,"isSMB":true,"notes":"not needed","carbs":5}])",DeserializationOption::Filter(filter));
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(!e && treatments.count==1 && treatments.events[0].smb && treatments.events[0].carbs==5 && doc[0]["notes"].isNull(),"Production treatment filter preserves marker fields and removes notes");
  deserializeJson(doc,R"({"timestamp":"2026-10-27T11:59:00Z","predBGs":{"IOB":[100,"bad",120],"COB":[100,110,130],"UAM":[null,true,-5,601],"ZT":[100,90]}})");
  model::forecast(doc.as<JsonVariantConst>(),now,predictions);
  check(predictions.t==now-60 && predictions.count[0]==3 && std::isnan(predictions.bg[0][1]) && predictions.bg[0][2]==120 && predictions.count[2]==0,"Prediction validation keeps five-minute indices and rejects malformed series");
  deserializeJson(doc,R"({"timestamp":"2026-10-27T12:05:00Z","predBGs":{"IOB":[200,210]}})");
  model::forecast(doc.as<JsonVariantConst>(),now,predictions);
  check(predictions.t==now-60,"Future forecast source cannot replace current predictions");
  check(!model::fresh(predictions.t,now+601,600),"Forecast staleness uses calculation time");
  deserializeJson(doc,R"([{"created_at":"2026-10-27T12:00:00Z","openaps":{"suggested":{"timestamp":"2026-10-27T11:58:00Z","predBGs":{"IOB":[100,110]}},"enacted":{"timestamp":"2026-10-27T11:59:00Z","predBGs":{"COB":[100,120]}}}}])");
  model::statuses(doc.as<JsonVariantConst>(),"",now,s);
  check(s.forecast.t==now-60 && s.forecast.count[1]==2 && s.forecast.count[0]==0,"Choose newest prediction run without mixing calculation sources");
  deserializeJson(doc,R"([{"created_at":"2026-10-27T12:00:00Z","openaps":{}}])");
  model::statuses(doc.as<JsonVariantConst>(),"",now,s);
  check(!s.forecast.t,"Missing predictions clear a previously populated forecast");
  doc.clear();
  JsonObject run=doc.to<JsonObject>();run["timestamp"]=now;
  JsonArray predicted=run["predBGs"]["IOB"].to<JsonArray>();
  for (int i=0;i<70;++i) predicted.add(100+i);
  model::forecast(doc.as<JsonVariantConst>(),now,predictions);
  check(predictions.count[0]==49 && predictions.bg[0][48]==148,"Forecast length bounded to four hours at five-minute steps");
  {
    DynamicJsonDocument many(128*1024);JsonArray events=many.to<JsonArray>();
    for(int i=int(model::MAX_TREATMENTS)+20;i>=0;--i) {
      JsonObject row=events.createNestedObject();row["date"]=now-i*30;row["carbs"]=1;
    }
    model::treatments(many.as<JsonVariantConst>(),now-86400,now,treatments);
    check(!many.overflowed() && treatments.count==model::MAX_TREATMENTS && treatments.limited && treatments.events[0].t==now-(model::MAX_TREATMENTS-1)*30 && treatments.events[model::MAX_TREATMENTS-1].t==now,"Bounded markers keep newest events even for unordered input");
  }
  deserializeJson(doc,R"([{"_id":"p1","date":1793102340000,"carbs":10}])");
  model::treatments(doc.as<JsonVariantConst>(),now-86400,now,treatments,false,false);
  deserializeJson(doc,R"([{"_id":"p1","date":1793102340000,"carbs":10},{"_id":"p2","date":1793102300000,"insulin":1}])");
  model::treatments(doc.as<JsonVariantConst>(),now-86400,now,treatments,true,false);
  check(treatments.count==2 && !treatments.limited,"Paged treatment merge retains earlier pages and deduplicates overlap");
  {
    DynamicJsonDocument page(32*1024);JsonArray rows=page.to<JsonArray>();
    for(int i=0;i<200;++i){JsonObject row=rows.createNestedObject();row["date"]=now-i*60;row["carbs"]=1;}
    model::treatments(page.as<JsonVariantConst>(),now-86400,now,treatments,false,false);
    check(!page.overflowed() && treatments.count==200 && !treatments.limited,"A full intermediate API page is not mislabeled as truncated history");
  }
  check(trend::arrow("Flat").dx==1 && trend::arrow("SingleUp").dy==-1 && trend::arrow("SingleDown").dy==1,"Trend directions map to horizontal and vertical arrows");
  check(trend::arrow("DoubleUp").count==2 && trend::arrow("DoubleDown").count==2 && trend::arrow("FortyFiveUp").dy<0 && trend::arrow("FortyFiveDown").dy>0,"Double and diagonal trends retain their meaning");
  check(!trend::arrow("NONE").count && !trend::arrow(nullptr).count,"Missing trends show unknown instead of an invented direction");
  model::treatmentFilter(filter);
  e=deserializeJson(doc,R"([{"_id":"a","date":1793102340000,"insulin":0.5,"isSMB":true,"pumpId":42,"pumpType":"Pump","pumpSerial":"A"},{"_id":"b","date":1793102341000,"insulin":0.5,"isSMB":true,"pumpId":"42","pumpType":"Pump","pumpSerial":"A"},{"_id":"c","date":1793102342000,"insulin":0.5,"isSMB":true,"pumpId":42,"pumpType":"Pump","pumpSerial":"A"}])",DeserializationOption::Filter(filter));
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(!e && treatments.count==1,"Repeated SMB uploads with one pump identity produce one marker");
  deserializeJson(doc,R"([{"_id":"a","date":1793102340123,"insulin":0.5,"isSMB":true},{"_id":"b","created_at":"2026-10-27T11:59:00.123Z","insulin":0.5,"type":"SMB"},{"_id":"c","date":1793102340123,"insulin":0.5,"isSMB":true}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==1,"SMB duplicate fallback matches exact milliseconds and dose across timestamp formats");
  deserializeJson(doc,R"([{"date":1793102340123,"insulin":0.5,"isSMB":true},{"date":1793102340124,"insulin":0.5,"isSMB":true},{"date":1793102340123,"insulin":0.6,"isSMB":true}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==3,"Distinct SMB milliseconds or amounts are not collapsed");
  deserializeJson(doc,R"([{"date":1793102340123,"insulin":0.5,"isSMB":true,"pumpId":42,"pumpType":"Pump","pumpSerial":"A"},{"date":1793102340123,"insulin":0.5,"isSMB":true,"pumpId":42,"pumpType":"Pump","pumpSerial":"B"}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==2,"SMB pump identifiers are scoped to pump type and serial");
  {
    alerts::State a;
    check(a.poll(200,true,0)==alerts::Event::High,"First fresh high reading requests one chirp");
    a.played(alerts::Event::High,0);
    check(a.poll(250,true,1800000)==alerts::Event::None,"Continuing highs do not repeat even when glucose changes");
    a.poll(180,true,1800100);
    check(a.poll(181,true,1800200)==alerts::Event::High,"Returning to range re-arms the next high episode");
  }
  {
    alerts::State a;
    check(a.poll(69,true,100)==alerts::Event::Low,"First fresh low reading requests one chirp");
    a.played(alerts::Event::Low,100);
    check(a.poll(60,true,100+alerts::LOW_REPEAT_MS-1)==alerts::Event::None &&
      a.poll(65,true,100+alerts::LOW_REPEAT_MS)==alerts::Event::Low,
      "Low reminder fires at 15 minutes, not before");
    a.played(alerts::Event::Low,100+alerts::LOW_REPEAT_MS);
    check(a.poll(60,true,100+alerts::LOW_REPEAT_MS+1)==alerts::Event::None,
      "Successful low reminder restarts its 15-minute interval");
    a.poll(70,true,100+alerts::LOW_REPEAT_MS+2);
    check(a.poll(69,true,100+alerts::LOW_REPEAT_MS+3)==alerts::Event::Low,
      "A new low after an in-range reading chirps immediately");
  }
  {
    alerts::State a;a.poll(200,true,0);a.played(alerts::Event::High,0);
    check(a.poll(100,false,1000)==alerts::Event::None && a.poll(200,true,2000)==alerts::Event::None,
      "Stale data and reconnects do not re-arm an ongoing high");
    check(a.poll(60,true,3000)==alerts::Event::Low,"A direct high-to-low transition chirps for the low");
    a.played(alerts::Event::Low,3000);
    check(a.poll(60,false,3000+alerts::LOW_REPEAT_MS)==alerts::Event::None &&
      a.poll(60,true,3001+alerts::LOW_REPEAT_MS)==alerts::Event::Low,
      "Stale low reminders are silent and resume only with fresh low data");
  }
  {
    alerts::State a;
    check(a.poll(70,true,0)==alerts::Event::None && a.poll(180,true,1)==alerts::Event::None &&
      a.poll(NAN,true,2)==alerts::Event::None && a.poll(9,true,3)==alerts::Event::None,
      "70 and 180 are in range; missing and invalid glucose never chirp");
    check(a.poll(60,true,4)==alerts::Event::Low && a.poll(60,true,5)==alerts::Event::Low,
      "A failed or unacknowledged tone does not consume the first alert");
  }
  {
    alerts::State a;uint32_t start=0xfffffff0u;
    a.poll(60,true,start);a.played(alerts::Event::Low,start);
    check(a.poll(60,true,uint32_t(start+1000u))==alerts::Event::None &&
      a.poll(60,true,uint32_t(start+alerts::LOW_REPEAT_MS))==alerts::Event::Low,
      "Low repeat timing survives the millis counter wrap");
  }
  {
    alerts::State a;a.setEnabled(false);
    check(!a.isEnabled() && a.poll(60,true,0)==alerts::Event::None &&
      a.poll(200,true,1)==alerts::Event::None,"Muted alerts suppress both low and high chirps");
    a.setEnabled(true);bool initialHigh=a.poll(200,true,2)==alerts::Event::High;
    a.played(alerts::Event::High,2);
    check(a.isEnabled() && initialHigh && a.poll(200,true,3)==alerts::Event::None,
      "Re-enabling evaluates current fresh glucose once");
    a.setEnabled(true);
    check(a.poll(200,true,4)==alerts::Event::None,"Setting an already-enabled toggle does not re-arm highs");
    a.setEnabled(false);bool silent=a.poll(60,true,900000)==alerts::Event::None;
    a.setEnabled(true);bool initialLow=a.poll(60,true,900001)==alerts::Event::Low;
    a.played(alerts::Event::Low,900001);
    check(silent && initialLow && a.poll(60,true,900002)==alerts::Event::None,
      "Mute blocks low reminders; re-enable starts a fresh low reminder interval");
  }
  check(powerstate::classify(0x20,5.0f,150,0,0)==powerstate::Source::Battery,
    "VBUS-present alone does not prove incoming USB power");
  check(powerstate::classify(0x30,5.0f,150,0,0)==powerstate::Source::USB,
    "Usable USB supplying current is shown even without a charging flag");
  check(powerstate::classify(0x30,0,0,0,0)==powerstate::Source::Battery,
    "Unplugged USB ADC values override lingering input flags");
  check(powerstate::classify(0x30,5.0f,0,0,0)==powerstate::Source::Battery,
    "Backfed USB voltage with no incoming current is not USB power");
  check(powerstate::classify(0xc0,0,0,5.0f,150)==powerstate::Source::External,
    "Usable separate ACIN input is classified as external power");
  check(powerstate::classify(0x30,5.0f,NAN,0,0)==powerstate::Source::Battery,
    "Invalid current data cannot assert USB power");
  check(powerstate::classify(0xf0,5.0f,0,5.0f,150)==powerstate::Source::External,
    "Input selection follows measured supply instead of unused USB voltage");
  {
    DynamicJsonDocument items(4096),wrapped(6144);
    model::makeFilter(items,true);nsapi::responseFilter(wrapped,items,true);
    e=deserializeJson(doc,R"({"status":200,"result":[{"date":1793102400000,"sgv":123,"direction":"Flat","noise":7}]})",DeserializationOption::Filter(wrapped));
    bool parsed=model::entries(nsapi::collection(doc.as<JsonVariantConst>(),true),now,h);
    check(!e && parsed && h.count==1 && h.p[0].sgv==123 && doc["result"][0]["noise"].isNull(),
      "Production v3 response filter and wrapped glucose result");
    deserializeJson(doc,R"({"status":200,"result":{"sgv":999}})");
    check(!model::entries(nsapi::collection(doc.as<JsonVariantConst>(),true),now,h) && h.count==1,
      "Invalid v3 result cannot overwrite a good glucose snapshot");
    deserializeJson(doc,R"({"status":403,"result":[]})");
    check(nsapi::collection(doc.as<JsonVariantConst>(),true).isNull(),"Non-success v3 status is rejected");
    deserializeJson(doc,R"({"status":"200","result":[]})");
    check(nsapi::collection(doc.as<JsonVariantConst>(),true).isNull(),"V3 status must be numeric");
    deserializeJson(doc,R"({"status":200,"result":[]})");
    check(model::entries(nsapi::collection(doc.as<JsonVariantConst>(),true),now,h) && !h.count,
      "Successful empty v3 collection clears old data");
    deserializeJson(doc,R"([{"date":1793102400000,"sgv":124}])");
    check(model::entries(nsapi::collection(doc.as<JsonVariantConst>(),false),now,h) && h.count==1 &&
      nsapi::collection(doc.as<JsonVariantConst>(),true).isNull(),"Legacy arrays remain explicit v1-only responses");
    model::makeFilter(items,false);nsapi::responseFilter(wrapped,items,true);
    e=deserializeJson(doc,R"({"status":200,"result":[{"date":1793102400000,"device":"aaps","openaps":{"suggested":{"timestamp":"2026-10-27T12:00:00Z","IOB":1,"predBGs":{"IOB":[100,110]}}}}]})",DeserializationOption::Filter(wrapped));
    model::statuses(nsapi::collection(doc.as<JsonVariantConst>(),true),"aaps",now,s);
    check(!e && s.uploaded==now && s.iob.value==1 && s.forecast.count[0]==2,
      "V3 canonical device date and predictions survive envelope filtering");
    model::treatmentFilter(items);nsapi::responseFilter(wrapped,items,true);
    e=deserializeJson(doc,R"({"status":200,"result":[{"identifier":"public-id","date":1793102340000,"carbs":10},{"identifier":"public-id","date":1793102340000,"carbs":10},{"identifier":"deleted","date":1793102340000,"carbs":10,"isValid":false}]})",DeserializationOption::Filter(wrapped));
    model::treatments(nsapi::collection(doc.as<JsonVariantConst>(),true),now-10800,now,treatments);
    check(!e && treatments.count==1 && !strcmp(treatments.events[0].id,"public-id"),
      "V3 public identifiers deduplicate treatments and tombstones are excluded");
    deserializeJson(doc,R"([{"date":1793102400000,"sgv":123,"isValid":false},{"date":1793102400000,"sgv":124,"isDeleted":true}])");
    model::entries(doc.as<JsonVariantConst>(),now,h);
    check(!h.count,"Deleted glucose documents never become current readings");
    deserializeJson(doc,R"([{"date":1793102400000,"isValid":false,"openaps":{"suggested":{"timestamp":"2026-10-27T12:00:00Z","IOB":1}}}])");
    model::statuses(doc.as<JsonVariantConst>(),"",now,s);
    check(!s.uploaded,"Deleted device status cannot revive loop or pump values");
  }
  {
    const char *token=nullptr;int64_t expires=0;
    deserializeJson(doc,R"({"token":"aaa.bbb.ccc","exp":1793106000})");
    check(nsapi::tokenResponse(doc.as<JsonVariantConst>(),now,token,expires) && expires==now+3600,
      "Authorization response yields bounded JWT and expiration");
    check(!nsapi::renewNeeded(true,now+61,now) && nsapi::renewNeeded(true,now+60,now) &&
      nsapi::renewNeeded(false,now+3600,now),"JWT renews before expiry and when missing");
    deserializeJson(doc,R"({"token":"aaa.bbb.ccc","exp":1793102400})");
    check(!nsapi::tokenResponse(doc.as<JsonVariantConst>(),now,token,expires),"Expired JWT is not accepted");
    deserializeJson(doc,R"({"token":"aaa.bbb.ccc"})");
    check(!nsapi::tokenResponse(doc.as<JsonVariantConst>(),now,token,expires),"JWT without expiration fails closed");
    deserializeJson(doc,R"({"token":"aaa.bbb.ccc\r\nInjected","exp":1793106000})");
    check(!nsapi::tokenResponse(doc.as<JsonVariantConst>(),now,token,expires),"JWT header-injection characters are rejected");
    deserializeJson(doc,R"({"token":"aaa..ccc","exp":1793106000})");
    check(!nsapi::tokenResponse(doc.as<JsonVariantConst>(),now,token,expires),"Empty JWT segments are rejected");
  }
  {
    static smblabels::Layout labels;
    auto width=[](const smblabels::Group &g){return g.count>1?30:24;};
    auto valid=[&](){
      for(size_t i=0;i<labels.size;++i) {
        const auto &a=labels.groups[i];
        if(a.x<30 || a.x+a.width>201) return false;
        for(size_t j=i+1;j<labels.size;++j) {
          const auto &b=labels.groups[j];
          if(a.lane==b.lane && a.x<b.x+b.width+3 && b.x<a.x+a.width+3) return false;
        }
      }
      return true;
    };
    labels.add(60,0.2f);labels.add(160,0.3f);
    check(labels.arrange(30,200,width) && labels.size==2 && valid(),"Isolated SMB labels remain separate and bounded");
    labels.reset();labels.add(100,0.2f);labels.add(101,0.3f);
    check(labels.arrange(30,200,width) && labels.size==2 && labels.groups[0].lane!=labels.groups[1].lane && valid(),
      "Two close SMBs use separate label rows before combining");
    labels.add(102,0.4f);
    bool arranged=labels.arrange(30,200,width);float sum=0;unsigned count=0;bool span=false;
    for(size_t i=0;i<labels.size;++i){const auto &g=labels.groups[i];sum+=g.units;count+=g.count;span|=g.count>1 && g.last>g.first;}
    check(arranged && labels.size==2 && valid() && fabsf(sum-0.9f)<0.0001f && count==3 && span,
      "Crowded SMB labels combine adjacent amounts while preserving count and timestamp span");
    labels.reset();labels.add(30,0.1f);labels.add(200,0.2f);
    check(labels.arrange(30,200,width) && valid(),"Edge labels stay inside history pane");
    labels.reset();for(unsigned i=0;i<128;++i)labels.add(30+i,0.1f);
    arranged=labels.arrange(30,200,width);sum=0;count=0;
    for(size_t i=0;i<labels.size;++i){sum+=labels.groups[i].units;count+=labels.groups[i].count;}
    check(arranged && valid() && count==128 && fabsf(sum-12.8f)<0.001f,
      "Full treatment capacity fits without overlapping labels or losing dose totals");
    labels.reset();check(labels.size==0 && labels.arrange(30,200,width),"New snapshot clears prior SMB label groups");
  }
  {
    alerts::StaleState stale;
    check(stale.poll(false,false,true)==0,"Fresh sources do not produce stale alerts");
    check(stale.poll(true,false,true)==1 && stale.poll(true,false,true)==1,"Stale alert remains pending until played");
    stale.played(1);check(stale.poll(true,false,true)==0,"Stale glucose sounds once per episode");
    check(stale.poll(true,true,true)==2,"AAPS stale episode remains independent of glucose");
    stale.played(2);check(stale.poll(true,true,true)==0,"Both stale episodes are acknowledged");
    stale.poll(false,true,true);check(stale.poll(true,true,true)==1,"Fresh recovery rearms only recovered source");
    check(stale.poll(true,true,false)==0 && stale.poll(true,true,true)==3,"Mute suppresses stale sounds and enabling rearms them");
    stale.poll(false,false,true);stale.played(3);
    check(stale.poll(false,true,true)==2,"Acknowledging an obsolete alert cannot consume a later episode");
    check(model::fresh(now-600,now,alerts::STALE_SECONDS) && !model::fresh(now-601,now,alerts::STALE_SECONDS),
      "Stale audio threshold is strictly more than ten minutes");
  }
  {
    graphscale::Range range;auto bounds=range.bounds();
    check(bounds.low==70 && bounds.high==180,"Empty graph has neutral target-range bounds");
    range.add(110);bounds=range.bounds();
    check(bounds.low==80 && bounds.high==140,"Flat glucose has padded minimum span without 250 ceiling");
    range.add(150);bounds=range.bounds();
    check(bounds.low==100 && bounds.high==160,"Visible glucose extrema define graph scale");
    graphscale::Range wide;wide.add(40);wide.add(350);bounds=wide.bounds();
    check(bounds.low<=40 && bounds.high>350,"Wide glucose excursions remain within padded scale");
    graphscale::Range invalid;invalid.add(NAN);invalid.add(0);invalid.add(601);bounds=invalid.bounds();
    check(bounds.low==70 && bounds.high==180,"Invalid glucose cannot distort graph scale");
    float lo=0,hi=0;
    check(graphscale::targetBand({100,160},lo,hi) && lo==100 && hi==160,"Target shading fills an entirely in-range scale");
    check(graphscale::targetBand({40,120},lo,hi) && lo==70 && hi==120,"Target shading clips to visible intersection");
    check(!graphscale::targetBand({200,300},lo,hi) && !graphscale::targetBand({20,60},lo,hi),"Out-of-range scales have no misleading green shading");
  }
  {
    check(quicksettings::validHours(1) && quicksettings::validHours(3) && quicksettings::validHours(6) && quicksettings::validHours(12) && !quicksettings::validHours(2),
      "Quick settings expose only supported graph windows");
    check(quicksettings::restoreHours(99,6)==6 && quicksettings::restoreHours(0,0)==3 && quicksettings::restoreHours(12,3)==12,
      "Invalid stored graph preferences use safe defaults");
    quicksettings::Credentials credentials;
    check(!quicksettings::validCredentials(credentials),"Wi-Fi setup rejects empty network names");
    strlcpy(credentials.ssid,"Example network",sizeof(credentials.ssid));
    check(quicksettings::validCredentials(credentials),"Wi-Fi setup supports explicit open networks");
    strlcpy(credentials.password,"short",sizeof(credentials.password));
    check(!quicksettings::validCredentials(credentials),"Wi-Fi setup rejects short WPA passwords");
    strlcpy(credentials.password,"example-password",sizeof(credentials.password));
    check(quicksettings::validCredentials(credentials),"Wi-Fi setup accepts bounded WPA credentials");
    memset(credentials.ssid,'x',sizeof(credentials.ssid));
    check(!quicksettings::validCredentials(credentials),"Corrupt unterminated saved Wi-Fi credentials are rejected");
    strlcpy(credentials.ssid,"bad\nnetwork",sizeof(credentials.ssid));
    check(!quicksettings::validCredentials(credentials),"Wi-Fi setup rejects control characters in credentials");
  }
  {
    runtimecfg::Settings base,next;
    strlcpy(base.url,"https://nightscout.example.com",sizeof(base.url));strlcpy(base.token,"fixture-subject-token",sizeof(base.token));
    strlcpy(base.timezone,"UTC0",sizeof(base.timezone));strlcpy(base.ntp1,"pool.ntp.org",sizeof(base.ntp1));strlcpy(base.ntp2,"time.cloudflare.com",sizeof(base.ntp2));
    quicksettings::Credentials wifi;strlcpy(wifi.ssid,"Primary",sizeof(wifi.ssid));strlcpy(wifi.password,"fixture-password",sizeof(wifi.password));
    runtimecfg::promoteWifi(base,wifi);
    check(runtimecfg::valid(base),"Runtime profile accepts a bounded default configuration");
    next=base;next.url[0]=next.token[0]=0;
    check(runtimecfg::valid(next),"Unconfigured Nightscout does not block Wi-Fi recovery or menu settings");
    next=base;next.magic=0;check(!runtimecfg::valid(next),"Stored profile version mismatch is rejected");
    next=base;memset(next.token,'x',sizeof(next.token));check(!runtimecfg::valid(next),"Unterminated stored secrets are rejected");
    check(!runtimecfg::https("http://example.com") && !runtimecfg::https("https://user@example.com") && !runtimecfg::https("https://example.com?token=x") && runtimecfg::https("https://example.com/prefix"),
      "Runtime Nightscout URL keeps HTTPS origin and optional path restrictions");
    deserializeJson(doc,R"({"token":"","hours":12,"poll":90})");
    check(runtimecfg::patch(doc.as<JsonVariantConst>(),base,next) && !strcmp(next.token,base.token) && next.hours==12 && next.poll==90,
      "Blank token preserves saved secret while nonsecret fields change");
    deserializeJson(doc,R"({"url":"https://another.example.com","token":""})");
    check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Changing server cannot silently forward the previous saved token");
    deserializeJson(doc,R"({"url":"https://another.example.com","token":"new-server-token"})");
    check(runtimecfg::patch(doc.as<JsonVariantConst>(),base,next) && !strcmp(next.token,"new-server-token"),"Changing server accepts an explicitly entered replacement token");
    deserializeJson(doc,R"({"poll":true})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Boolean cannot become a polling interval");
    deserializeJson(doc,R"({"poll":29})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Runtime polling minimum is enforced");
    deserializeJson(doc,R"({"hours":2})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Runtime history supports only menu timeframes");
    deserializeJson(doc,R"({"low":150,"high":100})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Alert thresholds must remain ordered");
    deserializeJson(doc,R"({"wifi":[{"ssid":"Primary","password":""}]})");
    check(runtimecfg::patch(doc.as<JsonVariantConst>(),base,next) && !strcmp(next.wifi[0].password,base.wifi[0].password),"Existing Wi-Fi password is preserved when omitted or blank");
    deserializeJson(doc,R"({"wifi":[{"ssid":"Unknown","password":""}]})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"New open Wi-Fi must be explicitly selected");
    deserializeJson(doc,R"({"wifi":[{"ssid":"Guest","open":true}]})");
    check(runtimecfg::patch(doc.as<JsonVariantConst>(),base,next) && !next.wifi[0].password[0],"Explicit open network is supported without revealing another password");
    deserializeJson(doc,R"({"wifi":[{"ssid":"Guest","open":true,"password":"unexpected"}]})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Conflicting open-network password is rejected");
    deserializeJson(doc,R"({"wifi":[{"ssid":"Primary"},{"ssid":"Primary"}]})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Duplicate saved Wi-Fi names cannot hide fallback entries");
    deserializeJson(doc,R"({"wifi":[]})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Portal cannot accidentally erase all saved networks");
    deserializeJson(doc,R"({"wifi":[{"ssid":"Phone","password":"fixture-backup"},{"ssid":"Primary"}]})");
    check(runtimecfg::patch(doc.as<JsonVariantConst>(),base,next) && next.wifiCount==2 && !strcmp(next.wifi[0].ssid,"Phone") && !strcmp(next.wifi[1].password,base.wifi[0].password),
      "Network priority changes preserve each network's own secret");
    bool changed=runtimecfg::connectionChanged(base,next);next=base;strlcpy(next.token,"replacement-subject",sizeof(next.token));
    check(!changed && runtimecfg::connectionChanged(base,next),"Only Nightscout identity changes invalidate cached connection data");
    doc.clear();runtimecfg::publicJson(doc.to<JsonObject>(),base,7);
    check(!doc.containsKey("token") && doc["tokenSet"].as<bool>() && !doc["wifi"][0].containsKey("password") && doc["wifi"][0]["passwordSet"].as<bool>(),
      "Public settings response omits all token and Wi-Fi password values");
    next=base;for(unsigned i=1;i<5;++i){snprintf(wifi.ssid,sizeof(wifi.ssid),"Network%u",i);runtimecfg::promoteWifi(next,wifi);}
    strlcpy(wifi.ssid,"Newest",sizeof(wifi.ssid));runtimecfg::promoteWifi(next,wifi);
    check(next.wifiCount==5 && !strcmp(next.wifi[0].ssid,"Newest") && runtimecfg::valid(next),"Recovery hotspot promotes a tested network while bounding backups to five");
    runtimecfg::promoteWifi(next,wifi);check(next.wifiCount==5,"Promoting an existing network never creates duplicates");
    deserializeJson(doc,R"({"v3":"true"})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"String booleans cannot change API mode");
    deserializeJson(doc,R"({"brightness":256})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Display brightness is bounded to device range");
    deserializeJson(doc,R"({"sgvStale":0})");check(!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Zero freshness limits are rejected");
    deserializeJson(doc,R"({"token":"abc\u0000tail"})");bool rejectedTokenNul=!runtimecfg::patch(doc.as<JsonVariantConst>(),base,next);
    deserializeJson(doc,R"({"wifi":[{"ssid":"Primary","password":"password\u0000tail"}]})");
    check(rejectedTokenNul && !runtimecfg::patch(doc.as<JsonVariantConst>(),base,next),"Embedded NUL cannot truncate portal tokens or Wi-Fi passwords");
    check(!strcmp(base.token,"fixture-subject-token") && base.hours==3,"Rejected or successful patch parsing never mutates the active source profile");
  }
  {
    runtimecfg::Access access;
    check(!access.authorized(true,0),"LAN portal starts without an authenticated session");
    for(unsigned i=0;i<5;++i)access.failed(100);
    check(access.limited(100) && access.limited(60099) && !access.limited(60100),"Five incorrect portal codes enforce a one-minute cooldown");
    access.login(1000);check(!access.authorized(false,1001) && access.authorized(true,1002),"Only a matching session cookie authorizes portal access");
    check(access.authorized(true,800000) && access.authorized(true,1500000),"Active authenticated requests renew the inactivity timer");
    check(!access.authorized(true,2400000),"Idle portal sessions expire at fifteen minutes");
    access.login(0xfffffff0u);check(access.authorized(true,20),"Portal session timers handle monotonic counter wrap");
    access.clear();check(!access.authorized(true,21),"Disabling or locking the portal revokes its session");
    alerts::State custom;
    check(custom.poll(75,true,0,80,190)==alerts::Event::Low && custom.poll(185,true,1,80,190)==alerts::Event::None,
      "Portal alert thresholds affect actual glucose alerts consistently");
  }
  {
    check(!strcmp(tlsdiagnostics::message(0x8),"TLS CA/chain untrusted"),"TLS diagnostics distinguish untrusted chain");
    check(!strcmp(tlsdiagnostics::message(0x4|0x8),"TLS hostname mismatch"),"TLS hostname failure remains explicit with multiple flags");
    check(!strcmp(tlsdiagnostics::message(0x1|0x200),"TLS cert date/clock invalid"),"TLS certificate dates are distinguished from trust errors");
    check(!strcmp(tlsdiagnostics::message(0x10000),"TLS certificate algorithm/key"),"TLS algorithm failures are not mislabeled as missing roots");
    check(!strcmp(tlsdiagnostics::message(0x40),"TLS peer certificate missing"),"TLS diagnostics identify missing peer certificates");
    check(!strcmp(tlsdiagnostics::message(0),"TLS verify: flags unavailable") && !strcmp(tlsdiagnostics::message(0xffffffffu),"TLS verify: flags unavailable"),
      "Unavailable TLS verification state is not interpreted as all failures");
  }
  {
    using Action=tlsanchors::Action;
    check(tlsanchors::action(0x8,true)==Action::AcceptAnchor,"Exact root subject and key can anchor a cross-signed presentation");
    check(tlsanchors::action(0x8|0x1,true)==Action::PreserveFailure && tlsanchors::action(0x8|0x200,true)==Action::PreserveFailure,"Trusted root identity cannot waive certificate date failures");
    check(tlsanchors::action(0x8|0x4,true)==Action::PreserveFailure,"Trusted root identity cannot waive hostname mismatch");
    check(tlsanchors::action(0x8|0x800,true)==Action::PreserveFailure,"Root anchoring preserves key-usage constraints");
    check(tlsanchors::action(0x8|0x4000,true)==Action::AcceptAnchor && tlsanchors::action(0x8|0x4000,false)==Action::PreserveFailure,"Weak self/cross signature exception applies only to an exact anchor");
    check(tlsanchors::action(0x8,false)==Action::CheckIssuer,"Unknown root identity still requires verified issuer signature");
    const unsigned char key1[]={1,2,3},key2[]={1,2,4};
    check(tlsanchors::sameKey(key1,3,key1,3) && !tlsanchors::sameKey(key1,3,key2,3) && !tlsanchors::sameKey(key1,2,key1,3) && !tlsanchors::sameKey(key1,0,key1,0),"Trust requires exact nonempty public-key bytes, not subject name alone");
  }
  check(quicksettings::validHours(24),"Tab5 accepts a 24 hour runtime profile");
  check(tabgraph::x(100,100,200)==88 && tabgraph::x(200,100,200)==1232 && tabgraph::x(150,100,200)==660,"Graph time mapping spans native landscape width");
  check(tabgraph::y(0,40,250)==598 && tabgraph::y(600,40,250)==320,"Graph clipping stays within bounds");
  check(tabgraph::range(3)==24 && tabgraph::range(4)==3,"Touch range selection wraps safely");
  check(!wifisignal::bars(false,-40) && !wifisignal::bars(true,0),"Disconnected or invalid RSSI never shows strong Wi-Fi");
  check(wifisignal::bars(true,-55)==4 && wifisignal::bars(true,-56)==3 && wifisignal::bars(true,-67)==3 && wifisignal::bars(true,-68)==2,"Wi-Fi strong/good thresholds match Core2");
  check(wifisignal::bars(true,-75)==2 && wifisignal::bars(true,-76)==1,"Wi-Fi fair/weak boundary matches Core2");
  {
    tm local={};local.tm_year=126;local.tm_mon=10;local.tm_mday=6;local.tm_wday=5;local.tm_hour=7;local.tm_min=5;
    char clock[12],date[32];clockdisplay::format(local,true,clock,sizeof(clock),date,sizeof(date));
    check(!strcmp(clock,"07:05") && !strcmp(date,"Fri 06 Nov"),"Header clock uses clear padded local time and date");
    clockdisplay::format(local,false,clock,sizeof(clock),date,sizeof(date));
    check(!strcmp(clock,"--:--") && !strcmp(date,"Waiting for time") && !clockdisplay::valid(0),"Unset RTC does not display a misleading epoch date");
  }
  check(smblabels::needsLabel(true,0.5f) && smblabels::needsLabel(true,0.01f) && !smblabels::needsLabel(false,2.f) && !smblabels::needsLabel(true,0.f),"Every positive SMB dose receives a label, including doses at or below 0.5U");
  {
   static smblabels::Layout dense;dense.reset();for(unsigned i=0;i<512;++i)dense.add(88+i*2,0.6f);
   bool ok=dense.arrange(88,1232,[](const smblabels::Group &g){return g.count>1?120:50;},8,24);
   unsigned count=0;float units=0;for(size_t i=0;i<dense.size;++i){count+=dense.groups[i].count;units+=dense.groups[i].units;}
   check(ok && count==512 && fabsf(units-307.2f)<0.02f,"Crowded floating dose labels retain all eligible counts and total units");
   tm local={};local.tm_hour=0;local.tm_min=5;char value[16];clockdisplay::formatTime(local,value,sizeof(value),true);
   check(!strcmp(value,"12:05 AM"),"12-hour display formats midnight correctly");
   local.tm_hour=12;clockdisplay::formatTime(local,value,sizeof(value),true);check(!strcmp(value,"12:05 PM"),"12-hour display formats noon correctly");
   local.tm_hour=23;clockdisplay::formatTime(local,value,sizeof(value),true);check(!strcmp(value,"11:05 PM"),"12-hour display distinguishes evening PM");
  }
  {
   nslive::Pending pending;pending.add(nslive::Entries,100);pending.add(nslive::Treatments,1000);
   check(!pending.take(1599)&&pending.take(1600)==5,"Live bursts coalesce without postponing the first deadline");
   pending.add(nslive::Status,1601);check(!pending.take(3000)&&pending.take(3101)==2,"Events arriving after a fetch starts remain pending");
   pending.add(7,0xfffffff0u);check(pending.take(1500)==7,"Live debounce handles millis rollover");
   char buffer[257];nslive::Frames frames(buffer,256);unsigned messages=0,pings=0;std::string received;
   auto emit=[&](uint8_t op,char *p,size_t n){if(op==1){++messages;for(size_t i=0;i<n;i++)received+=p[i];}if(op==9)++pings;return true;};
   const uint8_t fragmented[]={0x01,2,'4','2',0x89,1,'x',0x80,2,'[',']'};bool ok=true;
   for(uint8_t b:fragmented)ok=ok&&frames.feed(b,emit);
   check(ok&&messages==1&&pings==1&&received=="42[]","Fragmented Socket.IO text survives an interleaved WebSocket ping");
   frames.reset();check(frames.feed(0x81,emit)&&!frames.feed(0x80,emit),"Masked server frames are rejected");
   frames.reset();check(frames.feed(0x80,emit)&&!frames.feed(0,emit),"Orphan continuation frames are rejected");
   frames.reset();check(frames.feed(0x09,emit)&&!frames.feed(0,emit),"Fragmented control frames are rejected");
   frames.reset();ok=true;const uint8_t huge[]={0x81,126,1,1};for(uint8_t b:huge)ok=ok&&frames.feed(b,emit);
   check(!ok,"Oversized live messages fail closed rather than exhausting memory");
   frames.reset();received="";messages=0;ok=true;const uint8_t head[]={0x81,126,0,126};for(uint8_t b:head)ok=ok&&frames.feed(b,emit);for(int i=0;i<126;i++)ok=ok&&frames.feed('x',emit);
   check(ok&&messages==1&&received.length()==126,"Extended-length frames assemble across arbitrary input chunks");
   frames.reset();messages=0;check(frames.feed(0x81,emit)&&frames.feed(0,emit)&&messages==1,"Empty text frames terminate without consuming the next frame");
   frames.reset();check(frames.feed(0xC1,emit)&&!frames.feed(0,emit),"Unnegotiated WebSocket compression is rejected");
   frames.reset();check(frames.feed(0x82,emit)&&!frames.feed(0,emit),"Binary frames cannot be mistaken for live JSON");
   frames.reset();ok=frames.feed(0x01,emit)&&frames.feed(0,emit)&&frames.feed(0x81,emit)&&frames.feed(0,emit);
   check(!ok,"New text cannot overwrite an unfinished fragmented message");
   frames.reset();ok=frames.feed(0x81,emit)&&frames.feed(126,emit)&&frames.feed(0,emit)&&frames.feed(1,emit);
   check(!ok,"Non-minimal extended frame lengths are rejected");
   frames.reset();ok=frames.feed(0x89,emit)&&frames.feed(126,emit);check(!ok,"Oversized control frames are rejected before allocating payload");
   frames.reset();ok=true;const uint8_t longHead[]={0x81,127,0,0,0,0,0,1,0,0};for(uint8_t b:longHead)ok=ok&&frames.feed(b,emit);
   check(!ok,"64-bit frame lengths obey the same message memory limit");
   frames.reset();check(frames.feed(0x88,emit)&&!frames.feed(0,[](uint8_t,char*,size_t){return false;}),"Close callback failure propagates to reconnect recovery");
   StaticJsonDocument<96> filter;filter["colName"]=true;DynamicJsonDocument event(256);
   auto err=deserializeJson(event,R"({"colName":"treatments","doc":{"insulin":1,"nested":[1,2,3]}}])",DeserializationOption::Filter(filter));
   check(!err&&nslive::collection(event["colName"]|"")==4&&!event.containsKey("doc"),"Live routing ignores clinical payload and accepts the Socket.IO array suffix");
   check(nslive::collection("entries")==1&&nslive::collection("devicestatus")==2&&!nslive::collection("settings"),"Only monitored collections route live refreshes");
  }
  {
   alertaudio::Settings prefs;check(alertaudio::valid(prefs)&&prefs.intensity==1,"Sound defaults select Medium with independent alert mappings");
   bool covered[44]={},ok=true;for(unsigned c=0;c<20;c++)for(unsigned v=0;v<3;v++){
    unsigned index=alertaudio::clipIndex(c,v);ok &= index<44&&alertaudio::clips[index].samples>0;if(index<44)covered[index]=true;
   }
   for(bool c:covered)ok &= c;check(ok,"Sound picker reaches every one of the 44 embedded recordings");
   prefs.choice[3]=20;check(!alertaudio::valid(prefs),"Corrupt stored sound choices are rejected");prefs=alertaudio::Settings{};prefs.intensity=3;check(!alertaudio::valid(prefs),"Corrupt stored intensity is rejected");
   check(alertaudio::clipIndex(12,0)==alertaudio::clipIndex(12,2)&&alertaudio::clipIndex(0,0)!=alertaudio::clipIndex(0,2),"Tones retain their single recording while Trio families change intensity");
   static smblabels::Layout labels;labels.reset();labels.add(100,.1f);labels.add(101,.2f);labels.add(102,.5f);
   bool placed=labels.arrange(30,300,[](const smblabels::Group&){return 70;},8,48,3);
   check(placed&&labels.size==3,"Three crowded small SMBs retain individual labels in three lanes");
  }
  {
   check(alerts::alarmVolume(0)==alerts::MIN_ALERT_VOLUME && alerts::alarmVolume(16)==alerts::MIN_ALERT_VOLUME,"Saved volume 0 or very low still plays alarms at the floor");
   check(alerts::alarmVolume(80)==80 && alerts::alarmVolume(255)==255 && alerts::alarmVolume(999)==255,"Normal alarm volumes are unchanged");
   static runtimecfg::Settings a,b;a=runtimecfg::Settings{};b=a;
   b.brightness=10;b.volume=0;b.sound=false;b.mmol=true;b.forecast=false;b.hours=24;b.low=80;b.high=200;
   check(!runtimecfg::networkChanged(a,b),"Display and alert-only saves do not wake the network task");
   b=a;b.poll=120;bool poll=runtimecfg::networkChanged(a,b);b=a;strcpy(b.timezone,"UTC0");bool tz=runtimecfg::networkChanged(a,b);
   b=a;b.wifiCount=1;strcpy(b.wifi[0].ssid,"x");bool wifi=runtimecfg::networkChanged(a,b);b=a;b.v3=!a.v3;
   check(poll && tz && wifi && runtimecfg::networkChanged(a,b),"Poll, time, Wi-Fi and connection changes wake the network task");
   DynamicJsonDocument ds(4096);model::Status st;
   deserializeJson(ds,R"([{"created_at":"2026-10-27T11:59:50Z","device":"xDrip-phone","uploaderBattery":80},{"created_at":"2026-10-27T11:59:40Z","device":"xDrip-phone","uploaderBattery":80},{"created_at":"2026-10-27T11:59:30Z","device":"openaps://phone","openaps":{"suggested":{"timestamp":"2026-10-27T11:59:00Z","IOB":1.5,"COB":10}}}])");
   check(model::statuses(ds.as<JsonVariantConst>(),"",now,st) && st.loopTime==now-60,"Loop status survives newer non-loop devicestatus uploads");
  }
  {
   setenv("TZ","EST5EDT,M3.2.0/2,M11.1.0/2",1);tzset();
   const int64_t t0=1794243600; // 2026-11-09 17:00:00Z = 12:00 EST
   check(graphtime::localOffset(t0)==-5*3600 && graphtime::localOffset(1783612800)==-4*3600,"Local offset follows EST/EDT");
   check(graphtime::alignDown(t0+1799,3600)==t0 && graphtime::alignUp(t0+1,3*3600)==t0+3*3600,"Ticks align to local clock hours");
   check(graphtime::alignUp(t0-6*3600-60,6*3600)==t0-6*3600,"Six-hour ticks land on whole hours");
   check(graphtime::tickStep(12)==3*3600 && graphtime::tickStep(24)==6*3600 && graphtime::tickStep(3)==3600,"Tick spacing per range");
   check(graphtime::bucketSize(1)==300 && graphtime::bucketSize(3)==900 && graphtime::bucketSize(6)==1800 && graphtime::bucketSize(12)==3600 && graphtime::bucketSize(24)==7200,"About twelve SMB buckets in every view");
   {graphscale::Range r;r.add(100);r.add(140);auto bb=r.bounds();
    graphscale::Range f;f.add(100);f.add(140);f.add(260);auto fb=f.bounds();
    graphscale::Range e;auto eb=e.bounds();
    check(bb.low>=80 && bb.low<100 && bb.high>140 && bb.high<=160 && fb.high>=260 && fb.high<=300 && eb.low==70 && eb.high==180,"Scale fits visible data, expands for forecasts and has a sane empty default");
    check(graphscale::gridStep(60)==10 && graphscale::gridStep(200)==50 && graphscale::gridStep(400)==100 && graphscale::gridStep(4)==1,"Gridline spacing adapts to the visible span");}
   static graphtime::Buckets b;const int64_t start=t0-12*3600+1500,end=t0+1500;b.build(start,end,3600);
   bool aligned=b.size==13 && b.items[0].from==t0-12*3600 && b.items[12].to==t0+3600;
   for(size_t i=1;i<b.size;++i)aligned&=b.items[i].from==b.items[i-1].to && b.items[i].to-b.items[i].from==3600;
   check(aligned,"Hourly buckets are contiguous and clock-aligned across a 12h window");
   b.add(t0-10,0.1f);b.add(t0-3599,0.25f);b.add(t0,0.05f);b.add(t0-20*3600,1.f);
   const auto *k=b.find(t0-1);
   check(k && k->count==2 && std::fabs(k->units-0.35f)<1e-5f && b.find(t0)->count==1 && std::fabs(b.maximum()-0.35f)<1e-5f,"Bucket totals, counts and out-of-window doses");
   static graphtime::Buckets d;d.build(1793512800-5*3600,1793512800+5*3600,7200); // across 2026-11-01 DST end (06:00Z)
   bool dst=d.size>0;for(size_t i=0;i<d.size;++i){time_t f=time_t(d.items[i].from);tm l;localtime_r(&f,&l);int64_t len=d.items[i].to-d.items[i].from;
    dst&=l.tm_min==0 && len>=3600 && len<=3*3600 && (!i||d.items[i].from==d.items[i-1].to);}
   {time_t f=time_t(d.items[d.size-1].from);tm l;localtime_r(&f,&l);dst&=l.tm_hour%2==0;}
   check(dst,"Two-hour buckets stay contiguous on whole hours across a DST change and realign after it");
   setenv("TZ","UTC0",1);tzset();
  }
  {
   DynamicJsonDocument ds(4096);model::Status st;
   deserializeJson(ds,R"([{"created_at":"2026-10-27T11:59:30Z","device":"openaps://phone","uploaderBattery":72,"isCharging":true,"openaps":{"suggested":{"timestamp":"2026-10-27T11:59:00Z","IOB":1}}}])");
   check(model::statuses(ds.as<JsonVariantConst>(),"",now,st) && st.uploaderBattery==72 && st.uploaderCharging && st.uploaderTime==now-30,"AAPS uploaderBattery and isCharging are read from the loop record");
   deserializeJson(ds,R"([{"created_at":"2026-10-27T11:59:30Z","uploader":{"battery":41},"openaps":{"suggested":{"timestamp":"2026-10-27T11:59:00Z"}}}])");
   check(model::statuses(ds.as<JsonVariantConst>(),"",now,st) && st.uploaderBattery==41 && !st.uploaderCharging,"Nested uploader.battery is accepted");
   deserializeJson(ds,R"([{"created_at":"2026-10-27T11:59:30Z","uploaderBattery":150,"openaps":{"suggested":{"timestamp":"2026-10-27T11:59:00Z"}}}])");
   check(model::statuses(ds.as<JsonVariantConst>(),"",now,st) && std::isnan(st.uploaderBattery) && !st.uploaderTime,"Out-of-range uploader battery is ignored");
   model::CareAges care;
   deserializeJson(ds,R"([{"eventType":"Site Change","created_at":"2026-10-25T10:00:00Z"},{"eventType":"Site Change","created_at":"2026-10-26T12:00:00Z"},
     {"eventType":"Sensor Start","date":1792670400000},{"eventType":"Sensor Change","created_at":"2026-10-23T12:00:00Z"},
     {"eventType":"Insulin Change","created_at":"2026-10-27T13:00:00Z"},{"eventType":"Insulin Change","created_at":"2026-10-26T00:00:00Z","isValid":false},
     {"eventType":"Insulin Change","created_at":"2026-10-25T00:00:00Z"},{"eventType":"Note","created_at":"2026-10-27T11:00:00Z"}])");
   check(model::careEvents(ds.as<JsonVariantConst>(),now,care) && care.cannula==now-86400 && care.sensor==now-4*86400 && care.insulin==now-2*86400-12*3600,
     "Care ages use the latest valid event of each kind; future, deleted and other events are ignored");
   char v[16];careage::format(now-17*3600-59,now,v,sizeof(v));bool f1=!strcmp(v,"17h");careage::format(now-(3*24+4)*3600,now,v,sizeof(v));bool f2=!strcmp(v,"3d 4h");
   careage::format(0,now,v,sizeof(v));bool f3=!strcmp(v,"--");careage::format(now+60,now,v,sizeof(v));
   check(f1&&f2&&f3&&!strcmp(v,"0h"),"Care age text: hours under two days, then days and hours");
   check(careage::level(now-47*3600,now,careage::CANNULA)==careage::Level::Info && careage::level(now-48*3600,now,careage::CANNULA)==careage::Level::Warn &&
     careage::level(now-72*3600,now,careage::INSULIN)==careage::Level::Urgent && careage::level(now-215*3600,now,careage::SENSOR)==careage::Level::Ok &&
     careage::level(0,now,careage::SENSOR)==careage::Level::Unknown,"Care age warning levels");
  }
  {
   DynamicJsonDocument ds(4096);model::Status st;
   deserializeJson(ds,R"([{"created_at":"2026-10-27T11:59:30Z","openaps":{"suggested":{"timestamp":"2026-10-27T11:59:00Z"}},"pump":{"reservoir":123.4,"battery":{"percent":80},"status":{"status":"normal","timestamp":"2026-10-27T11:58:00Z"}}}])");
   check(model::statuses(ds.as<JsonVariantConst>(),"",now,st) && std::fabs(st.reservoir-123.4f)<1e-3f && st.pumpBattery==80 && std::isnan(st.pumpVoltage) && st.pumpInfoTime==now-120,"Pump reservoir and battery percent use the pump status time");
   deserializeJson(ds,R"([{"created_at":"2026-10-27T11:59:30Z","openaps":{"suggested":{"timestamp":"2026-10-27T11:59:00Z"}},"pump":{"reservoir_display_override":"50+","battery":{"voltage":1.42}}}])");
   check(model::statuses(ds.as<JsonVariantConst>(),"",now,st) && !strcmp(st.reservoirOverride,"50+") && std::isnan(st.reservoir) && std::isnan(st.pumpBattery) && std::fabs(st.pumpVoltage-1.42f)<1e-4f && st.pumpInfoTime==now-30,
     "Reservoir override and battery voltage fall back to the upload time");
   deserializeJson(ds,R"([{"created_at":"2026-10-27T11:59:30Z","openaps":{"suggested":{"timestamp":"2026-10-27T11:59:00Z"}},"pump":{"reservoir":-3,"battery":{"percent":140}}}])");
   check(model::statuses(ds.as<JsonVariantConst>(),"",now,st) && std::isnan(st.reservoir) && std::isnan(st.pumpBattery) && !st.pumpInfoTime,"Out-of-range pump values are ignored");
   char t1[16],t2[16],t3[16],t4[16],t5[16];
   pumpstatus::reservoirText(123.4f,"",t1,16);pumpstatus::reservoirText(7.46f,nullptr,t2,16);pumpstatus::reservoirText(NAN,"50+",t3,16);pumpstatus::reservoirText(NAN,"",t4,16);pumpstatus::batteryText(NAN,1.418f,t5,16);
   check(!strcmp(t1,"123U")&&!strcmp(t2,"7.5U")&&!strcmp(t3,"50+U")&&!strcmp(t4,"--")&&!strcmp(t5,"1.42V"),"Reservoir and battery text");
   check(pumpstatus::reservoir(10)==pumpstatus::Level::Ok && pumpstatus::reservoir(9.9f)==pumpstatus::Level::Warn && pumpstatus::reservoir(4.9f)==pumpstatus::Level::Urgent &&
     pumpstatus::battery(30,NAN)==pumpstatus::Level::Ok && pumpstatus::battery(29,NAN)==pumpstatus::Level::Warn && pumpstatus::battery(19,NAN)==pumpstatus::Level::Urgent &&
     pumpstatus::battery(NAN,1.33f)==pumpstatus::Level::Warn && pumpstatus::battery(NAN,NAN)==pumpstatus::Level::Unknown,"Pump warning levels match Nightscout defaults");
  }
  Serial.printf("RESULT: %u passed, %u failed\n",passed,failed);
  return failed?1:0;
}

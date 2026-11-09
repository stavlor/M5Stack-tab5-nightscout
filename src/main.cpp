#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "secure_client.h"
#include "ca_bundle_info.h"
#include "tab5_bundle_info.h"
#include <HTTPClient.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_system.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>
#include <esp_memory_utils.h>
#include "clock_display.h"
#include "quick_settings.h"
#include "runtime_settings.h"
#include <freertos/semphr.h>
#include <atomic>
#include <new>
#include "body_buffer.h"
#include "model.h"
#include "treatments.h"
#include "display_settings.h"
#include "alerts.h"
#include "power_state.h"
#include "ns_settings.h"
#include "ns_api.h"
#include "smb_labels.h"
#include "audio_settings.h"
#include "audio_catalog.h"
#include "graph_scale.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif
#ifdef RUN_PARSER_TESTS
#include "parser_tests.h"
#endif

extern const uint8_t caBundleStart[] asm("_binary_certs_tab5_crt_bundle_bin_start");
static_assert(cfg::GRAPH_HOURS>=1 && cfg::GRAPH_HOURS<=24,"Graph hours must be 1..24");
static_assert(cfg::POLL_SECONDS>=30,"Poll no faster than 30 seconds");
struct Snapshot {
  model::History history;
  model::Status status;
  model::Treatments treatments;
  char glucoseError[48]="Starting", statusError[48]="Starting", treatmentError[48]="Starting";
  int64_t clockAt=0, monoAt=0;
  uint32_t connectionEpoch=0;
};
void resetSnapshot(Snapshot &value) {value.~Snapshot();::new(static_cast<void*>(&value)) Snapshot();}
// Each snapshot is ~64 KiB. Keep all three copies (UI, network worker, hand-off)
// in PSRAM so internal RAM stays available for TLS; hand-off is latest-wins.
Snapshot *ui=nullptr,*pendingSnapshot=nullptr,*workerSnapshot=nullptr;
SemaphoreHandle_t snapshotMutex=nullptr;
bool snapshotReady=false;
Snapshot *newSnapshot() {
  void *memory=heap_caps_malloc(sizeof(Snapshot),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!memory)memory=malloc(sizeof(Snapshot));
  return memory?new(memory) Snapshot():nullptr;
}
void publishSnapshot(const Snapshot &value) {
  xSemaphoreTake(snapshotMutex,portMAX_DELAY);*pendingSnapshot=value;snapshotReady=true;xSemaphoreGive(snapshotMutex);
}
bool receiveSnapshot() {
  bool received=false;xSemaphoreTake(snapshotMutex,portMAX_DELAY);
  if(snapshotReady){*ui=*pendingSnapshot;snapshotReady=false;received=true;}
  xSemaphoreGive(snapshotMutex);return received;
}
TaskHandle_t networkTask;
bool mmol=cfg::MMOL;
bool showForecast=true;
std::atomic<unsigned> graphHours{quicksettings::restoreHours(cfg::GRAPH_HOURS,3)};
unsigned pollHours=3; // Network task takes one timeframe snapshot per polling cycle.
bool settingsOpen=false;
std::atomic<bool> liveUpdatesEnabled{true};
std::atomic<bool> liveOnline{false};
// History gestures and range changes redraw from the full-day cache; no network request.

std::atomic<bool> portalRequested{false},portalCancel{false},reconnectRequested{false};
std::atomic<int> portalState{0}; // idle, waiting, ready, testing, saved, failed, timed out, canceled
char portalName[32]={},portalPassword[17]={};

alerts::State glucoseAlerts;
alerts::StaleState staleAlerts;
bool staleSoundActive=false,previewSoundActive=false;
int activeGlucoseAudio=-1;
uint8_t stalePlayingMask=0;
alertaudio::Settings audioSettings;
bool audioSettingsOpen=false;
unsigned audioPickerAlert=alertaudio::AlertCount;
extern const uint8_t audioStart[] asm("_binary_assets_trio_trio_pcm_bin_start");
extern const uint8_t audioEnd[] asm("_binary_assets_trio_trio_pcm_bin_end");
runtimecfg::Settings liveSettings,uiSettings,netSettings;
bool saveAudioSettings(const alertaudio::Settings &next){
 if(!alertaudio::valid(next))return false;Preferences p;bool ok=false;
 if(p.begin("ns-tab5",false)){ok=p.putBytes("alert-audio",&next,sizeof(next))==sizeof(next);p.end();}
 if(ok)audioSettings=next;return ok;
}
void logSpeakerStatus(){
 auto c=M5.Speaker.config();uint8_t dac=0,power=0,amp=0;
 bool codec=M5.In_I2C.readRegister(0x10,25,&dac,1,400000)&&M5.In_I2C.readRegister(0x10,4,&power,1,400000);
 bool io=M5.In_I2C.readRegister(0x43,5,&amp,1,400000);
 Serial.printf("Speaker: vol=%u rate=%u stereo=%u out=%d mclk=%d\n",M5.Speaker.getVolume(),unsigned(c.sample_rate),c.stereo,c.pin_data_out,c.pin_mck);Serial.flush();
 Serial.printf("Speaker: codec=%u DACmute=%02X power=%02X IO=%u amp=%02X playing=%u\n",codec,dac,power,io,amp,M5.Speaker.isPlaying());Serial.flush();
}
bool prepareAlertSpeaker(){
 static bool ready=false;if(ready)return true;
 if(!M5.Speaker.begin())return false;
 // Start MCLK/BCLK with a short silent request before restarting the codec.
 // Data is permanent; channel 0 is released before the real clip is queued.
 static const int16_t silence[16]={};
 if(!M5.Speaker.playRaw(silence,16,48000,false,300,0,true))return false;
 vTaskDelay(pdMS_TO_TICKS(10));
 // ES8388 DAC-only startup: select DAC LRCK, restart the digital state
 // machine, and route only DAC signals to the mixers. The pinned library's
 // Tab5 callback leaves a different clock/mixer state on this hardware.
 // Register meanings/start sequence: Espressif esp_codec_dev ES8388 driver.
 static const uint8_t sequence[][2]={{25,0x24},{24,0x02},{39,0x90},{42,0x90},{43,0x80},{2,0xF0},{2,0x00},{3,0xFF},{4,0x3C},{25,0x20}};
 bool ok=true;for(const auto &r:sequence)ok=M5.In_I2C.writeRegister8(0x10,r[0],r[1],400000)&&ok;
 M5.getIOExpander(0).digitalWrite(1,true);
 M5.Speaker.stop(0);ready=ok;Serial.printf("Speaker DAC startup: %s\n",ok?"ready":"I2C failed");return ok;
}
bool playAlertClip(unsigned alert){
 if(alert>=alertaudio::AlertCount||!glucoseAlerts.isEnabled()||!prepareAlertSpeaker())return false;
 unsigned index=alertaudio::clipIndex(audioSettings.choice[alert],audioSettings.intensity);
 const auto &clip=alertaudio::clips[index];
 if(size_t(clip.offset)+clip.samples>size_t(audioEnd-audioStart))return false;
 bool ok=M5.Speaker.playRaw(audioStart+clip.offset,clip.samples,alertaudio::SampleRate,false,1,-1,true);
 if(ok)Serial.printf("Alert audio: event=%u choice=%u intensity=%u samples=%u\n",alert,audioSettings.choice[alert],audioSettings.intensity,unsigned(clip.samples));
 logSpeakerStatus();return ok;
}

SemaphoreHandle_t settingsMutex=nullptr,networkGate=nullptr;
std::atomic<uint32_t> settingsRevision{1},connectionEpoch{1};
uint32_t uiRevision=0,uiEpoch=1;
std::atomic<bool> lanRequested{false},apSetupActive{false};
struct LanStatus {int state=0;char address[48]={},code[9]={};};
QueueHandle_t lanUpdates=nullptr;
LanStatus lanStatus;
bool wifiMenuOpen=false;
char settingsMessage[64]={};
std::atomic<bool> networkBusy{false};
uint32_t lastAppliedAt=0;
bool haveApplied=false;
std::atomic<bool> rtcSyncPending{false};
enum class Resource {Glucose,Status,Treatments};
namespace layout {
constexpr int PLOT_TOP=62;
constexpr int LEGEND_Y=52, TIME_Y=184;
constexpr int IOB_Y=194, BASAL_Y=216, FOOTER_Y=231;
}

int64_t monotonicSeconds() { return esp_timer_get_time()/1000000; }
int64_t displayNow() {
  return ui->clockAt ? ui->clockAt+monotonicSeconds()-ui->monoAt : 0;
}
void currentSettings(runtimecfg::Settings &out,uint32_t &revision,uint32_t *epoch=nullptr) {
  xSemaphoreTake(settingsMutex,portMAX_DELAY);out=liveSettings;revision=settingsRevision.load();if(epoch)*epoch=connectionEpoch.load();xSemaphoreGive(settingsMutex);
}
bool storeSettings(const runtimecfg::Settings &next,uint32_t expected,char *error,size_t cap) {
  if(!runtimecfg::valid(next)) {strlcpy(error,"Invalid settings",cap);return false;}
  xSemaphoreTake(settingsMutex,portMAX_DELAY);
  if(expected!=settingsRevision.load()) {xSemaphoreGive(settingsMutex);strlcpy(error,"Settings changed; reload before saving",cap);return false;}
  Preferences saved;bool ok=false,wake=false;
  if(saved.begin("ns-runtime",false)) {ok=saved.putBytes("profile",&next,sizeof(next))==sizeof(next);saved.end();}
  if(ok) {
    wake=runtimecfg::networkChanged(liveSettings,next);
    if(runtimecfg::connectionChanged(liveSettings,next)) ++connectionEpoch;
    liveSettings=next;++settingsRevision;
  }
  xSemaphoreGive(settingsMutex);
  strlcpy(error,ok?"":"Could not save settings",cap);
  if(ok && wake && networkTask) xTaskNotifyGive(networkTask);
  return ok;
}
void loadRuntimeSettings() {
  runtimecfg::Settings defaults;
  strlcpy(defaults.url,cfg::NS_URL,sizeof(defaults.url));strlcpy(defaults.token,cfg::NS_TOKEN,sizeof(defaults.token));
  strlcpy(defaults.device,cfg::AAPS_DEVICE,sizeof(defaults.device));defaults.v3=nscfg::USE_V3;
  strlcpy(defaults.timezone,displaycfg::TIMEZONE,sizeof(defaults.timezone));
  strlcpy(defaults.ntp1,cfg::NTP1,sizeof(defaults.ntp1));strlcpy(defaults.ntp2,cfg::NTP2,sizeof(defaults.ntp2));
  defaults.hours=quicksettings::restoreHours(cfg::GRAPH_HOURS,3);defaults.poll=cfg::POLL_SECONDS;
  defaults.sgvStale=cfg::SGV_STALE_SECONDS;defaults.apsStale=cfg::AAPS_STALE_SECONDS;
  defaults.low=cfg::LOW_MGDL;defaults.high=cfg::HIGH_MGDL;defaults.brightness=cfg::BRIGHTNESS;defaults.mmol=cfg::MMOL;defaults.volume=alerts::VOLUME;
  if(!runtimecfg::https(defaults.url) || strlen(cfg::NS_URL)>=sizeof(defaults.url)) {defaults.url[0]=0;defaults.token[0]=0;}
  if(strlen(cfg::NS_TOKEN)>=sizeof(defaults.token) || !strcmp(defaults.token,"YOUR_READ_ONLY_ACCESS_TOKEN"))defaults.token[0]=0;
  defaults.poll=std::max(30u,std::min(defaults.poll,900u));
  defaults.sgvStale=std::max(60u,std::min(defaults.sgvStale,3600u));defaults.apsStale=std::max(60u,std::min(defaults.apsStale,3600u));
  if(defaults.low<20 || defaults.low>179)defaults.low=70;
  if(defaults.high<=defaults.low || defaults.high>600)defaults.high=180;
  defaults.brightness=std::min(defaults.brightness,255u);defaults.volume=std::min(defaults.volume,255u);
  if(!defaults.timezone[0])strlcpy(defaults.timezone,"UTC0",sizeof(defaults.timezone));
  if(!defaults.ntp1[0])strlcpy(defaults.ntp1,"pool.ntp.org",sizeof(defaults.ntp1));
  quicksettings::Credentials configuredWifi;
  strlcpy(configuredWifi.ssid,cfg::WIFI_SSID,sizeof(configuredWifi.ssid));strlcpy(configuredWifi.password,cfg::WIFI_PASSWORD,sizeof(configuredWifi.password));
  if(strcmp(configuredWifi.ssid,"YOUR_2_4_GHZ_WIFI") && quicksettings::validCredentials(configuredWifi)) runtimecfg::promoteWifi(defaults,configuredWifi);
  Preferences old;
  if(old.begin("ns-display",true)) {
    defaults.hours=quicksettings::restoreHours(old.getUInt("hours",defaults.hours),defaults.hours);
    defaults.mmol=old.getBool("mmol",defaults.mmol);defaults.forecast=old.getBool("forecast",true);defaults.sound=old.getBool("alerts-on",true);old.end();
  }
  if(old.begin("ns-wifi",true)) {
    quicksettings::Credentials previous;
    if(old.getBytesLength("network")==sizeof(previous) && old.getBytes("network",&previous,sizeof(previous))==sizeof(previous)) runtimecfg::promoteWifi(defaults,previous);
    old.end();
  }
  liveSettings=defaults;
  Preferences saved;
  if(saved.begin("ns-runtime",true)) {
    runtimecfg::Settings candidate;
    if(saved.getBytesLength("profile")==sizeof(candidate) && saved.getBytes("profile",&candidate,sizeof(candidate))==sizeof(candidate) && runtimecfg::valid(candidate)) liveSettings=candidate;
    saved.end();
  }
  uiSettings=netSettings=liveSettings;
}
bool configured() {
  return runtimecfg::https(netSettings.url) && netSettings.token[0] && strcmp(netSettings.token,"YOUR_READ_ONLY_ACCESS_TOKEN");
}
// Bounded decoded response sink. HTTPClient::writeToStream handles HTTP chunks;
// don't deserialize http.getStream() directly (chunk framing is not JSON).
void *allocateBody(size_t n) {
  void *p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}
class Body : public Stream {
public:
  static constexpr size_t limit=192*1024;
  BodyBuffer buffer;
  explicit Body(size_t capacity) : buffer(capacity,allocateBody) {}
  size_t write(uint8_t c) override {return write(&c,1);}
  size_t write(const uint8_t *p,size_t n) override {return buffer.append(p,n);}
  int available() override {return 0;}
  int read() override {return -1;}
  int peek() override {return -1;}
  void flush() override {}
};
// Portal documents prefer PSRAM so an open settings page does not compete
// with TLS buffers for the smaller internal heap.
struct PortalAllocator {
  void *allocate(size_t size) {return allocateBody(size);}
  void deallocate(void *value) {free(value);}
  void *reallocate(void *value,size_t size) {
    if(!size) {free(value);return nullptr;}
    void *next=heap_caps_realloc(value,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return next?next:realloc(value,size);
  }
};
using PortalDocument=BasicJsonDocument<PortalAllocator>;
// Network task only: JWTs are kept in RAM and never written to Preferences.
String jwt;
int64_t jwtExpiry=0;
uint32_t authFailedAt=0;
bool authCooling=false;
char lastAuthError[48]={};
String urlEncode(const char *value) {
  String out;
  while (*value) {
    uint8_t c=uint8_t(*value++);
    if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.' || c=='~') out+=char(c);
    else {char encoded[4];snprintf(encoded,sizeof(encoded),"%%%02X",c);out+=encoded;}
  }
  return out;
}
void authFailure(const char *message) {
  jwt="";jwtExpiry=0;authCooling=true;authFailedAt=millis();
  strlcpy(lastAuthError,message,sizeof(lastAuthError));
}
void tlsFailure(SecureClient &tls,const char *label,int httpCode,char *error,size_t cap) {
  char detail[160]={};int code=tls.lastError(detail,sizeof(detail));uint32_t flags=tls.verificationFlags();
  if(flags || code==MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)strlcpy(error,tlsdiagnostics::message(flags),cap);
  else if(code<0)snprintf(error,cap,"TLS error -0x%04X",unsigned(-code));
  else snprintf(error,cap,"Network error %d",httpCode);
  Serial.printf("%s HTTP-client=%d TLS=%d verify=0x%08lX %s; UTC=%lld free=%u largest=%u roots=%u verifier=Tab5-root-key\n",
    label,httpCode,code,(unsigned long)flags,detail,(long long)time(nullptr),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
    unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(CA_BUNDLE_ROOT_COUNT));
}
bool ensureJwt(char *error,size_t cap) {
  int64_t now=time(nullptr);
  if (!nsapi::renewNeeded(jwt.length()>0,jwtExpiry,now)) return true;
  if (authCooling && uint32_t(millis()-authFailedAt)<30000) {
    strlcpy(error,lastAuthError,cap);return false;
  }
  auto fail=[&](const char *message){authFailure(message);strlcpy(error,message,cap);return false;};
  if (now<1767225600) return fail("V3 auth needs valid clock");
  SecureClient tls(caBundleStart);
  HTTPClient http;http.setUserAgent("ESP32HTTPClient");http.setConnectTimeout(10000);http.setTimeout(20000);http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  // This standard NS endpoint necessarily carries the subject token in its
  // TLS-protected path. Never print the path, credentials or response body.
  if (!http.begin(tls,String(netSettings.url)+"/api/v2/authorization/request/"+urlEncode(netSettings.token)))
    return fail("V3 auth setup failed");
  http.addHeader("Accept","application/json");http.addHeader("Accept-Encoding","identity");
  int code=http.GET();
  if (code!=200) {
    char message[48];
    if(code<0)tlsFailure(tls,"Auth",code,message,sizeof(message));
    else snprintf(message,sizeof(message),"Auth HTTP %d",code);
    http.end();
    return fail(message);
  }
  constexpr size_t maxAuth=8192;
  if (http.getSize()>int(maxAuth)) {http.end();return fail("Auth response too large");}
  Body body(http.getSize()>=0?size_t(http.getSize()):maxAuth);
  if (!body.buffer.data) {http.end();return fail("Auth memory unavailable");}
  int got=http.writeToStream(&body),expected=http.getSize();http.end();
  if (got<0 || body.buffer.overflow || (expected>=0 && got!=expected)) return fail("Auth response incomplete");
  DynamicJsonDocument filter(256);filter["token"]=true;filter["exp"]=true;
  DynamicJsonDocument doc(6144);
  auto err=deserializeJson(doc,body.buffer.data,body.buffer.length,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(8));
  const char *value=nullptr;int64_t expiry=0;
  if (err || !nsapi::tokenResponse(doc.as<JsonVariantConst>(),time(nullptr),value,expiry))
    return fail("Auth token/expiry invalid");
  jwt="";
  if (!jwt.concat(value)) return fail("Auth token memory unavailable");
  jwtExpiry=expiry;authCooling=false;lastAuthError[0]=0;
  return true;
}
bool fetch(const String &path, Resource resource, Snapshot &s, int64_t now, char *error, size_t cap, bool allowRenew=true,model::Treatments *accumulator=nullptr,size_t *rowCount=nullptr) {
  if (netSettings.v3 && !ensureJwt(error,cap)) return false;
  const bool glucose=resource==Resource::Glucose;
  const char *label=glucose?"SGV":resource==Resource::Status?"AAPS":"Treatments";
  SecureClient tls(caBundleStart);
  HTTPClient http;
  http.setUserAgent("ESP32HTTPClient");
  http.setConnectTimeout(10000);
  http.setTimeout(20000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(tls,String(netSettings.url)+path)) {strlcpy(error,"HTTPS setup failed",cap); return false;}
  http.addHeader("Accept","application/json");
  http.addHeader("Accept-Encoding","identity");
  if (netSettings.v3) http.addHeader("Authorization","Bearer "+jwt);
  else http.addHeader("api-secret",netSettings.token);
  int code=http.GET();
  if (netSettings.v3 && code==401 && allowRenew) {
    http.end();jwt="";jwtExpiry=0;
    if (!ensureJwt(error,cap)) return false;
    return fetch(path,resource,s,time(nullptr),error,cap,false,accumulator,rowCount);
  }
  if (netSettings.v3 && code==401) authFailure("V3 access denied after renewal");
  bool ok=false;
  if (code!=200) {
    if (code<0) {
      tlsFailure(tls,label,code,error,cap);
    }
    else snprintf(error,cap,"HTTP %d%s",code,(code==401 || code==403)?" check read token":"");
  } else if (http.getSize()>int(Body::limit)) strlcpy(error,"Response too large",cap);
  else {
    const int declared=http.getSize();
    Body body(declared>=0 ? size_t(declared) : Body::limit);
    if (!body.buffer.data) {
      strlcpy(error,"Response memory unavailable",cap);
      http.end(); return false;
    }
    int got=http.writeToStream(&body);
    const int expected=http.getSize(); // chunked decoding may set this now
    Serial.printf("%s body: result=%d bytes=%u expected=%d limit=%u overflow=%d\n",
      label,got,unsigned(body.buffer.length),expected,
      unsigned(body.buffer.capacity),body.buffer.overflow);
    if (body.buffer.overflow) strlcpy(error,"Response exceeds 192 KiB",cap);
    else if (got<0) snprintf(error,cap,"Body read %d (%u bytes)",got,unsigned(body.buffer.length));
    else if (expected>=0 && got!=expected) snprintf(error,cap,"Short body %d/%d",got,expected);
    else {
      // Filter retains supported glucose forecasts and removes unused subtrees.
      DynamicJsonDocument items(4096),filter(6144);
      if (resource==Resource::Treatments) model::treatmentFilter(items);
      else model::makeFilter(items,glucose);
      nsapi::responseFilter(filter,items,netSettings.v3);
      // PSRAM-backed and sized from the body (zero-copy strings), so a wider
      // devicestatus window cannot exhaust a fixed 64 KiB pool.
      PortalDocument doc(std::min<size_t>(std::max<size_t>(65536,body.buffer.length*4),1024*1024));
      if(!doc.capacity()) {strlcpy(error,"JSON memory unavailable",cap);http.end();return false;}
      auto err=deserializeJson(doc,body.buffer.data,body.buffer.length,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(16));
      if (err) strlcpy(error,"Invalid/oversized JSON",cap);
      else {
        JsonVariantConst collection=nsapi::collection(doc.as<JsonVariantConst>(),netSettings.v3);
        if (resource==Resource::Glucose) ok=model::entries(collection,now,s.history);
        else if (resource==Resource::Status) ok=model::statuses(collection,netSettings.device,now,s.status);
        else {
          if(rowCount)*rowCount=collection.size();
          ok=model::treatments(collection,now-int64_t(pollHours)*3600,now,accumulator?*accumulator:s.treatments,accumulator!=nullptr,accumulator==nullptr);
        }
        strlcpy(error,ok?"":netSettings.v3?"Invalid V3 result/status":"Expected JSON array",cap);
        if(ok && glucose && collection.size()>=model::MAX_POINTS) strlcpy(error,"SGV history limited",cap);
      }
    }
  }
  http.end(); return ok;
}
bool connectSavedWifi() {
  if(WiFi.status()==WL_CONNECTED) return true;
  for(unsigned i=0;i<netSettings.wifiCount;++i) {
    const auto &network=netSettings.wifi[i];
    WiFi.disconnect(false);WiFi.begin(network.ssid,network.password);
    for(int wait=0;wait<40 && WiFi.status()!=WL_CONNECTED;++wait) {
      if(portalRequested.load()) return false;
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    if(WiFi.status()==WL_CONNECTED && WiFi.SSID()==network.ssid) return true;
  }
  return false;
}
// The network task owns all Wi-Fi mutations. Normal Nightscout fetches pause
// for setup; the UI continues aging cached readings and running enabled alerts.
void runWifiSetup() {
  snprintf(portalName,sizeof(portalName),"Tab5-Setup-%04X",unsigned(esp_random()&0xffff));
  snprintf(portalPassword,sizeof(portalPassword),"%08X%08X",unsigned(esp_random()),unsigned(esp_random()));
  WiFi.setAutoReconnect(false);WiFi.mode(WIFI_AP_STA);
  if(!WiFi.softAP(portalName,portalPassword,1,0,1)) {portalState.store(5);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(false);return;}
  WebServer server(80);DNSServer dns;
  dns.start(53,"*",WiFi.softAPIP());
  bool success=false;
  auto page=[&]() {
    if(server.client().localIP()!=WiFi.softAPIP()) {server.send(403,"text/plain","Join the Tab5 setup Wi-Fi first.");return;}
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"text/html",R"HTML(<!doctype html><html><meta name="viewport" content="width=device-width,initial-scale=1"><title>Tab5 Wi-Fi</title><body style="font:18px sans-serif;max-width:420px;margin:30px auto;padding:15px"><h2>Tab5 Wi-Fi</h2><p>Enter a 2.4 GHz network. Leave the password empty for an open network.</p><form method="post" action="/save"><p><label>Network name<br><input name="ssid" maxlength="32" required></label></p><p><label>Password<br><input name="password" type="password" maxlength="63" autocomplete="new-password"></label></p><button>Test and save</button></form><p>The previous settings are kept if connection fails.</p></body></html>)HTML");
  };
  server.on("/",HTTP_GET,page);server.onNotFound(page);
  server.on("/save",HTTP_POST,[&]() {
    if(server.client().localIP()!=WiFi.softAPIP()) {server.send(403,"text/plain","Join the Tab5 setup Wi-Fi first.");return;}
    server.sendHeader("Cache-Control","no-store");
    String ssid=server.arg("ssid"),password=server.arg("password");
    quicksettings::Credentials c;
    if(ssid.length()>32 || password.length()>63) {server.send(400,"text/plain","Network name or password too long.");return;}
    strlcpy(c.ssid,ssid.c_str(),sizeof(c.ssid));strlcpy(c.password,password.c_str(),sizeof(c.password));
    if(strlen(c.ssid)!=ssid.length() || strlen(c.password)!=password.length() || !quicksettings::validCredentials(c)) {server.send(400,"text/plain","Enter a network name and an empty or 8-63 character password.");return;}
    portalState.store(3);WiFi.disconnect(false);
    for(int i=0;i<100 && WiFi.status()==WL_CONNECTED && !portalCancel.load();++i) vTaskDelay(pdMS_TO_TICKS(20));
    WiFi.begin(c.ssid,c.password);
    auto connected=[&](){return WiFi.status()==WL_CONNECTED && WiFi.SSID()==ssid;};
    for(int i=0;i<80 && !connected() && !portalCancel.load();++i) vTaskDelay(pdMS_TO_TICKS(250));
    if(!connected() || portalCancel.load()) {
      portalState.store(5);server.send(400,"text/plain","Connection failed. Previous settings kept. Retry or cancel on the Tab5.");return;
    }
    runtimecfg::Settings candidate;uint32_t revision;currentSettings(candidate,revision);
    bool stored=runtimecfg::promoteWifi(candidate,c);
    char saveError[64]={};
    if(stored) stored=storeSettings(candidate,revision,saveError,sizeof(saveError));
    if(!stored) {portalState.store(5);server.send(500,"text/plain","Could not save. Previous settings kept.");return;}
    success=true;
    server.sendHeader("Cache-Control","no-store");server.send(200,"text/plain","Connected and saved. You can reconnect your phone to your usual network.");
    portalState.store(4);
  });
  server.begin();portalState.store(2);uint32_t started=millis();
  while(!success && !portalCancel.load() && uint32_t(millis()-started)<300000) {
    dns.processNextRequest();server.handleClient();vTaskDelay(pdMS_TO_TICKS(10));
  }
  if(success) vTaskDelay(pdMS_TO_TICKS(500));
  else portalState.store(portalCancel.load()?7:6);
  server.stop();dns.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);WiFi.disconnect(false);
}
#include "ns_live.h"
NightscoutLive *liveClient=nullptr;
void serviceLiveDuringHttp(){
 static bool servicing=false;static uint32_t last=0;
 if(!liveClient||servicing||xTaskGetCurrentTaskHandle()!=networkTask||uint32_t(millis()-last)<20)return;
 servicing=true;last=millis();liveClient->poll();liveOnline.store(liveClient->online());servicing=false;
}
void worker(void*) {
  Snapshot &s=*workerSnapshot;
  NightscoutLive live(caBundleStart);liveClient=&live;
  unsigned requested=nslive::All;uint32_t lastFull=0;
  void *treatmentMemory=heap_caps_malloc(sizeof(model::Treatments),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  auto *staged=treatmentMemory?new(treatmentMemory) model::Treatments:nullptr;
  unsigned failures=0;
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(false);
  String lastTimezone,lastNtp1,lastNtp2;
  for (;;) {
    xSemaphoreTake(networkGate,portMAX_DELAY);
    if(portalRequested.exchange(false)) {
      live.close(true);liveOnline.store(false);
      if(portalCancel.load()) portalState.store(7);
      else {apSetupActive.store(true);runWifiSetup();apSetupActive.store(false);}
    }
    uint32_t revision,epoch;currentSettings(netSettings,revision,&epoch);
    if(s.connectionEpoch!=epoch) {
      live.close(true);liveOnline.store(false);requested=nslive::All;
      resetSnapshot(s);s.connectionEpoch=epoch;jwt="";jwtExpiry=0;authCooling=false;
    }
    if(lastTimezone!=netSettings.timezone || lastNtp1!=netSettings.ntp1 || lastNtp2!=netSettings.ntp2) {
      configTzTime(netSettings.timezone,netSettings.ntp1,netSettings.ntp2);
      lastTimezone=netSettings.timezone;lastNtp1=netSettings.ntp1;lastNtp2=netSettings.ntp2;
    }
    if(reconnectRequested.exchange(false)) {live.close(true);liveOnline.store(false);vTaskDelay(pdMS_TO_TICKS(500));WiFi.disconnect(false);}
    if(!liveUpdatesEnabled.load()||!netSettings.v3){live.close(true);liveOnline.store(false);}
    Serial.printf("Refresh: collection mask=%u%s\n",requested,requested==nslive::All?" full-day backfill":" live change");
    pollHours=24; // Cache a full day so touch panning never waits on HTTPS.
    networkBusy.store(true);
    bool good=false;
    if(!connectSavedWifi()) {
      strlcpy(s.glucoseError,"WiFi offline",sizeof(s.glucoseError));
      strlcpy(s.statusError,"WiFi offline",sizeof(s.statusError));
    } else if(!configured()) {
      strlcpy(s.glucoseError,"Configure Nightscout",sizeof(s.glucoseError));
      strlcpy(s.statusError,"Use LAN portal or config.h",sizeof(s.statusError));
    } else {
        int64_t now=time(nullptr);
        if (now<1767225600) {
          strlcpy(s.glucoseError,"Waiting for NTP clock",sizeof(s.glucoseError));
          strlcpy(s.statusError,"TLS needs valid time",sizeof(s.statusError));
        } else {
          // Keep a monotonic anchor so an NTP outage never freezes data age.
          s.clockAt=now; s.monoAt=monotonicSeconds();
          const long long sinceMs=(now-int64_t(pollHours)*3600)*1000;
          char since[32],until[32];snprintf(since,sizeof(since),"%lld",sinceMs);
          snprintf(until,sizeof(until),"%lld",(long long)(now+120)*1000);
          String entryPath=netSettings.v3?
            String("/api/v3/entries?type$eq=sgv&date$gte=")+since+"&date$lte="+until+
              "&sort$desc=date&limit=288&fields=date,sgv,direction,isValid,isDeleted":
            String("/api/v1/entries/sgv.json?count=288&find[date][$gte]=")+since;
          bool a=!(requested&nslive::Entries)||fetch(entryPath,Resource::Glucose,s,now,s.glucoseError,sizeof(s.glucoseError));
          String path=netSettings.v3?
            "/api/v3/devicestatus?sort$desc=date&limit=10&fields=date,created_at,device,openaps,pump,isValid,isDeleted":
            "/api/v1/devicestatus.json?count=2&find[openaps][$exists]=true";
          if (*netSettings.device) path+=String(netSettings.v3?"&device$eq=":"&find[device]=")+urlEncode(netSettings.device);
          bool b=!(requested&nslive::Status)||fetch(path,Resource::Status,s,time(nullptr),s.statusError,sizeof(s.statusError));
          String treatmentPath;
          if (netSettings.v3) {
            char current[32];snprintf(current,sizeof(current),"%lld",(long long)time(nullptr)*1000);
            treatmentPath=String("/api/v3/treatments?date$gte=")+since+"&date$lte="+current+
              "&sort$desc=date&limit=200&fields=identifier,date,created_at,mills,timestamp,carbs,insulin,isSMB,type,isValid,isDeleted,eventType,pumpId,pumpType,pumpSerial";
          } else {
            time_t windowStart=time(nullptr)-int64_t(pollHours)*3600;
            struct tm utc;gmtime_r(&windowStart,&utc);
            char iso[32];strftime(iso,sizeof(iso),"%Y-%m-%dT%H:%M:%S.000Z",&utc);
            char legacy[320];snprintf(legacy,sizeof(legacy),
              "/api/v1/treatments.json?count=200&find[$or][0][date][$gte]=%lld&find[$or][1][created_at][$gte]=%s",
              (long long)windowStart*1000,iso);
            treatmentPath=legacy;
          }
          bool c=true;
          if(!(requested&nslive::Treatments)) {}
          else if(netSettings.v3 && staged) {
            staged->count=0;staged->limited=false;c=true;
            unsigned pages=0;size_t rows=0;
            for(;pages<16;++pages) {
              String page=treatmentPath+"&skip="+String(pages*200);
              if(!fetch(page,Resource::Treatments,s,now,s.treatmentError,sizeof(s.treatmentError),true,staged,&rows)) {c=false;break;}
              if(rows<200) {++pages;break;}
              vTaskDelay(1); // Give radio/idle work an explicit turn between pages.
            }
            if(c) {
              if(rows>=200)staged->limited=true; // Bounded request budget, not a full page alone.
              s.treatments=*staged;
              Serial.printf("Treatments cache: pages=%u markers=%u truncated=%u\n",pages,unsigned(staged->count),unsigned(staged->limited));
            }
          } else if(netSettings.v3) {c=false;strlcpy(s.treatmentError,"Treatment PSRAM unavailable",sizeof(s.treatmentError));}
          else c=fetch(treatmentPath,Resource::Treatments,s,now,s.treatmentError,sizeof(s.treatmentError));
          good=a&&b&&c;
        }
    }
    networkBusy.store(false);
    publishSnapshot(s);
    xSemaphoreGive(networkGate);
    failures=good?0:std::min(failures+1,4u);
    if(good&&requested==nslive::All)lastFull=millis();
    // Failed snapshots retry as a full refresh; partial successes never hide errors.
    unsigned wait=good?netSettings.poll:std::min(15u*(1u<<failures),300u);
    uint32_t finished=millis();
    unsigned safety=std::max(30u,std::min(300u,std::min(netSettings.sgvStale/2,netSettings.apsStale/2)));
    for(;;){
      if(ulTaskNotifyTake(pdTRUE,0)){requested=nslive::All;break;}
      bool eligible=liveUpdatesEnabled.load()&&netSettings.v3&&configured()&&WiFi.status()==WL_CONNECTED&&time(nullptr)>=1767225600;
      if(!eligible){live.close(true);liveOnline.store(false);}
      else {
        // Only the network task owns the live socket. No nested TLS connection
        // attempts inside REST; the hook above only services existing heartbeats.
        if(live.due()&&xSemaphoreTake(networkGate,0)==pdTRUE){live.connect(netSettings.url,netSettings.token);xSemaphoreGive(networkGate);}
        live.poll();liveOnline.store(live.online());
        if(live.recovered){live.recovered=false;live.pending={};requested=nslive::All;break;}
      }
      if(good&&live.online()){
        if(uint32_t(millis()-lastFull)>=safety*1000){requested=nslive::All;break;}
        unsigned dirty=live.pending.take(millis());if(dirty){requested=dirty;break;}
      }else if(uint32_t(millis()-finished)>=wait*1000){requested=nslive::All;break;}
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
}
bool testNightscoutConnection(const runtimecfg::Settings &settings,char *error,size_t cap) {
  if(!settings.token[0] || !strcmp(settings.token,"YOUR_READ_ONLY_ACCESS_TOKEN")) {strlcpy(error,"Set a read-only Nightscout token",cap);return false;}
  if(time(nullptr)<1767225600) {strlcpy(error,"Wait for a valid NTP clock before testing HTTPS",cap);return false;}
  String accessToken;
  auto get=[&](const String &path,Body &body,bool authenticate) {
    SecureClient tls(caBundleStart);
    HTTPClient http;http.setUserAgent("ESP32HTTPClient");http.setConnectTimeout(10000);http.setTimeout(20000);http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(!http.begin(tls,String(settings.url)+path)) return -1;
    http.addHeader("Accept","application/json");http.addHeader("Accept-Encoding","identity");
    if(authenticate) {if(settings.v3)http.addHeader("Authorization","Bearer "+accessToken);else http.addHeader("api-secret",settings.token);}
    int code=http.GET();
    if(code<0)tlsFailure(tls,"Portal-test",code,error,cap);
    if(code==200) {int got=http.writeToStream(&body);int expected=http.getSize();if(got<0 || body.buffer.overflow || (expected>=0 && got!=expected))code=-2;}
    http.end();return code;
  };
  if(settings.v3) {
    Body auth(8192);if(!auth.buffer.data) {strlcpy(error,"Test response allocation failed",cap);return false;}
    int code=get(String("/api/v2/authorization/request/")+urlEncode(settings.token),auth,false);
    if(code!=200) {if(code>0)snprintf(error,cap,"Auth HTTP %d",code);else strlcpy(error,"Auth TLS/network or response failed",cap);return false;}
    PortalDocument filter(256),doc(6144);filter["token"]=true;filter["exp"]=true;
    auto jsonError=deserializeJson(doc,auth.buffer.data,auth.buffer.length,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(8));
    const char *token=nullptr;int64_t expires=0;
    if(jsonError || !nsapi::tokenResponse(doc.as<JsonVariantConst>(),time(nullptr),token,expires)) {strlcpy(error,"Invalid authorization response",cap);return false;}
    accessToken=token;
  }
  Body body(8192);if(!body.buffer.data) {strlcpy(error,"Test response allocation failed",cap);return false;}
  int code=get(settings.v3?"/api/v3/entries?type$eq=sgv&limit=1&fields=date,sgv":"/api/v1/entries/sgv.json?count=1",body,true);
  if(code!=200) {if(code>0)snprintf(error,cap,"Entries HTTP %d",code);else strlcpy(error,"Entries TLS/network or response failed",cap);return false;}
  PortalDocument doc(6144);
  if(deserializeJson(doc,body.buffer.data,body.buffer.length,DeserializationOption::NestingLimit(8)) || nsapi::collection(doc.as<JsonVariantConst>(),settings.v3).isNull()) {
    strlcpy(error,"Invalid entries response",cap);return false;
  }
  return true;
}
#include "lan_service.h"
String amount(float n, unsigned digits=2) {return std::isfinite(n)?String(n,digits):String("--");}
String compactAmount(float value) {
  String text=amount(value,2);
  while (text.indexOf('.')>=0 && text.endsWith("0")) text.remove(text.length()-1);
  if (text.endsWith(".")) text.remove(text.length()-1);
  return text;
}
String age(int64_t t,int64_t now) {
  if (!model::validTime(t,now)) return "--";
  int64_t m=std::max(int64_t(0),now-t)/60;
  return m>999 ? ">999m" : String(long(m))+"m";
}
#include "tab5_ui.h"
void updateGlucoseAlerts(int64_t now) {
 static bool retrying=false;static alerts::Event retryEvent=alerts::Event::None;static uint32_t lastAttempt=0;
 uint32_t tick=millis();bool have=ui->history.count>0;
 model::Point latest=have?ui->history.p[ui->history.count-1]:model::Point{};
 bool fresh=have&&model::fresh(latest.t,now,uiSettings.sgvStale);
 if(!M5.Speaker.isPlaying()){activeGlucoseAudio=-1;previewSoundActive=false;}
 int band=fresh?(latest.sgv<uiSettings.low?int(alertaudio::Low):latest.sgv>uiSettings.high?int(alertaudio::High):-1):-1;
 if(activeGlucoseAudio>=0&&band!=activeGlucoseAudio){M5.Speaker.stop();activeGlucoseAudio=-1;}
 alerts::Event event=glucoseAlerts.poll(latest.sgv,fresh,tick,uiSettings.low,uiSettings.high);
 if(event==alerts::Event::None){retrying=false;return;}
 if(retrying&&retryEvent==event&&uint32_t(tick-lastAttempt)<30000)return;
 if(previewSoundActive||staleSoundActive){M5.Speaker.stop();previewSoundActive=staleSoundActive=false;stalePlayingMask=0;}
 if(M5.Speaker.isPlaying())return;
 lastAttempt=tick;retryEvent=event;unsigned which=event==alerts::Event::Low?alertaudio::Low:alertaudio::High;
 if(playAlertClip(which)){glucoseAlerts.played(event,tick);activeGlucoseAudio=which;retrying=false;}
 else{retrying=true;Serial.println("Glucose sound could not be queued; will retry while fresh.");}
}
void updateStaleAlerts(int64_t now) {
 static bool startupGraceDone=false,retrying=false;static uint32_t lastAttempt=0;
 uint32_t tick=millis();if(tick>=600000)startupGraceDone=true;
 bool have=ui->history.count>0;int64_t bg=have?ui->history.p[ui->history.count-1].t:0;
 bool bgStale=(have||startupGraceDone)&&!model::fresh(bg,now,alerts::STALE_SECONDS);
 bool loopStale=(ui->status.loopTime>0||startupGraceDone)&&!model::fresh(ui->status.loopTime,now,alerts::STALE_SECONDS);
 uint8_t current=(bgStale?1:0)|(loopStale?2:0);
 uint8_t pending=staleAlerts.poll(bgStale,loopStale,glucoseAlerts.isEnabled());
 if(staleSoundActive){
  if(!(current&stalePlayingMask)||!glucoseAlerts.isEnabled()){M5.Speaker.stop();staleSoundActive=false;stalePlayingMask=0;}
  else if(!M5.Speaker.isPlaying()){staleSoundActive=false;stalePlayingMask=0;}
  else return;
 }
 if(!pending){retrying=false;return;}
 if(retrying&&uint32_t(tick-lastAttempt)<30000)return;
 if(previewSoundActive){M5.Speaker.stop();previewSoundActive=false;}
 if(M5.Speaker.isPlaying())return;
 uint8_t bit=(pending&1)?1:2;unsigned which=bit==1?alertaudio::GlucoseStale:alertaudio::LoopStale;
 if(playAlertClip(which)){staleAlerts.played(bit);stalePlayingMask=bit;staleSoundActive=true;retrying=false;}
 else{retrying=true;lastAttempt=tick;}
}
bool applyUiSettings(bool force=false) {
  if(!force && uiRevision==settingsRevision.load())return false;
  runtimecfg::Settings next;uint32_t revision,epoch;currentSettings(next,revision,&epoch);
  bool changedThresholds=uiSettings.low!=next.low || uiSettings.high!=next.high;
  uiSettings=next;uiRevision=revision;mmol=next.mmol;showForecast=next.forecast;graphHours.store(next.hours);
  if(changedThresholds && glucoseAlerts.isEnabled())glucoseAlerts.setEnabled(false);
  glucoseAlerts.setEnabled(next.sound);
  M5.Display.setBrightness(settingsOpen?std::max(20u,next.brightness):next.brightness);M5.Speaker.setVolume(alerts::alarmVolume(next.volume));
  if(!next.sound) {M5.Speaker.stop();staleSoundActive=previewSoundActive=false;activeGlucoseAudio=-1;stalePlayingMask=0;}
  if(uiEpoch!=epoch) {
    glucoseAlerts.setEnabled(false);glucoseAlerts.setEnabled(next.sound);staleAlerts=alerts::StaleState{};
    if(staleSoundActive)M5.Speaker.stop();staleSoundActive=false;
    int64_t clock=ui->clockAt,mono=ui->monoAt;resetSnapshot(*ui);ui->clockAt=clock;ui->monoAt=mono;ui->connectionEpoch=epoch;uiEpoch=epoch;
  }
  rendered=false;return true;
}
void saveDisplaySettings() {
  runtimecfg::Settings next;uint32_t revision;currentSettings(next,revision);
  next.hours=graphHours.load();next.mmol=mmol;next.forecast=showForecast;next.sound=glucoseAlerts.isEnabled();
  storeSettings(next,revision,settingsMessage,sizeof(settingsMessage));applyUiSettings(true);
}
void toggleGlucoseAlerts() {
  glucoseAlerts.setEnabled(!glucoseAlerts.isEnabled());saveDisplaySettings();
  if(!settingsOpen)rendered=false;
  updateGlucoseAlerts(displayNow());
}
void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("Tab5 startup: initializing hardware");
  Serial.flush();
  auto c=M5.config(); c.internal_imu=false; c.internal_mic=false; c.internal_rtc=true;
#ifdef RUN_PARSER_TESTS
  c.internal_spk=false;
#else
  c.internal_spk=true;
#endif
  M5.begin(c);
  Serial.println("Tab5 startup: hardware initialized");
  // ES8388 receives standard two-slot I2S even for mono source clips.
  auto speakerConfig=M5.Speaker.config();speakerConfig.stereo=true;speakerConfig.sample_rate=48000;M5.Speaker.config(speakerConfig);
  M5.Display.setRotation(1);
  WiFi.setPins(12,13,11,10,9,8,15);
  if(M5.Display.width()!=1280 || M5.Display.height()!=720) { Serial.begin(115200); Serial.println("Unsupported display dimensions; expected landscape Tab5"); for(;;)delay(1000); } M5.Display.setBrightness(cfg::BRIGHTNESS);
  Serial.begin(115200);
  // Install once, before either worker can create a TLS context.
  if(!project_tls_memory_init()) {
    Serial.println("TLS memory allocator initialization failed");
    line(20,"TLS memory setup failed",TFT_RED);for(;;)delay(1000);
  }
  Serial.printf("TLS memory: PSRAM preferred with internal fallback, free=%u internal=%u\n",
    unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
  Serial.printf("TLS trust: Mozilla/certifi %s roots=%u verifier=Tab5-root-key bundle=%s\n",CA_BUNDLE_SOURCE_VERSION,unsigned(CA_BUNDLE_ROOT_COUNT),TAB5_CA_BUNDLE_SHA256);
  M5.Speaker.setVolume(alerts::VOLUME);
#ifdef RUN_PARSER_TESTS
  if(xTaskCreatePinnedToCore([](void*) {runParserTests();vTaskDelete(nullptr);},"parser-tests",49152,nullptr,1,nullptr,1)!=pdPASS)
    Serial.println("Parser test task allocation failed");
  for (;;) delay(1000);
#endif
  settingsMutex=xSemaphoreCreateMutex();networkGate=xSemaphoreCreateMutex();
  snapshotMutex=xSemaphoreCreateMutex();lanUpdates=xQueueCreate(1,sizeof(LanStatus));
  ui=newSnapshot();pendingSnapshot=newSnapshot();workerSnapshot=newSnapshot();
  if(!settingsMutex || !networkGate || !snapshotMutex || !lanUpdates || !ui || !pendingSnapshot || !workerSnapshot) {
    line(20,"Memory allocation failed",TFT_RED);for(;;)delay(1000);
  }
  Serial.printf("Snapshots: 3 x %u bytes in %s; internal free=%u\n",unsigned(sizeof(Snapshot)),
    esp_ptr_external_ram(ui)&&esp_ptr_external_ram(pendingSnapshot)&&esp_ptr_external_ram(workerSnapshot)?"PSRAM":"internal RAM",
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
  loadRuntimeSettings();
  Serial.printf("RTC hardware: %s; stores UTC\n",M5.Rtc.isEnabled()?"detected":"not detected");
  esp_sntp_set_time_sync_notification_cb([](struct timeval *value) {
    if(value && clockdisplay::valid(value->tv_sec))rtcSyncPending.store(true);
  });
  Preferences tab; if(tab.begin("ns-tab5",true)){nightMode=tab.getBool("night",false);clock12=tab.getBool("clock-12",false);rotated180=tab.getBool("rotate-180",false);liveUpdatesEnabled.store(tab.getBool("live-updates",true));if(tab.getBytesLength("alert-audio")==sizeof(audioSettings)){
    alertaudio::Settings saved;if(tab.getBytes("alert-audio",&saved,sizeof(saved))==sizeof(saved)&&alertaudio::valid(saved))audioSettings=saved;
  }tab.end();}
  M5.Display.setRotation(rotated180?3:1);
  applyUiSettings(true);
  // HTTP/TLS receive loops may yield without blocking; share idle priority so
  // CPU0 idle/watchdog work can still run during a slow multi-page response.
  if(xTaskCreatePinnedToCore(worker,"network",49152,nullptr,0,&networkTask,0)!=pdPASS ||
     xTaskCreatePinnedToCore(lanWorker,"lan-portal",16384,nullptr,1,nullptr,0)!=pdPASS) {
    line(20,"Task allocation failed",TFT_RED);for(;;)delay(1000);
  }
  draw();
  // Every alarm runs in loop(). If it hangs, reboot instead of silently not alarming.
  // 30 s keeps normal NVS writes and full redraws far inside the budget.
  esp_task_wdt_config_t watchdog={};watchdog.timeout_ms=30000;watchdog.idle_core_mask=1u<<0;watchdog.trigger_panic=true;
  esp_err_t wdt=esp_task_wdt_reconfigure(&watchdog);
  if(wdt==ESP_ERR_INVALID_STATE)wdt=esp_task_wdt_init(&watchdog);
  enableLoopWDT();
  Serial.printf("Loop watchdog: %s (30 s, reboot on hang)\n",wdt==ESP_OK?"enabled":"configure failed");
}
void loop() {
  static int pressX=0,pressY=0;
  M5.update();
  if(rtcSyncPending.exchange(false) && M5.Rtc.isEnabled()) {
    time_t at=time(nullptr);tm utc={};
    if(clockdisplay::valid(at)) {
      gmtime_r(&at,&utc);M5.Rtc.setDateTime(&utc);
      m5::rtc_datetime_t stored;
      if(M5.Rtc.getDateTime(&stored))Serial.printf("RTC readback UTC: %04d-%02d-%02d %02d:%02d:%02d\n",stored.date.year,stored.date.month,stored.date.date,stored.time.hours,stored.time.minutes,stored.time.seconds);
    }
  }
  bool changed=applyUiSettings();
  bool lanChanged=xQueueReceive(lanUpdates,&lanStatus,0)==pdTRUE;
  if(receiveSnapshot()) {
    if(ui->connectionEpoch!=uiEpoch) {resetSnapshot(*ui);ui->connectionEpoch=uiEpoch;}
    changed=true;
  }
  for(size_t i=0;i<M5.Touch.getCount();++i) {
    auto touch=M5.Touch.getDetail(i);
    if(touch.wasPressed()) {pressX=touch.x;pressY=touch.y;}
    if(touch.wasReleased()) {
      int delta=touch.x-pressX;
      if(!settingsOpen && pressY>=tabgraph::top && pressY<=tabgraph::bottom && abs(delta)>100) {
        historyOffset=std::max(int64_t(0),std::min(int64_t(86400-graphHours.load()*3600),historyOffset+(delta>0?3600:-3600)));selection="";
      } else tabTouch(touch.x,touch.y);
      changed=true;break;
    }
  }
  int64_t now=displayNow();
  updateGlucoseAlerts(now);updateStaleAlerts(now);
  static uint32_t lastDraw=0;
  if(changed || lanChanged || !rendered || uint32_t(millis()-lastDraw)>=1000) {
    if(settingsOpen) drawSettings(); else draw();
    lastDraw=millis();rendered=true;
  }
  delay(10);
}

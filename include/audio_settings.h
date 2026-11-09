#pragma once
#include <stdint.h>
namespace alertaudio {
constexpr unsigned ChoiceCount=20,IntensityCount=3,AlertCount=4;
enum Alert:unsigned {Low=0,High=1,GlucoseStale=2,LoopStale=3};
struct Settings {uint8_t version=1,intensity=1,choice[4]={0,1,2,3},reserved[2]={};};
inline bool valid(const Settings &s){if(s.version!=1||s.intensity>=3)return false;for(auto c:s.choice)if(c>=ChoiceCount)return false;return true;}
inline unsigned clipIndex(unsigned choice,unsigned intensity){if(choice>=ChoiceCount||intensity>=3)return 0;return choice<12?choice*3+intensity:36+choice-12;}
inline const char *intensityName(unsigned i){static const char *names[]={"Mild","Medium","Harsh"};return names[i<3?i:1];}
inline const char *alertName(unsigned i){static const char *names[]={"Low glucose","High glucose","Glucose stale","Loop stale"};return names[i<4?i:0];}
inline const char *choiceName(unsigned i){static const char *names[]={"Acoustic / Low","Acoustic / High","Acoustic / Time-sensitive","Acoustic / Device critical","Acoustic / Low soon","Acoustic / Urgent low","Electronic / Low","Electronic / High","Electronic / Time-sensitive","Electronic / Device critical","Electronic / Low soon","Electronic / Urgent low","Glass bell","Hard saw wave","Harp","Meow","Open can","Open wrapper","Saw wave","Sine wave"};return names[i<ChoiceCount?i:0];}
}

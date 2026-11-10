#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>

namespace mwt {
// Store portable #RRGGBB values, not platform-dependent native COLORREFs.
inline std::optional<uint32_t> parseColor(std::string value) {
  if(!value.empty()&&value.front()=='#')value.erase(0,1);
  if(value.size()!=6)return {};
  uint32_t rgb=0;
  for(unsigned char c:value){unsigned n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;if(n>15)return {};rgb=(rgb<<4)|n;}
  return rgb;
}
inline std::string colorText(std::optional<uint32_t> value) {
  if(!value)return {};
  char text[8];std::snprintf(text,sizeof(text),"#%06X",unsigned(*value&0xffffff));return text;
}
struct Hsv {double h=0,s=0,v=0;};
inline uint32_t hsvRgb(Hsv hsv) {
  double h=std::fmod(std::max(0.0,hsv.h),360.0)/60.0,s=std::clamp(hsv.s,0.0,1.0),v=std::clamp(hsv.v,0.0,1.0);
  double c=v*s,x=c*(1-std::abs(std::fmod(h,2.0)-1)),m=v-c,r=0,g=0,b=0;
  if(h<1){r=c;g=x;}else if(h<2){r=x;g=c;}else if(h<3){g=c;b=x;}else if(h<4){g=x;b=c;}else if(h<5){r=x;b=c;}else{r=c;b=x;}
  return (uint32_t(std::lround((r+m)*255))<<16)|(uint32_t(std::lround((g+m)*255))<<8)|uint32_t(std::lround((b+m)*255));
}
inline Hsv rgbHsv(uint32_t rgb) {
  double r=((rgb>>16)&255)/255.0,g=((rgb>>8)&255)/255.0,b=(rgb&255)/255.0;
  double hi=std::max({r,g,b}),lo=std::min({r,g,b}),d=hi-lo,h=0;
  if(d){if(hi==r)h=60*std::fmod((g-b)/d,6.0);else if(hi==g)h=60*((b-r)/d+2);else h=60*((r-g)/d+4);if(h<0)h+=360;}
  return {h,hi?d/hi:0,hi};
}
class ColorSettings {
public:
  using Get=const char* (*)(const char*,const char*);
  using Set=void (*)(const char*,const char*,const char*,bool);
  static constexpr const char* section="MediaWaveThumbnails";
  static constexpr const char* keys[2]={"color_normal","color_selected"};
  Get get=nullptr;Set set=nullptr;
  std::function<void()> changed;
  std::array<std::optional<uint32_t>,2> saved{},draft{};
  bool editing=false;
  void load(){for(int i=0;i<2;++i){const char* value=get?get(section,keys[i]):nullptr;saved[i]=parseColor(value?value:"");}}
  uint32_t resolve(int target,uint32_t fallback)const{return (editing?draft:saved)[target].value_or(fallback);}
  bool usesDefault(int target)const{return !(editing?draft:saved)[target].has_value();}
  std::string text(int target)const{return colorText((editing?draft:saved)[target]);}
  void begin(){if(editing)return;load();draft=saved;editing=true;}
  void preview(int target,std::optional<uint32_t> color){if(!editing)return;if(draft[target]==color)return;draft[target]=color;if(changed)changed();}
  void resetDraft(){if(!editing)return;draft={};if(changed)changed();}
  void save(){if(!editing)return;saved=draft;editing=false;for(int i=0;i<2;++i){auto text=colorText(saved[i]);if(set)set(section,keys[i],text.c_str(),true);}if(changed)changed();}
  void cancel(){if(!editing)return;editing=false;draft=saved;if(changed)changed();}
  void resetSaved(){editing=false;saved=draft={};for(const char* key:keys)if(set)set(section,key,"",true);if(changed)changed();}
};
}

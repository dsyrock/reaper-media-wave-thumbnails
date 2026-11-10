#include "../src/color_settings.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
namespace {
std::map<std::string,std::string> ext;
int writes=0,previews=0;
const char* get(const char*,const char* key){return ext[key].c_str();}
void set(const char*,const char* key,const char* value,bool persist){if(!persist)throw std::runtime_error("save must persist");ext[key]=value;++writes;}
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
int main(){try{
  check(mwt::parseColor("#000000")==0&&mwt::parseColor("ffA013")==0xffa013,"RGB format parsing");
  for(const char* bad:{"","#12345","#1234567","#GG0000","123456 trailing"})check(!mwt::parseColor(bad),"malformed RGB accepted");
  check(mwt::colorText(0x12abef)=="#12ABEF"&&mwt::colorText({}).empty(),"portable color format");
  for(unsigned r=0;r<=255;r+=17)for(unsigned g=0;g<=255;g+=17)for(unsigned b=0;b<=255;b+=17){
    uint32_t rgb=(r<<16)|(g<<8)|b,out=mwt::hsvRgb(mwt::rgbHsv(rgb));
    for(int shift:{0,8,16})check(std::abs(int((out>>shift)&255)-int((rgb>>shift)&255))<=1,"HSV round-trip changed RGB");
  }
  mwt::ColorSettings colors;colors.get=get;colors.set=set;colors.changed=[] {++previews;};
  ext["color_normal"]="#12abef";ext["color_selected"]="invalid";colors.load();
  check(colors.resolve(0,0)==0x12abef&&colors.resolve(1,0xaabbcc)==0xaabbcc,"load/default behavior");
  colors.begin();colors.preview(0,0xff0000);check(colors.resolve(0,0)==0xff0000&&previews==1&&writes==0,"live preview unexpectedly persisted");
  colors.cancel();check(colors.resolve(0,0)==0x12abef&&ext["color_normal"]=="#12abef"&&writes==0,"cancel lost saved setting");
  colors.begin();colors.preview(0,0x22aa44);colors.preview(1,0xffffff);colors.save();
  check(ext["color_normal"]=="#22AA44"&&ext["color_selected"]=="#FFFFFF"&&writes==2,"save did not persist both choices");
  mwt::ColorSettings restarted;restarted.get=get;restarted.set=set;restarted.load();
  check(restarted.resolve(0,0)==0x22aa44&&restarted.resolve(1,0)==0xffffff,"restart lost selected colors");
  restarted.begin();restarted.resetDraft();check(restarted.resolve(0,0x111111)==0x111111,"reset preview did not follow default");
  restarted.cancel();check(restarted.resolve(0,0)==0x22aa44,"cancel after reset changed persisted color");
  restarted.resetSaved();check(ext["color_normal"].empty()&&ext["color_selected"].empty(),"reset did not clear custom colors");
  std::cout<<"PASS RGB/HSV, preview without disk writes, cancel, save, restart, theme defaults, reset\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

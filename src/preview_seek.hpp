#pragma once
// Include after reaper_plugin.h (Win32 or host-provided SWELL).
#include "waveform_file.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>

namespace mwt {
inline std::string previewText(HWND w) {
#ifdef _WIN32
  std::array<wchar_t,32768> wide{};
  int length=GetWindowTextW(w,wide.data(),int(wide.size()));
  int size=WideCharToMultiByte(CP_UTF8,0,wide.data(),length,nullptr,0,nullptr,nullptr);
  std::string result(size,'\0');
  if(size)WideCharToMultiByte(CP_UTF8,0,wide.data(),length,result.data(),size,nullptr,nullptr);
  return result;
#else
  std::array<char,32768> value{};GetWindowText(w,value.data(),int(value.size()));return value.data();
#endif
}
inline double thumbnailRatio(double x,double left,double right) {
  return std::clamp((x-left-6)/std::max(1.0,right-left-13),0.0,1.0);
}
inline bool previewModifiers() {
  return (GetAsyncKeyState(VK_CONTROL)|GetAsyncKeyState(VK_SHIFT)|GetAsyncKeyState(VK_MENU))&0x8000;
}
class PreviewSeek {
  std::string pendingPath;
  double ratio=0;
  uint64_t requestedAt=0,releasedAt=0,openedAt=0;
  bool released=false,opened=false;
public:
  uint64_t requests=0,completed=0,cancelled=0;
  double lastRatio=0;
  std::string lastPath,error;
  bool pending()const{return !pendingPath.empty();}
  const std::string& path()const{return pendingPath;}
  bool request(const std::string& file,double fraction,uint64_t time) {
    if(!waveformPath(file)||!std::isfinite(fraction))return false;
    pendingPath=file;ratio=std::clamp(fraction,0.0,1.0);requestedAt=time;
    released=opened=false;error.clear();++requests;return true;
  }
  void cancel(){if(pending()){++cancelled;pendingPath.clear();}}
  // Selected path is re-resolved by the backend: a queued click must not act
  // on an old row after sorting, filtering, changing directory or selection.
  bool tick(HWND root,const std::string& selectedPath,uint64_t time) {
    if(!pending())return false;
    if(!IsWindow(root)||selectedPath!=pendingPath){cancel();return true;}
    if(time-requestedAt>4000){error="Preview seek timed out";cancel();return true;}
    if(GetAsyncKeyState(VK_LBUTTON)&0x8000)return false;
    if(!released){released=true;releasedAt=time;return false;}
    if(time-releasedAt<60)return false;
    if(!opened){
      SendMessage(root,WM_COMMAND,1009,0); // native Preview: Stop
      // Load from the current list selection, never OpenMediaExplorer(path):
      // that API can navigate out of a database/search to the source folder.
      // Stop immediately in the same UI callback, before the deferred seek.
      SendMessage(root,WM_COMMAND,1008,0); // native Preview: Play/load selection
      SendMessage(root,WM_COMMAND,1009,0);
      opened=true;openedAt=time;return true;
    }
    if(time-openedAt<100)return false;
    HWND wave=GetDlgItem(root,1046),label=GetDlgItem(root,1014);
    auto name=std::filesystem::u8path(pendingPath).filename().u8string();
    if(!wave||previewText(label).find(name)==std::string::npos||previewText(GetDlgItem(root,1016)).empty())return false;
    RECT bounds{};GetClientRect(wave,&bounds);
    int width=int(bounds.right-bounds.left),height=int(bounds.bottom-bounds.top);
    if(width<16||height<1){error="Expand the native Media Explorer preview panel to seek";cancel();return true;}
    // The thumbnail represents the whole source, regardless of preview zoom.
    // Reset the native view rather than guessing its current scroll range.
    SendMessage(root,WM_COMMAND,42093,0); // native Preview: Zoom out full
    int x=std::clamp(int(std::lround(ratio*width)),0,width-1);
    int y=height/2;
    SendMessage(wave,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
    SendMessage(wave,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    SendMessage(root,WM_COMMAND,40027,0); // native Play from last seek position
    lastPath=pendingPath;lastRatio=ratio;++completed;pendingPath.clear();return true;
  }
};
}

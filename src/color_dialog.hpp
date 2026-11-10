#pragma once
#include "color_settings.hpp"
#include "wave_geometry.hpp"
#include <vector>
#include <cstring>
#ifndef _WIN32
#include "WDL/swell/swell-dlggen.h"
#undef BEGIN
#undef END
#endif

namespace mwt::colorui {
constexpr int width=410,height=338;
constexpr int normal=3101,selected=3102,theme=3103,hex=3104,validation=3105,reset=3106;
inline HWND window=nullptr;
inline ColorSettings* settings=nullptr;
inline std::function<uint32_t(int)> fallback;
inline int target=0,drag=0;
inline bool updating=false;
inline double scale=1;
inline Hsv hsv;
inline HBITMAP square=nullptr,hueBar=nullptr;
inline int squareHue=-1;
#ifdef _WIN32
inline HFONT font=nullptr;
#endif
inline COLORREF native(uint32_t rgb){return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);}
inline RECT rect(int x,int y,int w,int h){return {int(x*scale),int(y*scale),int((x+w)*scale),int((y+h)*scale)};}
inline void text(int id,const char* value){
#ifdef _WIN32
  SetDlgItemTextA(window,id,value);
#else
  SetDlgItemText(window,id,value);
#endif
}
inline std::string text(int id){std::array<char,64> value{};
#ifdef _WIN32
  GetDlgItemTextA(window,id,value.data(),int(value.size()));
#else
  GetDlgItemText(window,id,value.data(),int(value.size()));
#endif
  return value.data();
}
inline void repaint(){if(window)InvalidateRect(window,nullptr,FALSE);}
inline void sync(bool updateHsv=true){
  if(!window||!settings)return;
  updating=true;
  auto rgb=settings->resolve(target,fallback(target));if(updateHsv)hsv=rgbHsv(rgb);
  auto value=colorText(rgb);text(hex,value.c_str());text(validation,"");EnableWindow(GetDlgItem(window,IDOK),TRUE);
  CheckDlgButton(window,normal,target==0);CheckDlgButton(window,selected,target==1);CheckDlgButton(window,theme,settings->usesDefault(target));
  updating=false;repaint();
}
inline void preview(uint32_t rgb,bool updateHsv){settings->preview(target,rgb);sync(updateHsv);}
inline HBITMAP bitmap(int w,int h,const std::vector<uint32_t>& pixels){
#ifdef _WIN32
  BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
  info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;
  HBITMAP result=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
  if(result)std::memcpy(bits,pixels.data(),pixels.size()*4);
  return result;
#else
  return CreateBitmap(w,h,1,32,(unsigned char*)pixels.data());
#endif
}
inline void blit(HDC dc,HBITMAP image,const RECT& bounds,int w,int h){
  if(!image)return;
#ifdef _WIN32
  HDC source=CreateCompatibleDC(dc);auto old=SelectObject(source,image);
  StretchBlt(dc,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,source,0,0,w,h,SRCCOPY);
  SelectObject(source,old);DeleteDC(source);
#else
  (void)w;(void)h;DrawImageInRect(dc,(HICON)image,&bounds);
#endif
}
inline void fill(HDC dc,const RECT& r,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);}
inline void outline(HDC dc,const RECT& r,COLORREF color){
  auto pen=CreatePen(PS_SOLID,1,color);auto old=SelectObject(dc,pen);
  MoveToEx(dc,r.left,r.top,nullptr);LineTo(dc,r.right-1,r.top);LineTo(dc,r.right-1,r.bottom-1);LineTo(dc,r.left,r.bottom-1);LineTo(dc,r.left,r.top);
  SelectObject(dc,old);DeleteObject(pen);
}
inline void paint(HDC dc){
  RECT area{};GetClientRect(window,&area);fill(dc,area,GetSysColor(COLOR_BTNFACE));
  int h=int(hsv.h);
  if(!square||squareHue!=h){
    if(square)DeleteObject(square);
    std::vector<uint32_t> pixels(220*164);
    for(int y=0;y<164;++y)for(int x=0;x<220;++x)pixels[y*220+x]=0xff000000|hsvRgb({double(h),x/219.0,1-y/163.0});
    square=bitmap(220,164,pixels);squareHue=h;
  }
  if(!hueBar){std::vector<uint32_t> pixels(18*164);for(int y=0;y<164;++y)for(int x=0;x<18;++x)pixels[y*18+x]=0xff000000|hsvRgb({y*359.0/163,1,1});hueBar=bitmap(18,164,pixels);}
  auto sv=rect(16,62,220,164),bar=rect(248,62,18,164);
  blit(dc,square,sv,220,164);blit(dc,hueBar,bar,18,164);
  int x=int((16+hsv.s*219)*scale),y=int((62+(1-hsv.v)*163)*scale),hy=int((62+hsv.h/359*163)*scale);
  outline(dc,{x-4,y-4,x+5,y+5},RGB(255,255,255));outline(dc,{x-5,y-5,x+6,y+6},RGB(0,0,0));
  outline(dc,{bar.left-2,hy-2,bar.right+2,hy+3},RGB(255,255,255));
  constexpr uint32_t presets[]={0x37a698,0x50b5ff,0xf3b74a,0xec7187,0xaa8dea,0x79c66a,0xffffff,0x222222};
  for(int i=0;i<8;++i){auto r=rect(16+i*32,237,25,18);fill(dc,r,native(presets[i]));outline(dc,r,RGB(90,90,90));}
  auto sample=rect(16,267,378,30);fill(dc,sample,target?RGB(55,78,110):RGB(48,48,48));
  auto pen=CreatePen(PS_SOLID,std::max(1,int(scale)),native(settings->resolve(target,fallback(target))));auto old=SelectObject(dc,pen);
  for(int px=0;px<370;++px){double amp=(.2+.8*std::abs(std::sin(px*.047)))*std::abs(std::sin(px*.39));int xx=int((20+px)*scale),yy=int(amp*11*scale);int mid=(sample.top+sample.bottom)/2;MoveToEx(dc,xx,mid-yy,nullptr);LineTo(dc,xx,mid+yy+1);}
  SelectObject(dc,old);DeleteObject(pen);
}
inline void pick(int x,int y){
  if(!settings)return;
  double px=x/scale,py=y/scale;
  if(drag==1){hsv.s=std::clamp((px-16)/219,0.0,1.0);hsv.v=1-std::clamp((py-62)/163,0.0,1.0);}
  if(drag==2)hsv.h=std::clamp((py-62)/163,0.0,1.0)*359;
  preview(hsvRgb(hsv),false);
}
inline void control(const char* kind,const char* label,int id,int x,int y,int w,int h,int style=0){
#ifdef _WIN32
  HWND child=CreateWindowExA(!std::strcmp(kind,"EDIT")?WS_EX_CLIENTEDGE:0,kind,label,WS_CHILD|WS_VISIBLE|style,x*scale,y*scale,w*scale,h*scale,window,(HMENU)(INT_PTR)id,GetModuleHandle(nullptr),nullptr);
  SendMessage(child,WM_SETFONT,(WPARAM)font,TRUE);
#else
  if(!std::strcmp(kind,"BUTTON"))SWELL_MakeButton(0,label,id,x*scale,y*scale,w*scale,h*scale,style);
  else if(!std::strcmp(kind,"EDIT"))SWELL_MakeEditField(id,x*scale,y*scale,w*scale,h*scale,style);
  else SWELL_MakeLabel(-1,label,id,x*scale,y*scale,w*scale,h*scale,0);
#endif
}
inline INT_PTR CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
  switch(msg){
    case WM_INITDIALOG:{
      window=hwnd;target=drag=0;
#ifdef _WIN32
      HDC dc=GetDC(hwnd);scale=std::max(1.0,GetDeviceCaps(dc,LOGPIXELSX)/96.0);ReleaseDC(hwnd,dc);
      font=CreateFontA(-int(12*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,"Segoe UI");
      RECT r{0,0,int(width*scale),int(height*scale)};AdjustWindowRectEx(&r,GetWindowLong(hwnd,GWL_STYLE),FALSE,GetWindowLong(hwnd,GWL_EXSTYLE));
      SetWindowPos(hwnd,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);
#else
#ifdef SWELL_TARGET_OSX
      scale=1;
#else
      scale=std::max(1.0,SWELL_GetScaling256()/256.0);
#endif
      SetWindowPos(hwnd,nullptr,0,0,width*scale,height*scale,SWP_NOMOVE|SWP_NOZORDER);
      SWELL_MakeSetCurParms(1,1,0,0,hwnd,false,false);
#endif
      control("BUTTON","Normal",normal,16,12,126,22,BS_AUTORADIOBUTTON|WS_TABSTOP|WS_GROUP);
      control("BUTTON","Selected",selected,148,12,126,22,BS_AUTORADIOBUTTON|WS_TABSTOP);
      control("BUTTON","Use default / follow theme",theme,16,37,270,20,BS_AUTOCHECKBOX|WS_TABSTOP);
      control("STATIC","HEX #RRGGBB",0,282,64,112,18);
      control("EDIT","",hex,282,85,112,25,WS_TABSTOP|ES_AUTOHSCROLL);
      control("STATIC","",validation,282,118,112,64);
      control("STATIC","Changes preview live",0,282,197,112,42);
      control("BUTTON","Reset all",reset,16,309,100,23,WS_TABSTOP);
      control("BUTTON","Save",IDOK,218,309,82,23,WS_TABSTOP|BS_DEFPUSHBUTTON);
      control("BUTTON","Cancel",IDCANCEL,310,309,84,23,WS_TABSTOP);
#ifndef _WIN32
      SWELL_MakeSetCurParms(1,1,0,0,nullptr,false,false);
#endif
      sync();return TRUE;
    }
    case WM_COMMAND:{
      int id=LOWORD(wp);
      if(id==normal||id==selected){target=id==selected;sync();return TRUE;}
      if(id==theme&&!updating){if(IsDlgButtonChecked(hwnd,theme))settings->preview(target,{});else settings->preview(target,settings->resolve(target,fallback(target)));sync();return TRUE;}
      if(id==hex&&HIWORD(wp)==EN_CHANGE&&!updating){auto value=parseColor(text(hex));if(value){settings->preview(target,value);hsv=rgbHsv(*value);CheckDlgButton(hwnd,theme,FALSE);text(validation,"");}else text(validation,"Enter six hex digits");EnableWindow(GetDlgItem(hwnd,IDOK),bool(value));repaint();return TRUE;}
      if(id==reset){settings->resetDraft();sync();return TRUE;}
      if(id==IDOK){if(IsWindowEnabled(GetDlgItem(hwnd,IDOK))){settings->save();DestroyWindow(hwnd);}return TRUE;}
      if(id==IDCANCEL){DestroyWindow(hwnd);return TRUE;}
      break;
    }
    case WM_LBUTTONDOWN:{
      double x=short(LOWORD(lp))/scale,y=short(HIWORD(lp))/scale;
      if(x>=16&&x<236&&y>=62&&y<226)drag=1;
      else if(x>=248&&x<266&&y>=62&&y<226)drag=2;
      if(drag){SetCapture(hwnd);pick(short(LOWORD(lp)),short(HIWORD(lp)));return TRUE;}
      constexpr uint32_t presets[]={0x37a698,0x50b5ff,0xf3b74a,0xec7187,0xaa8dea,0x79c66a,0xffffff,0x222222};
      if(y>=237&&y<255&&x>=16&&x<272){int i=int(x-16)/32;if(i<8&&int(x-16)%32<25){preview(presets[i],true);return TRUE;}}
      break;
    }
    case WM_MOUSEMOVE:if(drag){pick(short(LOWORD(lp)),short(HIWORD(lp)));return TRUE;}break;
    case WM_LBUTTONUP:if(drag){pick(short(LOWORD(lp)),short(HIWORD(lp)));drag=0;ReleaseCapture();return TRUE;}break;
    case WM_CLOSE:DestroyWindow(hwnd);return TRUE;
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);if(dc)paint(dc);EndPaint(hwnd,&ps);return TRUE;}
    case WM_DESTROY:
      if(settings)settings->cancel();
      window=nullptr;drag=0;
      if(square)DeleteObject(square);
      if(hueBar)DeleteObject(hueBar);
      square=hueBar=nullptr;squareHue=-1;
#ifdef _WIN32
      if(font)DeleteObject(font);
      font=nullptr;
#endif
      return TRUE;
  }
  return FALSE;
}
#ifndef _WIN32
inline void createResource(HWND,int){}
inline SWELL_DialogRegHelper resource(&SWELL_curmodule_dialogresource_head,createResource,7012,SWELL_DLG_WS_FLIPPED|SWELL_DLG_WS_NOAUTOSIZE|SWELL_DLG_WS_OPAQUE,"Waveform colors",width,height,1,1);
#endif
inline void close(){if(window)DestroyWindow(window);}
inline int keyboard(MSG* msg,accelerator_register_t*){
  if(!window||(msg->hwnd!=window&&!IsChild(window,msg->hwnd)))return 0;
  if(msg->message==WM_KEYDOWN){
    if(msg->wParam==VK_ESCAPE){close();return 1;}
    if(msg->wParam==VK_RETURN){if(IsWindowEnabled(GetDlgItem(window,IDOK)))procedure(window,WM_COMMAND,IDOK,0);return 1;}
  }
#ifdef _WIN32
  if(IsDialogMessage(window,msg))return 1;
#endif
  return -1; // edit keys must not trigger REAPER's global shortcuts
}
inline accelerator_register_t accelerator{keyboard,true,nullptr};
inline bool show(HWND owner,ColorSettings& model,std::function<uint32_t(int)> defaults){
  if(window){ShowWindow(window,SW_SHOW);SetForegroundWindow(window);return true;}
  settings=&model;fallback=std::move(defaults);model.begin();
#ifdef _WIN32
  alignas(DWORD) std::array<unsigned char,512> buffer{};
  auto* templ=reinterpret_cast<DLGTEMPLATE*>(buffer.data());
  templ->style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN|DS_MODALFRAME|DS_SETFONT;templ->cx=240;templ->cy=220;
  auto* p=reinterpret_cast<WORD*>(buffer.data()+sizeof(DLGTEMPLATE));*p++=0;*p++=0;
  const wchar_t title[]=L"Waveform colors",face[]=L"Segoe UI";
  std::memcpy(p,title,sizeof(title));p+=sizeof(title)/2;*p++=9;std::memcpy(p,face,sizeof(face));
  HWND result=CreateDialogIndirectParamW(GetModuleHandle(nullptr),templ,owner,procedure,0);
#else
  HWND result=SWELL_CreateDialog(SWELL_curmodule_dialogresource_head,MAKEINTRESOURCE(7012),owner,procedure,0);
#endif
  if(!result){model.cancel();return false;}
  ShowWindow(result,SW_SHOW);SetForegroundWindow(result);return true;
}
}

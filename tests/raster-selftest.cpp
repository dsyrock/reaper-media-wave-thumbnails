#include "../src/wave_raster.hpp"
#include <cassert>
#include <iostream>
int main() {
  HDC dc=CreateCompatibleDC(nullptr);assert(dc);
  std::array<float,192> high{},low{};float peak=1;
  for(int i=0;i<192;++i){high[i]=float(i%19)/18;low[i]=-float(i%13)/12;}
  for(int width:{1,20,192,305,800})for(int height:{1,2,11,32})for(bool selected:{false,true}) {
    COLORREF bg=selected?RGB(0,120,215):RGB(255,255,255),fg=selected?RGB(255,255,255):RGB(55,166,152);
    HBITMAP pixels=mwt::rasterWave(dc,width,height,high,low,peak,fg,bg);assert(pixels);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* ref=nullptr;HBITMAP legacy=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&ref,nullptr,0);assert(legacy);
    HGDIOBJ old=SelectObject(dc,legacy);
    RECT rect{0,0,width,height};HBRUSH brush=CreateSolidBrush(bg);FillRect(dc,&rect,brush);DeleteObject(brush);
    HPEN pen=CreatePen(PS_SOLID,1,fg);HGDIOBJ oldPen=SelectObject(dc,pen);
    int mid=height/2;double scale=std::max(1,height/2)/std::max(.01f,peak);
    for(int x=0;x<width;++x) {
      int bin=x*192/width,a=mid-int(std::round(high[bin]*scale)),b=mid-int(std::round(low[bin]*scale));
      MoveToEx(dc,x,std::clamp(a,0,height-1),nullptr);LineTo(dc,x,std::clamp(std::max(a+1,b),1,height));
    }
    SelectObject(dc,oldPen);DeleteObject(pen);SelectObject(dc,old);GdiFlush();
    DIBSECTION view{};assert(GetObjectW(pixels,sizeof(view),&view));
    for(int i=0;i<width*height;++i)assert((static_cast<uint32_t*>(ref)[i]&0xffffff)==(static_cast<uint32_t*>(view.dsBm.bmBits)[i]&0xffffff));
    DeleteObject(legacy);DeleteObject(pixels);
  }
  DeleteDC(dc);std::cout<<"PASS DIB wave raster matches legacy GDI pixels across widths, heights and selection colors\n";
  return 0;
}

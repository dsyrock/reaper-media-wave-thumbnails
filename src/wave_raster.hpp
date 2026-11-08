#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mwt {
inline uint32_t dibColor(COLORREF color){return (uint32_t(GetRValue(color))<<16)|(uint32_t(GetGValue(color))<<8)|GetBValue(color);}
template<size_t N>
HBITMAP rasterWave(HDC dc,int width,int height,const std::array<float,N>& high,const std::array<float,N>& low,float peak,COLORREF fg,COLORREF bg) {
  if(width<=0||height<=0||uint64_t(width)*height>4ULL*1024*1024)return nullptr;
  BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
  info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
  void* memory=nullptr;
  HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&memory,nullptr,0);
  if(!bitmap)return nullptr;
  auto* pixels=static_cast<uint32_t*>(memory);
  std::fill(pixels,pixels+size_t(width)*height,dibColor(bg));
  std::array<int,N> top{},bottom{};
  int mid=height/2;double scale=std::max(1,height/2)/std::max(.01f,peak);
  for(size_t i=0;i<N;++i) {
    int a=mid-int(std::round(high[i]*scale)),b=mid-int(std::round(low[i]*scale));
    top[i]=std::clamp(a,0,height-1);bottom[i]=std::clamp(std::max(a+1,b),1,height);
  }
  uint32_t color=dibColor(fg);
  for(int x=0;x<width;++x) {
    size_t bin=std::min(N-1,size_t(x)*N/width);
    for(int y=top[bin];y<bottom[bin];++y)pixels[size_t(y)*width+x]=color;
  }
  return bitmap;
}
}

#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "wave_geometry.hpp"

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
  uint32_t color=dibColor(fg);
  waveColumns(width,height,high,low,peak,[&](int x,int y){pixels[size_t(y)*width+x]=color;});
  return bitmap;
}
}

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
namespace mwt {
// Platform-independent geometry; pixel storage/color layout belongs to the UI backend.
template<size_t N,class Pixel>
void waveColumns(int width,int height,const std::array<float,N>& high,const std::array<float,N>& low,float peak,Pixel pixel) {
  if(width<=0||height<=0)return;
  std::array<int,N> top{},bottom{};int mid=height/2;
  double scale=std::max(1,height/2)/std::max(.01f,peak);
  for(size_t i=0;i<N;++i){int a=mid-int(std::round(high[i]*scale)),b=mid-int(std::round(low[i]*scale));top[i]=std::clamp(a,0,height-1);bottom[i]=std::clamp(std::max(a+1,b),1,height);}
  for(int x=0;x<width;++x){size_t bin=std::min(N-1,size_t(x)*N/width);for(int y=top[bin];y<bottom[bin];++y)pixel(x,y);}
}
}

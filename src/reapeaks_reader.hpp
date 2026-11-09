#pragma once
// Read-only waveform reader based on Cockos' published format:
// https://www.reaper.fm/sdk/reapeaks.txt (v1.0 RPKM / v1.1 RPKN / v1.2 RPKL).
// No REAPER calls, audio decoder, or extra on-disk cache is used here.
#include "platform_file.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mwt {
constexpr int peakBins=192;
enum class PeakResult {Ready,Unsupported,Stale};
struct PeakData {
  std::array<float,peakBins> high{},low{};
  float peak{};
  uint64_t bytesRead{};
};
inline uint32_t u32(const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline int16_t i16(const unsigned char* p){return int16_t(uint16_t(p[0])|(uint16_t(p[1])<<8));}
inline float decodePeak(int16_t v,bool large) {
  if(!large)return v/32768.0f;
  int n=v;
  if(n>24576)return std::exp2(float(n-24576)/1024);
  if(n<-24576)return -std::exp2(float(-n-24576)/1024);
  return n/24576.0f;
}
inline PeakResult readPeaks(const std::wstring& source,const std::wstring& peakPath,const FileStamp& expected,PeakData& out) {
  ReadOnlyFile file(peakPath);
  FileStamp before{},after{};
  if(!file.snapshot(before))return PeakResult::Unsupported;
  uint64_t length=before.size;
  auto readAt=[&](uint64_t offset,void* data,uint32_t bytes) {
    if(offset>length||bytes>length-offset)return false;
    if(!file.readAt(offset,data,bytes))return false;
    out.bytesRead+=bytes;return true;
  };
  unsigned char header[18]{};
  if(!readAt(0,header,sizeof(header)))return PeakResult::Unsupported;
  bool old=std::memcmp(header,"RPKM",4)==0,large=std::memcmp(header,"RPKL",4)==0;
  if(!old&&!large&&std::memcmp(header,"RPKN",4)!=0)return PeakResult::Unsupported;
  unsigned channels=header[4],levels=header[5];
  uint32_t rate=u32(header+6);
  if(!channels||!levels||levels>16||!rate||rate>1000000)return PeakResult::Unsupported;
  uint32_t sourceTime=uint32_t(expected.time/10000000ULL-11644473600ULL);
  int64_t timeDiff=int32_t(sourceTime-u32(header+10));
  if(uint32_t(expected.size)!=u32(header+14)||(std::abs(timeDiff)>2&&std::abs(std::abs(timeDiff)-3600)>2))return PeakResult::Stale;
  std::array<unsigned char,16*8> table{};
  if(!readAt(18,table.data(),levels*8))return PeakResult::Unsupported;
  struct Level {uint32_t count,division;uint64_t offset;};
  std::vector<Level> main;
  uint64_t offset=18+levels*8;
  bool auxiliary=false;
  uint32_t priorDivision=0;
  for(unsigned i=0;i<levels;++i) {
    int32_t division=int32_t(u32(table.data()+i*8));
    uint32_t count=u32(table.data()+i*8+4);
    if(division<=0){auxiliary=true;continue;}
    if(auxiliary||uint32_t(division)<priorDivision)return PeakResult::Unsupported;
    priorDivision=uint32_t(division);
    uint64_t bytes=uint64_t(count)*channels*(old?2:4);
    if(offset>length||bytes>length-offset)return PeakResult::Unsupported;
    if(count)main.push_back({count,uint32_t(division),offset});
    offset+=bytes;
  }
  if(main.empty())return PeakResult::Unsupported;
  // Keep at least two cached time intervals per output pixel when possible.
  // Short files use the finest available cache; never open audio for more detail.
  auto selected=main.front();
  for(const auto& level:main)if(level.count>=peakBins*2)selected=level;
  const auto fine=main.front();
  uint64_t span=uint64_t(fine.count)*fine.division,coverage=uint64_t(selected.count)*selected.division;
  if(span>(UINT64_MAX-(peakBins-1))/peakBins)return PeakResult::Unsupported;
  uint64_t bytes=uint64_t(selected.count)*channels*(old?2:4);
  if(bytes>32ULL*1024*1024)return PeakResult::Unsupported;
  std::vector<unsigned char> data(static_cast<size_t>(bytes));
  if(!readAt(selected.offset,data.data(),uint32_t(bytes)))return PeakResult::Unsupported;
  const unsigned stride=channels*(old?2:4);
  // Coarse mipmaps can omit an unfinished final interval. Read just that tail
  // from the finest layer, so an end-of-file transient is never discarded.
  uint32_t tailFirst=fine.count;
  std::vector<unsigned char> tail;
  if(coverage<span) {
    tailFirst=uint32_t(coverage/fine.division);
    uint64_t tailBytes=uint64_t(fine.count-tailFirst)*stride;
    if(bytes+tailBytes>32ULL*1024*1024)return PeakResult::Unsupported;
    tail.resize(static_cast<size_t>(tailBytes));
    if(!readAt(fine.offset+uint64_t(tailFirst)*stride,tail.data(),uint32_t(tailBytes)))return PeakResult::Unsupported;
  }
  auto add=[&](const std::vector<unsigned char>& buffer,const Level& level,uint32_t base,uint64_t begin,uint64_t end,float& hi,float& lo) {
    uint32_t first=uint32_t(std::min<uint64_t>(level.count,begin/level.division));
    uint32_t last=uint32_t(std::min<uint64_t>(level.count,(end+level.division-1)/level.division));
    for(uint32_t frame=std::max(first,base);frame<last;++frame)for(unsigned ch=0;ch<channels;++ch) {
      const auto* pair=buffer.data()+uint64_t(frame-base)*stride+ch*(old?2:4);
      float upper=decodePeak(i16(pair),large),lower=old?-std::abs(upper):decodePeak(i16(pair+2),large);
      if(old)upper=std::abs(upper);
      if(upper<lower)return false;
      hi=std::max(hi,upper);lo=std::min(lo,lower);
    }
    return true;
  };
  for(int bin=0;bin<peakBins;++bin) {
    uint64_t begin=uint64_t(bin)*span/peakBins,end=(uint64_t(bin+1)*span+peakBins-1)/peakBins;
    float hi=0,lo=0;
    if(!add(data,selected,0,begin,end,hi,lo))return PeakResult::Unsupported;
    if(!tail.empty()&&end>coverage&&!add(tail,fine,tailFirst,std::max(begin,coverage),end,hi,lo))return PeakResult::Unsupported;
    out.high[bin]=std::clamp(hi,-1.0f,1.0f);out.low[bin]=std::clamp(lo,-1.0f,1.0f);
    out.peak=std::max({out.peak,out.high[bin],-out.low[bin]});
  }
  FileStamp current;
  if(!fileStamp(source,current)||!(current==expected))return PeakResult::Stale;
  if(!file.snapshot(after)||!(before==after))return PeakResult::Unsupported;
  return PeakResult::Ready;
}
}

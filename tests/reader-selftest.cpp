#include "../src/peak_reader_pool.hpp"
#include <cassert>
#include <iostream>
using Bytes=std::vector<unsigned char>;
void u32(Bytes& b,uint32_t n){for(int i=0;i<4;++i)b.push_back(static_cast<unsigned char>(n>>(i*8)));}
void i16(Bytes& b,int n){b.push_back(static_cast<unsigned char>(n));b.push_back(static_cast<unsigned char>(n>>8));}
void write(const std::wstring& path,const Bytes& b) {
  HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);assert(f!=INVALID_HANDLE_VALUE);
  DWORD count=0;assert(WriteFile(f,b.data(),DWORD(b.size()),&count,nullptr)&&count==b.size());CloseHandle(f);
}
Bytes header(const char* magic,int channels,int levels,const mwt::FileStamp& stamp) {
  Bytes b(magic,magic+4);b.push_back(static_cast<unsigned char>(channels));b.push_back(static_cast<unsigned char>(levels));
  u32(b,48000);u32(b,uint32_t(stamp.time/10000000ULL-11644473600ULL));u32(b,uint32_t(stamp.size));return b;
}
int wmain(int argc,wchar_t** argv) {
  if(argc==4) {
    mwt::FileStamp stamp;assert(mwt::fileStamp(argv[2],stamp));mwt::PeakData data;
    auto result=mwt::readPeaks(argv[2],argv[3],stamp,data);
    std::cout<<"{\"result\":"<<int(result)<<",\"peak\":"<<data.peak<<",\"bytes\":"<<data.bytesRead<<",\"high\":[";
    for(int i=0;i<mwt::peakBins;++i)std::cout<<(i?",":"")<<data.high[i];
    std::cout<<"],\"low\":[";for(int i=0;i<mwt::peakBins;++i)std::cout<<(i?",":"")<<data.low[i];std::cout<<"]}\n";
    return result==mwt::PeakResult::Ready?0:2;
  }
  assert(argc==2);std::wstring root=argv[1],source=root+L"\\source.bin",peak=root+L"\\test.reapeaks";
  write(source,Bytes(4096));mwt::FileStamp stamp;assert(mwt::fileStamp(source,stamp));
  auto read=[&](const Bytes& b,mwt::PeakData& out){write(peak,b);return mwt::readPeaks(source,peak,stamp,out);};
  auto b=header("RPKN",2,1,stamp);u32(b,128);u32(b,384);
  for(int i=0;i<384;++i){i16(b,8192);i16(b,-8192);i16(b,i==100?29491:16384);i16(b,-16384);}
  mwt::PeakData n;assert(read(b,n)==mwt::PeakResult::Ready);
  assert(std::abs(n.high[0]-.5f)<.0001&&std::abs(n.low[0]+.5f)<.0001&&n.high[50]>.899f&&n.peak>.899f);
  auto shortAudio=header("RPKN",2,2,stamp);u32(shortAudio,128);u32(shortAudio,384);u32(shortAudio,48000);u32(shortAudio,0);
  shortAudio.insert(shortAudio.end(),b.begin()+26,b.end());
  mwt::PeakData shortData;assert(read(shortAudio,shortData)==mwt::PeakResult::Ready&&shortData.high==n.high);
  auto ending=header("RPKN",1,2,stamp);u32(ending,1);u32(ending,1000);u32(ending,2);u32(ending,499);
  for(int i=0;i<1000;++i){i16(ending,i==999?29491:8192);i16(ending,-8192);}
  for(int i=0;i<499;++i){i16(ending,8192);i16(ending,-8192);}
  mwt::PeakData endData;assert(read(ending,endData)==mwt::PeakResult::Ready&&endData.high[191]>.899f);
  auto spectral=header("RPKN",2,3,stamp);u32(spectral,128);u32(spectral,384);u32(spectral,uint32_t(-'s'));u32(spectral,384);u32(spectral,uint32_t(-'r'));u32(spectral,1);
  spectral.insert(spectral.end(),b.begin()+26,b.end());spectral.resize(spectral.size()+384*2*4+2*8);
  mwt::PeakData s;assert(read(spectral,s)==mwt::PeakResult::Ready&&s.high==n.high&&s.low==n.low);
  auto old=header("RPKM",1,1,stamp);u32(old,128);u32(old,192);for(int i=0;i<192;++i)i16(old,-16384);
  mwt::PeakData m;assert(read(old,m)==mwt::PeakResult::Ready&&m.high[0]==.5f&&m.low[0]==-.5f);
  auto large=header("RPKL",1,1,stamp);u32(large,128);u32(large,384);for(int i=0;i<384;++i){i16(large,25600);i16(large,-25600);}
  mwt::PeakData l;assert(mwt::decodePeak(25600,true)==2&&mwt::decodePeak(-25600,true)==-2);
  assert(read(large,l)==mwt::PeakResult::Ready&&l.peak==1&&l.low[0]==-1);
  auto stale=b;stale[14]^=1;mwt::PeakData x;assert(read(stale,x)==mwt::PeakResult::Stale);
  auto shortFile=b;shortFile.resize(30);assert(read(shortFile,x)==mwt::PeakResult::Unsupported);
  auto overflow=b;for(int i=22;i<26;++i)overflow[i]=255;assert(read(overflow,x)==mwt::PeakResult::Unsupported);
  auto invalid=b;invalid[0]='X';assert(read(invalid,x)==mwt::PeakResult::Unsupported);
  auto reversed=b;reversed[26]=255;reversed[27]=127;reversed[28]=0;reversed[29]=128;
  // Corrupt a min/max pair so maximum < minimum.
  reversed[26]=0;reversed[27]=128;reversed[28]=255;reversed[29]=127;
  assert(read(reversed,x)==mwt::PeakResult::Unsupported);
  write(peak,b);
  mwt::PeakReaderPool pool;pool.start();
  for(uint64_t i=1;i<=1000;++i)assert(pool.submit({source,peak,stamp,1,i}));
  auto cancelled=pool.retain({});assert(!cancelled.empty());pool.stop();
  pool.start();assert(pool.submit({source,peak,stamp,1,1001}));
  bool completed=false;
  for(int i=0;i<1000&&!completed;++i){auto results=pool.take();if(!results.empty()){assert(results[0].result==mwt::PeakResult::Ready);completed=true;}else Sleep(1);}
  assert(completed);pool.stop();
  std::cout<<"PASS RPKM/RPKN/RPKL, stereo, transient bin, auxiliary layers, stale metadata, truncation, overflow and invalid sample pairs\n";
  std::cout<<"PASS pending-reader cancellation, shutdown and restart\n";
  return 0;
}

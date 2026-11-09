#include "../src/peak_reader_pool.hpp"
#include "../src/wave_geometry.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv){
 try {
  require(argc==2,"test output directory required");auto root=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(root);
  auto source=root/std::filesystem::u8path(u8"音频-test.bin"),peak=root/"source.reapeaks";
  {std::ofstream f(source,std::ios::binary);f<<std::string(4096,'\0');}
  auto wide=[](const std::filesystem::path& p){
#ifdef _WIN32
    return p.wstring();
#else
    return mwt::pathWide(p.u8string());
#endif
  };
  mwt::FileStamp stamp;require(mwt::fileStamp(wide(source),stamp),"Unicode source stat failed");
  std::vector<unsigned char> bytes{'R','P','K','N',2,2};
  auto put32=[&](uint32_t n){for(int i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(n>>(i*8)));};
  auto put16=[&](int n){bytes.push_back(static_cast<unsigned char>(n));bytes.push_back(static_cast<unsigned char>(n>>8));};
  put32(48000);put32(uint32_t(stamp.time/10000000ULL-11644473600ULL));put32(4096);put32(128);put32(384);put32(48000);put32(0);
  for(int i=0;i<384;++i){put16(8192);put16(-8192);put16(i==383?29491:16384);put16(-16384);}
  auto write=[&]{std::ofstream f(peak,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));};write();
  mwt::PeakData data;require(mwt::readPeaks(wide(source),wide(peak),stamp,data)==mwt::PeakResult::Ready,"cached waveform read failed");
  require(data.high[0]==.5f&&data.low[0]==-.5f&&data.high[191]>.899f,"stereo/end transient wrong");
  int pixels=0;mwt::waveColumns(317,11,data.high,data.low,data.peak,[&](int x,int y){require(x>=0&&x<317&&y>=0&&y<11,"pixel bounds");++pixels;});require(pixels>0,"empty geometry");
  mwt::PeakReaderPool pool;pool.start();require(pool.submit({wide(source),wide(peak),stamp,1,1}),"submit failed");bool done=false;
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(std::chrono::steady_clock::now()<deadline){auto results=pool.take();if(!results.empty()){require(results[0].result==mwt::PeakResult::Ready,"worker read failed");done=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  pool.stop();require(done,"worker timed out");bytes.resize(24);write();mwt::PeakData invalid;
  require(mwt::readPeaks(wide(source),wide(peak),stamp,invalid)==mwt::PeakResult::Unsupported,"truncated cache accepted");
  std::cout<<"PASS Unicode/stat/random-read, stereo/end transient, geometry, reader queue, corrupt cache\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

#include "../third_party/reaper/reaper_plugin.h"
#include "peak_reader_pool.hpp"
#include "cpu_profile.hpp"
#include "wave_raster.hpp"
#include "dc_state.hpp"
#include "waveform_file.hpp"
#include "preview_seek.hpp"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// PCM_source remains opaque: call only the SDK's C API (MinGW compatible).
namespace {
constexpr char section[] = "MediaWaveThumbnails";
constexpr wchar_t columnTitle[] = L"Waveform";
constexpr char version[] = "0.4.2";
constexpr int bins = 192;
constexpr size_t bitmapLimit = 512, bitmapBudget = 16 * 1024 * 1024;
constexpr UINT_PTR subclassId = 0x4D575431;
reaper_plugin_info_t host{};
HWND explorer{}, list{};
bool enabled = true, busy{}, rowsDirty = true;
bool pipelinePending=true,statusDirty=true;
uint64_t lastValidation{},lastDirectoryCheck{},lastLayoutCheck{};
uint64_t nextIdleCheck{};
int column = -1;
int waveWidth = 196, columnMenuDepth{};
int waveOrder = -1;
bool layoutPending{};
uint64_t layoutChangedAt{};
bool changingColumns{};
char columnCookie{};
uint64_t columnsReadyAfter{};
uint64_t waveRevision{}, bitmapTick{}, draws{}, builds{}, pathLookups{}, lastScan{}, lastStatus{};
uint64_t sourceOpens{}, peakReads{}, bitmapBuilds{}, bitmapHits{}, bitmapEvictions{};
uint64_t fastPeakHits{}, peakBuildStarts{}, peakBuildProbes{};
uint64_t peakCacheInvalidations{};
uint64_t directPeakReads{},directPeakBytes{},directFallbacks{},jobTicket{};
uint64_t pageChangedAt{},pageReadyMs{},pagePublications{};
int firstDrawReady=-1;
bool pageNativeRefreshDone=false,pageHadDataWork=false;
bool nativeItemDraw{};
LRESULT nativeItemFlags{};
mwt::CpuCost timerCost,paintCost,rasterCost,scanCost,statusCost;
uint64_t rowScans{},fileStats{},textQueries{},statusWrites{};
mwt::PeakReaderPool peakReaders;
mwt::PreviewSeek previewSeek;
int seekRow=-1;
uint64_t dirtySince{}, restoreLatency{};
int observedTop = -1, observedCount = -1;
std::string lastError;
std::wstring currentDirectory;
gaccel_register_t toggleAction{}, refreshAction{};

HWND (*OpenMediaExplorer)(const char*, bool);
PCM_source* (*CreateSource)(const char*, bool);
void (*DestroySource)(PCM_source*);
double (*SourceLength)(PCM_source*, bool*);
int (*SourceChannels)(PCM_source*);
int (*BuildPeaks)(PCM_source*, int);
int (*GetPeaks)(PCM_source*, double, double, int, int, int, double*);
const char* (*GetExtState)(const char*, const char*);
void (*SetExtState)(const char*, const char*, const char*, bool);
int (*GetToggleState)(int, int);
bool (*IsMediaExtension)(const char*, bool);
void (*GetPeakFileNameEx)(const char*, char*, int, bool);
void (*ClearPeakCache)();

std::string utf8(const std::wstring& s) {
  if (s.empty()) return {};
  int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
  std::string out(n,'\0');
  WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);
  return out;
}
std::wstring wide(const char* text) {
  int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,nullptr,0);
  if(length<=0)return {};
  std::wstring result(length,L'\0');
  MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,result.data(),length);
  result.resize(length-1);return result;
}
std::wstring windowText(HWND w) {
  int n=GetWindowTextLengthW(w); std::wstring s(n+1,L'\0');
  s.resize(GetWindowTextW(w,s.data(),n+1)); return s;
}
std::string json(const std::string& s) {
  std::string out="\"";
  for (unsigned char c:s) {
    if (c=='"'||c=='\\') {out+='\\';out+=char(c);}
    else if(c=='\n')out+="\\n";
    else if(c>=32)out+=char(c);
  }
  return out+'"';
}
using Stamp=mwt::FileStamp;
bool stamp(const std::wstring& path,Stamp& out){++fileStats;return mwt::fileStamp(path,out);}
struct Wave {
  Stamp stamp;
  std::array<float,bins> high{},low{};
  int state{}; // 0 queued, 1 ready, -1 unsupported/error
  int attempts{};
  uint64_t retryAfter{};
  uint64_t revision{};
  float peak{};
  bool sourceChanged{};
  bool directTried{},loading{},published{},batchRead{};
  uint64_t ticket{};
};
// Peak arrays live for the whole session. Only disposable render surfaces use
// LRU eviction; losing a surface must never cause another source/peak read.
std::map<std::wstring,Wave> cache;
std::map<int,std::wstring> rows;
std::map<int,Wave*> rowWaves;
using BitmapKey = std::tuple<uint64_t,int,int,COLORREF,COLORREF>;
struct Raster {
  HBITMAP handle{};
  size_t bytes{};
  uint64_t used{};
  Raster(HBITMAP h,size_t n,uint64_t tick):handle(h),bytes(n),used(tick){}
  Raster(const Raster&)=delete;
  Raster& operator=(const Raster&)=delete;
  Raster(Raster&& other) noexcept:handle(std::exchange(other.handle,nullptr)),bytes(other.bytes),used(other.used){}
  ~Raster(){if(handle)DeleteObject(handle);}
};
std::map<BitmapKey,Raster> bitmaps;
size_t bitmapBytes{};
HDC bitmapDC{};

void clearBitmaps(){bitmaps.clear();bitmapBytes=0;}
void discardBitmaps(const std::wstring& path) {
  auto wave=cache.find(path);if(wave==cache.end())return;
  for(auto it=bitmaps.begin();it!=bitmaps.end();) {
    if(std::get<0>(it->first)==wave->second.revision){bitmapBytes-=it->second.bytes;it=bitmaps.erase(it);}
    else ++it;
  }
}
void reserveBitmap(size_t bytes) {
  while(!bitmaps.empty()&&(bitmaps.size()>=bitmapLimit||bitmapBytes+bytes>bitmapBudget)) {
    auto victim=std::min_element(bitmaps.begin(),bitmaps.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
    bitmapBytes-=victim->second.bytes;bitmaps.erase(victim);++bitmapEvictions;
  }
}
void dirtyRows() {
  rowWaves.clear();rows.clear();rowsDirty=true;pipelinePending=true;statusDirty=true;
  pageChangedAt=GetTickCount64();firstDrawReady=-1;
  pageNativeRefreshDone=false;pageHadDataWork=false;
  if(!dirtySince)dirtySince=GetTickCount64();
}
PCM_source* activeSource{};
std::wstring activePath;
bool building{};
uint64_t buildStarted{};
bool currentPeakFile(const std::wstring& path,const Wave& wave,bool& stale,std::wstring& peakPath) {
  std::array<char,32768> name{};
  GetPeakFileNameEx(utf8(path).c_str(),name.data(),int(name.size()),false);
  peakPath=wide(name.data());
  Stamp peaks;
  bool exists=stamp(peakPath,peaks);
  stale=exists&&peaks.time<wave.stamp.time;
  return !wave.sourceChanged&&exists&&!stale;
}

// OWNERDATA list text belongs to REAPER. Ask its existing notification handler
// for each original cell instead of reading private row pointers.
std::wstring textAt(int row,int subitem) {
  ++textQueries;
  std::array<wchar_t,32768> buffer;
  buffer.front()=0;
  NMLVDISPINFOW n{};
  n.hdr={list,UINT_PTR(GetDlgCtrlID(list)),LVN_GETDISPINFOW};
  n.item.mask=LVIF_TEXT;n.item.iItem=row;n.item.iSubItem=subitem;
  n.item.pszText=buffer.data();n.item.cchTextMax=int(buffer.size());
  SendMessageW(explorer,WM_NOTIFY,n.hdr.idFrom,LPARAM(&n));
  buffer.back()=0;
  if(n.item.pszText && n.item.pszText!=LPSTR_TEXTCALLBACKW)return n.item.pszText;
  return {};
}
std::wstring resolveFile(int row,Stamp* found=nullptr) {
  std::wstring name=textAt(row,0),ext=textAt(row,3);
  if(name.empty()||name==L"..")return {};
  if(!ext.empty() && ext.front()==L'.')ext.erase(0,1);
  std::vector<std::wstring> names{name};
  if(!ext.empty()&&ext.find_first_of(L"\\/: *?<>|")==std::wstring::npos)names.push_back(name+L"."+ext);
  for(const auto& n:names) {
    auto suffix=std::filesystem::path(n).extension().wstring();
    if(suffix.empty()||!mwt::waveformExtension(utf8(suffix))||!IsMediaExtension(utf8(suffix.substr(1)).c_str(),false))continue;
    std::filesystem::path candidate(n);
    if(!candidate.is_absolute()) {
      std::filesystem::path directory(currentDirectory);
      if(!directory.is_absolute())continue;
      candidate=directory/candidate;
    }
    Stamp value;
    if(stamp(candidate.wstring(),value)){if(found)*found=value;return candidate.lexically_normal().wstring();}
  }
  return {};
}
void stopBuild() {
  if(activeSource) {
    if(building)BuildPeaks(activeSource,2);
    DestroySource(activeSource);
  }
  activeSource=nullptr;activePath.clear();building=false;
}
void invalidate(int row=-1) {
  statusDirty=true;
  if(!IsWindow(list)||column<0||ListView_GetItemCount(list)<=0)return;
  RECT cell{},client{};
  if(!ListView_GetSubItemRect(list,row>=0?row:std::max(0,ListView_GetTopIndex(list)),column,LVIR_BOUNDS,&cell)||!GetClientRect(list,&client))return;
  RECT rect{std::max(cell.left,client.left),std::max(cell.top,client.top),std::min(cell.right,client.right),row>=0?std::min(cell.bottom,client.bottom):client.bottom};
  if(rect.left<rect.right&&rect.top<rect.bottom)InvalidateRect(list,&rect,FALSE);
}
bool readWave(bool retryOnEmpty) {
  auto it=cache.find(activePath);
  if(it==cache.end()){stopBuild();return true;}
  auto& wave=it->second;
  bool qn=false;double length=SourceLength(activeSource,&qn);
  int channels=SourceChannels(activeSource);
  if(qn||!std::isfinite(length)||length<=0||channels<=0||channels>128) {wave.state=-1;stopBuild();return true;}
  std::vector<double> buffer(size_t(bins)*channels*2,0.0);
  ++peakReads;
  int result=GetPeaks(activeSource,bins/length,0,channels,bins,0,buffer.data());
  int count=result&0xfffff;
  if(count>0&&count<=bins) {
    for(int x=0;x<bins;++x) {
      int i=std::min(x,count-1);
      double hi=0,lo=0;
      for(int ch=0;ch<channels;++ch) {
        hi=std::max(hi,buffer[size_t(i)*channels+ch]);
        lo=std::min(lo,buffer[size_t(bins)*channels+size_t(i)*channels+ch]);
      }
      wave.high[x]=float(std::clamp(hi,-1.0,1.0));
      wave.low[x]=float(std::clamp(lo,-1.0,1.0));
      wave.peak=std::max({wave.peak,std::abs(wave.high[x]),std::abs(wave.low[x])});
    }
    wave.state=1;++builds;pageHadDataWork=true;
  }else {
    if(!retryOnEmpty)return false;
    // A simultaneous Media Explorer preview may still be publishing its peak
    // cache. Retry transient empty reads before treating the source as missing.
    wave.state=++wave.attempts>=5?-1:0;
    wave.retryAfter=GetTickCount64()+500;
  }
  stopBuild();return true;
}
bool processNext() {
  if(activeSource) {
    statusDirty=true;
    if(GetTickCount64()-buildStarted>30000) {
      cache[activePath].state=-1;lastError="Peak build exceeded 30 seconds";stopBuild();return true;
    }
    if(building && BuildPeaks(activeSource,1)!=0)return false;
    if(building){BuildPeaks(activeSource,2);building=false;}
    readWave(true);return true;
  }
  for(auto& [row,path]:rows) {
    auto it=cache.find(path);
    if(it==cache.end()||it->second.state!=0||it->second.loading||!it->second.directTried||it->second.retryAfter>GetTickCount64())continue;
    if(!mwt::waveformPath(utf8(path))){it->second.state=-1;continue;}
    statusDirty=true;
    bool stale=false;
    std::wstring peakPath;
    bool haveCurrentPeaks=currentPeakFile(path,it->second,stale,peakPath);
    // REAPER may retain a peak object even after its source file changed.
    // Drop those stale global references BEFORE creating the new source.
    if(it->second.sourceChanged||stale) {
      ClearPeakCache();++peakCacheInvalidations;
      // BuildPeaks(0) can accept a stale peak file. Remove only the derived
      // .reapeaks file reported by REAPER for this changed source.
      auto extension=std::filesystem::path(peakPath).extension().wstring();
      if(_wcsicmp(extension.c_str(),L".reapeaks")==0&&peakPath!=path&&!DeleteFileW(peakPath.c_str())) {
        DWORD error=GetLastError();
        if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND) {
          it->second.state=-1;
          lastError="Unable to invalidate stale REAPER peak file (Windows error "+std::to_string(error)+")";
          return true;
        }
      }
    }
    activePath=path;++sourceOpens;activeSource=CreateSource(utf8(path).c_str(),true);
    if(!activeSource){it->second.state=-1;activePath.clear();return true;}
    bool qn=false;double length=SourceLength(activeSource,&qn);
    if(qn||length<=0||SourceChannels(activeSource)<=0){it->second.state=-1;stopBuild();return true;}
    // Existing .reapeaks (or already available source peaks) can be read
    // immediately. Do not send every visited source through the builder first.
    if(haveCurrentPeaks&&readWave(false)) {
      if(it->second.state==1)++fastPeakHits;
      return true;
    }
    ++peakBuildProbes;
    buildStarted=GetTickCount64();building=BuildPeaks(activeSource,0)!=0;
    if(building){++peakBuildStarts;it->second.batchRead=false;}
    else readWave(true);
    return !building;
  }
  return false;
}
void processQueue() {
  // Cache hits are already restored as one row map by scanRows(). For misses,
  // batch inexpensive existing-peak reads, yielding after 8 ms or 64 steps.
  auto started=GetTickCount64();
  for(int n=0;n<64;++n) {
    if(!processNext()||GetTickCount64()-started>=8)break;
  }
}
void retainVisibleReads() {
  std::set<std::wstring> paths;
  for(const auto& row:rows)paths.insert(row.second);
  for(const auto& job:peakReaders.retain(paths)) {
    auto it=cache.find(job.path);
    if(it!=cache.end()&&it->second.ticket==job.ticket){it->second.loading=false;it->second.directTried=false;it->second.batchRead=false;it->second.ticket=0;}
  }
}
void takeDirectResults() {
  for(auto& result:peakReaders.take()) {
    auto it=cache.find(result.job.path);
    if(it==cache.end()||it->second.revision!=result.job.revision||it->second.ticket!=result.job.ticket)continue;
    auto& wave=it->second;wave.loading=false;
    pipelinePending=true;statusDirty=true;
    Stamp now;
    if(!stamp(result.job.path,now)||!(now==wave.stamp)) {
      discardBitmaps(result.job.path);
      wave=Wave{};wave.stamp=now;wave.revision=++waveRevision;wave.sourceChanged=true;wave.directTried=true;
      rowsDirty=true;continue;
    }
    if(result.result==mwt::PeakResult::Ready) {
      wave.high=result.data.high;wave.low=result.data.low;wave.peak=result.data.peak;
      wave.state=1;wave.published=false;pageHadDataWork=true;++directPeakReads;++builds;directPeakBytes+=result.data.bytesRead;
    }else {
      if(result.result==mwt::PeakResult::Stale){wave.sourceChanged=true;wave.batchRead=false;}
      ++directFallbacks;
    }
  }
}
void queueDirectReads() {
  // Wait briefly for the native visible row set to settle, then dispatch the
  // whole page. Workers only read cache files; REAPER/GDI stay on this thread.
  if(GetTickCount64()-pageChangedAt<80)return;
  for(const auto& [row,path]:rows) {
    auto& wave=cache.at(path);
    if(wave.state!=0||wave.loading||wave.directTried)continue;
    wave.directTried=true;
    bool stale=false;std::wstring peakPath;
    if(!currentPeakFile(path,wave,stale,peakPath))continue;
    wave.ticket=++jobTicket;wave.loading=true;wave.batchRead=true;
    if(!peakReaders.submit({path,peakPath,wave.stamp,wave.revision,wave.ticket}))wave.loading=false;
  }
}
int publishedRows() {
  int ready=0;
  for(const auto& row:rowWaves)if(row.second->state==1&&row.second->published)++ready;
  return ready;
}
void publishPreparedPage() {
  for(const auto& row:rows) {
    const auto& wave=cache.at(row.second);
    if(wave.loading||(wave.batchRead&&wave.state==0))return;
  }
  int changed=0,changedRow=-1;
  for(const auto& row:rows) {
    auto& wave=cache.at(row.second);
    if(wave.state==1&&!wave.published){wave.published=true;++changed;changedRow=row.first;}
  }
  if(changed) {
    ++pagePublications;pageReadyMs=GetTickCount64()-pageChangedAt;
    if(pageHadDataWork&&!pageNativeRefreshDone) {
      // A new asynchronous view may have been painted before its native theme
      // state settled. Refresh native text once, without recoloring it ourselves.
      pageNativeRefreshDone=true;InvalidateRect(list,nullptr,FALSE);statusDirty=true;
    }else invalidate(changed==1?changedRow:-1);
  }
  pipelinePending=activeSource!=nullptr;
  for(const auto& row:rowWaves)if(row.second->state==0){pipelinePending=true;break;}
}
void drawEnvelope(HDC dc,const RECT& r,const Wave& wave,COLORREF color) {
  HPEN pen=CreatePen(PS_SOLID,1,color);
  if(!pen)return;
  HGDIOBJ old=SelectObject(dc,pen);
  int mid=(r.top+r.bottom)/2;
  double scale=std::max<int>(1,(r.bottom-r.top)/2)/std::max(0.01f,wave.peak);
  for(int x=r.left;x<r.right;++x) {
    int bin=std::clamp<int>((x-r.left)*bins/std::max<int>(1,r.right-r.left),0,bins-1);
    int top=mid-int(std::round(wave.high[bin]*scale));
    int bottom=mid-int(std::round(wave.low[bin]*scale));
    MoveToEx(dc,x,std::clamp<int>(top,r.top,r.bottom-1),nullptr);
    LineTo(dc,x,std::clamp<int>(std::max(top+1,bottom),r.top+1,r.bottom));
  }
  SelectObject(dc,old);DeleteObject(pen);
}
bool blitWave(HDC target,const RECT& r,const Wave& wave,COLORREF fg,COLORREF bg) {
  int width=r.right-r.left,height=r.bottom-r.top;
  size_t bytes=size_t(width)*height*4;
  if(bytes>bitmapBudget)return false;
  if(!bitmapDC)bitmapDC=CreateCompatibleDC(target);
  if(!bitmapDC)return false;
  BitmapKey key{wave.revision,width,height,fg,bg};
  auto it=bitmaps.find(key);
  bool hit=it!=bitmaps.end();
  if(!hit) {
    mwt::CpuScope measure(rasterCost);
    Raster raster(mwt::rasterWave(target,width,height,wave.high,wave.low,wave.peak,fg,bg),bytes,++bitmapTick);
    if(!raster.handle)return false;
    reserveBitmap(bytes);
    it=bitmaps.try_emplace(std::move(key),std::move(raster)).first;
    bitmapBytes+=bytes;++bitmapBuilds;
  }
  it->second.used=++bitmapTick;
  HGDIOBJ old=SelectObject(bitmapDC,it->second.handle);
  bool copied=BitBlt(target,r.left,r.top,width,height,bitmapDC,0,0,SRCCOPY)!=FALSE;
  SelectObject(bitmapDC,old);
  if(copied&&hit)++bitmapHits;
  return copied;
}
void paintWave(NMLVCUSTOMDRAW* n) {
  mwt::CpuScope measure(paintCost);
  mwt::DcState state(n->nmcd.hdc);
  if(!state)return;
  statusDirty=true;
  int row=int(n->nmcd.dwItemSpec);RECT r{};
  if(!ListView_GetSubItemRect(list,row,column,LVIR_BOUNDS,&r))return;
  IntersectClipRect(n->nmcd.hdc,r.left,r.top,r.right,r.bottom);
  COLORREF bg=ListView_GetTextBkColor(list);
  if(bg==CLR_NONE)bg=ListView_GetBkColor(list);
  bool selected=ListView_GetItemState(list,row,LVIS_SELECTED)!=0;
  if(selected)bg=GetSysColor(COLOR_HIGHLIGHT);
  COLORREF oldBrush=SetDCBrushColor(n->nmcd.hdc,bg);
  FillRect(n->nmcd.hdc,&r,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  SetDCBrushColor(n->nmcd.hdc,oldBrush);
  auto rowIt=rowWaves.find(row);if(rowIt==rowWaves.end())return;
  auto& wave=*rowIt->second;
  r.left+=6;r.right-=6;r.top+=3;r.bottom-=3;
  if(r.right<=r.left||r.bottom<=r.top)return;
  COLORREF color=selected?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(55,166,152);
  if(wave.state==1&&wave.published) {
    if(firstDrawReady<0)firstDrawReady=publishedRows();
    if(!blitWave(n->nmcd.hdc,r,wave,color,bg))drawEnvelope(n->nmcd.hdc,r,wave,color);
    ++draws;
  }else if(wave.state==0) {
    HPEN pen=CreatePen(PS_SOLID,1,color);HGDIOBJ old=SelectObject(n->nmcd.hdc,pen);
    int mid=(r.top+r.bottom)/2;
    MoveToEx(n->nmcd.hdc,r.left,mid,nullptr);LineTo(n->nmcd.hdc,r.right,mid);
    SelectObject(n->nmcd.hdc,old);DeleteObject(pen);
  }
}
bool ownsColumn() {
  if(!IsWindow(list))return false;
  HWND header=ListView_GetHeader(list);
  auto isOurs=[&](int i) {
    HDITEMW item{};item.mask=HDI_LPARAM;
    return i>=0&&Header_GetItem(header,i,&item)&&item.lParam==reinterpret_cast<LPARAM>(&columnCookie);
  };
  if(isOurs(column))return true;
  for(int i=0,n=Header_GetItemCount(header);i<n;++i)if(isOurs(i)){column=i;return true;}
  column=-1;return false;
}
void saveWaveLayout(bool force=false) {
  if(!changingColumns&&ownsColumn()) {
    HDITEMW item{};item.mask=HDI_ORDER;
    if(Header_GetItem(ListView_GetHeader(list),column,&item)) {
      int width=ListView_GetColumnWidth(list,column);
      if(item.iOrder!=waveOrder||width!=waveWidth) {
        waveOrder=item.iOrder;waveWidth=width;
        layoutPending=true;layoutChangedAt=GetTickCount64();statusDirty=true;
      }
    }
  }
  if(layoutPending&&(force||GetTickCount64()-layoutChangedAt>=250)) {
    auto order=std::to_string(waveOrder),width=std::to_string(waveWidth);
    SetExtState(section,"wave_order",order.c_str(),true);
    SetExtState(section,"wave_width",width.c_str(),true);
    layoutPending=false;statusDirty=true;
  }
}
void removeWaveColumn() {
  previewSeek.cancel();
  if(!ownsColumn())return;
  saveWaveLayout(true);
  int own=column;column=-1;
  bool prior=std::exchange(changingColumns,true);
  ListView_DeleteColumn(list,own);
  changingColumns=prior;
}
bool addWaveColumn() {
  if(ownsColumn())return true;
  int count=Header_GetItemCount(ListView_GetHeader(list));
  if(count<=0)return false;
  // Capture the ORIGINAL order before extending the native schema. In
  // particular, Mark/Fav can precede File and must retain their positions.
  std::vector<int> order(count);
  if(!ListView_GetColumnOrderArray(list,count,order.data()))return false;
  auto file=std::find(order.begin(),order.end(),0);
  auto slot=waveOrder>=0?order.begin()+std::min(waveOrder,count):(file==order.end()?order.end():file+1);
  order.insert(slot,count);
  bool prior=std::exchange(changingColumns,true);
  column=count;
  LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM|LVCF_ORDER;
  c.pszText=const_cast<wchar_t*>(columnTitle);c.cx=waveWidth;c.iSubItem=column;c.iOrder=count;
  if(ListView_InsertColumn(list,column,&c)<0){column=-1;changingColumns=prior;return false;}
  HDITEMW tag{};tag.mask=HDI_LPARAM;tag.lParam=reinterpret_cast<LPARAM>(&columnCookie);
  Header_SetItem(ListView_GetHeader(list),column,&tag);
  ListView_SetColumnOrderArray(list,int(order.size()),order.data());
  changingColumns=prior;
  dirtyRows();return true;
}
void detach();
void scanRows();
LRESULT CALLBACK parentProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
  if(m==WM_CLOSE||m==WM_DESTROY) {
    // Native shutdown saves column preferences. It must see only native
    // columns, rather than our extra logical index/order positions.
    detach();return DefSubclassProc(w,m,wp,lp);
  }
  if(m==WM_COMMAND) {
    int cmd=LOWORD(wp);
    if(cmd==42172||(cmd>=42338&&cmd<=42347)||cmd==42359) {
      removeWaveColumn();++columnMenuDepth;
      LRESULT result=DefSubclassProc(w,m,wp,lp);
      --columnMenuDepth;columnsReadyAfter=GetTickCount64()+100;
      return result;
    }
  }
  if(m==WM_NOTIFY && lp && reinterpret_cast<NMHDR*>(lp)->hwndFrom==list) {
    auto hdr=reinterpret_cast<NMHDR*>(lp);
    if(hdr->code==LVN_BEGINDRAG)previewSeek.cancel();
    if(hdr->code==NM_CLICK||hdr->code==NM_DBLCLK){
      auto* click=reinterpret_cast<NMITEMACTIVATE*>(lp);
      previewSeek.cancel();
      if(click->iSubItem==column&&column>=0){
        if(!mwt::previewModifiers()&&ownsColumn()){
          scanRows(); // refresh hidden full paths in database/search results
          RECT cell{};auto it=rowWaves.find(click->iItem);
          if(it!=rowWaves.end()&&it->second->state==1&&it->second->published&&ListView_GetSubItemRect(list,click->iItem,column,LVIR_BOUNDS,&cell)){
            auto path=rows.count(click->iItem)?rows.at(click->iItem):std::wstring{};
            if(!path.empty()){
              double fraction=mwt::thumbnailRatio(click->ptAction.x,cell.left,cell.right);
              NMITEMACTIVATE native=*click;native.hdr.code=NM_CLICK;native.iSubItem=0;
              // Let REAPER commit its current preview selection using a valid
              // native column, without forwarding a double-click insertion.
              DefSubclassProc(w,m,wp,reinterpret_cast<LPARAM>(&native));
              seekRow=click->iItem;previewSeek.request(utf8(path),fraction,GetTickCount64());statusDirty=true;
            }
          }
        }
        return 0; // double-clicking a waveform must not insert into the project
      }
    }
    if(hdr->code==LVN_GETDISPINFOW) {
      auto n=reinterpret_cast<NMLVDISPINFOW*>(lp);
      if(n->item.iSubItem==column && column>=0) {
        if((n->item.mask&LVIF_TEXT)&&n->item.pszText&&n->item.cchTextMax>0)n->item.pszText[0]=0;
        return 0;
      }
    }
    if(hdr->code==LVN_GETDISPINFOA) {
      auto n=reinterpret_cast<NMLVDISPINFOA*>(lp);
      if(n->item.iSubItem==column && column>=0) {
        if((n->item.mask&LVIF_TEXT)&&n->item.pszText&&n->item.cchTextMax>0)n->item.pszText[0]=0;
        return 0;
      }
    }
    if(hdr->code==LVN_COLUMNCLICK) {
      if(reinterpret_cast<NMLISTVIEW*>(lp)->iSubItem==column)return 0;
      dirtyRows();
    }
    if(hdr->code==NM_CUSTOMDRAW && column>=0) {
      auto n=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
      // Do not force subitem notifications or skip the native cell paint.
      // Native theme/font/color setup must run with its original draw stages.
      if((n->nmcd.dwDrawStage&CDDS_SUBITEM)&&n->iSubItem==column)return CDRF_DODEFAULT;
      LRESULT result=CDRF_DODEFAULT;
      if(n->nmcd.dwDrawStage==CDDS_PREPAINT) {
        result=DefSubclassProc(w,m,wp,lp);
        nativeItemDraw=(result&CDRF_NOTIFYITEMDRAW)!=0;
        return result|CDRF_NOTIFYITEMDRAW;
      }
      if(n->nmcd.dwDrawStage==CDDS_ITEMPREPAINT) {
        if(nativeItemDraw)result=DefSubclassProc(w,m,wp,lp);
        nativeItemFlags=result;
        return result|CDRF_NOTIFYPOSTPAINT;
      }
      if(n->nmcd.dwDrawStage==CDDS_ITEMPOSTPAINT) {
        if(nativeItemDraw&&(nativeItemFlags&CDRF_NOTIFYPOSTPAINT))result=DefSubclassProc(w,m,wp,lp);
        try {paintWave(n);}catch(const std::exception& e){lastError=e.what();statusDirty=true;}
        return result;
      }
      return DefSubclassProc(w,m,wp,lp);
    }
  }
  if(m==WM_NCDESTROY)RemoveWindowSubclass(w,parentProc,subclassId);
  return DefSubclassProc(w,m,wp,lp);
}
LRESULT CALLBACK listProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
  if(!changingColumns) {
    bool order=m==LVM_SETCOLUMNORDERARRAY;
    bool structural=m==LVM_INSERTCOLUMNA||m==LVM_INSERTCOLUMNW||m==LVM_DELETECOLUMN;
    if(structural||(order&&int(wp)!=Header_GetItemCount(ListView_GetHeader(w)))) {
      bool deletingOurs=m==LVM_DELETECOLUMN&&ownsColumn()&&int(wp)==column;
      removeWaveColumn();columnsReadyAfter=GetTickCount64()+100;
      dirtyRows();
      if(deletingOurs)return TRUE;
      return DefSubclassProc(w,m,wp,lp);
    }
    if(m==WM_CONTEXTMENU&&reinterpret_cast<HWND>(wp)==ListView_GetHeader(w)) {
      removeWaveColumn();++columnMenuDepth;
      LRESULT result=DefSubclassProc(w,m,wp,lp);
      --columnMenuDepth;columnsReadyAfter=GetTickCount64()+100;
      return result;
    }
  }
  if(m==LVM_SETITEMCOUNT||m==LVM_DELETEALLITEMS||m==LVM_SORTITEMS||m==LVM_SORTITEMSEX) {
    dirtyRows();
  }
  if(m==WM_VSCROLL||m==WM_MOUSEWHEEL||m==LVM_ENSUREVISIBLE||m==WM_KEYDOWN) {
    rowsDirty=true;
    if(!dirtySince)dirtySince=GetTickCount64();
  }
  if(m==WM_NCDESTROY)RemoveWindowSubclass(w,listProc,subclassId);
  return DefSubclassProc(w,m,wp,lp);
}
void detach() {
  previewSeek.cancel();seekRow=-1;
  stopBuild();dirtyRows();
  removeWaveColumn();
  if(IsWindow(list))RemoveWindowSubclass(list,listProc,subclassId);
  if(IsWindow(explorer)) {
    RemoveWindowSubclass(explorer,parentProc,subclassId);
  }
  explorer=nullptr;list=nullptr;column=-1;currentDirectory.clear();
}
BOOL CALLBACK findExplorer(HWND w,LPARAM) {
  HWND child=GetDlgItem(w,1001);
  if(child&&GetDlgItem(w,1002)) {
    wchar_t name[64]{};GetClassNameW(child,name,64);
    if(std::wstring(name)==L"SysListView32") {explorer=w;list=child;return FALSE;}
  }
  return TRUE;
}
BOOL CALLBACK findRoot(HWND w,LPARAM) {
  findExplorer(w,0);
  if(!explorer)EnumChildWindows(w,findExplorer,0);
  return explorer?FALSE:TRUE;
}
bool attach() {
  EnumThreadWindows(GetCurrentThreadId(),findRoot,0);
  if(!list)return false;
  if(!SetWindowSubclass(explorer,parentProc,subclassId,0)){explorer=list=nullptr;return false;}
  if(!SetWindowSubclass(list,listProc,subclassId,0)){detach();return false;}
  // Let native startup finish applying saved widths and order first.
  columnsReadyAfter=GetTickCount64()+100;
  observedTop=observedCount=-1;rowsDirty=true;lastScan=0;return true;
}
void scanRows() {
  mwt::CpuScope measure(scanCost);++rowScans;
  rowsDirty=false;
  auto directory=windowText(GetDlgItem(explorer,1002));
  if(directory!=currentDirectory){rowWaves.clear();rows.clear();}
  currentDirectory=std::move(directory);
  std::map<int,std::wstring> next;
  int first=std::max(0,ListView_GetTopIndex(list));
  int last=std::min(ListView_GetItemCount(list),first+ListView_GetCountPerPage(list)+2);
  std::vector<Stamp> stamps(size_t(std::max(0,last-first)));
  std::vector<int> unresolved;
  for(int row=first;row<last;++row) {
    Stamp value;
    auto path=resolveFile(row,&value);
    if(path.empty()) {
      auto ext=textAt(row,3);
      if(mwt::waveformExtension(utf8(ext))&&IsMediaExtension(utf8(ext).c_str(),false)) {
        auto previous=rows.find(row);
        if(previous!=rows.end()) {
          auto name=textAt(row,0),base=std::filesystem::path(previous->second).filename().wstring();
          if((name==base||name+L"."+ext==base)&&stamp(previous->second,value))path=previous->second;
        }
        if(path.empty())unresolved.push_back(row);
      }
    }
    if(!path.empty()){next.emplace(row,path);stamps[size_t(row-first)]=value;}
  }
  if(!unresolved.empty()&&GetToggleState(32063,42026)==0) {
    // Query the native database/search rows synchronously, then restore both
    // display preferences before returning to the UI message loop.
    bool leading=GetToggleState(32063,42134)==1;
    SendMessageW(explorer,WM_COMMAND,42026,0);
    ++pathLookups;
    try {
      for(int row:unresolved) {
        Stamp value;auto path=resolveFile(row,&value);
        if(!path.empty()){next[row]=path;stamps[size_t(row-first)]=value;}
      }
    }catch(...) {
      SendMessageW(explorer,WM_COMMAND,42026,0);
      if(leading&&GetToggleState(32063,42134)!=1)SendMessageW(explorer,WM_COMMAND,42134,0);
      throw;
    }
    SendMessageW(explorer,WM_COMMAND,42026,0);
    if(leading&&GetToggleState(32063,42134)!=1)SendMessageW(explorer,WM_COMMAND,42134,0);
  }
  for(auto it=next.begin();it!=next.end();) {
    const auto& path=it->second;
    Stamp fileStamp=stamps.at(size_t(it->first-first));
    auto [entry,inserted]=cache.try_emplace(path);
    if(inserted||!(entry->second.stamp==fileStamp)) {
      if(path==activePath)stopBuild();
      discardBitmaps(path);
      entry->second=Wave{};entry->second.stamp=fileStamp;entry->second.revision=++waveRevision;
      entry->second.sourceChanged=!inserted;
      pipelinePending=true;statusDirty=true;
    }
    ++it;
  }
  bool changed=rows!=next;
  if(changed){rowWaves.clear();rows=std::move(next);pageChangedAt=GetTickCount64();firstDrawReady=-1;pageNativeRefreshDone=false;pageHadDataWork=false;retainVisibleReads();}
  if(changed||rowWaves.size()!=rows.size()) {
    rowWaves.clear();for(const auto& row:rows)rowWaves.emplace(row.first,&cache.at(row.second));
  }
  if(changed){pipelinePending=true;invalidate();}
  if(activeSource && std::none_of(rows.begin(),rows.end(),[](const auto& row){return row.second==activePath;}))stopBuild();
  observedTop=first;observedCount=ListView_GetItemCount(list);
  if(dirtySince){restoreLatency=GetTickCount64()-dirtySince;dirtySince=0;}
}
void validateVisibleRows() {
  for(const auto& row:rows) {
    Stamp now;
    if(!stamp(row.second,now)){rowsDirty=true;pipelinePending=true;return;}
    auto& wave=*rowWaves.at(row.first);
    if(!(wave.stamp==now)) {
      if(row.second==activePath)stopBuild();
      discardBitmaps(row.second);
      wave=Wave{};wave.stamp=now;wave.revision=++waveRevision;wave.sourceChanged=true;
      pipelinePending=true;invalidate(row.first);
    }
  }
}
void publishStatus() {
  mwt::CpuScope measure(statusCost);++statusWrites;
  std::ostringstream s;
  int ready=0;for(const auto& row:rowWaves)if(row.second->state==1)++ready;
  s<<"{\"version\":"<<json(version)<<",\"enabled\":"<<(enabled?"true":"false")
   <<",\"attached\":"<<(ownsColumn()?"true":"false")<<",\"column\":"<<column
   <<",\"visible_files\":"<<rows.size()<<",\"ready\":"<<ready<<",\"cache\":"<<cache.size()
   <<",\"builds\":"<<builds<<",\"draws\":"<<draws<<",\"path_lookups\":"<<pathLookups
   <<",\"source_opens\":"<<sourceOpens<<",\"peak_reads\":"<<peakReads
   <<",\"fast_peak_hits\":"<<fastPeakHits<<",\"peak_build_probes\":"<<peakBuildProbes<<",\"peak_build_starts\":"<<peakBuildStarts
   <<",\"peak_cache_invalidations\":"<<peakCacheInvalidations
   <<",\"direct_peak_reads\":"<<directPeakReads<<",\"direct_peak_bytes\":"<<directPeakBytes<<",\"direct_fallbacks\":"<<directFallbacks
   <<",\"published_ready\":"<<publishedRows()<<",\"first_draw_ready\":"<<firstDrawReady<<",\"page_ready_ms\":"<<pageReadyMs
   <<",\"page_publications\":"<<pagePublications
   <<",\"row_scans\":"<<rowScans<<",\"file_stats\":"<<fileStats<<",\"text_queries\":"<<textQueries<<",\"status_writes\":"<<statusWrites
   <<",\"cpu_timer_ms\":"<<mwt::milliseconds(timerCost)<<",\"cpu_paint_ms\":"<<mwt::milliseconds(paintCost)
   <<",\"cpu_raster_ms\":"<<mwt::milliseconds(rasterCost)<<",\"cpu_scan_ms\":"<<mwt::milliseconds(scanCost)
   <<",\"cpu_status_ms\":"<<mwt::milliseconds(statusCost)
   <<",\"cpu_timer_cycles\":"<<timerCost.cycles<<",\"cpu_paint_cycles\":"<<paintCost.cycles
#ifdef MWT_PROFILE
   <<",\"profile_enabled\":true"
#endif
   <<",\"wave_order\":"<<waveOrder<<",\"wave_width\":"<<waveWidth<<",\"layout_pending\":"<<(layoutPending?"true":"false")
   <<",\"bitmap_builds\":"<<bitmapBuilds<<",\"bitmap_hits\":"<<bitmapHits
   <<",\"bitmap_cache\":"<<bitmaps.size()<<",\"bitmap_bytes\":"<<bitmapBytes<<",\"bitmap_evictions\":"<<bitmapEvictions
   <<",\"peak_array_bytes\":"<<cache.size()*bins*2*sizeof(float)<<",\"restore_latency_ms\":"<<restoreLatency
   <<",\"seek_requests\":"<<previewSeek.requests<<",\"seek_completed\":"<<previewSeek.completed<<",\"seek_cancelled\":"<<previewSeek.cancelled
   <<",\"seek_pending\":"<<(previewSeek.pending()?"true":"false")<<",\"seek_ratio\":"<<previewSeek.lastRatio<<",\"seek_path\":"<<json(previewSeek.lastPath)<<",\"seek_error\":"<<json(previewSeek.error)
   <<",\"directory\":"<<json(utf8(currentDirectory))<<",\"error\":"<<json(lastError)<<",\"files\":[";
  bool first=true;
  for(const auto& [r,p]:rows) {
    if(!first)s<<',';
    first=false;const auto& wave=cache.at(p);
    s<<"{\"row\":"<<r<<",\"path\":"<<json(utf8(p))<<",\"state\":"<<wave.state<<",\"peak\":"<<wave.peak<<'}';
  }
  s<<"]}";SetExtState(section,"status",s.str().c_str(),false);statusDirty=false;
}
void timer() {
  if(busy)return;
  mwt::CpuScope measure(timerCost);
  busy=true;
  try {
    auto now=GetTickCount64();
#ifdef MWT_PROFILE
    const char* command=GetExtState(section,"profile_command");
    if(command&&std::string(command)=="drop_bitmaps") {
      clearBitmaps();invalidate();SetExtState(section,"profile_command","",false);
    }
#endif
    bool statusDue=statusDirty&&now-lastStatus>=300;
    if(!previewSeek.pending()&&!rowsDirty&&!pipelinePending&&!layoutPending&&!statusDue&&now<nextIdleCheck){busy=false;return;}
    nextIdleCheck=now+100;
    takeDirectResults();
    if(enabled) {
      if(list&&!IsWindow(list))detach();
      if(!list && now-lastScan>=500){lastScan=now;attach();}
      if(list) {
        bool haveColumn=ownsColumn();
        if(!haveColumn&&!columnMenuDepth&&now>=columnsReadyAfter)haveColumn=addWaveColumn();
        if(haveColumn&&(layoutPending||now-lastLayoutCheck>=500)){lastLayoutCheck=now;saveWaveLayout();}
        if(haveColumn&&IsWindowVisible(list)) {
          bool changed=rowsDirty||ListView_GetTopIndex(list)!=observedTop||ListView_GetItemCount(list)!=observedCount;
          if(!changed&&now-lastDirectoryCheck>=250) {
            lastDirectoryCheck=now;changed=windowText(GetDlgItem(explorer,1002))!=currentDirectory;
          }
          if(changed){lastScan=lastValidation=lastDirectoryCheck=now;scanRows();}
          else if(now-lastValidation>=1000){lastValidation=now;validateVisibleRows();}
          if(pipelinePending){queueDirectReads();processQueue();publishPreparedPage();}
          if(previewSeek.pending()){
            auto row=rows.find(seekRow);
            auto path=(row!=rows.end()&&ListView_GetItemState(list,seekRow,LVIS_SELECTED))?utf8(row->second):std::string{};
            if(previewSeek.tick(explorer,path,now))statusDirty=true;
          }
        }
      }
    }
    bool update=statusDirty;
#ifdef MWT_PROFILE
    update=true;
#endif
    if(update&&now-lastStatus>=300){lastStatus=now;publishStatus();}
  }catch(const std::exception& e){lastError=e.what();statusDirty=true;stopBuild();}
  busy=false;
}
bool hookCommand(int cmd,int) {
  if(cmd==toggleAction.accel.cmd) {
    enabled=!enabled;if(!enabled)detach();
    SetExtState(section,"enabled",enabled?"1":"0",true);publishStatus();return true;
  }
  if(cmd==refreshAction.accel.cmd) {
    previewSeek.cancel();
    stopBuild();peakReaders.retain({});clearBitmaps();dirtyRows();cache.clear();lastScan=0;lastError.clear();invalidate();publishStatus();return true;
  }
  return false;
}
int toggleState(int cmd){return cmd==toggleAction.accel.cmd?int(enabled):-1;}
template<typename T> bool load(T& fn,const char* name){fn=reinterpret_cast<T>(host.GetFunc(name));return fn!=nullptr;}
int layoutSetting(const char* key,int fallback,int max) {
  const char* text=GetExtState(section,key);
  if(!text||!*text)return fallback;
  char* end=nullptr;long n=std::strtol(text,&end,10);
  return *end==0&&n>=0&&n<=max?int(n):fallback;
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(HINSTANCE,reaper_plugin_info_t* rec) {
  if(!rec) {
    if(host.Register) {
      host.Register("-timer",reinterpret_cast<void*>(timer));
      peakReaders.stop();detach();saveWaveLayout(true);clearBitmaps();
      if(bitmapDC){DeleteDC(bitmapDC);bitmapDC=nullptr;}
      host.Register("-hookcommand",reinterpret_cast<void*>(hookCommand));
      host.Register("-toggleaction",reinterpret_cast<void*>(toggleState));
      host.Register("-gaccel",&toggleAction);host.Register("-gaccel",&refreshAction);
    }
    return 0;
  }
  if(rec->caller_version!=REAPER_PLUGIN_VERSION)return 0;
  host=*rec;
  if(!load(OpenMediaExplorer,"OpenMediaExplorer")||!load(CreateSource,"PCM_Source_CreateFromFileEx")||
     !load(DestroySource,"PCM_Source_Destroy")||!load(SourceLength,"GetMediaSourceLength")||
     !load(SourceChannels,"GetMediaSourceNumChannels")||!load(BuildPeaks,"PCM_Source_BuildPeaks")||
     !load(GetPeaks,"PCM_Source_GetPeaks")||!load(GetExtState,"GetExtState")||
     !load(SetExtState,"SetExtState")||!load(GetToggleState,"GetToggleCommandStateEx")||
     !load(IsMediaExtension,"IsMediaExtension")||!load(GetPeakFileNameEx,"GetPeakFileNameEx")||
     !load(ClearPeakCache,"ClearPeakCache"))return 0;
  enabled=std::string(GetExtState(section,"enabled"))!="0";
  waveOrder=layoutSetting("wave_order",-1,65535);
  waveWidth=layoutSetting("wave_width",196,65535);
  try {peakReaders.start();}catch(const std::exception& e){lastError=e.what();return 0;}
  toggleAction.accel.cmd=WORD(host.Register("command_id",const_cast<char*>("MWT_TOGGLE")));
  toggleAction.desc="Media Explorer: Toggle waveform thumbnails";
  refreshAction.accel.cmd=WORD(host.Register("command_id",const_cast<char*>("MWT_REFRESH")));
  refreshAction.desc="Media Explorer: Refresh waveform thumbnail cache";
  host.Register("gaccel",&toggleAction);host.Register("gaccel",&refreshAction);
  host.Register("hookcommand",reinterpret_cast<void*>(hookCommand));
  host.Register("toggleaction",reinterpret_cast<void*>(toggleState));
  host.Register("timer",reinterpret_cast<void*>(timer));
  publishStatus();return 1;
}

#include "../third_party/reaper/reaper_plugin.h"
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
constexpr char version[] = "0.2.2";
constexpr int bins = 192;
constexpr size_t bitmapLimit = 256, bitmapBudget = 16 * 1024 * 1024;
constexpr UINT_PTR subclassId = 0x4D575431;
reaper_plugin_info_t host{};
HWND explorer{}, list{};
bool enabled = true, busy{}, rowsDirty = true;
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
struct Stamp {
  uint64_t size{}, time{};
  bool operator==(const Stamp& b) const {return size==b.size&&time==b.time;}
};
bool stamp(const std::wstring& path,Stamp& out) {
  WIN32_FILE_ATTRIBUTE_DATA a{};
  if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&a)||(a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
  out={uint64_t(a.nFileSizeHigh)<<32|a.nFileSizeLow,uint64_t(a.ftLastWriteTime.dwHighDateTime)<<32|a.ftLastWriteTime.dwLowDateTime};
  return true;
}
struct Wave {
  Stamp stamp;
  std::array<float,bins> high{},low{};
  int state{}; // 0 queued, 1 ready, -1 unsupported/error
  int attempts{};
  uint64_t retryAfter{};
  uint64_t revision{};
  float peak{};
  bool sourceChanged{};
};
// Peak arrays live for the whole session. Only disposable render surfaces use
// LRU eviction; losing a surface must never cause another source/peak read.
std::map<std::wstring,Wave> cache;
std::map<int,std::wstring> rows;
using BitmapKey = std::tuple<std::wstring,uint64_t,int,int,COLORREF,COLORREF>;
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
  for(auto it=bitmaps.begin();it!=bitmaps.end();) {
    if(std::get<0>(it->first)==path){bitmapBytes-=it->second.bytes;it=bitmaps.erase(it);}
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
  rows.clear();rowsDirty=true;
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
  std::array<wchar_t,32768> buffer{};
  NMLVDISPINFOW n{};
  n.hdr={list,UINT_PTR(GetDlgCtrlID(list)),LVN_GETDISPINFOW};
  n.item.mask=LVIF_TEXT;n.item.iItem=row;n.item.iSubItem=subitem;
  n.item.pszText=buffer.data();n.item.cchTextMax=int(buffer.size());
  SendMessageW(explorer,WM_NOTIFY,n.hdr.idFrom,LPARAM(&n));
  if(n.item.pszText && n.item.pszText!=LPSTR_TEXTCALLBACKW)return n.item.pszText;
  return {};
}
bool isFile(const std::wstring& p) {Stamp s;return stamp(p,s);}
std::wstring resolveFile(int row) {
  std::wstring name=textAt(row,0),ext=textAt(row,3);
  if(name.empty()||name==L"..")return {};
  if(!ext.empty() && ext.front()==L'.')ext.erase(0,1);
  std::vector<std::wstring> names{name};
  if(!ext.empty()&&ext.find_first_of(L"\\/: *?<>|")==std::wstring::npos)names.push_back(name+L"."+ext);
  for(const auto& n:names) {
    auto suffix=std::filesystem::path(n).extension().wstring();
    if(suffix.empty()||!IsMediaExtension(utf8(suffix.substr(1)).c_str(),false))continue;
    if(isFile(n))return std::filesystem::path(n).lexically_normal().wstring();
    auto full=(std::filesystem::path(currentDirectory)/n).wstring();
    if(isFile(full))return std::filesystem::path(full).lexically_normal().wstring();
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
void invalidate() {if(IsWindow(list))InvalidateRect(list,nullptr,FALSE);}
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
    wave.state=1;++builds;
  }else {
    if(!retryOnEmpty)return false;
    // A simultaneous Media Explorer preview may still be publishing its peak
    // cache. Retry transient empty reads before treating the source as missing.
    wave.state=++wave.attempts>=5?-1:0;
    wave.retryAfter=GetTickCount64()+500;
  }
  stopBuild();invalidate();return true;
}
bool processNext() {
  if(activeSource) {
    if(GetTickCount64()-buildStarted>30000) {
      cache[activePath].state=-1;lastError="Peak build exceeded 30 seconds";stopBuild();return true;
    }
    if(building && BuildPeaks(activeSource,1)!=0)return false;
    if(building){BuildPeaks(activeSource,2);building=false;}
    readWave(true);return true;
  }
  for(auto& [row,path]:rows) {
    auto it=cache.find(path);
    if(it==cache.end()||it->second.state!=0||it->second.retryAfter>GetTickCount64())continue;
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
    if(building)++peakBuildStarts;
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
bool blitWave(HDC target,const RECT& r,const std::wstring& path,const Wave& wave,COLORREF fg,COLORREF bg) {
  int width=r.right-r.left,height=r.bottom-r.top;
  size_t bytes=size_t(width)*height*4;
  if(bytes>bitmapBudget)return false;
  if(!bitmapDC)bitmapDC=CreateCompatibleDC(target);
  if(!bitmapDC)return false;
  BitmapKey key{path,wave.revision,width,height,fg,bg};
  auto it=bitmaps.find(key);
  bool hit=it!=bitmaps.end();
  if(!hit) {
    Raster raster(CreateCompatibleBitmap(target,width,height),bytes,++bitmapTick);
    if(!raster.handle)return false;
    HGDIOBJ old=SelectObject(bitmapDC,raster.handle);
    RECT bounds{0,0,width,height};
    HBRUSH brush=CreateSolidBrush(bg);
    if(!brush){SelectObject(bitmapDC,old);return false;}
    FillRect(bitmapDC,&bounds,brush);DeleteObject(brush);
    drawEnvelope(bitmapDC,bounds,wave,fg);
    SelectObject(bitmapDC,old);
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
  int row=int(n->nmcd.dwItemSpec);RECT r{};
  if(!ListView_GetSubItemRect(list,row,column,LVIR_BOUNDS,&r))return;
  COLORREF bg=ListView_GetTextBkColor(list);
  if(bg==CLR_NONE)bg=ListView_GetBkColor(list);
  bool selected=ListView_GetItemState(list,row,LVIS_SELECTED)!=0;
  if(selected)bg=GetSysColor(COLOR_HIGHLIGHT);
  HBRUSH brush=CreateSolidBrush(bg);FillRect(n->nmcd.hdc,&r,brush);DeleteObject(brush);
  auto rowIt=rows.find(row);
  if(rowIt==rows.end())return;
  auto it=cache.find(rowIt->second);if(it==cache.end())return;
  auto& wave=it->second;
  r.left+=6;r.right-=6;r.top+=3;r.bottom-=3;
  if(r.right<=r.left||r.bottom<=r.top)return;
  COLORREF color=selected?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(55,166,152);
  if(wave.state==1) {
    if(!blitWave(n->nmcd.hdc,r,rowIt->second,wave,color,bg))drawEnvelope(n->nmcd.hdc,r,wave,color);
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
        layoutPending=true;layoutChangedAt=GetTickCount64();
      }
    }
  }
  if(layoutPending&&(force||GetTickCount64()-layoutChangedAt>=250)) {
    auto order=std::to_string(waveOrder),width=std::to_string(waveWidth);
    SetExtState(section,"wave_order",order.c_str(),true);
    SetExtState(section,"wave_width",width.c_str(),true);
    layoutPending=false;
  }
}
void removeWaveColumn() {
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
      if(n->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM) && n->iSubItem==column) {
        try {paintWave(n);}catch(const std::exception& e){lastError=e.what();}
        return CDRF_SKIPDEFAULT;
      }
      LRESULT result=DefSubclassProc(w,m,wp,lp);
      if(n->nmcd.dwDrawStage==CDDS_PREPAINT)result|=CDRF_NOTIFYITEMDRAW;
      if(n->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)result|=CDRF_NOTIFYSUBITEMDRAW;
      return result;
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
  rowsDirty=false;
  auto directory=windowText(GetDlgItem(explorer,1002));
  if(directory!=currentDirectory)rows.clear();
  currentDirectory=std::move(directory);
  std::map<int,std::wstring> next;
  int first=std::max(0,ListView_GetTopIndex(list));
  int last=std::min(ListView_GetItemCount(list),first+ListView_GetCountPerPage(list)+2);
  std::vector<int> unresolved;
  for(int row=first;row<last;++row) {
    auto path=resolveFile(row);
    if(path.empty()) {
      auto ext=textAt(row,3);
      if(!ext.empty()&&IsMediaExtension(utf8(ext).c_str(),false)) {
        auto previous=rows.find(row);
        if(previous!=rows.end()) {
          auto name=textAt(row,0),base=std::filesystem::path(previous->second).filename().wstring();
          if(name==base||name+L"."+ext==base)path=previous->second;
        }
        if(path.empty())unresolved.push_back(row);
      }
    }
    if(!path.empty())next.emplace(row,path);
  }
  if(!unresolved.empty()&&GetToggleState(32063,42026)==0) {
    // Query the native database/search rows synchronously, then restore both
    // display preferences before returning to the UI message loop.
    bool leading=GetToggleState(32063,42134)==1;
    SendMessageW(explorer,WM_COMMAND,42026,0);
    ++pathLookups;
    try {
      for(int row:unresolved) {
        auto path=resolveFile(row);
        if(!path.empty())next[row]=path;
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
    Stamp fileStamp;
    if(!stamp(path,fileStamp)){it=next.erase(it);continue;}
    auto [entry,inserted]=cache.try_emplace(path);
    if(inserted||!(entry->second.stamp==fileStamp)) {
      if(path==activePath)stopBuild();
      discardBitmaps(path);
      entry->second=Wave{};entry->second.stamp=fileStamp;entry->second.revision=++waveRevision;
      entry->second.sourceChanged=!inserted;
    }
    ++it;
  }
  if(rows!=next){rows=std::move(next);invalidate();}
  if(activeSource && std::none_of(rows.begin(),rows.end(),[](const auto& row){return row.second==activePath;}))stopBuild();
  observedTop=first;observedCount=ListView_GetItemCount(list);
  if(dirtySince){restoreLatency=GetTickCount64()-dirtySince;dirtySince=0;}
}
void publishStatus() {
  std::ostringstream s;
  int ready=0;for(const auto& [r,p]:rows){auto it=cache.find(p);if(it!=cache.end()&&it->second.state==1)++ready;}
  s<<"{\"version\":"<<json(version)<<",\"enabled\":"<<(enabled?"true":"false")
   <<",\"attached\":"<<(ownsColumn()?"true":"false")<<",\"column\":"<<column
   <<",\"visible_files\":"<<rows.size()<<",\"ready\":"<<ready<<",\"cache\":"<<cache.size()
   <<",\"builds\":"<<builds<<",\"draws\":"<<draws<<",\"path_lookups\":"<<pathLookups
   <<",\"source_opens\":"<<sourceOpens<<",\"peak_reads\":"<<peakReads
   <<",\"fast_peak_hits\":"<<fastPeakHits<<",\"peak_build_probes\":"<<peakBuildProbes<<",\"peak_build_starts\":"<<peakBuildStarts
   <<",\"peak_cache_invalidations\":"<<peakCacheInvalidations
   <<",\"wave_order\":"<<waveOrder<<",\"wave_width\":"<<waveWidth<<",\"layout_pending\":"<<(layoutPending?"true":"false")
   <<",\"bitmap_builds\":"<<bitmapBuilds<<",\"bitmap_hits\":"<<bitmapHits
   <<",\"bitmap_cache\":"<<bitmaps.size()<<",\"bitmap_bytes\":"<<bitmapBytes<<",\"bitmap_evictions\":"<<bitmapEvictions
   <<",\"peak_array_bytes\":"<<cache.size()*bins*2*sizeof(float)<<",\"restore_latency_ms\":"<<restoreLatency
   <<",\"directory\":"<<json(utf8(currentDirectory))<<",\"error\":"<<json(lastError)<<",\"files\":[";
  bool first=true;
  for(const auto& [r,p]:rows) {
    if(!first)s<<',';
    first=false;const auto& wave=cache.at(p);
    s<<"{\"row\":"<<r<<",\"path\":"<<json(utf8(p))<<",\"state\":"<<wave.state<<",\"peak\":"<<wave.peak<<'}';
  }
  s<<"]}";SetExtState(section,"status",s.str().c_str(),false);
}
void timer() {
  if(busy)return;
  busy=true;
  try {
    auto now=GetTickCount64();
    if(enabled) {
      if(list&&!IsWindow(list))detach();
      if(!list && now-lastScan>=500){lastScan=now;attach();}
      if(list) {
        bool haveColumn=ownsColumn();
        if(!haveColumn&&!columnMenuDepth&&now>=columnsReadyAfter)haveColumn=addWaveColumn();
        if(haveColumn)saveWaveLayout();
        if(haveColumn&&IsWindowVisible(list)) {
          if(rowsDirty||ListView_GetTopIndex(list)!=observedTop||ListView_GetItemCount(list)!=observedCount||now-lastScan>=1000) {
            lastScan=now;scanRows();
          }
          processQueue();
        }
      }
    }
    if(now-lastStatus>=300){lastStatus=now;publishStatus();}
  }catch(const std::exception& e){lastError=e.what();stopBuild();}
  busy=false;
}
bool hookCommand(int cmd,int) {
  if(cmd==toggleAction.accel.cmd) {
    enabled=!enabled;if(!enabled)detach();
    SetExtState(section,"enabled",enabled?"1":"0",true);publishStatus();return true;
  }
  if(cmd==refreshAction.accel.cmd) {
    stopBuild();clearBitmaps();cache.clear();dirtyRows();lastScan=0;lastError.clear();invalidate();publishStatus();return true;
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
      detach();saveWaveLayout(true);clearBitmaps();
      if(bitmapDC){DeleteDC(bitmapDC);bitmapDC=nullptr;}
      host.Register("-timer",reinterpret_cast<void*>(timer));
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

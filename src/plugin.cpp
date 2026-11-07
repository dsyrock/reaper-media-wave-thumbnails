#include "../third_party/reaper/reaper_plugin.h"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// PCM_source remains opaque: call only the SDK's C API (MinGW compatible).
namespace {
constexpr char section[] = "MediaWaveThumbnails";
constexpr wchar_t columnTitle[] = L"Waveform";
constexpr int bins = 192, cacheLimit = 512;
constexpr UINT_PTR subclassId = 0x4D575431;
reaper_plugin_info_t host{};
HWND explorer{}, list{};
bool enabled = true, busy{};
int column = -1;
uint64_t clockTick{}, draws{}, builds{}, pathLookups{}, lastScan{}, lastStatus{};
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

std::string utf8(const std::wstring& s) {
  if (s.empty()) return {};
  int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
  std::string out(n,'\0');
  WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);
  return out;
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
  uint64_t used{};
};
std::map<std::wstring,Wave> cache;
std::map<int,std::wstring> rows;
PCM_source* activeSource{};
std::wstring activePath;
bool building{};
uint64_t buildStarted{};

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
void finishWave() {
  auto it=cache.find(activePath);
  if(it==cache.end()){stopBuild();return;}
  auto& wave=it->second;
  bool qn=false;double length=SourceLength(activeSource,&qn);
  int channels=SourceChannels(activeSource);
  if(qn||!std::isfinite(length)||length<=0||channels<=0||channels>128) {wave.state=-1;stopBuild();return;}
  std::vector<double> buffer(size_t(bins)*channels*2,0.0);
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
    }
    wave.state=1;++builds;
  }else {
    // A simultaneous Media Explorer preview may still be publishing its peak
    // cache. Retry transient empty reads before treating the source as missing.
    wave.state=++wave.attempts>=5?-1:0;
    wave.retryAfter=GetTickCount64()+500;
  }
  stopBuild();invalidate();
}
void processQueue() {
  if(activeSource) {
    if(GetTickCount64()-buildStarted>30000) {
      cache[activePath].state=-1;lastError="Peak build exceeded 30 seconds";stopBuild();return;
    }
    if(building && BuildPeaks(activeSource,1)!=0)return;
    if(building){BuildPeaks(activeSource,2);building=false;}
    finishWave();return;
  }
  for(auto& [row,path]:rows) {
    auto it=cache.find(path);
    if(it==cache.end()||it->second.state!=0||it->second.retryAfter>GetTickCount64())continue;
    activePath=path;activeSource=CreateSource(utf8(path).c_str(),true);
    if(!activeSource){it->second.state=-1;activePath.clear();return;}
    bool qn=false;double length=SourceLength(activeSource,&qn);
    if(qn||length<=0||SourceChannels(activeSource)<=0){it->second.state=-1;stopBuild();return;}
    buildStarted=GetTickCount64();building=BuildPeaks(activeSource,0)!=0;
    if(!building)finishWave();
    return;
  }
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
  HPEN pen=CreatePen(PS_SOLID,1,color);HGDIOBJ old=SelectObject(n->nmcd.hdc,pen);
  int mid=(r.top+r.bottom)/2;
  float peak=0;
  for(int x=0;x<bins;++x)peak=std::max({peak,std::abs(wave.high[x]),std::abs(wave.low[x])});
  double scale=std::max<int>(1,(r.bottom-r.top)/2)/std::max(0.01f,peak);
  if(wave.state==1) {
    for(int x=r.left;x<r.right;++x) {
      int bin=std::clamp<int>((x-r.left)*bins/std::max<int>(1,r.right-r.left),0,bins-1);
      int top=mid-int(std::round(wave.high[bin]*scale));
      int bottom=mid-int(std::round(wave.low[bin]*scale));
      MoveToEx(n->nmcd.hdc,x,std::clamp<int>(top,r.top,r.bottom-1),nullptr);
      LineTo(n->nmcd.hdc,x,std::clamp<int>(std::max(top+1,bottom),r.top+1,r.bottom));
    }
    ++draws;
  }else if(wave.state==0) {
    MoveToEx(n->nmcd.hdc,r.left,mid,nullptr);LineTo(n->nmcd.hdc,r.right,mid);
  }
  SelectObject(n->nmcd.hdc,old);DeleteObject(pen);
}
bool ownsColumn() {
  if(!IsWindow(list)||column<0)return false;
  wchar_t title[64]{};LVCOLUMNW c{};c.mask=LVCF_TEXT;c.pszText=title;c.cchTextMax=64;
  return ListView_GetColumn(list,column,&c)&&std::wstring(title)==columnTitle;
}
LRESULT CALLBACK parentProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
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
      rows.clear();lastScan=0;
    }
    if(hdr->code==NM_CUSTOMDRAW && column>=0) {
      auto n=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
      if(n->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM) && n->iSubItem==column) {
        paintWave(n);return CDRF_SKIPDEFAULT;
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
  if(m==LVM_SETITEMCOUNT||m==LVM_DELETEALLITEMS||m==LVM_SORTITEMS||m==LVM_SORTITEMSEX) {
    rows.clear();lastScan=0;
  }
  if(m==WM_NCDESTROY)RemoveWindowSubclass(w,listProc,subclassId);
  return DefSubclassProc(w,m,wp,lp);
}
void detach() {
  stopBuild();rows.clear();
  if(ownsColumn())ListView_DeleteColumn(list,column);
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
  column=Header_GetItemCount(ListView_GetHeader(list));
  LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;c.pszText=const_cast<wchar_t*>(columnTitle);
  c.cx=196;c.iSubItem=column;
  if(ListView_InsertColumn(list,column,&c)<0){detach();return false;}
  // Display second, retain every existing logical subitem index.
  std::vector<int> order(column+1);
  if(ListView_GetColumnOrderArray(list,column+1,order.data())) {
    order.erase(std::remove(order.begin(),order.end(),column),order.end());
    order.insert(order.begin()+std::min(size_t(1),order.size()),column);
    ListView_SetColumnOrderArray(list,column+1,order.data());
  }
  lastScan=0;return true;
}
void scanRows() {
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
      entry->second=Wave{};entry->second.stamp=fileStamp;
    }
    entry->second.used=++clockTick;
    ++it;
  }
  if(rows!=next){rows=std::move(next);invalidate();}
  if(activeSource && std::none_of(rows.begin(),rows.end(),[](const auto& row){return row.second==activePath;}))stopBuild();
  while(cache.size()>cacheLimit) {
    auto victim=cache.end();
    for(auto it=cache.begin();it!=cache.end();++it) {
      if(it->first==activePath)continue;
      if(victim==cache.end()||it->second.used<victim->second.used)victim=it;
    }
    if(victim==cache.end())break;
    cache.erase(victim);
  }
}
void publishStatus() {
  std::ostringstream s;
  int ready=0;for(const auto& [r,p]:rows){auto it=cache.find(p);if(it!=cache.end()&&it->second.state==1)++ready;}
  s<<"{\"version\":\"0.1.0\",\"enabled\":"<<(enabled?"true":"false")
   <<",\"attached\":"<<(ownsColumn()?"true":"false")<<",\"column\":"<<column
   <<",\"visible_files\":"<<rows.size()<<",\"ready\":"<<ready<<",\"cache\":"<<cache.size()
   <<",\"builds\":"<<builds<<",\"draws\":"<<draws<<",\"path_lookups\":"<<pathLookups
   <<",\"directory\":"<<json(utf8(currentDirectory))<<",\"error\":"<<json(lastError)<<",\"files\":[";
  bool first=true;
  for(const auto& [r,p]:rows) {
    if(!first)s<<',';
    first=false;const auto& wave=cache.at(p);
    float peak=0;for(int x=0;x<bins;++x)peak=std::max({peak,std::abs(wave.high[x]),std::abs(wave.low[x])});
    s<<"{\"row\":"<<r<<",\"path\":"<<json(utf8(p))<<",\"state\":"<<wave.state<<",\"peak\":"<<peak<<'}';
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
        if(!ownsColumn()){detach();attach();}
        if(IsWindowVisible(list)) {
          if(now-lastScan>=200){lastScan=now;scanRows();}
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
    stopBuild();cache.clear();rows.clear();lastScan=0;lastError.clear();invalidate();publishStatus();return true;
  }
  return false;
}
int toggleState(int cmd){return cmd==toggleAction.accel.cmd?int(enabled):-1;}
template<typename T> bool load(T& fn,const char* name){fn=reinterpret_cast<T>(host.GetFunc(name));return fn!=nullptr;}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(HINSTANCE,reaper_plugin_info_t* rec) {
  if(!rec) {
    if(host.Register) {
      detach();host.Register("-timer",reinterpret_cast<void*>(timer));
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
     !load(IsMediaExtension,"IsMediaExtension"))return 0;
  enabled=std::string(GetExtState(section,"enabled"))!="0";
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

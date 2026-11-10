#include "reaper/reaper_plugin.h"
#include "peak_reader_pool.hpp"
#include "wave_geometry.hpp"
#include "waveform_file.hpp"
#include "preview_seek.hpp"
#include "WDL/lice/lice.h" // pixel layout only; graphics functions come from host SWELL
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>
#include <tuple>

// Generic (Linux) SWELL sends ITEMPREPAINT per cell, after the native row
// background. Its return value cannot skip default drawing. Draw only our
// empty cell, with alpha, and leave every native notification unchanged.
namespace {
constexpr char section[]="MediaWaveThumbnails";
constexpr char version[]="0.4.2-linux-preview";
constexpr char columnTitle[]="Waveform";
reaper_plugin_info_t host{};
HWND explorer{},list{};
WNDPROC original{};
bool enabled=true,busy=false,editing=false,statusDirty=true;
bool columnMismatch=false;
int column=-1,savedOrder=-1,savedWidth=196,nativeCount=0;
char columnCookie;
constexpr char columnProperty[]="MediaWaveThumbnails.linux.column";
bool layoutPending=false;
uint64_t layoutAt{},columnsAfter{},nextScan{},pageAt{},revision{},ticket{};
uint64_t directReads{},sourceOpens{},draws{},rasterTick{},bitmapBuilds{};
int observedTop=-1,observedCount=-1;
std::string directory,error;
gaccel_register_t toggle{},refresh{};
mwt::PeakReaderPool readers;
mwt::PreviewSeek previewSeek;
int seekRow=-1;
struct Wave {
  mwt::FileStamp stamp;
  mwt::PeakData data;
  int state=0,attempts=0;
  bool loading=false,tried=false,published=false,changed=false;
  uint64_t revision=0,ticket=0,retryAt=0;
};
std::map<std::string,Wave> cache;
std::map<int,std::string> rows;
using Key=std::tuple<uint64_t,int,int,COLORREF>;
struct Raster {
  HDC dc;size_t bytes;uint64_t used;
  Raster(HDC h,size_t n):dc(h),bytes(n),used(++rasterTick){}
  Raster(const Raster&)=delete;
  ~Raster(){SWELL_DeleteGfxContext(dc);}
};
std::map<Key,Raster> images;
size_t imageBytes=0;
const char* (*GetExtState)(const char*,const char*);
void (*SetExtState)(const char*,const char*,const char*,bool);
int (*GetToggleState)(int,int);
bool (*IsMediaExtension)(const char*,bool);
void (*PeakName)(const char*,char*,int,bool);
void (*ClearPeakCache)();
PCM_source* (*CreateSource)(const char*,bool);
void (*DestroySource)(PCM_source*);
double (*SourceLength)(PCM_source*,bool*);
int (*Channels)(PCM_source*);
int (*BuildPeaks)(PCM_source*,int);
int (*GetPeaks)(PCM_source*,double,double,int,int,int,double*);
PCM_source* source=nullptr;
std::string sourcePath;
bool building=false;
uint64_t buildStart=0;

uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::string text(HWND w){std::array<char,32768> b{};GetWindowText(w,b.data(),int(b.size()));return b.data();}
std::string item(int row,int sub){std::array<char,32768> b{};ListView_GetItemText(list,row,sub,b.data(),int(b.size()));return b.data();}
std::string quote(const std::string& value){std::string out="\"";for(unsigned char c:value){if(c=='"'||c=='\\')out+='\\';if(c>=32)out+=char(c);}return out+'"';}
void report(){
  if(!statusDirty)return;
  int ready=0;for(const auto& row:rows)if(cache.at(row.second).state==1)++ready;
  std::ostringstream s;s<<"{\"platform\":\"linux\",\"version\":"<<quote(version)<<",\"experimental\":true,\"enabled\":"<<(enabled?"true":"false")
    <<",\"attached\":"<<(column>=0&&list?"true":"false")<<",\"visible_files\":"<<rows.size()<<",\"ready\":"<<ready<<",\"cache\":"<<cache.size()
    <<",\"direct_peak_reads\":"<<directReads<<",\"source_opens\":"<<sourceOpens<<",\"draws\":"<<draws<<",\"bitmap_builds\":"<<bitmapBuilds
    <<",\"seek_requests\":"<<previewSeek.requests<<",\"seek_completed\":"<<previewSeek.completed<<",\"seek_cancelled\":"<<previewSeek.cancelled
    <<",\"seek_pending\":"<<(previewSeek.pending()?"true":"false")<<",\"seek_ratio\":"<<previewSeek.lastRatio<<",\"seek_path\":"<<quote(previewSeek.lastPath)<<",\"seek_error\":"<<quote(previewSeek.error)<<",\"error\":"<<quote(error)<<'}';
  SetExtState(section,"status",s.str().c_str(),false);statusDirty=false;
}
bool ownsColumn(){
  // ListView_GetColumn was only added to SWELL in Dec 2023, after REAPER
  // 6.83. Its missing-function stub returns no title and caused endless insert.
  return column>=0&&column==nativeCount&&IsWindow(list)&&
    GetProp(list,columnProperty)==(HANDLE)&columnCookie&&
    Header_GetItemCount(ListView_GetHeader(list))==nativeCount+1;
}
void saveLayout(bool force=false){
  if(editing||!ownsColumn())return;
  int count=Header_GetItemCount(ListView_GetHeader(list));std::vector<int> order(count);
  // Generic SWELL follows Windows: visual position -> logical column index.
  if(!ListView_GetColumnOrderArray(list,count,order.data()))return;
  auto pos=std::find(order.begin(),order.end(),column);if(pos==order.end())return;
  int slot=int(pos-order.begin()),width=ListView_GetColumnWidth(list,column);
  if(width!=savedWidth||slot!=savedOrder){savedOrder=slot;savedWidth=width;layoutAt=now();layoutPending=true;}
  if(layoutPending&&(force||now()-layoutAt>=250)){
    auto a=std::to_string(savedOrder),b=std::to_string(savedWidth);
    SetExtState(section,"linux_wave_order",a.c_str(),true);SetExtState(section,"linux_wave_width",b.c_str(),true);layoutPending=false;
  }
}
void removeColumn(){
  previewSeek.cancel();
  saveLayout(true);bool prior=editing;editing=true;
  if(ownsColumn()){ListView_DeleteColumn(list,column);RemoveProp(list,columnProperty);}
  // Preserve the marker on an ambiguous live list, so toggling cannot append
  // a second column. Destroying/reopening that list clears the marker safely.
  column=-1;editing=prior;statusDirty=true;
}
bool addColumn(){
  if(editing||!IsWindow(list))return false;
  if(ownsColumn())return true;
  // If native columns changed unexpectedly, retain our insertion record and
  // stop. Never append again merely because ownership cannot be confirmed.
  if(column>=0)return false;
  if(GetProp(list,columnProperty)==(HANDLE)&columnCookie){
    error="Waveform column ownership is ambiguous; close and reopen Media Explorer";
    statusDirty=true;return false;
  }
  column=-1;int count=Header_GetItemCount(ListView_GetHeader(list));if(count<4)return false;
  std::vector<int> order(count);if(!ListView_GetColumnOrderArray(list,count,order.data()))return false;
  auto file=std::find(order.begin(),order.end(),0);
  int slot=savedOrder<0?std::min(count,int(file-order.begin())+1):std::clamp(savedOrder,0,count);
  LVCOLUMN c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<char*>(columnTitle);c.cx=savedWidth;
  editing=true;ListView_InsertColumn(list,count,&c);
  if(Header_GetItemCount(ListView_GetHeader(list))!=count+1){editing=false;error="SWELL could not insert Waveform column";statusDirty=true;return false;}
  nativeCount=count;column=count;
  if(!SetProp(list,columnProperty,(HANDLE)&columnCookie)){
    ListView_DeleteColumn(list,column);column=-1;editing=false;
    error="Cannot track Waveform column ownership";columnsAfter=UINT64_MAX;statusDirty=true;return false;
  }
  order.insert(order.begin()+slot,column);
  ListView_SetColumnOrderArray(list,int(order.size()),order.data());editing=false;
  saveLayout();InvalidateRect(list,nullptr,FALSE);statusDirty=true;return true;
}
void stopSource(){if(source){if(building)BuildPeaks(source,2);DestroySource(source);}source=nullptr;building=false;sourcePath.clear();}
void cancelHiddenReads(const std::set<std::wstring>& paths){
  for(const auto& job:readers.retain(paths)){auto i=cache.find(mwt::pathUtf8(job.path));if(i!=cache.end()&&i->second.ticket==job.ticket){i->second.loading=false;i->second.tried=false;}}
}
LRESULT parentProc(HWND,UINT,WPARAM,LPARAM);
std::string resolve(int,const std::string&);
void scan();
void detach(){
  previewSeek.cancel();seekRow=-1;
  stopSource();cancelHiddenReads({});rows.clear();removeColumn();
  if(IsWindow(explorer)&&original&&(WNDPROC)GetWindowLong(explorer,GWL_WNDPROC)==parentProc)SetWindowLong(explorer,GWL_WNDPROC,(LONG_PTR)original);
  explorer=list=nullptr;original=nullptr;nextScan=0;observedTop=observedCount=-1;statusDirty=true;
}
void draw(NMLVCUSTOMDRAW* n){
  if(!n->nmcd.hdc||!ownsColumn())return;
  auto r=rows.find(int(n->nmcd.dwItemSpec));if(r==rows.end())return;
  const auto& wave=cache.at(r->second);if(wave.state!=1||!wave.published)return;
  RECT cell{},client{};if(!ListView_GetSubItemRect(list,r->first,column,LVIR_BOUNDS,&cell))return;
  GetClientRect(list,&client);
  client.top=std::max(int(client.top),SWELL_GetListViewHeaderHeight(list));
  int left=cell.left+6,top=cell.top+3,width=cell.right-cell.left-12,height=cell.bottom-cell.top-6;
  if(width<=0||height<=0||uint64_t(width)*height>4ULL*1024*1024)return;
  COLORREF color=ListView_GetItemState(list,r->first,LVIS_SELECTED)?n->clrText:RGB(48,174,170);
  Key key{wave.revision,width,height,color};auto image=images.find(key);
  if(image==images.end()){
    // SWELL uses LICE_MemBitmap; align the context width so its pixel row span
    // equals this width. Never assume an arbitrary logical width is the span.
    int stride=(width+7)&~7;size_t bytes=size_t(stride)*height*4;
    while(!images.empty()&&(images.size()>=512||imageBytes+bytes>16*1024*1024)){
      auto old=std::min_element(images.begin(),images.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});imageBytes-=old->second.bytes;images.erase(old);
    }
    HDC dc=SWELL_CreateMemContext(n->nmcd.hdc,stride,height);if(!dc)return;
    auto* pixels=static_cast<LICE_pixel*>(SWELL_GetCtxFrameBuffer(dc));
    if(!pixels){SWELL_DeleteGfxContext(dc);error="SWELL memory framebuffer unavailable";statusDirty=true;return;}
    std::fill(pixels,pixels+size_t(stride)*height,0);
    auto ink=LICE_RGBA(GetRValue(color),GetGValue(color),GetBValue(color),255);
    mwt::waveColumns(width,height,wave.data.high,wave.data.low,wave.data.peak,[&](int x,int y){pixels[size_t(y)*stride+x]=ink;});
    image=images.try_emplace(key,dc,bytes).first;imageBytes+=bytes;++bitmapBuilds;
  }
  // Crop coordinates instead of changing the shared native DC's pen, colors,
  // clip stack or background. Native header/scrollbars are painted afterwards.
  int x=std::max(left,int(client.left)),y=std::max(top,int(client.top));
  int right=std::min(left+width,int(client.right)),bottom=std::min(top+height,int(client.bottom));
  if(right>x&&bottom>y){image->second.used=++rasterTick;BitBlt(n->nmcd.hdc,x,y,right-x,bottom-y,image->second.dc,x-left,y-top,SRCCOPY_USEALPHACHAN);++draws;statusDirty=true;}
}
LRESULT parentProc(HWND w,UINT message,WPARAM wp,LPARAM lp){
  WNDPROC next=original;
  if(message==WM_CLOSE||message==WM_DESTROY){detach();return next?next(w,message,wp,lp):0;}
  if(message==WM_NOTIFY&&lp){auto* h=(NMHDR*)lp;if(h->hwndFrom==list&&column>=0){
    if(h->code==LVN_BEGINDRAG)previewSeek.cancel();
    if(h->code==NM_CLICK||h->code==NM_DBLCLK){
      auto* click=(NMLISTVIEW*)lp;previewSeek.cancel();
      if(click->iSubItem==column){
        scan();
        auto row=rows.find(click->iItem);RECT cell{};
        if(!mwt::previewModifiers()&&ownsColumn()&&row!=rows.end()&&cache.at(row->second).state==1&&cache.at(row->second).published&&ListView_GetSubItemRect(list,click->iItem,column,LVIR_BOUNDS,&cell)){
          auto path=row->second;
          if(!path.empty()){
            double fraction=mwt::thumbnailRatio(click->ptAction.x,cell.left,cell.right);
            NMLISTVIEW native=*click;native.hdr.code=NM_CLICK;native.iSubItem=0;
            if(next)next(w,message,wp,(LPARAM)&native);
            seekRow=click->iItem;previewSeek.request(path,fraction,now());statusDirty=true;
          }
        }
        return 0;
      }
    }
    if(h->code==LVN_GETDISPINFO){auto* n=(NMLVDISPINFO*)lp;if(n->item.iSubItem==column){if(n->item.pszText&&n->item.cchTextMax>0)n->item.pszText[0]=0;n->item.iImage=-1;n->item.state=0;return 0;}}
    if(h->code==NM_CUSTOMDRAW){auto* n=(NMLVCUSTOMDRAW*)lp;if(n->iSubItem==column){try{draw(n);}catch(const std::exception& e){error=e.what();statusDirty=true;}return 0;}}
    if(h->code==LVN_COLUMNCLICK){if(((NMLISTVIEW*)lp)->iSubItem==column)return 0;nextScan=0;}
  }}
  bool nativeLayout=message==WM_COMMAND&&(LOWORD(wp)==42172||(LOWORD(wp)>=42338&&LOWORD(wp)<=42347)||LOWORD(wp)==42359);
  if(nativeLayout||(message==WM_CONTEXTMENU&&(HWND)wp==list)){
    removeColumn();bool prior=editing;editing=true;
    LRESULT result=next?next(w,message,wp,lp):0;editing=prior;columnsAfter=now()+150;nextScan=0;return result;
  }
  if(message==WM_COMMAND)nextScan=0;
  return next?next(w,message,wp,lp):0;
}
BOOL findExplorer(HWND w,LPARAM){
  HWND candidate=GetDlgItem(w,1001);if(!candidate||!GetDlgItem(w,1002))return TRUE;
  char name[64]{};GetClassName(candidate,name,sizeof(name));if(std::strcmp(name,"SysListView32")!=0)return TRUE;
  WNDPROC proc=(WNDPROC)GetWindowLong(w,GWL_WNDPROC);if(!proc)return TRUE;
  explorer=w;list=candidate;original=proc;SetWindowLong(w,GWL_WNDPROC,(LONG_PTR)parentProc);columnsAfter=now()+150;statusDirty=true;return FALSE;
}
BOOL findRoot(HWND w,LPARAM){if(!findExplorer(w,0))return FALSE;EnumChildWindows(w,findExplorer,0);return explorer?FALSE:TRUE;}
std::string resolve(int row,const std::string& dir){
  auto name=item(row,0),ext=item(row,3);if(name.empty()||name=="..")return {};
  if(!ext.empty()&&ext[0]=='.')ext.erase(0,1);
  std::vector<std::string> names{name};if(!ext.empty()&&ext.find_first_of("/\\ <>|")==std::string::npos)names.push_back(name+"."+ext);
  for(const auto& value:names){std::filesystem::path path(value);auto suffix=path.extension().string();
    if(suffix.empty()||!mwt::waveformExtension(suffix)||!IsMediaExtension(suffix.c_str()+1,false))continue;
    if(!path.is_absolute()){std::filesystem::path base(dir);if(!base.is_absolute())continue;path=base/path;}
    mwt::FileStamp stamp;if(mwt::fileStamp(mwt::pathWide(path.string()),stamp))return path.lexically_normal().string();
  }return {};
}
void scan(){
  int first=std::max(0,ListView_GetTopIndex(list)),count=ListView_GetItemCount(list);
  int last=std::min(count,first+ListView_GetCountPerPage(list)+2);
  std::map<int,std::string> next;std::vector<int> missing;
  for(int row=first;row<last;++row){auto path=resolve(row,directory);if(path.empty()){auto ext=item(row,3);if(mwt::waveformExtension(ext)&&IsMediaExtension(ext.c_str(),false))missing.push_back(row);}else next[row]=path;}
  if(!missing.empty()&&GetToggleState(32063,42026)==0){bool leading=GetToggleState(32063,42134)==1;
    SendMessage(explorer,WM_COMMAND,42026,0);
    try{for(int row:missing){auto path=resolve(row,directory);if(!path.empty())next[row]=path;}}
    catch(...){SendMessage(explorer,WM_COMMAND,42026,0);if(leading&&GetToggleState(32063,42134)!=1)SendMessage(explorer,WM_COMMAND,42134,0);throw;}
    SendMessage(explorer,WM_COMMAND,42026,0);if(leading&&GetToggleState(32063,42134)!=1)SendMessage(explorer,WM_COMMAND,42134,0);
  }
  for(auto i=next.begin();i!=next.end();){mwt::FileStamp stamp;if(!mwt::fileStamp(mwt::pathWide(i->second),stamp)){i=next.erase(i);continue;}
    auto [entry,inserted]=cache.try_emplace(i->second);auto& wave=entry->second;
    if(inserted||!(wave.stamp==stamp)){if(sourcePath==i->second)stopSource();wave=Wave{};wave.stamp=stamp;wave.revision=++revision;wave.changed=!inserted;}
    ++i;
  }
  if(rows!=next){rows=std::move(next);pageAt=now();statusDirty=true;InvalidateRect(list,nullptr,FALSE);}
  std::set<std::wstring> paths;for(const auto& row:rows)paths.insert(mwt::pathWide(row.second));
  if(source&&paths.find(mwt::pathWide(sourcePath))==paths.end())stopSource();
  cancelHiddenReads(paths);
  observedTop=first;observedCount=count;
}
std::string peakPath(const std::string& path){std::array<char,32768> b{};PeakName(path.c_str(),b.data(),int(b.size()),false);return b.data();}
void dataTick(){
  for(auto& result:readers.take()){
    auto i=cache.find(mwt::pathUtf8(result.job.path));if(i==cache.end()||i->second.ticket!=result.job.ticket||i->second.revision!=result.job.revision)continue;
    auto& wave=i->second;wave.loading=false;mwt::FileStamp current;
    if(!mwt::fileStamp(result.job.path,current)||!(current==wave.stamp)){wave.tried=false;nextScan=0;continue;}
    if(result.result==mwt::PeakResult::Ready){wave.data=result.data;wave.state=1;++directReads;statusDirty=true;}
    else if(result.result==mwt::PeakResult::Stale)wave.changed=true;
  }
  if(now()-pageAt<80)return;
  for(const auto& row:rows){auto& wave=cache.at(row.second);if(wave.state||wave.loading||wave.tried)continue;wave.tried=true;
    auto peak=peakPath(row.second);mwt::FileStamp stamp;
    if(!wave.changed&&!peak.empty()&&mwt::fileStamp(mwt::pathWide(peak),stamp)&&stamp.time>=wave.stamp.time){
      wave.ticket=++ticket;wave.loading=true;if(!readers.submit({mwt::pathWide(row.second),mwt::pathWide(peak),wave.stamp,wave.revision,wave.ticket}))wave.loading=false;
    }
  }
  if(source){
    auto& wave=cache.at(sourcePath);
    if(now()-buildStart>30000){wave.state=-1;stopSource();statusDirty=true;}
    else if(!building||BuildPeaks(source,1)==0){
      if(building){BuildPeaks(source,2);building=false;}
      mwt::FileStamp current;
      if(!mwt::fileStamp(mwt::pathWide(sourcePath),current)||!(current==wave.stamp)){stopSource();nextScan=0;return;}
      bool qn=false;double length=SourceLength(source,&qn);int channels=Channels(source);int count=0;
      if(!qn&&length>0&&channels>0&&channels<=128){
        std::vector<double> b(mwt::peakBins*channels*2);count=GetPeaks(source,mwt::peakBins/length,0,channels,mwt::peakBins,0,b.data())&0xfffff;
        if(count>0&&count<=mwt::peakBins){
          wave.data={};for(int x=0;x<mwt::peakBins;++x){int k=std::min(x,count-1);float hi=0,lo=0;
            for(int ch=0;ch<channels;++ch){hi=std::max(hi,float(b[k*channels+ch]));lo=std::min(lo,float(b[mwt::peakBins*channels+k*channels+ch]));}
            wave.data.high[x]=std::clamp(hi,-1.f,1.f);wave.data.low[x]=std::clamp(lo,-1.f,1.f);wave.data.peak=std::max({wave.data.peak,hi,-lo});
          }wave.state=1;
        }
      }
      if(!wave.state){if(!count&&wave.attempts<5)wave.retryAt=now()+500;else wave.state=-1;}
      stopSource();statusDirty=true;
    }
  }
  if(!source)for(const auto& row:rows){auto& wave=cache.at(row.second);if(wave.state||wave.loading||!wave.tried||now()<wave.retryAt)continue;
    if(!mwt::waveformPath(row.second)){wave.state=-1;statusDirty=true;continue;}
    auto peak=peakPath(row.second);mwt::FileStamp existing;bool exists=!peak.empty()&&mwt::fileStamp(mwt::pathWide(peak),existing);
    if(wave.changed||(exists&&existing.time<wave.stamp.time)){
      ClearPeakCache();std::filesystem::path p(peak);std::error_code ec;
      if(exists&&p.extension()==".reapeaks"&&p.string()!=row.second)std::filesystem::remove(p,ec);
      if(ec){wave.state=-1;error="Cannot invalidate stale peak cache: "+ec.message();statusDirty=true;continue;}
      wave.changed=false;
    }
    sourcePath=row.second;++sourceOpens;++wave.attempts;source=CreateSource(sourcePath.c_str(),true);
    if(!source){wave.state=-1;sourcePath.clear();statusDirty=true;continue;}
    buildStart=now();building=BuildPeaks(source,0)!=0;break;
  }
  for(const auto& row:rows)if(cache.at(row.second).loading)return;
  bool changed=false;for(const auto& row:rows){auto& wave=cache.at(row.second);if(wave.state==1&&!wave.published){wave.published=true;changed=true;}}
  if(changed){InvalidateRect(list,nullptr,FALSE);statusDirty=true;}
}
void timer(){
  if(busy)return;
  busy=true;
  try{
    if(enabled){
      if(!IsWindow(explorer)||!IsWindow(list)){if(explorer)detach();EnumWindows(findRoot,0);}
      if(IsWindow(list)&&!editing){
        if(column>=0&&!ownsColumn()){
          // Known native commands remove us before rebuilding. An unexplained
          // column-count change is unsafe to repair by guessing an index.
          error="Native columns changed unexpectedly; restore the layout or close and reopen Media Explorer";
          columnMismatch=true;
          statusDirty=true;report();busy=false;return;
        }
        if(columnMismatch&&ownsColumn()){columnMismatch=false;error.clear();statusDirty=true;}
        if(column<0&&now()>=columnsAfter)addColumn();
        if(column>=0&&IsWindowVisible(list)){
          saveLayout();auto dir=text(GetDlgItem(explorer,1002));
          if(dir!=directory){directory=dir;nextScan=0;}
          if(observedTop!=ListView_GetTopIndex(list)||observedCount!=ListView_GetItemCount(list))nextScan=0;
          if(now()>=nextScan){scan();nextScan=now()+1000;}
          dataTick();
          if(previewSeek.pending()){
            auto row=rows.find(seekRow);
            auto path=row!=rows.end()&&ListView_GetItemState(list,seekRow,LVIS_SELECTED)?row->second:std::string{};
            if(previewSeek.tick(explorer,path,now()))statusDirty=true;
          }
        }
      }
    }
    static uint64_t reportAt=0;if(now()-reportAt>=300){reportAt=now();report();}
  }catch(const std::exception& e){error=e.what();statusDirty=true;stopSource();}
  busy=false;
}
void clearImages(){images.clear();imageBytes=0;}
bool command(int cmd,int){
  if(cmd==toggle.accel.cmd){enabled=!enabled;if(!enabled)detach();SetExtState(section,"enabled",enabled?"1":"0",true);statusDirty=true;report();return true;}
  if(cmd==refresh.accel.cmd){previewSeek.cancel();stopSource();cancelHiddenReads({});rows.clear();cache.clear();clearImages();nextScan=0;statusDirty=true;return true;}return false;
}
int toggled(int cmd){return cmd==toggle.accel.cmd?int(enabled):-1;}
template<class T>bool load(T& f,const char* name){f=reinterpret_cast<T>(host.GetFunc(name));return f!=nullptr;}
int setting(const char* key,int fallback,int low,int high){auto* v=GetExtState(section,key);return v&&*v?std::clamp(std::atoi(v),low,high):fallback;}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE,reaper_plugin_info_t* rec){
  if(!rec){
    if(host.Register){host.Register("-timer",(void*)timer);readers.stop();detach();clearImages();host.Register("-hookcommand",(void*)command);host.Register("-toggleaction",(void*)toggled);host.Register("-gaccel",&toggle);host.Register("-gaccel",&refresh);}
    host={};return 0;
  }
  if(rec->caller_version!=REAPER_PLUGIN_VERSION)return 0;
  host=*rec;
  if(!load(GetExtState,"GetExtState")||!load(SetExtState,"SetExtState")||!load(GetToggleState,"GetToggleCommandStateEx")||!load(IsMediaExtension,"IsMediaExtension")||!load(PeakName,"GetPeakFileNameEx")||!load(ClearPeakCache,"ClearPeakCache")||!load(CreateSource,"PCM_Source_CreateFromFileEx")||!load(DestroySource,"PCM_Source_Destroy")||!load(SourceLength,"GetMediaSourceLength")||!load(Channels,"GetMediaSourceNumChannels")||!load(BuildPeaks,"PCM_Source_BuildPeaks")||!load(GetPeaks,"PCM_Source_GetPeaks")){host={};return 0;}
  enabled=std::string(GetExtState(section,"enabled"))!="0";
  savedOrder=setting("linux_wave_order",-1,0,1024);savedWidth=setting("linux_wave_width",196,24,4096);
  try{readers.start();}catch(...){host={};return 0;}
  toggle.accel.cmd=host.Register("command_id",(void*)"MWT_TOGGLE");toggle.desc="Media Explorer: Toggle waveform thumbnails";
  refresh.accel.cmd=host.Register("command_id",(void*)"MWT_REFRESH");refresh.desc="Media Explorer: Refresh waveform thumbnail cache";
  host.Register("gaccel",&toggle);host.Register("gaccel",&refresh);host.Register("hookcommand",(void*)command);host.Register("toggleaction",(void*)toggled);host.Register("timer",(void*)timer);statusDirty=true;report();return 1;
}

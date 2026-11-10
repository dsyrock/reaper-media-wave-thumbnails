#import <Cocoa/Cocoa.h>
#include "reaper/reaper_plugin.h" // staged official SDK + pinned host-provided SWELL
#include "peak_reader_pool.hpp"
#include "wave_geometry.hpp"
#include "waveform_file.hpp"
#include "preview_seek.hpp"
#include <chrono>
#include <filesystem>
#include <map>
#include <sstream>
#include <tuple>
#include <cstdlib>

namespace mac {void draw(NSRect,NSTableView*,NSInteger);void layoutChanged();}
@interface MWTWaveCell : NSCell @end
@implementation MWTWaveCell
- (BOOL)isOpaque{return NO;}
- (void)drawWithFrame:(NSRect)frame inView:(NSView*)view {
  if([view isKindOfClass:[NSTableView class]])mac::draw(frame,(NSTableView*)view,[(NSTableView*)view rowAtPoint:NSMakePoint(NSMidX(frame),NSMidY(frame))]);
}
@end
@interface MWTLayoutObserver : NSObject
- (void)changed:(NSNotification*)note;
@end
@implementation MWTLayoutObserver
- (void)changed:(NSNotification*)note{(void)note;mac::layoutChanged();}
@end

namespace mac {
constexpr char section[]="MediaWaveThumbnails";
constexpr char version[]="0.4.2";
reaper_plugin_info_t host{};
NSHashTable *roots=nil,*tables=nil;
NSTableColumn* own=nil;
MWTLayoutObserver* observer=nil;
WNDPROC original=nullptr;
bool enabled=true,editing=false,busy=false;
int logical=-1;
double savedWidth=196;
int savedOrder=-1;
bool layoutPending=false;
uint64_t layoutAt=0;
uint64_t nextScan{},revision{},ticket{},draws{},directReads{},sourceOpens{},rasterTick{};
NSRange observedRange=NSMakeRange(NSNotFound,0);
NSInteger observedRows=-1;
std::string error;
gaccel_register_t toggle{},refresh{};
mwt::PeakReaderPool readers;
mwt::PreviewSeek previewSeek;
int seekRow=-1;
struct Wave {mwt::FileStamp stamp;mwt::PeakData data;int state=0;bool loading=false,tried=false,published=false,changed=false;uint64_t revision=0,ticket=0;};
std::map<std::string,Wave> cache;
std::map<NSInteger,std::string> rows;
using Key=std::tuple<uint64_t,int,int,uint32_t>;
struct Image {
  NSImage* image;size_t bytes;uint64_t used;
  Image(NSImage* p,size_t n):image(p),bytes(n),used(++rasterTick){}
  Image(const Image&)=delete;
  ~Image(){[image release];}
};
std::map<Key,Image> images;
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
NSTableView* table(){return (NSTableView*)[tables anyObject];}
HWND root(){return (HWND)[roots anyObject];}
std::string text(HWND h){std::array<char,32768> b{};GetWindowText(h,b.data(),int(b.size()));return b.data();}
std::string item(int row,int sub){std::array<char,32768> b{};ListView_GetItemText((HWND)table(),row,sub,b.data(),int(b.size()));return b.data();}
std::string quote(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\')out+='\\';if(c>=32)out+=char(c);}return out+'"';}
void status(){std::ostringstream s;int ready=0;for(const auto& r:rows)if(cache.at(r.second).state==1)++ready;
  s<<"{\"platform\":\"macos\",\"version\":"<<quote(version)<<",\"experimental\":false,\"enabled\":"<<(enabled?"true":"false")
   <<",\"attached\":"<<(own&&table()?"true":"false")<<",\"cache\":"<<cache.size()<<",\"visible_files\":"<<rows.size()
   <<",\"ready\":"<<ready<<",\"direct_peak_reads\":"<<directReads<<",\"source_opens\":"<<sourceOpens<<",\"draws\":"<<draws
   <<",\"seek_requests\":"<<previewSeek.requests<<",\"seek_completed\":"<<previewSeek.completed<<",\"seek_cancelled\":"<<previewSeek.cancelled
   <<",\"seek_pending\":"<<(previewSeek.pending()?"true":"false")<<",\"seek_ratio\":"<<previewSeek.lastRatio<<",\"seek_path\":"<<quote(previewSeek.lastPath)<<",\"seek_error\":"<<quote(previewSeek.error)<<",\"error\":"<<quote(error)<<'}';
  SetExtState(section,"status",s.str().c_str(),false);
}
int ownIndex(){
  auto* t=table();if(!own||!t)return -1;
  NSInteger visual=[[t tableColumns] indexOfObjectIdenticalTo:own];if(visual==NSNotFound)return -1;
  std::vector<int> order([t numberOfColumns]);
  // SWELL macOS returns logical -> visual (unlike the Windows order array).
  if(!ListView_GetColumnOrderArray((HWND)t,int(order.size()),order.data()))return -1;
  for(size_t i=0;i<order.size();++i)if(order[i]==visual)return int(i);
  return -1;
}
void saveLayout(bool force=false){auto* t=table();if(editing||!own||!t)return;NSInteger index=[[t tableColumns] indexOfObjectIdenticalTo:own];if(index==NSNotFound)return;
  double width=[own width];if(savedOrder!=index||std::abs(savedWidth-width)>.5){savedOrder=int(index);savedWidth=width;
    layoutPending=true;layoutAt=now();}
  if(layoutPending&&(force||now()-layoutAt>=250)){auto a=std::to_string(savedOrder),b=std::to_string(savedWidth);SetExtState(section,"mac_wave_order",a.c_str(),true);SetExtState(section,"mac_wave_width",b.c_str(),true);layoutPending=false;}}
void layoutChanged(){if(!editing)saveLayout();}
void removeColumn(){previewSeek.cancel();if(!own)return;saveLayout(true);editing=true;int index=ownIndex();if(index>=0)ListView_DeleteColumn((HWND)table(),index);[own release];own=nil;logical=-1;editing=false;}
void clearImages(){images.clear();imageBytes=0;}
void stopSource(){if(source){if(building)BuildPeaks(source,2);DestroySource(source);}source=nullptr;building=false;sourcePath.clear();}
LRESULT parentProc(HWND,UINT,WPARAM,LPARAM);
std::string resolve(int,const std::string&);
void scan();
void detach(){
  previewSeek.cancel();seekRow=-1;
  stopSource();for(const auto& job:readers.retain({})){auto i=cache.find(mwt::pathUtf8(job.path));if(i!=cache.end()&&i->second.ticket==job.ticket){i->second.loading=false;i->second.tried=false;}}
  rows.clear();removeColumn();
  id t=table();if(t&&observer)[[NSNotificationCenter defaultCenter] removeObserver:observer name:nil object:t];
  HWND r=root();if(r&&original&&(WNDPROC)GetWindowLong(r,GWL_WNDPROC)==&parentProc)SetWindowLong(r,GWL_WNDPROC,(LONG_PTR)original);
  [tables removeAllObjects];[roots removeAllObjects];original=nullptr;nextScan=0;
}
LRESULT parentProc(HWND w,UINT message,WPARAM wp,LPARAM lp){
  WNDPROC next=original;
  if(message==WM_CLOSE||message==WM_DESTROY){detach();return next?next(w,message,wp,lp):0;}
  if(message==WM_NOTIFY&&lp){auto* hdr=(NMHDR*)lp;if(hdr->hwndFrom==(HWND)table()){
    if(hdr->code==LVN_BEGINDRAG)previewSeek.cancel();
    if(hdr->code==NM_CLICK||hdr->code==NM_DBLCLK){
      auto* click=(NMLISTVIEW*)lp;previewSeek.cancel();auto* t=table();
      NSInteger visual=own?[[t tableColumns] indexOfObjectIdenticalTo:own]:NSNotFound;
      if(visual!=NSNotFound&&click->iSubItem==visual){
        scan();
        auto row=rows.find(click->iItem);
        if(!mwt::previewModifiers()&&row!=rows.end()&&cache.at(row->second).state==1&&cache.at(row->second).published){
          NSRect cell=[t frameOfCellAtColumn:visual row:click->iItem];
          auto path=row->second;
          if(!path.empty()){
            double fraction=mwt::thumbnailRatio(click->ptAction.x,cell.origin.x,NSMaxX(cell));
            std::vector<int> order([t numberOfColumns]);
            if(ListView_GetColumnOrderArray((HWND)t,int(order.size()),order.data())){
              NMLISTVIEW native=*click;native.hdr.code=NM_CLICK;native.iSubItem=order[0];
              if(next)next(w,message,wp,(LPARAM)&native);
              seekRow=click->iItem;previewSeek.request(path,fraction,now());
            }
          }
        }
        return 0;
      }
    }
    int index=ownIndex();
    if(hdr->code==LVN_GETDISPINFO){auto* n=(NMLVDISPINFO*)lp;if(index>=0&&n->item.iSubItem==index){if(n->item.pszText&&n->item.cchTextMax)n->item.pszText[0]=0;n->item.state=0;n->item.iImage=-1;return 0;}}
    if(hdr->code==NM_CUSTOMDRAW&&index>=0&&((NMLVCUSTOMDRAW*)lp)->iSubItem==index)return 0;
    if(hdr->code==LVN_COLUMNCLICK&&index>=0&&((NMLISTVIEW*)lp)->iSubItem==index)return 0;
  }}
  if(message==WM_COMMAND){int cmd=LOWORD(wp);if(cmd==42172||(cmd>=42338&&cmd<=42347)||cmd==42359){removeColumn();editing=true;LRESULT result=next?next(w,message,wp,lp):0;editing=false;nextScan=now()+150;return result;}}
  if(message==WM_COMMAND)nextScan=0;
  return next?next(w,message,wp,lp):0;
}
bool addColumn(){auto* t=table();if(!t||editing)return false;if(ownIndex()>=0)return true;
  int count=int([t numberOfColumns]);if(count<4)return false;
  [own release];own=nil;editing=true;
  std::vector<int> nativeOrder(count);int filePosition=0;
  if(count>0&&ListView_GetColumnOrderArray((HWND)t,count,nativeOrder.data()))filePosition=nativeOrder[0];
  LVCOLUMN c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<char*>("Waveform");c.cx=int(savedWidth);
  ListView_InsertColumn((HWND)t,count,&c);
  if([t numberOfColumns]!=count+1){editing=false;error="SWELL could not insert the waveform column";return false;}
  own=[[[t tableColumns] lastObject] retain];[own setIdentifier:@"MediaWaveThumbnails.waveform"];[own setWidth:savedWidth];
  MWTWaveCell* cell=[[MWTWaveCell alloc] init];[own setDataCell:cell];[cell release];
  int slot=savedOrder<0?std::min(count,filePosition+1):std::clamp(savedOrder,0,count);[t moveColumn:count toColumn:slot];
  logical=count;editing=false;saveLayout();return true;
}
bool findView(NSView* view){
  id list=(id)GetDlgItem((HWND)view,1001);
  if([list isKindOfClass:[NSTableView class]]&&GetDlgItem((HWND)view,1002)){
    if([[(NSTableView*)list delegate] respondsToSelector:@selector(tableView:viewForTableColumn:row:)]){error="View-based Media Explorer tables are not supported by this backend";return false;}
    WNDPROC proc=(WNDPROC)GetWindowLong((HWND)view,GWL_WNDPROC);
    if(!proc){error="Media Explorer does not expose a SWELL window procedure";return false;}
    [roots addObject:view];[tables addObject:list];original=proc;SetWindowLong((HWND)view,GWL_WNDPROC,(LONG_PTR)&parentProc);nextScan=now()+150;
    [[NSNotificationCenter defaultCenter] addObserver:observer selector:@selector(changed:) name:NSTableViewColumnDidMoveNotification object:list];
    [[NSNotificationCenter defaultCenter] addObserver:observer selector:@selector(changed:) name:NSTableViewColumnDidResizeNotification object:list];
    return true;
  }
  for(NSView* child in [view subviews])if(findView(child))return true;
  return false;
}
std::string resolve(int row,const std::string& directory){
  std::string name=item(row,0),ext=item(row,3);if(name.empty()||name=="..")return {};
  if(!ext.empty()&&ext[0]=='.')ext.erase(0,1);
  std::vector<std::string> names{name};if(!ext.empty()&&ext.find_first_of("/\\ <>|")==std::string::npos)names.push_back(name+"."+ext);
  for(const auto& value:names){std::filesystem::path p(value);std::string suffix=p.extension().string();if(suffix.empty()||!mwt::waveformExtension(suffix)||!IsMediaExtension(suffix.c_str()+1,false))continue;
    if(!p.is_absolute()){std::filesystem::path d(directory);if(!d.is_absolute())continue;p=d/p;}
    mwt::FileStamp stamp;if(mwt::fileStamp(mwt::pathWide(p.string()),stamp))return p.lexically_normal().string();}
  return {};
}
void scan(){
  auto* t=table();NSRange range=[t rowsInRect:[t visibleRect]];if(range.location==NSNotFound){rows.clear();return;}
  int first=int(range.location),last=std::min(int([t numberOfRows]),first+int(range.length)+2);
  std::map<NSInteger,std::string> next;std::vector<int> missing;std::string directory=text(GetDlgItem(root(),1002));
  for(int row=first;row<last;++row){auto path=resolve(row,directory);if(path.empty()){auto ext=item(row,3);if(mwt::waveformExtension(ext)&&IsMediaExtension(ext.c_str(),false))missing.push_back(row);}else next[row]=path;}
  if(!missing.empty()&&GetToggleState(32063,42026)==0){bool leading=GetToggleState(32063,42134)==1;
    SendMessage(root(),WM_COMMAND,42026,0);
    try {for(int row:missing){auto path=resolve(row,directory);if(!path.empty())next[row]=path;}}
    catch(...){SendMessage(root(),WM_COMMAND,42026,0);if(leading&&GetToggleState(32063,42134)!=1)SendMessage(root(),WM_COMMAND,42134,0);throw;}
    SendMessage(root(),WM_COMMAND,42026,0);if(leading&&GetToggleState(32063,42134)!=1)SendMessage(root(),WM_COMMAND,42134,0);
  }
  for(auto i=next.begin();i!=next.end();){mwt::FileStamp stamp;if(!mwt::fileStamp(mwt::pathWide(i->second),stamp)){i=next.erase(i);continue;}
    auto [entry,inserted]=cache.try_emplace(i->second);if(inserted||!(entry->second.stamp==stamp)){if(sourcePath==i->second)stopSource();entry->second=Wave{};entry->second.stamp=stamp;entry->second.revision=++revision;entry->second.changed=!inserted;}
    ++i;}
  bool changed=rows!=next;rows=std::move(next);if(changed)[t setNeedsDisplay:YES];
  std::set<std::wstring> paths;for(const auto& row:rows)paths.insert(mwt::pathWide(row.second));
  if(source&&paths.find(mwt::pathWide(sourcePath))==paths.end())stopSource();
  for(const auto& job:readers.retain(paths)){auto i=cache.find(mwt::pathUtf8(job.path));if(i!=cache.end()&&i->second.ticket==job.ticket){i->second.loading=false;i->second.tried=false;}}
}
void dataTick(){
  for(auto& result:readers.take()){auto i=cache.find(mwt::pathUtf8(result.job.path));if(i==cache.end()||i->second.ticket!=result.job.ticket||i->second.revision!=result.job.revision)continue;
    auto& wave=i->second;wave.loading=false;
    mwt::FileStamp current;
    if(!mwt::fileStamp(result.job.path,current)||!(current==wave.stamp)){nextScan=0;wave.tried=false;continue;}
    if(result.result==mwt::PeakResult::Ready){wave.data=result.data;wave.state=1;++directReads;}
    else if(result.result==mwt::PeakResult::Stale)wave.changed=true;}
  for(const auto& row:rows){auto& wave=cache.at(row.second);if(wave.state||wave.loading||wave.tried)continue;wave.tried=true;
    std::array<char,32768> peak{};PeakName(row.second.c_str(),peak.data(),int(peak.size()),false);mwt::FileStamp stamp;
    if(!wave.changed&&mwt::fileStamp(mwt::pathWide(peak.data()),stamp)&&stamp.time>=wave.stamp.time){wave.ticket=++ticket;wave.loading=true;if(!readers.submit({mwt::pathWide(row.second),mwt::pathWide(peak.data()),wave.stamp,wave.revision,wave.ticket}))wave.loading=false;}}
  // Only one unfinished REAPER builder is advanced per main-thread tick.
  if(source){if(now()-buildStart>30000){cache[sourcePath].state=-1;stopSource();}
    else if(!building||BuildPeaks(source,1)==0){if(building){BuildPeaks(source,2);building=false;}
      auto& wave=cache.at(sourcePath);bool qn=false;double length=SourceLength(source,&qn);int channels=Channels(source);
      if(!qn&&length>0&&channels>0&&channels<=128){std::vector<double> b(mwt::peakBins*channels*2);int count=GetPeaks(source,mwt::peakBins/length,0,channels,mwt::peakBins,0,b.data())&0xfffff;
        if(count>0&&count<=mwt::peakBins){for(int x=0;x<mwt::peakBins;++x){int k=std::min(x,count-1);float hi=0,lo=0;for(int ch=0;ch<channels;++ch){hi=std::max(hi,float(b[k*channels+ch]));lo=std::min(lo,float(b[mwt::peakBins*channels+k*channels+ch]));}wave.data.high[x]=std::clamp(hi,-1.f,1.f);wave.data.low[x]=std::clamp(lo,-1.f,1.f);wave.data.peak=std::max({wave.data.peak,hi,-lo});}wave.state=1;}else wave.state=-1;
      }else wave.state=-1;stopSource();}}
  if(!source)for(const auto& row:rows){auto& wave=cache.at(row.second);if(wave.state||wave.loading||!wave.tried)continue;
    if(!mwt::waveformPath(row.second)){wave.state=-1;continue;}
    std::array<char,32768> peak{};PeakName(row.second.c_str(),peak.data(),int(peak.size()),false);mwt::FileStamp peakStamp;
    if(wave.changed||(mwt::fileStamp(mwt::pathWide(peak.data()),peakStamp)&&peakStamp.time<wave.stamp.time)){
      ClearPeakCache();std::filesystem::path p(peak.data());std::error_code ec;
      mwt::FileStamp existing;if(p.extension()==".reapeaks"&&mwt::fileStamp(mwt::pathWide(p.string()),existing))std::filesystem::remove(p,ec);
      if(ec){wave.state=-1;error="Cannot invalidate stale REAPER peak cache: "+ec.message();continue;}}
    sourcePath=row.second;++sourceOpens;source=CreateSource(sourcePath.c_str(),true);if(!source){wave.state=-1;sourcePath.clear();continue;}buildStart=now();building=BuildPeaks(source,0)!=0;break;}
  for(const auto& row:rows)if(cache.at(row.second).loading)return;
  bool changed=false;for(const auto& row:rows){auto& wave=cache.at(row.second);if(wave.state==1&&!wave.published){wave.published=true;changed=true;}}
  if(changed)[table() setNeedsDisplay:YES];
}
void draw(NSRect frame,NSTableView* t,NSInteger row){
  if(t!=table()||row<0)return;auto r=rows.find(row);if(r==rows.end())return;auto& wave=cache.at(r->second);if(wave.state!=1||!wave.published)return;
  frame=NSInsetRect(frame,6,3);if(frame.size.width<=0||frame.size.height<=0)return;
  double scale=t.window.backingScaleFactor;if(scale<=0)scale=1;
  int width=int(std::ceil(frame.size.width*scale)),height=int(std::ceil(frame.size.height*scale));
  if(uint64_t(width)*height>4ULL*1024*1024)return;
  NSColor* color=([t isRowSelected:row]?[NSColor selectedControlTextColor]:[NSColor systemTealColor]);
  color=[color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];if(!color)return;
  CGFloat red=0,green=0,blue=0,alpha=0;[color getRed:&red green:&green blue:&blue alpha:&alpha];
  uint32_t rgba=(uint32_t(red*255)<<24)|(uint32_t(green*255)<<16)|(uint32_t(blue*255)<<8)|uint32_t(alpha*255);
  Key key{wave.revision,width,height,rgba};auto image=images.find(key);
  if(image==images.end()){
    size_t bytes=size_t(width)*height*4;
    while(!images.empty()&&(images.size()>=512||imageBytes+bytes>16*1024*1024)){auto old=std::min_element(images.begin(),images.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});imageBytes-=old->second.bytes;images.erase(old);}
    NSBitmapImageRep* rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:width pixelsHigh:height bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:width*4 bitsPerPixel:32];
    if(!rep)return;auto* pixels=[rep bitmapData];std::memset(pixels,0,bytes);
    mwt::waveColumns(width,height,wave.data.high,wave.data.low,wave.data.peak,[&](int x,int y){auto* p=pixels+(size_t(y)*width+x)*4;p[0]=rgba>>24;p[1]=rgba>>16;p[2]=rgba>>8;p[3]=rgba;});
    NSImage* bitmap=[[NSImage alloc] initWithSize:frame.size];[bitmap addRepresentation:rep];[rep release];
    image=images.try_emplace(key,bitmap,bytes).first;imageBytes+=bytes;
  }
  image->second.used=++rasterTick;[NSGraphicsContext saveGraphicsState];
  @try{[NSBezierPath clipRect:frame];[image->second.image drawInRect:frame fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];}
  @finally{[NSGraphicsContext restoreGraphicsState];}
  ++draws;
}
void tick(){if(busy||![NSThread isMainThread])return;busy=true;
  @autoreleasepool {try {
    if(enabled){if(!table()||!root()){if(original)detach();for(NSWindow* w in [NSApp windows])if(findView([w contentView]))break;}
      if(table()&&root()&&!editing){if(!own&&now()>=nextScan)addColumn();
        if(own&&[table() window]&&![table() isHiddenOrHasHiddenAncestor]){
          saveLayout();NSRange visible=[table() rowsInRect:[table() visibleRect]];NSInteger count=[table() numberOfRows];
          if(!NSEqualRanges(visible,observedRange)||count!=observedRows)nextScan=0;
          if(now()>=nextScan){scan();observedRange=visible;observedRows=count;nextScan=now()+1000;}dataTick();
          if(previewSeek.pending()){
            auto row=rows.find(seekRow);
            auto path=row!=rows.end()&&[table() isRowSelected:seekRow]?row->second:std::string{};
            previewSeek.tick(root(),path,now());
          }}}}
    static uint64_t report=0;if(now()-report>=300){report=now();status();}
  }catch(const std::exception& e){error=e.what();stopSource();}}
  busy=false;
}
bool command(int cmd,int){if(cmd==toggle.accel.cmd){enabled=!enabled;if(!enabled)detach();SetExtState(section,"enabled",enabled?"1":"0",true);status();return true;}
  if(cmd==refresh.accel.cmd){previewSeek.cancel();stopSource();readers.retain({});rows.clear();cache.clear();clearImages();nextScan=0;return true;}return false;}
int toggled(int cmd){return cmd==toggle.accel.cmd?int(enabled):-1;}
template<class T>bool load(T& f,const char* name){f=reinterpret_cast<T>(host.GetFunc(name));return f!=nullptr;}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE,reaper_plugin_info_t* rec){
  using namespace mac;
  if(!rec){if(host.Register){host.Register("-timer",(void*)tick);readers.stop();detach();clearImages();host.Register("-hookcommand",(void*)command);host.Register("-toggleaction",(void*)toggled);host.Register("-gaccel",&toggle);host.Register("-gaccel",&refresh);}
    [[NSNotificationCenter defaultCenter] removeObserver:observer];[observer release];observer=nil;[roots release];roots=nil;[tables release];tables=nil;return 0;}
  if(rec->caller_version!=REAPER_PLUGIN_VERSION)return 0;host=*rec;
  if(!load(GetExtState,"GetExtState")||!load(SetExtState,"SetExtState")||!load(GetToggleState,"GetToggleCommandStateEx")||!load(IsMediaExtension,"IsMediaExtension")||!load(PeakName,"GetPeakFileNameEx")||!load(ClearPeakCache,"ClearPeakCache")||!load(CreateSource,"PCM_Source_CreateFromFileEx")||!load(DestroySource,"PCM_Source_Destroy")||!load(SourceLength,"GetMediaSourceLength")||!load(Channels,"GetMediaSourceNumChannels")||!load(BuildPeaks,"PCM_Source_BuildPeaks")||!load(GetPeaks,"PCM_Source_GetPeaks"))return 0;
  roots=[[NSHashTable weakObjectsHashTable] retain];tables=[[NSHashTable weakObjectsHashTable] retain];observer=[[MWTLayoutObserver alloc] init];
  enabled=std::string(GetExtState(section,"enabled"))!="0";
  const char* order=GetExtState(section,"mac_wave_order");if(order&&*order)savedOrder=std::clamp(std::atoi(order),0,1024);
  const char* width=GetExtState(section,"mac_wave_width");if(width&&*width)savedWidth=std::clamp(std::atof(width),24.0,4096.0);
  try{readers.start();}catch(...){return 0;}
  toggle.accel.cmd=host.Register("command_id",(void*)"MWT_TOGGLE");toggle.desc="Media Explorer: Toggle waveform thumbnails";
  refresh.accel.cmd=host.Register("command_id",(void*)"MWT_REFRESH");refresh.desc="Media Explorer: Refresh waveform thumbnail cache";
  host.Register("gaccel",&toggle);host.Register("gaccel",&refresh);host.Register("hookcommand",(void*)command);host.Register("toggleaction",(void*)toggled);host.Register("timer",(void*)tick);status();return 1;
}

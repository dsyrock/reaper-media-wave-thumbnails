// Headless ABI/notification test with a simulated host. This deliberately does
// not claim to test REAPER's actual windows or the real SWELL renderer.
#include "reaper/reaper_plugin.h"
#include "../src/platform_file.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
HWND root=(HWND)1,list=(HWND)2,pathBox=(HWND)3;
HWND preview=(HWND)4,previewName=(HWND)5,previewTime=(HWND)6;
int selectedRow=0,nativePlays=0,seekPixel=-1;
WORD heldButton=0,heldModifier=0;
std::string loadedPreview;
std::filesystem::path directory;
std::map<std::string,std::string> settings;
void (*tick)()=nullptr;
bool (*command)(int,int)=nullptr;
int (*toggleState)(int)=nullptr;
int nativeNotifies=0,opens=0,clears=0,allocations=0,blits=0;
int columnInserts=0,columnQueries=0,forbiddenOpens=0;
bool legacySwell=false;
std::map<std::string,HANDLE> properties;
bool invalidBlit=false,nativeLayoutClean=false;
struct Column{int index,width;std::string name;};
std::vector<Column> columns{{0,160,"File"},{1,50,"Size"},{2,50,"Date"},{3,60,"Type"}};
struct Surface{int width,height;std::vector<uint32_t> pixels;};
Surface target{900,200,std::vector<uint32_t>(900*200,0xff313131)};
std::string filename=u8"音频-🌊.wav";
std::vector<std::string> filenames{filename,"normal.xml","broken.XML","notes.MID"};
LRESULT nativeProc(HWND,UINT message,WPARAM wp,LPARAM lp){
  if(message==WM_NOTIFY){++nativeNotifies;auto* h=(NMHDR*)lp;
    if(h->code==LVN_GETDISPINFO){auto* n=(NMLVDISPINFO*)lp;auto name=filenames.at(n->item.iItem);auto value=n->item.iSubItem==0?name:n->item.iSubItem==3?name.substr(name.find_last_of('.')+1):"";std::snprintf(n->item.pszText,n->item.cchTextMax,"%s",value.c_str());}
    if(h->code==NM_CUSTOMDRAW)((NMLVCUSTOMDRAW*)lp)->clrText=RGB(211,212,213);
    return 73;
  }
  if(message==WM_COMMAND&&LOWORD(wp)==42172)nativeLayoutClean=columns.size()==4;
  if(message==WM_COMMAND&&LOWORD(wp)==1008)loadedPreview=filenames.at(selectedRow);
  if(message==WM_COMMAND&&LOWORD(wp)==40027)++nativePlays;
  return 91;
}
WNDPROC proc=nativeProc;
Column* col(int index){for(auto& c:columns)if(c.index==index)return &c;return nullptr;}
BOOL fEnumWindows(BOOL(*fn)(HWND,LPARAM),LPARAM lp){fn(root,lp);return TRUE;}
BOOL fEnumChildren(HWND,BOOL(*)(HWND,LPARAM),LPARAM){return TRUE;}
HWND fGetDlgItem(HWND w,int id){return w==root?(id==1001?list:id==1002?pathBox:id==1046?preview:id==1014?previewName:id==1016?previewTime:nullptr):nullptr;}
bool fIsWindow(HWND w){return w==root||w==list||w==pathBox||w==preview||w==previewName||w==previewTime;}
WORD fAsync(int key){return key==VK_LBUTTON?heldButton:key==VK_CONTROL?heldModifier:0;}
bool fVisible(HWND){return true;}
int fGetClass(HWND,char* out,int size){std::snprintf(out,size,"SysListView32");return 13;}
LONG_PTR fGetLong(HWND,int){return (LONG_PTR)proc;}
LONG_PTR fSetLong(HWND,int,LONG_PTR value){auto old=proc;proc=(WNDPROC)value;return (LONG_PTR)old;}
HANDLE fGetProp(HWND,const char* key){return properties[key];}
BOOL fSetProp(HWND,const char* key,HANDLE value){properties[key]=value;return TRUE;}
HANDLE fRemoveProp(HWND,const char* key){auto old=properties[key];properties.erase(key);return old;}
BOOL fGetText(HWND w,int,char* out,int size){std::snprintf(out,size,"%s",w==pathBox?directory.c_str():w==previewName?loadedPreview.c_str():w==previewTime?"0:00.000 / 0:30.000":"");return TRUE;}
HWND fHeader(HWND w){return w;}
int fColumnCount(HWND){return int(columns.size());}
int fWidth(HWND,int index){auto* c=col(index);return c?c->width:0;}
void fGetColumn(HWND,int index,LVCOLUMN* c){++columnQueries;auto* value=col(index);if(!value)return;if(c->mask&LVCF_TEXT)std::snprintf(c->pszText,c->cchTextMax,"%s",value->name.c_str());}
void fInsert(HWND,int index,const LVCOLUMN* c){++columnInserts;for(auto& v:columns)if(v.index>=index)++v.index;columns.push_back({index,c->cx,c->pszText});}
bool fDelete(HWND,int index){auto i=std::find_if(columns.begin(),columns.end(),[&](const auto& c){return c.index==index;});if(i==columns.end())return false;columns.erase(i);for(auto& c:columns)if(c.index>index)--c.index;return true;}
BOOL fOrder(HWND,int count,int* out){if(count!=int(columns.size()))return FALSE;for(int i=0;i<count;++i)out[i]=columns[i].index;return TRUE;}
BOOL fSetOrder(HWND,int count,int* order){std::vector<Column> next;for(int i=0;i<count;++i){auto* c=col(order[i]);if(!c)return FALSE;next.push_back(*c);}columns=std::move(next);return TRUE;}
int fCount(HWND){return int(filenames.size());}
int fTop(HWND){return 0;}
int fPerPage(HWND){return 8;}
int fSelected(HWND,int row,UINT mask){return row==selectedRow?(mask&LVIS_SELECTED):0;}
void fItemText(HWND,int row,int sub,char* out,int size){NMLVDISPINFO n{};n.hdr={list,1001,LVN_GETDISPINFO};n.item.mask=LVIF_TEXT;n.item.iItem=row;n.item.iSubItem=sub;n.item.pszText=out;n.item.cchTextMax=size;proc(root,WM_NOTIFY,1001,(LPARAM)&n);}
LRESULT fSend(HWND w,UINT msg,WPARAM wp,LPARAM lp){if(w==preview){if(msg==WM_LBUTTONDOWN)seekPixel=int(short(lp&0xffff));return 0;}return proc(w,msg,wp,lp);}
BOOL fInvalidate(HWND,const RECT*,int){return TRUE;}
void fClient(HWND w,RECT* r){*r=w==preview?RECT{0,0,1000,60}:RECT{0,0,900,200};}
int fHeaderHeight(HWND){return 24;}
bool fRect(HWND,int row,int index,int,RECT* r){int x=0;for(const auto& c:columns){if(c.index==index){*r={x,24+row*20,x+c.width,44+row*20};return true;}x+=c.width;}return false;}
HDC fCreateContext(HDC,int width,int height){++allocations;return (HDC)new Surface{width,height,std::vector<uint32_t>(size_t(width)*height,0)};}
void fDeleteContext(HDC dc){--allocations;delete (Surface*)dc;}
void* fFrameBuffer(HDC dc){return ((Surface*)dc)->pixels.data();}
int fSysColor(int){return RGB(255,255,255);}
void fBlit(HDC out,int x,int y,int w,int h,HDC in,int sx,int sy,int mode){
  auto& a=*(Surface*)out;auto& b=*(Surface*)in;
  if(mode!=(int)SRCCOPY_USEALPHACHAN||x<0||y<24||x+w>a.width||y+h>a.height||sx<0||sy<0||sx+w>b.width||sy+h>b.height){invalidBlit=true;return;}
  ++blits;for(int row=0;row<h;++row)for(int c=0;c<w;++c){auto pixel=b.pixels[(sy+row)*b.width+sx+c];if(pixel>>24)a.pixels[(y+row)*a.width+x+c]=pixel;}
}
int unexpected(){std::cerr<<"Unexpected SWELL API call\n";std::abort();}
void* swellApi(const char* name){
  if(legacySwell&&!std::strcmp(name,"ListView_GetColumn"))return nullptr;
#define API(n,f) if(!std::strcmp(name,n))return (void*)f
  API("EnumWindows",fEnumWindows);API("EnumChildWindows",fEnumChildren);API("GetDlgItem",fGetDlgItem);
  API("IsWindow",fIsWindow);API("IsWindowVisible",fVisible);API("GetClassName",fGetClass);API("GetWindowLong",fGetLong);API("SetWindowLong",fSetLong);
  API("GetProp",fGetProp);API("SetProp",fSetProp);API("RemoveProp",fRemoveProp);
  API("GetAsyncKeyState",fAsync);
  API("GetDlgItemText",fGetText);API("ListView_GetHeader",fHeader);API("Header_GetItemCount",fColumnCount);API("ListView_GetColumnWidth",fWidth);
  API("ListView_GetColumn",fGetColumn);API("ListView_InsertColumn",fInsert);API("ListView_DeleteColumn",fDelete);
  API("ListView_GetColumnOrderArray",fOrder);API("ListView_SetColumnOrderArray",fSetOrder);API("ListView_GetItemCount",fCount);API("ListView_GetTopIndex",fTop);
  API("ListView_GetCountPerPage",fPerPage);API("ListView_GetItemState",fSelected);API("ListView_GetItemText",fItemText);API("SendMessage",fSend);
  API("InvalidateRect",fInvalidate);API("GetClientRect",fClient);API("ListView_GetSubItemRect",fRect);API("SWELL_GetListViewHeaderHeight",fHeaderHeight);
  API("SWELL_CreateMemContext",fCreateContext);API("SWELL_DeleteGfxContext",fDeleteContext);API("SWELL_GetCtxFrameBuffer",fFrameBuffer);API("BitBlt",fBlit);API("GetSysColor",fSysColor);
  return (void*)unexpected; // fail if an unmodelled function is actually called
}
const char* getState(const char*,const char* key){return settings[key].c_str();}
void setState(const char*,const char* key,const char* value,bool){settings[key]=value;}
int getToggle(int,int){return 1;}
bool media(const char* ext,bool){return !strcasecmp(ext,"wav")||!strcasecmp(ext,"xml")||!strcasecmp(ext,"mid");}
void peakName(const char* path,char* out,int size,bool){std::snprintf(out,size,"%s.reapeaks",path);}
void clearPeaks(){++clears;}
PCM_source* createSource(const char* path,bool){
  auto ext=std::filesystem::path(path).extension().string();
  if(ext!=".wav"){++forbiddenOpens;return nullptr;}
  ++opens;return (PCM_source*)1;
}
void destroySource(PCM_source*){}
double sourceLength(PCM_source*,bool* qn){*qn=false;return 1;}
int channels(PCM_source*){return 2;}
int buildPeaks(PCM_source*,int){return 0;}
int getPeaks(PCM_source*,double,double,int ch,int count,int,double* b){for(int i=0;i<ch*count;++i){b[i]=.6;b[ch*count+i]=-.4;}return count;}
void* hostApi(const char* name){
  API("GetExtState",getState);API("SetExtState",setState);API("GetToggleCommandStateEx",getToggle);API("IsMediaExtension",media);API("GetPeakFileNameEx",peakName);
  API("ClearPeakCache",clearPeaks);API("PCM_Source_CreateFromFileEx",createSource);API("PCM_Source_Destroy",destroySource);API("GetMediaSourceLength",sourceLength);
  API("GetMediaSourceNumChannels",channels);API("PCM_Source_BuildPeaks",buildPeaks);API("PCM_Source_GetPeaks",getPeaks);return nullptr;
#undef API
}
int registerHost(const char* name,void* value){
  if(!std::strcmp(name,"timer"))tick=(void(*)())value;
  if(!std::strcmp(name,"-timer"))tick=nullptr;
  if(!std::strcmp(name,"hookcommand"))command=(bool(*)(int,int))value;
  if(!std::strcmp(name,"-hookcommand"))command=nullptr;
  if(!std::strcmp(name,"toggleaction"))toggleState=(int(*)(int))value;
  if(!std::strcmp(name,"-toggleaction"))toggleState=nullptr;
  if(!std::strcmp(name,"command_id"))return !std::strcmp((const char*)value,"MWT_TOGGLE")?5001:5002;
  return 1;
}
void pump(int ms){auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);do{tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(std::chrono::steady_clock::now()<end);}
void writeFixture(){
  auto source=directory/filename;{std::ofstream f(source,std::ios::binary);f<<std::string(4096,'\0');}
  mwt::FileStamp stamp;require(mwt::fileStamp(mwt::pathWide(source.string()),stamp),"fixture stat failed");
  std::ofstream f(source.string()+".reapeaks",std::ios::binary);f.write("RPKN\1\1",6);
  auto put32=[&](uint32_t n){for(int i=0;i<4;++i)f.put(char(n>>(8*i)));};auto put16=[&](int n){f.put(char(n));f.put(char(n>>8));};
  put32(48000);put32(uint32_t(stamp.time/10000000-11644473600ULL));put32(4096);put32(128);put32(384);
  for(int i=0;i<384;++i){put16(16384);put16(-8192);}
  {std::ofstream good(directory/"normal.xml");good<<"<?xml version=\"1.0\"?><score-partwise version=\"4.0\"></score-partwise>";}
  {std::ofstream bad(directory/"broken.XML");bad<<"score-partwise version=\"4.0\"></score-partwise>";}
  {std::ofstream midi(directory/"notes.MID");midi<<"MThd";}
}
void paint(){NMLVCUSTOMDRAW n{};n.nmcd.hdr={list,1001,NM_CUSTOMDRAW};n.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;n.nmcd.hdc=(HDC)&target;n.nmcd.dwItemSpec=0;n.iSubItem=4;n.clrText=RGB(1,2,3);auto original=n.clrText;int count=nativeNotifies;proc(root,WM_NOTIFY,1001,(LPARAM)&n);require(count==nativeNotifies&&n.clrText==original,"wave cell leaked into native draw handler");}
void click(double ratio,int code=NM_CLICK){RECT r{};fRect(list,0,4,0,&r);NMLISTVIEW n{};n.hdr={list,1001,UINT(code)};n.iItem=0;n.iSubItem=4;n.ptAction={int(r.left+6+ratio*(r.right-r.left-13)),r.top+8};proc(root,WM_NOTIFY,1001,(LPARAM)&n);}
}
int main(int argc,char** argv){
  try{
    require(argc==3||(argc==4&&!std::strcmp(argv[3],"--legacy-swell")),"plugin and data directory required");legacySwell=argc==4;directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);writeFixture();
    void* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!module)throw std::runtime_error(dlerror());
    auto swell=(int(*)(HINSTANCE,DWORD,LPVOID))dlsym(module,"SWELL_dllMain");
    auto entry=(int(*)(void*,reaper_plugin_info_t*))dlsym(module,"ReaperPluginEntry");require(swell&&entry,"missing plugin exports");
    require(swell(nullptr,DLL_PROCESS_ATTACH,(void*)swellApi)==1,"SWELL API import failed");
    reaper_plugin_info_t rec{};rec.caller_version=REAPER_PLUGIN_VERSION;rec.Register=registerHost;rec.GetFunc=hostApi;
    require(entry(nullptr,&rec)==1&&tick&&command&&toggleState,"extension load failed");
    pump(900);require(columns.size()==5,"wave column not attached");require(settings["status"].find("\"ready\":1")!=std::string::npos,"existing peak not read");require(opens==0,"existing peaks unnecessarily decoded audio");
    pump(1300);require(columnInserts==1&&columns.size()==5&&columnQueries==0,"legacy SWELL repeatedly appended columns");
    require(forbiddenOpens==0&&settings["status"].find("\"visible_files\":1")!=std::string::npos,"XML/MIDI triggered a source importer");
    // A foreign column-count change must pause, never trigger blind insertion.
    LVCOLUMN extra{};extra.cx=30;extra.pszText=(char*)"Foreign";fInsert(list,5,&extra);int inserts=columnInserts;
    pump(400);require(columnInserts==inserts&&columns.size()==6,"uncertain column ownership caused insertion loop");
    fDelete(list,5);pump(400);require(columns.size()==5,"restored native count did not recover");
    NMLVCUSTOMDRAW native{};native.nmcd.hdr={list,1001,NM_CUSTOMDRAW};native.iSubItem=0;require(proc(root,WM_NOTIFY,1001,(LPARAM)&native)==73&&native.clrText==RGB(211,212,213),"native drawing changed");
    paint();require(blits==1&&!invalidBlit,"wave bitmap not drawn or clipped incorrectly");int allocated=allocations;
    paint();require(blits==2&&allocations==allocated,"repeat paint did not reuse raster");
    require(std::count_if(target.pixels.begin(),target.pixels.end(),[](auto p){return p!=0xff313131;})>0,"empty wave raster");
    auto waveIt=std::find_if(columns.begin(),columns.end(),[](const auto& c){return c.index==4;});
    require(waveIt!=columns.end(),"wave column missing before layout change");
    auto wave=*waveIt;columns.erase(waveIt);columns.insert(columns.begin(),wave);col(4)->width=217;pump(400);
    require(settings["linux_wave_order"]=="0"&&settings["linux_wave_width"]=="217","layout not persisted");
    command(5001,0);require(columns.size()==4&&proc==nativeProc&&toggleState(5001)==0,"toggle did not restore native list");
    command(5001,0);pump(600);require(columns.size()==5&&columns[0].index==4&&col(4)->width==217&&opens==0,"layout/cache not restored");
    proc(root,WM_COMMAND,42172,0);require(nativeLayoutClean,"native layout command saw foreign column");pump(500);require(columns.size()==5,"column not restored after native command");
    click(.25);pump(700);require(nativePlays==1&&std::abs(seekPixel-250)<10,"thumbnail did not seek through native preview");
    click(.2);click(.8,NM_DBLCLK);pump(700);require(nativePlays==2&&std::abs(seekPixel-800)<10,"latest thumbnail click did not win");
    heldButton=0x8000;click(.3);pump(250);require(nativePlays==2,"audition started before mouse release");
    NMLISTVIEW drag{};drag.hdr={list,1001,LVN_BEGINDRAG};proc(root,WM_NOTIFY,1001,(LPARAM)&drag);heldButton=0;pump(300);require(nativePlays==2,"drag unexpectedly sought playback");
    heldModifier=0x8000;click(.4);heldModifier=0;pump(300);require(nativePlays==2,"modified selection click sought playback");
    click(.5);selectedRow=1;pump(300);require(nativePlays==2,"stale click acted after selection changed");selectedRow=0;
    require(settings["status"].find("\"seek_completed\":2")!=std::string::npos,"seek diagnostics missing");
    auto source=directory/filename;std::filesystem::last_write_time(source,std::filesystem::last_write_time(source)+std::chrono::seconds(2));pump(1800);
    require(opens>0&&clears>0,"source change did not invalidate stale peaks");require(settings["status"].find("\"ready\":1")!=std::string::npos,"fallback peaks not ready");
    require(forbiddenOpens==0,"repeated scan attempted XML/MIDI import");
    entry(nullptr,nullptr);require(!tick&&!command&&!toggleState&&proc==nativeProc&&columns.size()==4&&allocations==0,"unload leaked native state or raster");
    dlclose(module);std::cout<<"PASS load/unload, native notifications, Unicode reapeaks/no decode, alpha raster reuse, layout/toggle, native config isolation, source change/fallback, XML/MIDI exclusion, legacy SWELL/no insertion loop\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

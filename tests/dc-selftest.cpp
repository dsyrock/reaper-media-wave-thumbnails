#include "../src/dc_state.hpp"
#include <cassert>
#include <iostream>
int main(){
 HDC dc=CreateCompatibleDC(nullptr);assert(dc);
 SetTextColor(dc,RGB(192,192,192));SetBkColor(dc,RGB(48,48,48));SetBkMode(dc,TRANSPARENT);
 MoveToEx(dc,7,9,nullptr);HRGN clip=CreateRectRgn(2,3,100,200);SelectClipRgn(dc,clip);DeleteObject(clip);
 RECT before{},after{};GetClipBox(dc,&before);
 {mwt::DcState state(dc);assert(state);SetTextColor(dc,0);SetBkColor(dc,RGB(255,255,255));SetBkMode(dc,OPAQUE);MoveToEx(dc,99,88,nullptr);IntersectClipRect(dc,9,9,12,12);}
 POINT point{};GetCurrentPositionEx(dc,&point);GetClipBox(dc,&after);
 assert(GetTextColor(dc)==RGB(192,192,192)&&GetBkColor(dc)==RGB(48,48,48)&&GetBkMode(dc)==TRANSPARENT);
 assert(point.x==7&&point.y==9&&EqualRect(&before,&after));DeleteDC(dc);
 std::cout<<"PASS DC text/background colors, mode, current position and clipping restored\n";return 0;
}

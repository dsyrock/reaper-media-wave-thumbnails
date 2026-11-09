#pragma once
#include <windows.h>
namespace mwt {
class DcState {
  HDC dc;
  int saved;
public:
  explicit DcState(HDC value):dc(value),saved(SaveDC(value)){}
  ~DcState(){if(saved)RestoreDC(dc,saved);}
  explicit operator bool()const{return saved!=0;}
  DcState(const DcState&)=delete;
  DcState& operator=(const DcState&)=delete;
};
}

#pragma once
#include <windows.h>
#include <cstdint>
namespace mwt {
struct CpuCost {uint64_t calls{},ticks{},cycles{};};
#ifdef MWT_PROFILE
struct CpuScope {
  CpuCost& cost;LARGE_INTEGER start{};ULONG64 cycles{};
  explicit CpuScope(CpuCost& value):cost(value){QueryPerformanceCounter(&start);QueryThreadCycleTime(GetCurrentThread(),&cycles);}
  ~CpuScope(){LARGE_INTEGER now{};ULONG64 end=0;QueryThreadCycleTime(GetCurrentThread(),&end);QueryPerformanceCounter(&now);++cost.calls;cost.ticks+=now.QuadPart-start.QuadPart;cost.cycles+=end-cycles;}
};
inline double milliseconds(const CpuCost& c){LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);return c.ticks*1000.0/frequency.QuadPart;}
#else
struct CpuScope {explicit CpuScope(CpuCost&){};};
inline double milliseconds(const CpuCost&){return 0;}
#endif
}

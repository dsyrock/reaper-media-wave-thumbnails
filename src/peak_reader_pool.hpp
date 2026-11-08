#pragma once
#include "reapeaks_reader.hpp"
#include <condition_variable>
#include <atomic>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace mwt {
struct PeakJob {
  std::wstring path,peakPath;
  FileStamp stamp;
  uint64_t revision{},ticket{};
};
struct PeakCompletion {
  PeakJob job;
  PeakResult result=PeakResult::Unsupported;
  PeakData data;
};
class PeakReaderPool {
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<PeakJob> pending;
  std::vector<PeakCompletion> completed;
  std::vector<std::thread> threads;
  std::atomic<unsigned> readyCount{0};
  bool stopping=true;
  void run() {
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    for(;;) {
      PeakJob job;
      {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock,[&]{return stopping||!pending.empty();});
        if(stopping)return;
        job=std::move(pending.front());pending.pop_front();
      }
      PeakCompletion result;result.job=std::move(job);
      try {result.result=readPeaks(result.job.path,result.job.peakPath,result.job.stamp,result.data);}
      catch(const std::exception&) {result.result=PeakResult::Unsupported;}
      std::lock_guard<std::mutex> lock(mutex);
      if(!stopping){completed.push_back(std::move(result));readyCount.fetch_add(1,std::memory_order_release);}
    }
  }
public:
  ~PeakReaderPool(){stop();}
  void start() {
    stopping=false;
    try {for(int i=0;i<2;++i)threads.emplace_back([this]{run();});}
    catch(...) {stop();throw;}
  }
  bool submit(PeakJob job) {
    std::lock_guard<std::mutex> lock(mutex);
    if(stopping)return false;
    pending.push_back(std::move(job));wake.notify_one();return true;
  }
  std::vector<PeakJob> retain(const std::set<std::wstring>& paths) {
    std::vector<PeakJob> dropped;
    std::lock_guard<std::mutex> lock(mutex);
    for(auto it=pending.begin();it!=pending.end();) {
      if(paths.find(it->path)==paths.end()){dropped.push_back(std::move(*it));it=pending.erase(it);}
      else ++it;
    }
    return dropped;
  }
  std::vector<PeakCompletion> take() {
    std::vector<PeakCompletion> result;
    if(!readyCount.load(std::memory_order_acquire))return result;
    std::lock_guard<std::mutex> lock(mutex);
    result.swap(completed);readyCount.store(0,std::memory_order_relaxed);return result;
  }
  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping=true;pending.clear();wake.notify_all();
    }
    for(auto& thread:threads)if(thread.joinable())thread.join();
    threads.clear();completed.clear();readyCount.store(0,std::memory_order_relaxed);
  }
};
}

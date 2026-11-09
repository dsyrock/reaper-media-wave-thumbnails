#pragma once
#include <cstdint>
#include <string>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <codecvt>
#include <locale>
#endif
namespace mwt {
struct FileStamp {
  uint64_t size{},time{}; // 100ns ticks since 1601, on both platforms
  bool operator==(const FileStamp& b)const{return size==b.size&&time==b.time;}
};
#ifndef _WIN32
inline std::string pathUtf8(const std::wstring& path){return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(path);}
inline std::wstring pathWide(const std::string& path){return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(path);}
inline FileStamp statStamp(const struct stat& s) {
#ifdef __APPLE__
  const auto t=s.st_mtimespec;
#else
  const auto t=s.st_mtim;
#endif
  return {uint64_t(s.st_size),uint64_t(t.tv_sec+11644473600LL)*10000000ULL+uint64_t(t.tv_nsec)/100};
}
#endif
inline bool fileStamp(const std::wstring& path,FileStamp& out) {
#ifdef _WIN32
  WIN32_FILE_ATTRIBUTE_DATA a{};
  if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&a)||(a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
  out={uint64_t(a.nFileSizeHigh)<<32|a.nFileSizeLow,uint64_t(a.ftLastWriteTime.dwHighDateTime)<<32|a.ftLastWriteTime.dwLowDateTime};return true;
#else
  struct stat s{};if(::stat(pathUtf8(path).c_str(),&s)||!S_ISREG(s.st_mode))return false;
  out=statStamp(s);return true;
#endif
}
class ReadOnlyFile {
#ifdef _WIN32
  HANDLE handle=INVALID_HANDLE_VALUE;
#else
  int handle=-1;
#endif
public:
  explicit ReadOnlyFile(const std::wstring& path) {
#ifdef _WIN32
    handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_RANDOM_ACCESS,nullptr);
#else
    handle=::open(pathUtf8(path).c_str(),O_RDONLY|O_CLOEXEC);
#endif
  }
  ~ReadOnlyFile(){
#ifdef _WIN32
    if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
#else
    if(handle>=0)::close(handle);
#endif
  }
  ReadOnlyFile(const ReadOnlyFile&)=delete;
  ReadOnlyFile& operator=(const ReadOnlyFile&)=delete;
  bool snapshot(FileStamp& out)const {
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info{};if(handle==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(handle,&info))return false;
    out={uint64_t(info.nFileSizeHigh)<<32|info.nFileSizeLow,uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32|info.ftLastWriteTime.dwLowDateTime};return true;
#else
    struct stat s{};if(handle<0||::fstat(handle,&s)||!S_ISREG(s.st_mode))return false;out=statStamp(s);return true;
#endif
  }
  bool readAt(uint64_t offset,void* buffer,uint32_t bytes) {
#ifdef _WIN32
    LARGE_INTEGER pos{};pos.QuadPart=static_cast<LONGLONG>(offset);DWORD got=0;
    return SetFilePointerEx(handle,pos,nullptr,FILE_BEGIN)&&ReadFile(handle,buffer,bytes,&got,nullptr)&&got==bytes;
#else
    auto* p=static_cast<unsigned char*>(buffer);uint32_t done=0;
    while(done<bytes){ssize_t n=::pread(handle,p+done,bytes-done,static_cast<off_t>(offset+done));if(n<0&&errno==EINTR)continue;if(n<=0)return false;done+=uint32_t(n);}return true;
#endif
  }
};
}

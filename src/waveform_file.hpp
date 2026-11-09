#pragma once
#include <algorithm>
#include <string>

namespace mwt {
// IsMediaExtension also accepts MIDI and MusicXML. Opening those can invoke
// an importer (including modal errors) before a source can be inspected.
// Exclude them before asking REAPER to open anything, on every UI backend.
inline bool waveformExtension(std::string ext) {
  if(!ext.empty()&&ext.front()=='.')ext.erase(0,1);
  std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return c>='A'&&c<='Z'?char(c-'A'+'a'):char(c);});
  if(ext.empty())return false;
  for(const char* blocked:{"xml","musicxml","mxl","mid","midi","kar","syx","rpp","rpp-bak","rtracktemplate","rfxchain","txt"})
    if(ext==blocked)return false;
  return true; // remaining formats still require IsMediaExtension(ext,false)
}
inline bool waveformPath(const std::string& path) {
  auto dot=path.find_last_of('.'),slash=path.find_last_of("/\\");
  return dot!=std::string::npos&&(slash==std::string::npos||dot>slash)&&waveformExtension(path.substr(dot+1));
}
}

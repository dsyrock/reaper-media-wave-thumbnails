-- Native notification/preview integration test, executed through reaper-test.
-- It verifies position/state readback, not physical mouse gestures or sound.
assert(reaper.GetOS():match('Win'), 'This notification ABI test requires Windows')
local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails/seek-click'
reaper.RecursiveCreateDirectory(root, 0)
local function wav(name, seconds)
  local path = root .. '/' .. name
  local f = assert(io.open(path, 'wb'))
  local rate, frames = 12000, seconds*12000
  f:write('RIFF', string.pack('<I4', 36+frames*2), 'WAVEfmt ',
    string.pack('<I4I2I2I4I4I2I2', 16, 1, 1, rate, rate*2, 2, 16), 'data', string.pack('<I4', frames*2))
  local block = {}; for i=0,rate-1 do block[#block+1] = string.pack('<i2', math.floor(2000*math.sin(i*.1))) end
  for _=1,seconds do f:write(table.concat(block)) end
  f:close(); return path
end
local first = wav('01_30s中文.wav',30)
wav('02_40s.wav',40)
local mx = reaper.OpenMediaExplorer(first,false)
local list = reaper.JS_Window_FindChildByID(mx,1001)
local label = reaper.JS_Window_FindChildByID(mx,1016)
local pathbox = reaper.JS_Window_FindChildByID(mx,1002)
local function status() return reaper.GetExtState('MediaWaveThumbnails','status') end
local function num(s,key) return tonumber(s:match('"'..key..'":([%d%.]+)')) or 0 end
local function bytes(memory,size)
  local a,b = reaper.JS_String(memory,0,size); return type(a)=='string' and a or b
end
local function cell(row,column)
  local memory = assert(reaper.JS_Mem_Alloc(32))
  local address = reaper.JS_Window_AddressFromHandle(memory)
  reaper.JS_Mem_FromString(memory,0,string.pack('<i4i4i4i4',0,column,0,0),16)
  assert(reaper.JS_WindowMessage_Send(list,'0x1038',row,0,address,0)~=0,'GetSubItemRect failed')
  local l,t,r,b = string.unpack('<i4i4i4i4',bytes(memory,16))
  reaper.JS_Mem_Free(memory); return l,t,r,b
end
local function select(row)
  reaper.JS_ListView_SetItemState(list,-1,0,3)
  reaper.JS_ListView_SetItemState(list,row,3,3)
end
local function click(row,fraction,code,columnOverride)
  select(row)
  local column = columnOverride or num(status(),'column')
  local l,t,r,b = cell(row,column)
  local x,y = math.floor(l+6+fraction*(r-l-13)+.5),math.floor((t+b)/2)
  local memory = assert(reaper.JS_Mem_Alloc(80))
  local address = reaper.JS_Window_AddressFromHandle(memory)
  reaper.JS_Mem_FromString(memory,0,string.rep('\0',80),80)
  reaper.JS_Mem_FromString(memory,0,string.pack('<I8I8I4',math.floor(reaper.JS_Window_AddressFromHandle(list)),1001,(code or -2)&0xffffffff),20)
  reaper.JS_Mem_FromString(memory,24,string.pack('<i4i4',row,column),8)
  reaper.JS_Mem_FromString(memory,44,string.pack('<i4i4',x,y),8)
  reaper.JS_WindowMessage_Send(mx,'WM_NOTIFY',1001,0,address,0)
  reaper.JS_Mem_Free(memory)
end
local function position(expected,total)
  local text = reaper.JS_Window_GetTitle(label)
  local m,s,tm,ts = text:match('^(%d+):([%d%.]+)%s*/%s*(%d+):([%d%.]+)')
  assert(m,'Cannot read native position: '..text)
  local actual,length = tonumber(m)*60+tonumber(s),tonumber(tm)*60+tonumber(ts)
  assert(math.abs(length-total)<.1,'Wrong source loaded: '..text)
  assert(math.abs(actual-expected)<.8,'Wrong seek position: '..text..'; expected '..expected)
  print('PASS native position '..text..'; expected '..expected)
end
local phase,at,baseline,requests,originalDirectory = 0,reaper.time_precise()
local items = reaper.CountMediaItems(0)
local function nextPhase(n) phase,at=n,reaper.time_precise() end
local function run()
  local s = status()
  assert(reaper.time_precise()-at<20,'Seek phase '..phase..' timed out: '..s)
  assert(s:find('"error":""',1,true),s)
  assert(s:find('"seek_error":""',1,true),s)
  if phase==0 then
    if num(s,'ready')<2 or not s:find('02_40s.wav',1,true) then reaper.defer(run);return end
    baseline=num(s,'seek_completed');originalDirectory=reaper.JS_Window_GetTitle(pathbox)
    reaper.JS_WindowMessage_Send(mx,'WM_COMMAND',42094,0,0,0)
    reaper.JS_WindowMessage_Send(mx,'WM_COMMAND',42094,0,0,0)
    click(0,.25);nextPhase(1)
  elseif phase==1 and num(s,'seek_completed')==baseline+1 then
    position(7.5,30);baseline=baseline+1
    click(0,.2);click(0,.8);nextPhase(2)
  elseif phase==2 and num(s,'seek_completed')==baseline+1 then
    position(24,30);baseline=baseline+1
    click(1,.75);nextPhase(3)
  elseif phase==3 and num(s,'seek_completed')==baseline+1 then
    position(30,40);baseline=baseline+1
    assert(reaper.JS_Window_GetTitle(pathbox)==originalDirectory,'Seek navigated out of current directory')
    click(0,.1);select(1);nextPhase(4)
  elseif phase==4 and reaper.time_precise()-at>.6 then
    assert(num(s,'seek_completed')==baseline,'Stale queued click acted after selection changed')
    requests=num(s,'seek_requests');click(0,.5,-2,0);nextPhase(5)
  elseif phase==5 and reaper.time_precise()-at>.6 then
    assert(num(s,'seek_requests')==requests,'Filename click unexpectedly sought')
    click(0,.5,-3);nextPhase(6)
  elseif phase==6 and num(s,'seek_completed')==baseline+1 then
    position(15,30);assert(reaper.CountMediaItems(0)==items,'Waveform double-click inserted media')
    reaper.JS_WindowMessage_Send(mx,'WM_COMMAND',1009,0,0,0)
    print('SEEK_CLICK_ALL_PASSED REAPER='..reaper.GetAppVersion());return
  end
  reaper.defer(run)
end
run()

local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails'
local h = reaper.OpenMediaExplorer('', false)
local list = reaper.JS_Window_FindChildByID(h, 1001)
local function message(id, value) return reaper.JS_WindowMessage_Send(list, id, 0, 0, value or 0, 0) end
local previous = {message('0x1000'), message('0x1023'), message('0x1025')}
local function colors()
  message('0x1001', 0x303030); message('0x1024', 0xc0c0c0); message('0x1026', 0x303030)
end
reaper.atexit(function()
  message('0x1001', previous[1]); message('0x1024', previous[2]); message('0x1026', previous[3])
end)
local real
for i = 0, 100 do local f = reaper.EnumerateFiles(root .. '/RealLibrary', i); if not f then break end; if f:lower():match('%.wav$') then real=f;break end end
assert(real)
local task = coroutine.create(function()
  local paths = {root .. '/A/', root .. '/B/', root .. '/RealLibrary/'}
  colors()
  for i = 1, 24 do
    reaper.Main_OnCommand(reaper.NamedCommandLookup('_MWT_REFRESH'), 0)
    reaper.OpenMediaExplorer(paths[(i-1)%3+1], false)
    local untilTime = reaper.time_precise() + .12
    while reaper.time_precise() < untilTime do coroutine.yield() end
  end
  local deadline = reaper.time_precise() + 10
  repeat
    local s = reaper.GetExtState('MediaWaveThumbnails','status')
    assert(reaper.time_precise() < deadline, 'Dark page not ready')
    if s:find('RealLibrary',1,true) and (tonumber(s:match('"published_ready":(%d+)')) or 0)>=20 then break end
    coroutine.yield()
  until false
  local untilTime = reaper.time_precise() + .5
  while reaper.time_precise() < untilTime do coroutine.yield() end
  assert(message('0x1023') == 0xc0c0c0, 'Native text color changed')
  dofile('K:/codex/common/reaper-media-thumbnails/tests/capture.lua')
  print('PASS 24 rapid cold-view switches; native dark text color retained without playback; captured before restoring test colors')
end)
local function run()
  local ok, err = coroutine.resume(task); assert(ok, err)
  if coroutine.status(task) ~= 'dead' then reaper.defer(run) end
end
run()

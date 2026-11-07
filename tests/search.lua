local h = reaper.OpenMediaExplorer('', false)
local combo = reaper.JS_Window_FindChildByID(h, 1015)
assert(combo)
local original = reaper.JS_Window_GetTitle(combo)
local function setFilter(text)
  reaper.JS_Window_SetTitle(combo, text)
  reaper.JS_WindowMessage_Send(h, 'WM_COMMAND', 1015, 5, reaper.JS_Window_AddressFromHandle(combo), 0)
end
reaper.atexit(function() setFilter(original) end)
local phase, started = 1, reaper.time_precise()
setFilter('stereo')
local function run()
  local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(reaper.time_precise() - started < 15, 'Search timeout: ' .. s)
  if phase == 1 and s:find('"visible_files":1', 1, true) and s:find('"ready":1', 1, true) then
    assert(s:find('02_stereo.wav', 1, true), 'Filtered waveform belongs to wrong file')
    print('PASS database search: filtered row retains correct waveform')
    phase, started = 2, reaper.time_precise()
    setFilter('')
  elseif phase == 2 and s:find('"visible_files":3', 1, true) and s:find('"ready":3', 1, true) then
    print('PASS clearing search restores all database waveforms')
    return
  end
  reaper.defer(run)
end
run()

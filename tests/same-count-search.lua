local h = reaper.OpenMediaExplorer('', false)
local combo = reaper.JS_Window_FindChildByID(h, 1015)
local original = reaper.JS_Window_GetTitle(combo)
local function filter(text)
  reaper.JS_Window_SetTitle(combo, text)
  reaper.JS_WindowMessage_Send(h, 'WM_COMMAND', 1015, 5, reaper.JS_Window_AddressFromHandle(combo), 0)
end
reaper.atexit(function() filter(original) end)
reaper.OpenMediaExplorer(reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails/A/04_loop.wav', false)
local phase, deadline = 1, reaper.time_precise() + 10
filter('04_loop')
local function run()
  local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(reaper.time_precise() < deadline, 'Same-count search failed: ' .. s)
  if phase == 1 and s:find('"visible_files":1', 1, true) and s:find('04_loop.wav', 1, true) then
    phase, deadline = 2, reaper.time_precise() + 10; filter('05_loop')
  elseif phase == 2 and s:find('"visible_files":1', 1, true) and s:find('05_loop.wav', 1, true) and s:find('"published_ready":1', 1, true) then
    local peak = tonumber(s:match('05_loop%.wav","state":1,"peak":([%d%.]+)'))
    assert(peak and math.abs(peak - .1) < .015, 'Wrong waveform after same-count replacement')
    print('PASS search replacing one result with a different one-result set updates the waveform without polling native rows')
    return
  end
  reaper.defer(run)
end
run()

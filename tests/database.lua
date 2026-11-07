local started = reaper.time_precise()
local full = reaper.GetToggleCommandStateEx(32063, 42026)
local leading = reaper.GetToggleCommandStateEx(32063, 42134)
local refreshed = false
local function run()
  local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(reaper.time_precise() - started < 15, 'Database timeout: ' .. s)
  if s:find('"directory":"MediaWaveThumbnails test"', 1, true) and s:find('"ready":3', 1, true) then
    assert(reaper.GetToggleCommandStateEx(32063, 42026) == full, 'Full-path preference changed')
    assert(reaper.GetToggleCommandStateEx(32063, 42134) == leading, 'Leading-path preference changed')
    local peaks = {}
    for value in s:gmatch('"peak":([%d%.]+)') do peaks[#peaks + 1] = tonumber(value) end
    table.sort(peaks)
    assert(#peaks == 3 and math.abs(peaks[1] - .2) < .015 and math.abs(peaks[2] - .5) < .015 and math.abs(peaks[3] - .8) < .015, s)
    if not refreshed then
      refreshed = true
      reaper.Main_OnCommand(reaper.NamedCommandLookup('_MWT_REFRESH'), 0)
    else
      print('PASS native database: 3 rows, duplicate filenames resolve to distinct peaks, display preferences restored')
      return
    end
  end
  reaper.defer(run)
end
run()

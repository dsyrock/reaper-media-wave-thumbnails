local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails'
local function status() return reaper.GetExtState('MediaWaveThumbnails', 'status') end
local function number(s, name) return tonumber(s:match('"' .. name .. '":([%d%.]+)')) or 0 end
local function files(s)
  local out = {}
  for row, path, state, peak in s:gmatch('{"row":(%d+),"path":"(.-)","state":(-?%d+),"peak":([%deE%.+-]+)}') do
    out[#out + 1] = {row = tonumber(row), path = path, state = tonumber(state), peak = tonumber(peak)}
  end
  return out
end
local function handles()
  local h = reaper.OpenMediaExplorer('', false)
  local list = reaper.JS_Window_FindChildByID(h, 1001)
  local header = reaper.JS_Window_FindChildByID(list, 0)
  return h, list, header
end
local function columns()
  local _, _, header = handles()
  return reaper.JS_WindowMessage_Send(header, '0x1200', 0, 0, 0, 0)
end
local toggle = reaper.NamedCommandLookup('_MWT_TOGGLE')
assert(toggle > 0, 'Plugin did not register')
if reaper.GetToggleCommandState(toggle) == 0 then reaper.Main_OnCommand(toggle, 0) end
reaper.OpenMediaExplorer(root .. '/A/01_鼓点中文.wav', false)
local phase, started = 1, reaper.time_precise()
local baseColumns, totalBuilds
local function advance(n) phase, started = n, reaper.time_precise() end
local function run()
  local s = status()
  assert(reaper.time_precise() - started < 25, 'Phase ' .. phase .. ' timed out: ' .. s)
  assert(s:find('"error":""', 1, true), s)
  local rows = files(s)
  if phase == 1 and number(s, 'ready') >= 20 then
    local expected = {['01_鼓点中文.wav'] = .8, ['02_stereo.wav'] = .5, ['03_silence.wav'] = 0}
    for _, file in ipairs(rows) do
      for name, amplitude in pairs(expected) do
        if file.path:find(name, 1, true) then
          assert(file.state == 1 and math.abs(file.peak - amplitude) < .015, name .. ': ' .. file.peak)
          expected[name] = nil
        end
      end
    end
    assert(next(expected) == nil, 'Expected first files were not visible')
    baseColumns = columns()
    print('PASS multiple rows, Unicode, mono/stereo, silence; columns=' .. baseColumns)
    local _, list = handles()
    reaper.JS_ListView_EnsureVisible(list, 40, false)
    advance(2)
  elseif phase == 2 then
    local tail, corrupt = false, false
    for _, file in ipairs(rows) do
      if file.path:find('40_loop.wav', 1, true) and file.state == 1 then tail = true end
      if file.path:find('41_corrupt.wav', 1, true) and file.state == -1 then corrupt = true end
    end
    if tail and corrupt then
      print('PASS lazy generation after scroll, corrupt-file isolation')
      reaper.OpenMediaExplorer(root .. '/B/01_鼓点中文.wav', false)
      advance(3)
    end
  elseif phase == 3 and #rows == 1 and rows[1].state == 1 and rows[1].path:find('B', 1, true) then
    assert(math.abs(rows[1].peak - .2) < .015, 'Same-name source mixed up: ' .. s)
    print('PASS same filename in another folder: peak=' .. rows[1].peak)
    reaper.Main_OnCommand(toggle, 0)
    assert(columns() == baseColumns - 1, 'Own column was not removed')
    assert(status():find('"attached":false', 1, true), 'Still subclassed')
    for _ = 1, 3 do
      reaper.Main_OnCommand(toggle, 0)
      reaper.Main_OnCommand(toggle, 0)
    end
    assert(columns() == baseColumns - 1, 'Repeated toggle changed native columns')
    reaper.Main_OnCommand(toggle, 0)
    advance(4)
  elseif phase == 4 and s:find('"attached":true', 1, true) then
    assert(columns() == baseColumns, 'Duplicate or missing column after reattach')
    print('PASS disable restores native columns; repeated enable has no duplicate')
    totalBuilds = number(s, 'builds')
    reaper.OpenMediaExplorer(root .. '/A/01_鼓点中文.wav', false)
    advance(5)
  elseif phase == 5 and number(s, 'ready') >= 20 and rows[1] and rows[1].path:find('A', 1, true) then
    assert(number(s, 'builds') == totalBuilds, 'Known waveforms were rebuilt')
    print('PASS revisiting folder reuses waveform cache')
    print('ALL_REGRESSIONS_PASSED')
    return
  end
  reaper.defer(run)
end
run()

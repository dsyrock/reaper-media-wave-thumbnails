local helpers = dofile('K:/codex/common/reaper-media-thumbnails/tests/column-helpers.lua')
local start = reaper.time_precise()
local function run()
  local status = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(reaper.time_precise() - start < 8, 'Startup column restore timed out')
  if not status:find('"attached":true', 1, true) then reaper.defer(run); return end
  local first, filePosition, wavePosition
  for i, c in ipairs(helpers.snapshot()) do
    if i == 1 then first = c end
    if c.title == 'File' then filePosition = i end
    if c.title == 'Waveform' then wavePosition = i end
  end
  assert(first.title == 'Mark' and first.width == 56, 'Saved Mark-first layout/width was lost on startup')
  assert(wavePosition == filePosition + 1, 'Waveform does not follow File after startup')
  print('PASS saved Temporary Mark-first layout restored on startup; Mark width=56')
end
run()

local helpers = dofile('K:/codex/common/reaper-media-thumbnails/tests/column-helpers.lua')
local start = reaper.time_precise()
local function run()
  local status = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(reaper.time_precise() - start < 8, 'Startup column restore timed out')
  if not status:find('"attached":true', 1, true) then reaper.defer(run); return end
  local first, filePosition, wavePosition
  for i, c in ipairs(helpers.snapshot()) do
    if not first and c.title ~= 'Waveform' then first = c end
    if c.title == 'File' then filePosition = i end
    if c.title == 'Waveform' then wavePosition = i end
  end
  assert(first.title == 'Mark' and first.width == 56, 'Saved Mark-first layout/width was lost on startup')
  local saved = tonumber(reaper.GetExtState('MediaWaveThumbnails', 'wave_order'))
  assert(wavePosition == (saved and saved + 1 or filePosition + 1), 'Saved Waveform position was lost on startup')
  print('PASS saved Temporary Mark-first layout restored on startup; Mark width=56')
end
run()

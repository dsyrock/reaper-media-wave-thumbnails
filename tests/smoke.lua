local start = reaper.time_precise()
local function status() return reaper.GetExtState('MediaWaveThumbnails', 'status') end
local function run()
  local s = status()
  local ready = tonumber(s:match('"ready":(%d+)')) or 0
  if ready >= 3 then
    assert(s:find('"attached":true', 1, true), s)
    assert(s:find('鼓点中文', 1, true), 'Unicode file missing: ' .. s)
    assert(tonumber(s:match('"draws":(%d+)')) > 0, 'No waveform custom draw: ' .. s)
    print('MULTI_ROW_OK ' .. s)
    return
  end
  assert(reaper.time_precise() - start < 20, 'No ready multi-row waveforms: ' .. s)
  reaper.defer(run)
end
run()

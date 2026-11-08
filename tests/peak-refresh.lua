local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails/Refresh-' .. math.floor(reaper.time_precise() * 1000)
reaper.RecursiveCreateDirectory(root, 0)
local function writeWave(name, amplitude)
  local data, rate = {}, 12000
  for i = 0, rate - 1 do data[#data + 1] = string.pack('<i2', math.floor(amplitude * math.sin(i * 2 * math.pi * 220 / rate) * 32767)) end
  data = table.concat(data)
  local file = assert(io.open(root .. '/' .. name, 'wb'))
  file:write('RIFF', string.pack('<I4', #data + 36), 'WAVEfmt ',
    string.pack('<I4I2I2I4I4I2I2', 16, 1, 1, rate, rate * 2, 2, 16), 'data', string.pack('<I4', #data), data)
  file:close()
end
writeWave('Keep.wav', .8); writeWave('Changed.wav', .8)
reaper.OpenMediaExplorer(root .. '/Keep.wav', false)
local function n(s, key) return tonumber(s:match('"' .. key .. '":(%d+)')) or 0 end
local function peak(s, name) return tonumber(s:match(name .. '%.wav","state":1,"peak":([%d%.]+)')) end
local task = coroutine.create(function()
  local deadline = reaper.time_precise() + 10
  local s
  repeat
    s = reaper.GetExtState('MediaWaveThumbnails', 'status')
    assert(reaper.time_precise() < deadline, 'Fresh files not ready: ' .. s)
    coroutine.yield()
  until peak(s, 'Changed') and peak(s, 'Keep')
  local opens = n(s, 'source_opens')
  local untilTime = reaper.time_precise() + 1.1
  while reaper.time_precise() < untilTime do coroutine.yield() end
  writeWave('Changed.wav', .2)
  deadline = reaper.time_precise() + 10
  repeat
    s = reaper.GetExtState('MediaWaveThumbnails', 'status')
    assert(reaper.time_precise() < deadline, 'Stale peak data after source modification: ' .. s)
    coroutine.yield()
  until peak(s, 'Changed') and math.abs(peak(s, 'Changed') - .2) < .015
  assert(math.abs(peak(s, 'Keep') - .8) < .015)
  assert(n(s, 'source_opens') == opens + 1, 'Unchanged source was reopened')
  print('PASS direct peak reads stay fresh after same-size file modification; only changed source reopened')
end)
local function run()
  local ok, err = coroutine.resume(task); assert(ok, err)
  if coroutine.status(task) ~= 'dead' then reaper.defer(run) end
end
run()

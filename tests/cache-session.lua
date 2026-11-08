-- Regression for the old 512-entry limit and the independent bitmap LRU.
local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails'
local folder = root .. '/CacheSession-' .. math.floor(reaper.time_precise() * 1000)
reaper.RecursiveCreateDirectory(folder, 0)
local function wav(amplitude)
  local samples, rate, frames = {}, 8000, 2048
  for i = 0, frames - 1 do
    local value = amplitude * (.2 + .8 * i / frames) * math.sin(i * 2 * math.pi * 440 / rate)
    samples[#samples + 1] = string.pack('<i2', math.floor(value * 32767))
  end
  local data = table.concat(samples)
  return 'RIFF' .. string.pack('<I4', 36 + #data) .. 'WAVEfmt ' ..
    string.pack('<I4I2I2I4I4I2I2', 16, 1, 1, rate, rate * 2, 2, 16) ..
    'data' .. string.pack('<I4', #data) .. data
end
local function write(path, bytes)
  local file = assert(io.open(path, 'wb')); file:write(bytes); file:close()
end
local bytes = wav(.8)
for i = 1, 640 do write(folder .. '/' .. string.format('%04d.wav', i), bytes) end

local function num(s, key) return tonumber(s:match('"' .. key .. '":([%d%.]+)')) or 0 end
local function status()
  local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(s:match('"version":"0%.2%.%d+"'), 'Wrong test DLL: ' .. s)
  assert(s:find('"error":""', 1, true), s)
  return s
end
local function ready(s) return num(s, 'visible_files') > 0 and num(s, 'visible_files') == num(s, 'ready') end
local function await(predicate, seconds, label)
  local deadline = reaper.time_precise() + seconds
  while true do
    local s = status()
    if predicate(s) then return s end
    assert(reaper.time_precise() < deadline, label .. ': ' .. s)
    coroutine.yield()
  end
end
local function pause(seconds)
  local deadline = reaper.time_precise() + seconds
  while reaper.time_precise() < deadline do coroutine.yield() end
end
local h = reaper.OpenMediaExplorer('', false)
local list = reaper.JS_Window_FindChildByID(h, 1001)
local oldWidth, ownColumn
reaper.atexit(function()
  if oldWidth then reaper.JS_WindowMessage_Send(list, '0x101e', ownColumn, 0, oldWidth, 0) end
end)
local task = coroutine.create(function()
  reaper.Main_OnCommand(reaper.NamedCommandLookup('_MWT_REFRESH'), 0)
  reaper.OpenMediaExplorer(folder .. '/0001.wav', false)
  local seen, visited, highest = {}, 0, 0
  local page = reaper.JS_WindowMessage_Send(list, '0x1028', 0, 0, 0, 0)
  assert(page >= 10, 'Test viewport is too short')
  while visited < 640 do
    local s = await(function(value)
      return value:find('CacheSession-', 1, true) and ready(value) and
        (highest == 0 or value:find(string.format('%04d.wav', highest), 1, true))
    end, 15, 'Scrolling warm-up')
    for file in s:gmatch('"path":".-(%d%d%d%d)%.wav"') do
      local n = tonumber(file)
      if not seen[n] then seen[n], visited = true, visited + 1 end
    end
    if visited < 640 then
      local nextFile = 1
      while seen[nextFile] do nextFile = nextFile + 1 end
      highest = math.min(640, nextFile + page - 2)
      reaper.JS_ListView_EnsureVisible(list, math.min(highest, reaper.JS_ListView_GetItemCount(list) - 1), false)
      coroutine.yield()
    end
  end
  pause(.5)
  local s = status()
  assert(num(s, 'cache') >= 640, 'Peak cache still evicts old entries')
  assert(num(s, 'bitmap_cache') <= 256 and num(s, 'bitmap_bytes') <= 16 * 1024 * 1024, 'Bitmap cache exceeded budget')
  assert(num(s, 'bitmap_evictions') > 0, 'Test never exercised bitmap eviction')
  print('PASS visited 640 files; retained=' .. num(s, 'cache') ..
    '; peak_array_bytes=' .. num(s, 'peak_array_bytes') .. '; bitmap_evictions=' .. num(s, 'bitmap_evictions'))

  reaper.OpenMediaExplorer(root .. '/A/01_鼓点中文.wav', false)
  await(function(value) return value:find('鼓点中文', 1, true) and ready(value) end, 15, 'Visit another folder')
  pause(.4)
  s = status()
  local opens, reads, builds = num(s, 'source_opens'), num(s, 'peak_reads'), num(s, 'builds')
  reaper.OpenMediaExplorer(folder .. '/0001.wav', false)
  s = await(function(value) return value:find('0001.wav', 1, true) and ready(value) end, 10, 'Restore first page')
  assert(num(s, 'source_opens') == opens and num(s, 'peak_reads') == reads and num(s, 'builds') == builds,
    'Returning past the old 512 limit reopened or reread sources: ' .. s)
  assert(num(s, 'restore_latency_ms') < 1000, 'Warm row restoration stalled: ' .. s)
  print('PASS revisit earliest files: source opens +0, peak reads +0; row_restore_ms=' .. num(s, 'restore_latency_ms'))

  pause(.5)
  s = status()
  local surfaces, hits = num(s, 'bitmap_builds'), num(s, 'bitmap_hits')
  assert(reaper.JS_WindowMessage_Send(list, '0x1015', 0, 0, reaper.JS_ListView_GetItemCount(list) - 1, 0) ~= 0,
    'Native list redraw failed')
  s = await(function(value) return num(value, 'bitmap_hits') > hits end, 5, 'Repaint bitmap hit')
  assert(num(s, 'bitmap_builds') == surfaces, 'Ordinary repaint rerasterized cached waves')
  assert(num(s, 'peak_reads') == reads, 'Ordinary repaint reread peaks')
  print('PASS ordinary repaint blits cached bitmaps without rerasterizing or rereading peaks')

  ownColumn = num(s, 'column')
  oldWidth = reaper.JS_WindowMessage_Send(list, '0x101d', ownColumn, 0, 0, 0)
  reaper.JS_WindowMessage_Send(list, '0x101e', ownColumn, 0, oldWidth + 37, 0)
  s = await(function(value) return num(value, 'bitmap_builds') > surfaces end, 5, 'Resize bitmap variant')
  assert(num(s, 'peak_reads') == reads, 'Resizing reread peaks')
  print('PASS resizing builds a new bitmap variant using existing peak arrays')

  -- Same file size, changed modification time and waveform: refresh only it.
  write(folder .. '/0002.wav', wav(.2))
  s = await(function(value)
    local peak = value:match('0002%.wav","state":1,"peak":([%d%.]+)')
    return peak and math.abs(tonumber(peak) - .2) < .015
  end, 10, 'Changed file invalidation')
  assert(num(s, 'peak_reads') == reads + 1, 'Changed-file reload was not isolated')
  print('PASS file modification invalidates its peak array and bitmap; other files are reused')
  print('CACHE_SESSION_ALL_PASSED')
end)
local function run()
  local ok, err = coroutine.resume(task)
  assert(ok, err)
  if coroutine.status(task) ~= 'dead' then reaper.defer(run) end
end
run()

local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails'
local function status() return reaper.GetExtState('MediaWaveThumbnails', 'status') end
local function n(s, key) return tonumber(s:match('"' .. key .. '":([%d%.]+)')) or 0 end
local function pause(seconds)
  local deadline = reaper.time_precise() + seconds
  while reaper.time_precise() < deadline do coroutine.yield() end
end
local function await(predicate, label)
  local deadline = reaper.time_precise() + 10
  while true do
    local s = status()
    if predicate(s) then return s end
    assert(reaper.time_precise() < deadline, label .. ': ' .. s)
    coroutine.yield()
  end
end
local function ready(s, folder)
  return s:find(folder, 1, true) and n(s, 'published_ready') >= 20 and
    n(s, 'published_ready') == n(s, 'visible_files') and not s:find('"first_draw_ready":-1', 1, true)
end
local keys = {'cpu_timer_ms','cpu_paint_ms','cpu_raster_ms','cpu_scan_ms','cpu_status_ms',
  'cpu_timer_cycles','cpu_paint_cycles','row_scans','file_stats','text_queries','status_writes',
  'draws','bitmap_builds','bitmap_hits','source_opens','direct_peak_reads'}
local function delta(before, after)
  local parts = {}
  for _, key in ipairs(keys) do parts[#parts + 1] = '"' .. key .. '":' .. (n(after, key) - n(before, key)) end
  return '{' .. table.concat(parts, ',') .. '}'
end
local helpers = dofile('K:/codex/common/reaper-media-thumbnails/tests/column-helpers.lua')
local _, list = helpers.handles()
local real
for i = 0, 100 do
  local file = reaper.EnumerateFiles(root .. '/RealLibrary', i)
  if not file then break end
  if file:lower():match('%.wav$') then real = file; break end
end
assert(real, 'Missing real-library corpus')
local task = coroutine.create(function()
  assert(status():find('"profile_enabled":true', 1, true), 'Build with -Profile before benchmarking')
  reaper.OpenMediaExplorer(root .. '/A', false); pause(.3)
  reaper.OpenMediaExplorer(root .. '/A/01_鼓点中文.wav', false)
  await(function(s) return ready(s, '鼓点中文') end, 'Prime A')
  reaper.OpenMediaExplorer(root .. '/RealLibrary/' .. real, false)
  await(function(s) return ready(s, 'RealLibrary') end, 'Prime real library'); pause(.6)
  local results = {}
  local before = status(); pause(3.3)
  results[#results + 1] = '"idle":' .. delta(before, status())
  before = status()
  for _ = 1, 25 do
    local draws = n(status(), 'draws')
    reaper.JS_WindowMessage_Send(list, '0x1015', 0, 0, reaper.JS_ListView_GetItemCount(list) - 1, 0)
    await(function(s) return n(s, 'draws') > draws end, 'Repaint')
  end
  results[#results + 1] = '"repaint":' .. delta(before, status())
  before = status()
  for _ = 1, 25 do
    local builds = n(status(), 'bitmap_builds')
    reaper.SetExtState('MediaWaveThumbnails', 'profile_command', 'drop_bitmaps', false)
    await(function(s) return n(s, 'bitmap_builds') > builds end, 'Rasterize')
  end
  results[#results + 1] = '"raster":' .. delta(before, status())
  before = status()
  for _ = 1, 8 do
    reaper.OpenMediaExplorer(root .. '/A/01_鼓点中文.wav', false)
    await(function(s) return ready(s, '鼓点中文') end, 'Navigate A')
    reaper.OpenMediaExplorer(root .. '/RealLibrary/' .. real, false)
    await(function(s) return ready(s, 'RealLibrary') end, 'Navigate real')
  end
  results[#results + 1] = '"navigate":' .. delta(before, status())
  local output = '{' .. table.concat(results, ',') .. '}'
  reaper.SetExtState('MediaWaveThumbnails', 'cpu_benchmark', output, false)
  print('CPU_BENCHMARK ' .. output)
end)
local function run()
  local ok, err = coroutine.resume(task); assert(ok, err)
  if coroutine.status(task) ~= 'dead' then reaper.defer(run) end
end
run()

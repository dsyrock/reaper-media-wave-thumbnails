local base = 'K:/codex/common/reaper-media-thumbnails/tests/'
local helpers = dofile(base .. 'column-helpers.lua')
local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails'
-- When a database already contains this file, OpenMediaExplorer selects its
-- database row rather than changing folders. Leave that view first.
reaper.OpenMediaExplorer(root .. '/A', false)
local task = coroutine.create(function()
  local deadline = reaper.time_precise() + 10
  local h = reaper.OpenMediaExplorer('', false)
  local path = reaper.JS_Window_FindChildByID(h, 1002)
  while reaper.JS_Window_GetTitle(path):find('MediaWaveThumbnails test', 1, true) do
    assert(reaper.time_precise() < deadline, 'Could not leave test database')
    coroutine.yield()
  end
  reaper.OpenMediaExplorer(root .. '/A/01_鼓点中文.wav', false)
  deadline = reaper.time_precise() + 10
  while true do
    local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
    local ready = tonumber(s:match('"ready":(%d+)')) or 0
    if s:find('鼓点中文', 1, true) and ready >= 20 then break end
    assert(reaper.time_precise() < deadline, 'Fixture folder not ready: ' .. s)
    coroutine.yield()
  end
  local native, order, own = {}, {}, nil
  for _, c in ipairs(helpers.snapshot()) do
    if c.title == 'Waveform' then own = c.column else native[#native + 1] = c end
  end
  assert(own, 'Waveform not attached')
  order[1] = own
  for _, c in ipairs(native) do order[#order + 1] = c.column end
  helpers.setOrder(order)
  local _, list = helpers.handles()
  reaper.JS_WindowMessage_Send(list, '0x101e', own, 0, 317, 0)
  deadline = reaper.time_precise() + 5
  while reaper.GetExtState('MediaWaveThumbnails', 'wave_order') ~= '0' or
      reaper.GetExtState('MediaWaveThumbnails', 'wave_width') ~= '317' do
    assert(reaper.time_precise() < deadline, 'Live Waveform layout was not saved')
    coroutine.yield()
  end
  local file = assert(io.open(root .. '/restart-native-layout.lua', 'wb'))
  file:write('return {\n')
  for _, c in ipairs(native) do
    file:write(string.format('{column=%d,title=%q,width=%d},\n', c.column, c.title, c.width))
  end
  file:write('}\n'); file:close()
  local first = helpers.snapshot()[1]
  assert(first.title == 'Waveform' and first.width == 317)
  print('PASS Waveform moved to first column, width 317; persisted own layout; native layout recorded')
end)
local function run()
  local ok, err = coroutine.resume(task); assert(ok, err)
  if coroutine.status(task) ~= 'dead' then reaper.defer(run) end
end
run()

local base = 'K:/codex/common/reaper-media-thumbnails/tests/'
local columns = dofile(base .. 'column-helpers.lua')
local toggle = reaper.NamedCommandLookup('_MWT_TOGGLE')
local function pause(seconds)
  local deadline = reaper.time_precise() + seconds
  while reaper.time_precise() < deadline do coroutine.yield() end
end
local function waitAttached()
  local deadline = reaper.time_precise() + 5
  while true do
    local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
    if s:find('"attached":true', 1, true) then return end
    assert(reaper.time_precise() < deadline, 'Waveform failed to reattach: ' .. s)
    coroutine.yield()
  end
end
local function snapshotNative()
  local native = {}
  for _, c in ipairs(columns.snapshot()) do if c.title ~= 'Waveform' then native[#native + 1] = c end end
  return native
end
local function orderOf(layout)
  local order = {}; for _, c in ipairs(layout) do order[#order + 1] = c.column end; return order
end
local function verify(expected)
  local actual = columns.snapshot()
  assert(#actual == #expected + 1, 'Native column removed or Waveform duplicated')
  local native = snapshotNative()
  for i, c in ipairs(expected) do
    assert(native[i].column == c.column and native[i].title == c.title and native[i].width == c.width,
      'Native layout changed at position ' .. i .. ': ' .. native[i].title)
  end
  assert(native[1].title == 'Mark' and native[1].width > 0, 'Temporary Mark not visible at far left')
  local filePosition, wavePosition
  for i, c in ipairs(actual) do
    if c.title == 'File' then filePosition = i end
    if c.title == 'Waveform' then wavePosition = i end
  end
  assert(wavePosition == filePosition + 1, 'Waveform inserted before native File column')
  return actual
end
local task = coroutine.create(function()
  if reaper.GetToggleCommandState(toggle) == 1 then reaper.Main_OnCommand(toggle, 0) end
  local original = snapshotNative()
  local order, mark, favorite = {}, nil, nil
  for _, c in ipairs(original) do
    if c.title == 'Mark' then mark = c.column end
    if c.title == 'Fav' then favorite = c.column end
  end
  assert(mark and favorite, 'Test REAPER does not contain Mark/Fav')
  order = {mark, favorite}
  for _, c in ipairs(original) do if c.column ~= mark and c.column ~= favorite then order[#order + 1] = c.column end end
  columns.setOrder(order)
  local _, list = columns.handles()
  reaper.JS_WindowMessage_Send(list, '0x101e', mark, 0, 56, 0)
  local expected = snapshotNative()
  reaper.Main_OnCommand(toggle, 0); waitAttached()
  verify(expected)
  print('PASS Mark first, Fav second; all native titles, widths and relative order preserved')

  -- This is a NATIVE 25-column order, without the injected Waveform index.
  -- Version 0.2.0 rejects it because its live control has 26 columns.
  local actual = columns.snapshot()
  for _, c in ipairs(actual) do if c.title == 'Waveform' then reaper.JS_WindowMessage_Send(list, '0x101e', c.column, 0, 233, 0) end end
  columns.setOrder(orderOf(expected))
  pause(.4); waitAttached()
  actual = verify(expected)
  for _, c in ipairs(actual) do if c.title == 'Waveform' then assert(c.width == 233, 'Waveform width lost during native layout application') end end
  print('PASS native 25-column layout accepted with patch enabled; Mark remains first')

  local last = expected[#expected]
  assert(last.title == 'Image', 'Unexpected test column schema')
  assert(reaper.JS_WindowMessage_Send(list, '0x101c', last.column, 0, 0, 0) ~= 0, 'Delete native column failed')
  columns.insert(last.column, last.title, last.width)
  columns.setOrder(orderOf(expected))
  pause(.4); waitAttached(); verify(expected)
  print('PASS native column delete/reinsert: self column identified safely; Mark and all native columns preserved')

  for _ = 1, 3 do
    reaper.Main_OnCommand(toggle, 0)
    local native = snapshotNative()
    assert(#native == #expected, 'Disable removed native columns')
    for i, c in ipairs(expected) do assert(native[i].column == c.column and native[i].width == c.width) end
    reaper.Main_OnCommand(toggle, 0); waitAttached(); verify(expected)
  end
  print('PASS repeated enable/disable preserves Mark and the complete native layout')
  print('COLUMN_REGRESSIONS_PASSED')
end)
local function run()
  local ok, err = coroutine.resume(task); assert(ok, err)
  if coroutine.status(task) ~= 'dead' then reaper.defer(run) end
end
run()

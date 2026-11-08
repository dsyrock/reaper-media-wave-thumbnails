local function n(s, key) return tonumber(s:match('"' .. key .. '":([%d%.]+)')) or 0 end
local task = coroutine.create(function()
  local deadline = reaper.time_precise() + 10
  repeat
    local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
    assert(reaper.time_precise() < deadline, 'Page not settled')
    if n(s, 'published_ready') >= 20 and n(s, 'published_ready') == n(s, 'visible_files') then break end
    coroutine.yield()
  until false
  local settle = reaper.time_precise() + 1.2
  while reaper.time_precise() < settle do coroutine.yield() end
  local before = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(not before:find('"profile_enabled":true', 1, true), 'Use a release build')
  local untilTime = reaper.time_precise() + 2.4
  while reaper.time_precise() < untilTime do coroutine.yield() end
  local after = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(n(before, 'row_scans') == n(after, 'row_scans'), 'Idle page rescanned its native text')
  assert(n(before, 'status_writes') == n(after, 'status_writes'), 'Idle diagnostics kept rewriting unchanged status')
  assert(n(before, 'bitmap_builds') == n(after, 'bitmap_builds'), 'Idle bitmaps were regenerated')
  print('PASS release idle: no native row rescan, no unchanged diagnostic rewrite, no bitmap regeneration')
end)
local function run()
  local ok, err = coroutine.resume(task); assert(ok, err)
  if coroutine.status(task) ~= 'dead' then reaper.defer(run) end
end
run()

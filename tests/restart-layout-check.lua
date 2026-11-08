local helpers = dofile('K:/codex/common/reaper-media-thumbnails/tests/column-helpers.lua')
local expected = dofile(reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails/restart-native-layout.lua')
local deadline = reaper.time_precise() + 10
local function n(s, key) return tonumber(s:match('"' .. key .. '":(%d+)')) or 0 end
local function run()
  local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(s:find('"version":"0.3.0"', 1, true), 'Wrong test DLL: ' .. s)
  if n(s, 'ready') < 20 or n(s, 'ready') ~= n(s, 'visible_files') or s:find('"first_draw_ready":-1', 1, true) then
    assert(reaper.time_precise() < deadline, 'Restart restoration timed out: ' .. s)
    reaper.defer(run); return
  end
  local columns, native = helpers.snapshot(), {}
  assert(columns[1].title == 'Waveform', 'Waveform returned to its old position after restart')
  assert(columns[1].width == 317, 'Waveform width did not survive restart')
  for _, c in ipairs(columns) do if c.title ~= 'Waveform' then native[#native + 1] = c end end
  assert(#native == #expected)
  for i, c in ipairs(expected) do
    assert(c.column == native[i].column and c.title == native[i].title and c.width == native[i].width,
      'Native layout changed after restart at ' .. i)
  end
  assert(n(s, 'peak_build_starts') == 0 and n(s, 'peak_build_probes') == 0,
    'Existing peak files unnecessarily entered the peak-builder path: ' .. s)
  assert(n(s, 'direct_peak_reads') >= n(s, 'ready'), 'Ready sources did not use the cache-file reader')
  assert(n(s, 'source_opens') == 0 and n(s, 'peak_reads') == 0, 'Audio decoder/API path still used for cached files')
  assert(n(s, 'published_ready') == n(s, 'visible_files') and n(s, 'first_draw_ready') == n(s, 'visible_files'),
    'First waveform frame contained only part of the visible page: ' .. s)
  print('PASS real process restart: Waveform stays first with width 317; all native columns preserved')
  print('PASS existing peaks read directly: ready=' .. n(s, 'ready') ..
    ', cache_file_reads=' .. n(s, 'direct_peak_reads') .. ', source_opens=0, API_peak_reads=0, first_frame=' .. n(s, 'first_draw_ready'))
  print('RESTART_REGRESSION_PASSED')
end
run()

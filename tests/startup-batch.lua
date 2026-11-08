local deadline = reaper.time_precise() + 15
local function n(s, key) return tonumber(s:match('"' .. key .. '":(%d+)')) or 0 end
local function run()
  local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(s:match('"version":"0%.3%.%d+"'), 'Wrong DLL')
  assert(reaper.time_precise() < deadline, 'Startup page timeout: ' .. s)
  if not s:find('RealLibrary', 1, true) or n(s, 'published_ready') < 20 or
      n(s, 'published_ready') ~= n(s, 'visible_files') or s:find('"first_draw_ready":-1', 1, true) then
    reaper.defer(run); return
  end
  assert(n(s, 'source_opens') == 0 and n(s, 'peak_reads') == 0 and n(s, 'peak_build_starts') == 0, s)
  assert(n(s, 'direct_peak_reads') == n(s, 'visible_files'), s)
  assert(n(s, 'first_draw_ready') == n(s, 'visible_files'), 'First waveform paint was partial: ' .. s)
  assert(n(s, 'page_publications') == 1, 'Startup waveforms were published in multiple batches: ' .. s)
  local helpers = dofile('K:/codex/common/reaper-media-thumbnails/tests/column-helpers.lua')
  local first = helpers.snapshot()[1]
  assert(first.title == 'Waveform' and first.width == 317, 'Saved layout did not survive restart')
  print('PASS real restart with real-library caches: first_frame=' .. n(s, 'first_draw_ready') ..
    ', publications=1, decoder_opens=0, source_peak_API=0, PAGE_PREP_MS=' .. n(s, 'page_ready_ms'))
  print('PASS Waveform first, width 317 after actual process restart')
end
run()

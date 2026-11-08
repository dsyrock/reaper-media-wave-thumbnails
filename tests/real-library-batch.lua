local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails/RealLibrary'
local first
for i = 0, 100 do
  local file = reaper.EnumerateFiles(root, i)
  if not file then break end
  if file:lower():match('%.wav$') then first = file; break end
end
assert(first, 'Real-library corpus missing')
local helpers = dofile('K:/codex/common/reaper-media-thumbnails/tests/column-helpers.lua')
local _, list = helpers.handles()
for _, c in ipairs(helpers.snapshot()) do
  if c.title == 'Waveform' then reaper.JS_WindowMessage_Send(list, '0x101e', c.column, 0, 317, 0) end
end
local function n(s, key) return tonumber(s:match('"' .. key .. '":(%d+)')) or 0 end
reaper.Main_OnCommand(reaper.NamedCommandLookup('_MWT_REFRESH'), 0)
local before = reaper.GetExtState('MediaWaveThumbnails', 'status')
reaper.OpenMediaExplorer(root .. '/' .. first, false)
local deadline = reaper.time_precise() + 15
local function run()
  local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(reaper.time_precise() < deadline, 'Real-library page timed out: ' .. s)
  if not s:find('RealLibrary', 1, true) or n(s, 'published_ready') < 20 or
      n(s, 'published_ready') ~= n(s, 'visible_files') or s:find('"first_draw_ready":-1', 1, true) then
    reaper.defer(run); return
  end
  assert(n(s, 'source_opens') == n(before, 'source_opens'), 'Audio decoders still opened for existing caches: ' .. s)
  assert(n(s, 'peak_reads') == n(before, 'peak_reads'), 'REAPER source peak API still used: ' .. s)
  assert(n(s, 'peak_build_starts') == n(before, 'peak_build_starts'), 'Existing real peaks rebuilt: ' .. s)
  assert(n(s, 'direct_peak_reads') - n(before, 'direct_peak_reads') >= n(s, 'visible_files'))
  assert(n(s, 'first_draw_ready') == n(s, 'visible_files'), 'First painted waveform frame was partial: ' .. s)
  print('PASS real-library first frame: all ' .. n(s, 'visible_files') .. ' waveforms ready together; decoder opens +0, source peak API reads +0')
  print('PAGE_PREP_MS=' .. n(s, 'page_ready_ms') .. '; DIRECT_BYTES=' .. (n(s, 'direct_peak_bytes') - n(before, 'direct_peak_bytes')))
end
run()

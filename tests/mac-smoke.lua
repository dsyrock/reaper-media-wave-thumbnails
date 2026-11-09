-- Run inside an actual Mac REAPER after installing the experimental dylib.
assert(reaper.GetOS():match('OSX') or reaper.GetOS():match('macOS'), 'Requires macOS REAPER')
assert(reaper.NamedCommandLookup('_MWT_TOGGLE') > 0, 'Extension did not load')
reaper.OpenMediaExplorer('', false)
local started = reaper.time_precise()
local function run()
  local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(reaper.time_precise() - started < 15, 'Mac attachment timed out: ' .. s)
  assert(s:find('"platform":"macos"', 1, true), 'Wrong backend: ' .. s)
  assert(s:find('"error":""', 1, true), s)
  if not s:find('"attached":true', 1, true) then reaper.defer(run); return end
  print('MAC_ATTACHMENT_OK ' .. s)
end
run()

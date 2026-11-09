-- Run in Linux REAPER with the experimental .so installed, using reaper-test
-- only if a suitable Linux test environment is available.
assert(reaper.GetOS():match('Linux') or reaper.GetOS():match('linux'), 'Requires Linux REAPER')
assert(reaper.NamedCommandLookup('_MWT_TOGGLE') > 0, 'Extension did not load')
reaper.OpenMediaExplorer('', false)
local started = reaper.time_precise()
local function run()
  local status = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(reaper.time_precise() - started < 15, 'Linux attachment timed out: ' .. status)
  assert(status:find('"platform":"linux"', 1, true), 'Wrong backend: ' .. status)
  assert(status:find('"error":""', 1, true), status)
  if not status:find('"attached":true', 1, true) then reaper.defer(run); return end
  print('LINUX_ATTACHMENT_OK ' .. status)
end
run()

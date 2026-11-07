local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails'
local folder = root .. '/Cold-' .. string.format('%d', math.floor(reaper.time_precise() * 1000))
reaper.RecursiveCreateDirectory(folder, 0)
local source = assert(io.open(root .. '/A/01_鼓点中文.wav', 'rb'))
local bytes = source:read('*a'); source:close()
for i = 1, 8 do
  local output = assert(io.open(folder .. '/' .. i .. '.wav', 'wb'))
  output:write(bytes); output:close()
end
reaper.OpenMediaExplorer(folder .. '/1.wav', false)
local started = reaper.time_precise()
local function run()
  local s = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(reaper.time_precise() - started < 20, 'Cold cache timeout: ' .. s)
  if s:find('Cold-', 1, true) and s:find('"ready":8', 1, true) then
    print('PASS first-time peak generation for 8 fresh files, including selected preview source')
    return
  end
  reaper.defer(run)
end
run()

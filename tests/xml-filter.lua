-- Run through reaper-test. Fixtures may be replaced with the reported XMLs in
-- Projects/media-wave-thumbnails/xml-filter before running this script.
local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails/xml-filter'
reaper.RecursiveCreateDirectory(root, 0)
local function write(path, data)
  local f = assert(io.open(path, 'wb')); f:write(data); f:close()
end
local function fixture(name, data)
  local path = root .. '/' .. name
  local f = io.open(path, 'rb')
  if f then f:close() else write(path, data) end
end
fixture('normal.xml', '<?xml version="1.0"?><score-partwise version="4.0"></score-partwise>')
fixture('broken.XML', 'score-partwise version="4.0"></score-partwise>')
local samples = {}
for i = 0, 11999 do samples[#samples+1] = string.pack('<i2', math.floor(12000 * math.sin(i * .11))) end
local data = table.concat(samples)
write(root .. '/00_audio.wav', 'RIFF' .. string.pack('<I4', 36+#data) .. 'WAVEfmt ' ..
  string.pack('<I4I2I2I4I4I2I2', 16, 1, 1, 12000, 24000, 2, 16) .. 'data' .. string.pack('<I4', #data) .. data)
local toggle = reaper.NamedCommandLookup('_MWT_TOGGLE')
assert(toggle > 0, 'Plugin not loaded')
if reaper.GetToggleCommandState(toggle) == 0 then reaper.Main_OnCommand(toggle, 0) end
local h = reaper.OpenMediaExplorer(root .. '/00_audio.wav', false)
local list = reaper.JS_Window_FindChildByID(h, 1001)
local header = reaper.JS_Window_FindChildByID(list, 0)
local function columns() return reaper.JS_WindowMessage_Send(header, '0x1200', 0, 0, 0, 0) end
local started, stable, count, opens
started = reaper.time_precise()
local function run()
  local status = reaper.GetExtState('MediaWaveThumbnails', 'status')
  assert(reaper.time_precise() - started < 20, 'XML filtering timed out: ' .. status)
  assert(status:find('"error":""', 1, true), status)
  if not status:find('00_audio.wav', 1, true) or not status:find('"ready":1', 1, true) then reaper.defer(run); return end
  assert(not status:find('normal.xml', 1, true) and not status:find('broken.XML', 1, true), 'XML entered waveform cache: ' .. status)
  local current = tonumber(status:match('"source_opens":(%d+)'))
  if not stable then stable = reaper.time_precise(); count = columns(); opens = current end
  assert(columns() == count, 'Columns were repeatedly created')
  assert(current == opens, 'Settled directory repeatedly opened a source')
  if reaper.time_precise() - stable < 4 then reaper.defer(run); return end
  local xmlRows = 0
  for row = 0, reaper.JS_ListView_GetItemCount(list)-1 do
    local name = reaper.JS_ListView_GetItemText(list, row, 0)
    if name:lower():find('normal', 1, true) or name:lower():find('broken', 1, true) then xmlRows = xmlRows+1 end
  end
  print('XML_FILTER_OK ' .. status)
  print('Native XML rows=' .. xmlRows .. '; columns stable=' .. count .. '; REAPER=' .. reaper.GetAppVersion())
end
run()

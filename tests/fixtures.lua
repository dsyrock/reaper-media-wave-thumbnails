local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails'
reaper.RecursiveCreateDirectory(root .. '/A', 0)
reaper.RecursiveCreateDirectory(root .. '/B', 0)
local function wave(path, amplitude, seconds, channels)
  local rate = 12000
  local data = {}
  for i = 0, math.floor(rate * seconds) - 1 do
    local envelope = (i % 3000) / 3000
    for ch = 1, channels do
      local sample = amplitude * envelope * math.sin(i * 2 * math.pi * (220 + ch * 97) / rate)
      data[#data + 1] = string.pack('<i2', math.floor(sample * 32767))
    end
  end
  data = table.concat(data)
  local out = assert(io.open(path, 'wb'))
  out:write('RIFF', string.pack('<I4', 36 + #data), 'WAVEfmt ',
    string.pack('<I4I2I2I4I4I2I2', 16, 1, channels, rate, rate * channels * 2, channels * 2, 16),
    'data', string.pack('<I4', #data), data)
  out:close()
end
wave(root .. '/A/01_鼓点中文.wav', 0.8, 1, 1)
wave(root .. '/A/02_stereo.wav', 0.5, 2, 2)
wave(root .. '/A/03_silence.wav', 0, 1, 1)
wave(root .. '/B/01_鼓点中文.wav', 0.2, 1, 1)
for n = 4, 40 do wave(root .. '/A/' .. string.format('%02d_loop.wav', n), n / 50, 0.5, 1) end
local corrupt = assert(io.open(root .. '/A/41_corrupt.wav', 'wb'))
corrupt:write('not an audio file'); corrupt:close()
local note = assert(io.open(root .. '/A/readme.txt', 'w'))
note:write('ignore non-audio'); note:close()
reaper.OpenMediaExplorer(root .. '/A/01_鼓点中文.wav', false)
return {root = root, files = 41}

local root = reaper.GetResourcePath() .. '/Projects/media-wave-thumbnails'
reaper.RecursiveCreateDirectory(reaper.GetResourcePath() .. '/MediaDB', 0)
local db = reaper.GetResourcePath() .. '/MediaDB/MediaWaveThumbnailsTest.ReaperFileList'
local out = assert(io.open(db, 'wb'))
for _, path in ipairs({'A/01_鼓点中文.wav', 'A/02_stereo.wav', 'B/01_鼓点中文.wav'}) do
  local full = (root .. '/' .. path):gsub('/', '\\')
  out:write('FILE "', full, '" 1 0 0\n')
end
out:close()
return {database = db}

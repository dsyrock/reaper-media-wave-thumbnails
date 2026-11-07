local root = 'K:/codex/common/reaper-media-thumbnails/test-output'
reaper.RecursiveCreateDirectory(root, 0)
local h = reaper.OpenMediaExplorer('', false)
local ok, left, top, right, bottom = reaper.JS_Window_GetClientRect(h)
assert(ok)
local bitmap = reaper.JS_LICE_CreateBitmap(true, right - left, bottom - top)
local dst = reaper.JS_LICE_GetDC(bitmap)
local src = reaper.JS_GDI_GetClientDC(h)
reaper.JS_GDI_Blit(dst, 0, 0, src, 0, 0, right - left, bottom - top)
assert(reaper.JS_LICE_WritePNG(root .. '/media-explorer.png', bitmap, false))
reaper.JS_GDI_ReleaseDC(h, src)
reaper.JS_LICE_DestroyBitmap(bitmap)
return {path = root .. '/media-explorer.png', width = right - left, height = bottom - top}

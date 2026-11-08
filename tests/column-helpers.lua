local M = {}
function M.handles()
  local h = reaper.OpenMediaExplorer('', false)
  local list = reaper.JS_Window_FindChildByID(h, 1001)
  return h, list, reaper.JS_Window_FindChildByID(list, 0)
end
function M.title(list, column)
  local buffer = assert(reaper.JS_Mem_Alloc(1024))
  local address = reaper.JS_Window_AddressFromHandle(buffer)
  local ok, result = pcall(function()
    reaper.JS_Mem_FromString(buffer, 0, string.rep('\0', 1024), 1024)
    reaper.JS_Mem_FromString(buffer, 0, string.pack('<I4', 4), 4) -- LVCF_TEXT
    reaper.JS_Mem_FromString(buffer, 16, string.pack('<I8', math.floor(address + 128)), 8)
    reaper.JS_Mem_FromString(buffer, 24, string.pack('<i4', 256), 4)
    assert(reaper.JS_WindowMessage_Send(list, '0x105f', column, 0, address, 0) ~= 0)
    local a, b = reaper.JS_String(buffer, 128, 512)
    local bytes = type(a) == 'string' and a or b
    local text = {}
    for i = 1, #bytes - 1, 2 do
      local c = string.unpack('<I2', bytes, i)
      if c == 0 then break end
      text[#text + 1] = utf8.char(c)
    end
    return table.concat(text)
  end)
  reaper.JS_Mem_Free(buffer)
  assert(ok, result)
  return result
end
function M.snapshot()
  local _, list, header = M.handles()
  local count = reaper.JS_WindowMessage_Send(header, '0x1200', 0, 0, 0, 0)
  local result = {}
  for position = 0, count - 1 do
    local column = reaper.JS_WindowMessage_Send(header, '0x120f', position, 0, 0, 0)
    result[#result + 1] = {column = column, position = position, title = M.title(list, column),
      width = reaper.JS_WindowMessage_Send(list, '0x101d', column, 0, 0, 0)}
  end
  return result
end
function M.setOrder(order)
  local _, list = M.handles()
  local values = {}
  for _, column in ipairs(order) do values[#values + 1] = string.pack('<i4', column) end
  local bytes = table.concat(values)
  local buffer = assert(reaper.JS_Mem_Alloc(#bytes))
  reaper.JS_Mem_FromString(buffer, 0, bytes, #bytes)
  local address = reaper.JS_Window_AddressFromHandle(buffer)
  local result = reaper.JS_WindowMessage_Send(list, '0x103a', #order, 0, address, 0)
  reaper.JS_Mem_Free(buffer)
  assert(result ~= 0, 'Set column order failed')
end
function M.insert(column, title, width)
  local _, list = M.handles()
  local buffer = assert(reaper.JS_Mem_Alloc(1024))
  local address = reaper.JS_Window_AddressFromHandle(buffer)
  local text = {}
  for _, c in utf8.codes(title) do text[#text + 1] = string.pack('<I2', c) end
  text = table.concat(text) .. '\0\0'
  reaper.JS_Mem_FromString(buffer, 0, string.rep('\0', 1024), 1024)
  reaper.JS_Mem_FromString(buffer, 0, string.pack('<I4', 46), 4)
  reaper.JS_Mem_FromString(buffer, 8, string.pack('<i4', width), 4)
  reaper.JS_Mem_FromString(buffer, 16, string.pack('<I8', math.floor(address + 128)), 8)
  reaper.JS_Mem_FromString(buffer, 28, string.pack('<i4', column), 4)
  reaper.JS_Mem_FromString(buffer, 36, string.pack('<i4', column), 4)
  reaper.JS_Mem_FromString(buffer, 128, text, #text)
  local result = reaper.JS_WindowMessage_Send(list, '0x1061', column, 0, address, 0)
  reaper.JS_Mem_Free(buffer)
  assert(result == column, 'Native column insertion failed')
end
return M

-- Execute through reaper-test in an independent REAPER resource directory.
local section='MediaWaveThumbnails'
local root=reaper.GetResourcePath()..'/Projects/media-wave-thumbnails'
local backup=root..'/color-test-original.bin'
local oldNormal=reaper.GetExtState(section,'color_normal')
local oldSelected=reaper.GetExtState(section,'color_selected')
local check=io.open(backup,'rb')
if check then check:close() else
  local f=assert(io.open(backup,'wb'));f:write(string.pack('s4s4',oldNormal,oldSelected));f:close()
end
local action=reaper.NamedCommandLookup('_MWT_COLORS')
assert(action>0,'Color action missing')
local mx=reaper.OpenMediaExplorer(root..'/A/01_鼓点中文.wav',false)
local function status()return reaper.GetExtState(section,'status')end
local function value(s,key)return s:match('"'..key..'":"(.-)"')end
local function number(s,key)return tonumber(s:match('"'..key..'":(%d+)'))or 0 end
local function open()
  reaper.Main_OnCommand(action,0)
  return assert(reaper.JS_Window_Find('Waveform colors',true),'Color panel did not open')
end
local function command(h,id)reaper.JS_WindowMessage_Send(h,'WM_COMMAND',id,0,0,0)end
local function hex(h,color)
  local edit=assert(reaper.JS_Window_FindChildByID(h,3104))
  reaper.JS_Window_SetTitle(edit,color)
  reaper.JS_WindowMessage_Send(h,'WM_COMMAND',3104,0x300,reaper.JS_Window_AddressFromHandle(edit),0)
end
local phase,at=0,reaper.time_precise()
local dialog,before,normal,selected,sourceOpens,peakReads
local function nextPhase(n)phase,at=n,reaper.time_precise()end
local function run()
  local s=status()
  assert(reaper.time_precise()-at<15,'Color test phase '..phase..' timeout: '..s)
  assert(s:find('"error":""',1,true),s)
  if phase==0 then
    if number(s,'ready')<20 then reaper.defer(run);return end
    before=s;normal,selected=value(s,'color_normal'),value(s,'color_selected')
    sourceOpens,peakReads=number(s,'source_opens'),number(s,'direct_peak_reads')
    dialog=open();hex(dialog,'#E04080');nextPhase(1)
  elseif phase==1 and value(s,'color_normal')=='#E04080' then
    assert(reaper.GetExtState(section,'color_normal')==oldNormal,'Live preview was persisted before Save')
    assert(number(s,'source_opens')==sourceOpens and number(s,'direct_peak_reads')==peakReads,'Changing color reread audio/peaks')
    command(dialog,2);nextPhase(2)
  elseif phase==2 and value(s,'color_normal')==normal and s:find('"color_editing":false',1,true) then
    print('PASS live color preview without peak reads; Cancel restores previous color')
    dialog=open();hex(dialog,'#XYZ');command(dialog,1)
    assert(reaper.JS_Window_Find('Waveform colors',true),'Invalid HEX was saved')
    assert(reaper.GetExtState(section,'color_normal')==oldNormal,'Invalid HEX changed persisted setting')
    hex(dialog,'#E04080');command(dialog,3102);hex(dialog,'#50B5FF')
    command(dialog,1);nextPhase(3)
  elseif phase==3 and value(s,'color_normal')=='#E04080' and value(s,'color_selected')=='#50B5FF' and s:find('"color_editing":false',1,true) then
    assert(reaper.GetExtState(section,'color_normal')=='#E04080' and reaper.GetExtState(section,'color_selected')=='#50B5FF','Save did not persist selected colors')
    dialog=open();command(dialog,3106);nextPhase(4)
  elseif phase==4 and value(s,'color_normal')=='' and value(s,'color_selected')=='' then
    assert(reaper.GetExtState(section,'color_normal')=='#E04080','Reset preview persisted without Save')
    command(dialog,2);nextPhase(5)
  elseif phase==5 and value(s,'color_normal')=='#E04080' and value(s,'color_selected')=='#50B5FF' and s:find('"color_editing":false',1,true) then
    print('PASS invalid HEX rejected; both colors saved; Cancel after Reset restores saved colors')
    dialog=open()
    local ok,l,t,r,b=reaper.JS_Window_GetClientRect(dialog)
    assert(ok)
    local bitmap=reaper.JS_LICE_CreateBitmap(true,r-l,b-t)
    local src=reaper.JS_GDI_GetClientDC(dialog)
    reaper.JS_GDI_Blit(reaper.JS_LICE_GetDC(bitmap),0,0,src,0,0,r-l,b-t)
    reaper.JS_GDI_ReleaseDC(dialog,src)
    assert(reaper.JS_LICE_WritePNG(root..'/color-panel.png',bitmap,false))
    reaper.JS_LICE_DestroyBitmap(bitmap)
    command(dialog,2)
    print('COLOR_UI_PASSED; saved settings left for restart test; panel='..root..'/color-panel.png')
    return
  end
  reaper.defer(run)
end
run()

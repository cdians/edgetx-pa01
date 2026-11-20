-------------------------------------------------------------------------------
-- TBS Agent 1.01
-- release date: 2025-12
-- author: JimB40
-------------------------------------------------------------------------------
local toolName = "TNS|TBS Agent 1.01|TNE"
local DIR = '52/'
local LV = _VERSION
if LV ~= nil then
  local LM,LS = string.match(_VERSION,'^Lua (%d+)%.(%d+)')
  DIR = tonumber(LS) > 2 and '53/' or DIR
end
local SP = '/SCRIPTS/TOOLS/TBSAGENT/'
return loadScript(SP..DIR..'loader','Tx')('SA',SP,DIR)

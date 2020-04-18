local Rom = require("liblsdj.rom")
local Sav = require("liblsdj.sav")

local function loadRom(romData)
    return Rom(romData)
end

local function loadSav(savData)
    local sav = Sav(savData)
    if sav:isValid() then return sav end
end

return {
    loadRom = loadRom,
    loadSav = loadSav
}

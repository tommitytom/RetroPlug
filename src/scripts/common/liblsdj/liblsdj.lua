local Rom = require("liblsdj.rom")
local Sav = require("liblsdj.sav")

local function loadRom(romData)
    return Rom(romData)
end

local function loadSav(savData)
    return Sav(savData)
end

return {
    loadRom = loadRom,
    loadSav = loadSav
}

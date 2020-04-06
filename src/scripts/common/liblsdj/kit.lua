local class = require("class")
local fs = require("fs")

local KIT_NAME_OFFSET = 0x52
local KIT_NAME_SIZE = 6

local Kit = class()

function Kit:init(kitData)
    if kitData ~= nil then
	    self.name = kitData:slice(KIT_NAME_OFFSET, KIT_NAME_SIZE):toString()
        self.data = kitData
        self.checksum = kitData:hash()
    else
        self.name = ""
        self.data = nil
        self.checksum = nil
    end
end

function Kit:toFile(filePath)
    if self.data ~= nil then
        fs.save(self.data, filePath)
    else
        print("Failed to save kit: Kit slot is empty")
    end
end

local class = require("class")
local fs = require("fs")

local KIT_NAME_OFFSET = 0x52
local KIT_NAME_SIZE = 6

local Kit = class()

function Kit:init(kitData)
	if kitData == nil then
		self.name = ""
		self.data = nil
		return
	elseif type(kitData) == "string" then
		local fileData = fs.load(kitData)
		if fileData ~= nil then
			kitData = fileData
		else
			-- Failed to load kit
			return
		end
	end

	self.name = kitData:slice(KIT_NAME_OFFSET, KIT_NAME_SIZE):toString()
	self.data = kitData
end

function Kit:copyFrom(other)
	self.name = other.name
	self.data = other.data
end

function Kit:toBuffer(target)
	if target == nil then return self.data end
	target:copyFrom(self.data)
	return target
end

function Kit:toFile(filePath)
	if self.data ~= nil then
		fs.save(filePath, self.data)
	else
		print("Failed to save kit: Kit slot is empty")
	end
end

return Kit

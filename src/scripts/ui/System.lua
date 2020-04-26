local class = require("class")
local System = class()
local util = require("util")

function System:init(desc, buttons)
	self._desc = desc
	self._buttons = buttons
end

function System:setSram(data, reset)
	print(self._desc.idx, data, reset)
	_proxy:setSram(self._desc.idx, data, reset)
end

function System:setRom(data, reset)
	_proxy:setRom(self._desc.idx, data, reset)
	self._desc.sourceRomData = data
	self._desc.romName = util.getRomName(data)
end

function System:loadRom(data)
	_proxy:loadRom(self._desc.idx, data)
	self._desc.sourceRomData = data
	self._desc.romName = util.getRomName(data)
end

function System:desc()
	return self._desc
end

function System:sram()
	return self._desc.sourceSavData
end

function System:rom()
	return self._desc.sourceRomData
end

function System:buttons()
	return self._buttons
end

return System

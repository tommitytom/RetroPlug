local class = require("class")
local System = class()

function System:init(desc, buttons)
	self._desc = desc
	self._buttons = buttons
end

function System:setSram(data, reset)
	_proxy:setSram(self._desc.idx, data, reset)
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

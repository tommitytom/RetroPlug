local class = require("class")

local System = class()
function System:init(model, buttons, state)
	self._model = model
	self._desc = model:getDesc()
	self.desc = self._desc
	self._buttons = buttons
	self.state = state
end

function System:model()
	return self._model
end

function System:desc()
	return self._desc
end

function System:buttons()
	return self._buttons
end

function System:sendSerialByte(offset, byte)
	self._model:sendSerialByte(offset, byte)
end

return System

local class = require("class")
local System = class()

function System:init(model, buttons)
	self._model = model
	self._desc = model:getDesc()
	self._buttons = buttons
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

return System

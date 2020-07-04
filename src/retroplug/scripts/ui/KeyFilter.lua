local class = require("class")
local KeyFilter = class()
function KeyFilter:init()
	self._keyState = {}
end

function KeyFilter:onKey(key, down)
	if down == true then
		if self._keyState[key] ~= nil then return false end
		self._keyState[key] = true
	else
		self._keyState[key] = nil
	end

	return true
end

return KeyFilter

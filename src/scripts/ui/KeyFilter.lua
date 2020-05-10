local class = require("class")
local KeyFilter = class()
function KeyFilter:init()
	self._keyState = {}
end

function KeyFilter:onKey(key, down)
	local vk = key.vk
	if down == true then
		if self._keyState[vk] ~= nil then return false end
		self._keyState[vk] = true
	else
		self._keyState[vk] = nil
	end

	return true
end

return KeyFilter

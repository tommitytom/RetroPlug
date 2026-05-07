local class = require("class")

local AUTO_RELEASE_INTERVAL = 50

local KeyFilter = class()
function KeyFilter:init()
	self._keyState = {}
	self._ctrlHeld = false
end

function KeyFilter:onKey(key, down)
	if down == true then
		if self._keyState[key] ~= nil then return false end
		self._keyState[key] = AUTO_RELEASE_INTERVAL
	else
		self._keyState[key] = nil
	end

	if key == Key.Ctrl then
		self._ctrlHeld = down
	end

	return true
end

function KeyFilter:getKeyReleases(delta)
	if _OPERATING_SYSTEM ~= OperatingSystemType.MacOs then
		return
	end

	-- This is a bit gross.  On MacOs you don't receive key up events when
	-- the command key is being held (referred to here as control).
	-- To get around this, while the command key is held, all button presses that
	-- are not modifier keys will be automatically released after an arbitrary
	-- time period (currently 50ms).

	if self._ctrlHeld == true then
		local removals = {}

		for k, v in pairs(self._keyState) do
			if k ~= Key.Ctrl then
				local remaining = v - delta

				if remaining <= 0 then
					table.insert(removals, k)
				else
					self._keyState[k] = remaining
				end
			end
		end

		for _, v in ipairs(removals) do
			self._keyState[v] = nil
		end

		return removals
	end
end

return KeyFilter

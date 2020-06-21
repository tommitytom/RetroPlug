local class = require("class")

local NS_PER_MICROSECOND = 1000
local NS_PER_MILLISECOND = NS_PER_MICROSECOND * 1000
local NS_PER_SECOND = NS_PER_MILLISECOND * 1000
local NS_PER_MINUTE = NS_PER_SECOND * 60

local function timeFormat(ns)
	if ns < NS_PER_MICROSECOND then return tostring(ns) .. "ns" end
	if ns < NS_PER_MILLISECOND then return tostring(ns / NS_PER_MICROSECOND) .. "us" end
	if ns < NS_PER_SECOND then return tostring(ns / NS_PER_MILLISECOND) .. "ms" end
	if ns < NS_PER_MINUTE then return tostring(ns / NS_PER_SECOND) .. "s" end
	return tostring(ns / NS_PER_SECOND) .. "s"
end

local Timer = class()
function Timer:init()
	self:restart()
end

function Timer:restart()
	self._start = chrono.now()
end

function Timer:value()
	return chrono.now() - self._start
end

function Timer:log(prefix)
	prefix = prefix or ""
	print(prefix .. timeFormat(self:value()))
end

return Timer
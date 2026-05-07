local class = require("class")

local NS_PER_MICROSECOND = 1000
local NS_PER_MILLISECOND = NS_PER_MICROSECOND * 1000
local NS_PER_SECOND = NS_PER_MILLISECOND * 1000
local NS_PER_MINUTE = NS_PER_SECOND * 60
local NS_PER_HOUR = NS_PER_MINUTE * 60

local function formatMinsSecs(ns)
	return tostring(math.floor(ns / NS_PER_MINUTE)) .. "m " .. (ns % NS_PER_MINUTE) .. "s"
end

local function formatHoursMinsSecs(ns)
	return tostring(math.floor(ns / NS_PER_HOUR)) .. "h " .. formatMinsSecs(ns % NS_PER_HOUR)
end

local function timeFormat(ns)
	if ns < NS_PER_MICROSECOND then return tostring(ns) .. "ns" end
	if ns < NS_PER_MILLISECOND then return tostring(ns / NS_PER_MICROSECOND) .. "us" end
	if ns < NS_PER_SECOND then return tostring(ns / NS_PER_MILLISECOND) .. "ms" end
	if ns < NS_PER_MINUTE then return tostring(ns / NS_PER_SECOND) .. "s" end
	if ns < NS_PER_HOUR then return formatMinsSecs(ns) end
	return formatHoursMinsSecs(ns)
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

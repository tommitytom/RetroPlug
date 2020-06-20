local class = require("class")
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
	print(prefix .. self:value() .. 'ms')
end

return Timer
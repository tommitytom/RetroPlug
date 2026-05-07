local class = require("class")

local Error = class()
function Error:init(msg)
	self.msg = msg
end

function Error:toString()
	return self.msg
end

return Error

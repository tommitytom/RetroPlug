local Model = require("Model")

local class = require("class")
local View = class()
function View:setup(audioContext)
	self.model = Model(audioContext:getFileManager(), audioContext)
end

function View:onKey(key, down)
	print(key, down)
end

function View:onDoubleClick(x, y, mod)
	print(x, y, mod)
end

function View:onMouseDown(x, y, mod)
	print(x, y, mod)
end

function View:onPadButton(button, down)
	print(button, down)
end

function View:onDrop(x, y, items)
	print(x, y, items)
end

return View

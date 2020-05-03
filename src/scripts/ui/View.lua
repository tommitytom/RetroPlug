local class = require("class")

local Model = require("Model")

local View = class()
function View:setup(fileSystem, audioContext)
	self.model = Model(fileSystem, audioContext)
end

function View:onKey(key, down)

end

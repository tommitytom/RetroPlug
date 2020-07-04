local fs = require("fs")
local Project = require("Project")

local class = require("class")
local Model = class()
function Model:init(audioContext)
	self.audioContext = audioContext
	self.project = Project(audioContext)
end

function Model:onKey(key, down)

end

return Model

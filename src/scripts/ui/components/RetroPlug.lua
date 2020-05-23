local RetroPlug = component({ name = "RetroPlug", global = true })

local class = require("class")
local RetroPlugActions = class()
function RetroPlugActions:init(project)
	self.project = project
end

function RetroPlugActions:nextSystem(down)
	if down == true then self.project:nextSystem() end
end

function RetroPlug:init()
	self:registerActions(RetroPlugActions(self:project()))
end

return RetroPlug

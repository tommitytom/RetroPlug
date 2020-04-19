local class = require("class")

local EmualatorInstance = class()
function EmualatorInstance:init(system, components)
	self.system = system
	self.components = components
end

function EmualatorInstance:findComponent(compType)
	if type(compType) == "string" then
		for _, v in ipairs(self.components) do
			if v.__desc.name == compType then
				return v
			end
		end
	else
		for _, v in ipairs(self.components) do
			if v:isA(compType) == true then
				return v
			end
		end
	end
end

function EmualatorInstance:triggerEvent(name, ...)
	for i = #self.components, 1, -1 do
		local component = self.components[i]
		local event = component[name]
		if event ~= nil then
			local status = event(component, ...)
			if status ~= false then
				break
			end
		end
	end
end

return EmualatorInstance

local EventType = {
	Lifecycle = 0,
	Input = 1
}

local eventTypes = {
	onKey = EventType.Input,
	onMouseDown = EventType.Input,
	onMouseUp = EventType.Input,
	onDoubleClick = EventType.Input,
	onPadButton = EventType.Input
}

local module = {}

function module.findByName(components, name)
	local lowerName = string.lower(name)
	for _, v in ipairs(components) do
		if string.lower(v.__desc.name) == lowerName then
			return v
		end
	end
end

function module.findAction(components, name, action)
	local component = module.findByName(components, name)
	if component ~= nil then
		return component.__actions[string.lower(action)]
	end
end

function module.emitComponentEvent(eventName, components, ...)
	local evType = eventTypes[eventName] or EventType.Lifecycle

	for i = #components, 1, -1 do
		local component = components[i]
		local ev = component[eventName]
		if ev ~= nil then
			-- TODO: use pcall here?
			local handled = ev(component, ...)
			if evType == EventType.Input then
				if handled ~= false then return true end
			end
		end
	end

	return false
end

return module

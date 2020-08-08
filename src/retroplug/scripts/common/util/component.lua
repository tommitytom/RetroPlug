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

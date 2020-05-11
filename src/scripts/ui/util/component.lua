local function emitComponentEvent(eventName, components, ...)
	for i = #components, 1, -1 do
		local component = components[i]
		local ev = component[eventName]
		if ev ~= nil then
			local handled = ev(component, ...)
			if handled ~= false then return true end
		end
	end

	return false
end

return {
	emitComponentEvent = emitComponentEvent
}

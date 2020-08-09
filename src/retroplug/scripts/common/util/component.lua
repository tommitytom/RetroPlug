local util = require("util")

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
		if string.lower(v.getDesc().name) == lowerName then
			return v
		end
	end
end

local function keysToLower(obj)
	local ret = {}
	for k, v in pairs(obj) do
		ret[string.lower(k)] = v
	end

	return ret
end

function module.findAction(components, name, action)
	local component = module.findByName(components, name)
	if component ~= nil then
		local actions = keysToLower(component.actions)
		return actions[string.lower(action)]
	end
end

function module.createState(components)
	local state = {}
	for _, v in ipairs(components) do
		local desc = v.getDesc()
		local s = desc.systemState
		if s ~= nil then
			state[string.lower(desc.name)] = util.deepcopy(s)
		end
	end

	return state
end

function module.emitComponentEvent(components, eventName, ...)
	local evType = eventTypes[eventName] or EventType.Lifecycle

	for i = #components, 1, -1 do
		local component = components[i]
		local ev = component[eventName]
		if ev ~= nil then
			if component.requires == nil or component.requires() == true then
				-- TODO: use pcall here?
				local handled = ev(...)
				if evType == EventType.Input then
					if handled ~= false then return true end
				end
			end
		end
	end

	return false
end

return module

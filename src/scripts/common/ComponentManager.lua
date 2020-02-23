local _factory = { global = {}, instance = {} }
local _globalComponents = {}

local _allComponents = {}

local function loadComponent(name)
	local component = require(name)
	if component ~= nil then
		print("Registered component: " .. component.__desc.name)
		if component.__desc.global == true then
			table.insert(_factory.global, component)
		else
			table.insert(_factory.instance, component)
		end
	else
		print("Failed to load " .. name .. ": Script does not return a component")
	end
end

local function createComponents(desc, ...)
	local components = {}
	for _, v in ipairs(_factory.instance) do
		local d = v.__desc
		if d.romName == nil or desc.romName:find(d.romName) ~= nil then
			print("Attaching component " .. d.name)
			local comp = v.new(...)
			table.insert(components, comp)
			table.insert(_allComponents, comp)
		end
	end

	return components
end

local function runComponentHandlers(target, components, ...)
	local handled = false
	if components ~= nil then
		for _, v in ipairs(components) do
			local found = v[target]
			if found ~= nil then
				found(v, ...)
				handled = true
			end
		end
	end

	return handled
end

local function runGlobalHandlers(target, ...)
    return runComponentHandlers(target, _globalComponents, ...)
end

local function runAllHandlers(target, components, ...)
    local handled = runGlobalHandlers(target, ...)
    if components ~= nil then handled = runComponentHandlers(target, components, ...) end
    return handled
end

local function createGlobalComponents()
    for _, v in ipairs(_factory.global) do
		table.insert(_globalComponents, v.new())
	end
end

return {
    loadComponent = loadComponent,
    createComponents = createComponents,
    runComponentHandlers = runComponentHandlers,
    runGlobalHandlers = runGlobalHandlers,
    runAllHandlers = runAllHandlers,
	createGlobalComponents = createGlobalComponents,
	allComponents = _allComponents
}

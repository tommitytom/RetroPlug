local _factory = { global = {}, instance = {} }
local _projectComponents = {}

local _allComponents = {}

local _systemComponents = {
	["Button Handler"] = true
}

local function getComponentNamesMap()
	local names = {}
	for _, componentType in ipairs(_factory.instance) do
		names[componentType.__desc.name] = true
	end

	return names
end

local function isSystemComponent(component)
	return _systemComponents[component.__desc.name] or false
end

local function loadComponent(name)
	local component = require(name)
	if component ~= nil then
		print("Registered component: " .. component.__desc.name)
		if component.__desc.global == true then
			table.insert(_factory.global, component)
		end

		if component.__desc.global ~= true or component.__desc.system == true then
			table.insert(_factory.instance, component)
		end
	else
		print("Failed to load " .. name .. ": Script does not return a component")
	end
end

local function createComponent(target, name)
	for _, componentType in ipairs(_factory.instance) do
		local d = componentType.__desc
		if d.name == name then
			local valid, ret = pcall(componentType.new, target)
			if valid then
				return ret
			else
				print("Failed to create component: " .. ret)
			end
		end
	end
end

local function createSystemComponents(system)
	local desc = system.desc
	local components = {}
	for _, componentType in ipairs(_factory.instance) do
		local d = componentType.__desc
		if _systemComponents[d.name] == true or (d.romName ~= nil and desc.romName:find(d.romName) ~= nil) then
			print("Attaching component " .. d.name)
			local valid, ret = pcall(componentType.new, system)
			if valid then
				table.insert(components, ret)
				table.insert(_allComponents, ret)
			else
				print("Failed to load component: " .. ret)
			end
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
    return runComponentHandlers(target, _projectComponents, ...)
end

local function runAllHandlers(target, components, ...)
    local handled = runGlobalHandlers(target, ...)
    if components ~= nil then handled = runComponentHandlers(target, components, ...) end
    return handled
end

local function createProjectComponents(project)
    for _, v in ipairs(_factory.global) do
		table.insert(_projectComponents, v.new(project, true))
	end

	return _projectComponents
end

return {
	getComponentNamesMap = getComponentNamesMap,
	isSystemComponent = isSystemComponent,
	loadComponent = loadComponent,
	createComponent = createComponent,
    createSystemComponents = createSystemComponents,
    runComponentHandlers = runComponentHandlers,
    runGlobalHandlers = runGlobalHandlers,
    runAllHandlers = runAllHandlers,
	createProjectComponents = createProjectComponents,
	allComponents = _allComponents
}

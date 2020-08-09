local module = {}

local _factory = {}

function module.getComponentNamesMap()
	local names = {}
	for _, componentType in ipairs(_factory) do
		names[componentType.getDesc().name] = true
	end

	return names
end

function module.loadComponent(name)
	local component = require(name)
	if component ~= nil then
		print("Registered component: " .. component.getDesc().name)
		table.insert(_factory, component)
	else
		print("Failed to load " .. name .. ": Script does not return a component")
	end
end

function module.createComponent(target, name)
	for _, componentType in ipairs(_factory) do
		local d = componentType.getDesc()
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

function module.createComponents(project)
	print("------- COMPONENTS -------")
	local components = {}
	for _, componentType in ipairs(_factory) do
		local d = componentType.getDesc()
		print("Attaching component " .. d.name)
		table.insert(components, componentType)
	end

	return components
end

return module

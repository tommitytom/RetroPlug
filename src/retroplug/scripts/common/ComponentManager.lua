local module = {}

local _factory = {}

function module.getComponentNamesMap()
	local names = {}
	for _, componentType in ipairs(_factory) do
		names[componentType.__desc.name] = true
	end

	return names
end

function module.loadComponent(name)
	local component = require(name)
	if component ~= nil then
		print("Registered component: " .. component.__desc.name)
		table.insert(_factory, component)
	else
		print("Failed to load " .. name .. ": Script does not return a component")
	end
end

function module.createComponent(target, name)
	for _, componentType in ipairs(_factory) do
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

function module.createComponents(project)
	print("------- COMPONENTS -------")
	local components = {}
	for _, componentType in ipairs(_factory) do
		local d = componentType.__desc
		print("Attaching component " .. d.name)

		local valid, ret = pcall(componentType.new, project)
		if valid then
			table.insert(components, ret)
		else
			print("Failed to load component: " .. ret)
		end
	end

	return components
end

return module

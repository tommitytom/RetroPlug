local componentDescDefaults = {
	global = false
}

function component(desc)
	if desc.name == nil then
		error("A component descriptor must specify a name")
	end

	for k, v in pairs(componentDescDefaults) do
		if desc[k] == nil then
			desc[k] = v
		end
	end

	local c = {}
	c.__index = c
	c.__desc = desc
	c.new = function(system)
		local obj = {}
		setmetatable(obj, c)
		obj.system = system
		if c.init then c.init(obj)	end
		return obj
	end

	return c
end

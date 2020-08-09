

local componentDescDefaults = {
	global = false
}

local ACTION_IGNORE = { "_", "isA", "init" }

local function startsWith(str, start)
	return str:sub(1, #start) == start
end

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

	c.registerActions = function(obj, actions)
		local root = getmetatable(actions) or actions

		for k, action in pairs(root) do
			if type(action) == "function" then
				local ignore = false
				for _, v in ipairs(ACTION_IGNORE) do
					if startsWith(k, v) == true then
						ignore = true
						break
					end
				end

				if ignore == false then
					obj.__actions[string.lower(k)] = function(...)
						root[k](actions, ...)
					end
				end
			end
		end
	end

	c.new = function(project)
		local obj = { __actions = {}, __enabled = true }
		setmetatable(obj, c)

		function obj:project() return project end
		function obj:enabled() return obj.__enabled end
		function obj:setEnabled(enabled) obj.__enabled = enabled end
		if c.init then c.init(obj) end

		return obj
	end

	return c
end

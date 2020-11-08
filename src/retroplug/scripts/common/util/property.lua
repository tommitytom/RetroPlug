local function startsWith(str, start)
	return str:sub(1, #start) == start
end

local function getOrAdd(table, name)
	local p = table[name]
	if p == nil then
		p = {}
		table[name] = p
	end

	return p
end

local function logError(type, name)
	log.error("Failed to "..type.." property "..name.." - "..type.."ter does not exist")
end

local module = {}
function module.setupProperties(table)
	local props = {}

	for k, v in pairs(table) do
		if startsWith(k, "get_") then
			getOrAdd(props, k:sub(5)).getter = v
		end

		if startsWith(k, "set_") then
			getOrAdd(props, k:sub(5)).setter = v
		end
	end

	setmetatable(table, {
		__index = function(obj, key)
			local prop = props[key]
			if prop ~= nil then
				if prop.getter ~= nil then
					return prop.getter()
				else
					logError("get", key)
				end
			else
				return rawget(obj, key)
			end
		end,
		__newindex = function(obj, key, value)
			local prop = props[key]
			if prop ~= nil then
				if prop.setter ~= nil then
					prop.setter(value)
				else
					logError("set", key)
				end
			else
				rawset(obj, key, value)
			end
		end
	})
end

return module

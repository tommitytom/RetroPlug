local function startsWith(str, start)
	return str:sub(1, #start) == start
end

local module = {}

function module.setupProperties(table)
	local _getters = {}
	local _setters = {}

	for k, v in pairs(table) do
		if startsWith(k, "get_") then
			_getters[k:sub(5)] = v
		end

		if startsWith(k, "set_") then
			_setters[k:sub(5)] = v
		end
	end

	setmetatable(table, {
		__index = function(obj, key)
			local prop = _getters[key]
			if prop ~= nil then
				return prop()
			else
				return rawget(obj, key)
			end
		end,
		__newindex = function(obj, key, value)
			local prop = _setters[key]
			if prop ~= nil then
				return prop(value)
			else
				return rawset(obj, key, value)
			end
		end
	})
end

return module

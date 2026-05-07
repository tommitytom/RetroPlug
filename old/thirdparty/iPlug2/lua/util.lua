local util = {}

function util.scriptPath(depth)
	depth = depth or 2
	local str = debug.getinfo(depth, "S").source:sub(2)
	return str:match("(.*/)")
 end

function util.getTargetName(pluginName, name, ext)
	local p = pluginName .. "_" .. name .. "_%{cfg.platform}"
	if ext ~= nil then p = p .. "." .. ext end
	return p
end

function util.copyFields(target, source)
	for k, v in pairs(source) do
		if type(v) ~= "table" then
			target[k] = v
		else
			local t = target[k]
			if t == nil then
				t = {}
				target[k] = t
			end

			util.copyFields(t, v)
		end
	end
end

function util.flattenConfig(config, target)
	local flattened = {}

	util.copyFields(flattened, config.default)
	if config[target] ~= nil then util.copyFields(flattened, config[target]) end

	if flattened.outDir then
		if flattened.outDir32 == nil then flattened.outDir32 = flattened.outDir end
		if flattened.outDir64 == nil then flattened.outDir64 = flattened.outDir end
	end

	return flattened
end

return util

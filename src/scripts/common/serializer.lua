local serpent = require("serpent")

local function findComponentByName(components, name)
	for i, v in ipairs(components) do
		if v.__desc.name == name then
			return v
		end
	end
end

local function serializeInstance(instance)
	local instModel = {}
	--print(inspect(instance))
	for _, v in ipairs(instance.components) do
		local found = v["onSerialize"]
		if found ~= nil then
			-- TODO: Should maybe base the component name on the filename rather
			-- than using the user defined name here
			local componentData = { __componentName = v.__desc.name }
			found(v, componentData)
			table.insert(instModel, componentData)
		end
	end

	return instModel
end

local function serializeInstances(instances)
	local target = {}

	for i, inst in ipairs(instances) do
		if inst ~= nil then
			target[i] = serializeInstance(inst)
		end
	end

	return target
end

local function serializeInstancesToString(instances)
	return serpent.dump(serializeInstances(instances), { comment = false })
end

local function serializeInstanceToString(instance)
	return serpent.dump(serializeInstance(instance), { comment = false })
end

local function deserializeInstance(instance, model)
	for _, compModel in ipairs(model) do
		local component = findComponentByName(instance.components, compModel.__componentName)
		if component ~= nil then
			local found = component["onDeserialize"]
			if found ~= nil then
				found(component, compModel)
			end
		end
	end
end

local function deserializeInstanceFromString(instance, data)
	local ok, model = serpent.load(data)
	if ok == true then
		if model ~= nil then
			deserializeInstance(instance, model)
		end
	else
		print("Failed to deserialize components")
	end
end

local function deserializeInstancesFromString(instances, data)
	local ok, model = serpent.load(data)

	if ok == true then
		if model ~= nil then
			for i, inst in ipairs(instances) do
				local instModel = model[i]
				if inst ~= nil and instModel ~= nil then
					deserializeInstance(inst, instModel)
				end
			end
		end
	else
		print("Failed to deserialize components")
	end
end

return {
	serializeInstance = serializeInstance,
	serializeInstances = serializeInstances,
	serializeInstancesToString = serializeInstancesToString,
	deserializeInstancesFromString = deserializeInstancesFromString,
	serializeInstanceToString = serializeInstanceToString,
	deserializeInstanceFromString = deserializeInstanceFromString
}

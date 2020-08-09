function component(desc)
	if desc.name == nil then
		error("A component descriptor must specify a name")
	end

	return {
		getDesc = function() return desc end,
		events = {}
	}
end

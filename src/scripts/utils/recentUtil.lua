local RECENT_LIST_MAX_SIZE = 30

return function(tab, type, name, path)
	local recent = tab.recent or {}

	-- Remove duplicates
	for i, v in ipairs(recent) do
		if v.path == path then
			table.remove(recent, i)
			break
		end
	end

	table.insert(recent, 1, {
		type = type,
		name = name,
		path = path,
	})

	if #recent > RECENT_LIST_MAX_SIZE then
		-- TODO: Limit size
	end

	return tab
end

local RECENT_LIST_MAX_SIZE = 30

return function(tab, romPath, romName, sramPath, projectPath)
	local recent = tab.recent or {}

	-- Remove duplicates
	for i, v in ipairs(recent) do
		if v.romPath == romPath or v.sramPath == sramPath or v.projectPath == projectPath then
			table.remove(recent, i)
			break
		end
	end

	table.insert(recent, 1, {
		romPath = romPath,
		romName = romName,
		sramPath = sramPath,
		projectPath = projectPath
	})

	if #recent > RECENT_LIST_MAX_SIZE then
		-- TODO: Limit size
	end

	return tab
end

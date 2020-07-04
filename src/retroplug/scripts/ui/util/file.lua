local fs = require("fs")
local Error = require("Error")

local function loadPathOrData(data)
	if type(data) == "string" then
		-- Is a path, load the data
		local fileData = fs.load(data)
		if fileData ~= nil then return fileData end
		return nil, Error("Failed to load " .. data)
	elseif type(data) == "userdata" then
		-- TODO: Check if this is a DataBuffer
		return data
	end

	return nil, Error("Unable to load data of type '" .. type(data) .. "'")
end

return {
	loadPathOrData = loadPathOrData
}

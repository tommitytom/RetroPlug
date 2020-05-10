local dialog = require("dialog")

local function handleError(err, action, name)
	if err ~= nil then
		dialog.error(err, "Failed to " .. action .. " " .. name)
		print(err, "Failed to " .. action .. " " .. name)
	end
end

local function loadHandler(filter, name, cb)
	return function()
		dialog.loadFile(filter, function(path)
			handleError(cb(path), "load", name)
		end)
	end
end

local function saveHandler(filter, name, forceDialog, cb)
	return function()
		if forceDialog == true then
			dialog.saveFile(filter, function(path)
				handleError(cb(path), "save", name)
			end)
		else
			handleError(cb(), "save", name)
		end
	end
end

return {
	loadHandler = loadHandler,
	saveHandler = saveHandler
}

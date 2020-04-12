local _fm = _proxy:fileManager()

local function load(path)
	local f = _fm:loadFile(path, true)
	if f ~= nil then
		return f.data
	end

	return nil
end

local function save(path, data)
	return _fm:saveFile(path, data)
end

local function saveText(path, data)
	return _fm:saveTextFile(path, data)
end

local function exists(path)
	return _fm:exists(path)
end

return {
	load = load,
	save = save,
	saveText = saveText,
	exists = exists
}

local _fm = _proxy:fileManager()

local function load(path, force)
	force = force or true
	local f = _fm:loadFile(path, force)
	if f ~= nil then return f.data end
	return nil
end

local function save(path, data)
	if type(data) == "string" then
		return _fm:saveTextFile(path, data)
	else
		return _fm:saveFile(path, data)
	end
end

local function saveText(path, data)
	return _fm:saveTextFile(path, data)
end

local function exists(path)
	return _fm:exists(path)
end

local function watch(path, cb)
	--_fm:watch()
end

local function removeWatch(watchId)
end

return {
	load = load,
	save = save,
	saveText = saveText,
	exists = exists,
	watch = watch,
	removeWatch = removeWatch
}

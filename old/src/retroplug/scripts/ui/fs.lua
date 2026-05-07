local Error = require("Error")

local _fs = nil

local function setup(fileSystem)
	_fs = fileSystem
end

local function load(path, force)
	force = force or true
	local f = _fs:loadFile(path, force)
	if f ~= nil then return f.data end
	return nil
end

local function loadText(path)
	local file = io.open(path, "rb") -- r read mode and b binary mode
	if not file then return nil end
	local content = file:read("*a") -- *a or *all reads the whole file
	file:close()
	return content
end

local function save(path, data)
	local ok
	if type(data) == "string" then
		ok = _fs:saveTextFile(path, data)
	else
		ok = _fs:saveFile(path, data)
	end

	if ok == false then
		return Error("Failed to save " .. path)
	end
end

local function saveText(path, data)
	return _fs:saveTextFile(path, data)
end

local function exists(path)
	if type(path) == "string" then
		return _fs:exists(path)
	end

	return false
end

local function watch(path, cb)
	assert(false)
	--_fs:watch()
end

local function removeWatch(watchId)
	assert(false)
end

return {
	__setup = setup,
	load = load,
	loadText = loadText,
	save = save,
	saveText = saveText,
	exists = exists,
	watch = watch,
	removeWatch = removeWatch
}

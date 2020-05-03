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

local function save(path, data)
	if type(data) == "string" then
		return _fs:saveTextFile(path, data)
	else
		return _fs:saveFile(path, data)
	end
end

local function saveText(path, data)
	return _fs:saveTextFile(path, data)
end

local function exists(path)
	return _fs:exists(path)
end

local function watch(path, cb)
	--_fs:watch()
end

local function removeWatch(watchId)
end

return {
	__setup = setup,
	load = load,
	save = save,
	saveText = saveText,
	exists = exists,
	watch = watch,
	removeWatch = removeWatch
}

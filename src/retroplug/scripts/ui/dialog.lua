local _dialogCallback
local _supportsMultiple

local _view
local function setup(view)
	_view = view
end

local function formatExtensions(exts)
	if type(exts) == "string" then
		return exts
	elseif type(exts) == "table" then
		return table.concat(exts, ";")
	end

	return ""
end

local function prepareFilters(filters, target)
	local formatted = {}
	for _, v in ipairs(filters) do
		table.insert(formatted, { v[1], formatExtensions(v[2]) })
	end

	if #filters > 1 then
		local exts = ""
		for _, v in ipairs(formatted) do
			if exts ~= "" then exts = exts .. ";" end
			exts = exts .. v[2]
		end

		table.insert(filters, 1, { "Supported Files", exts })
	end

	local desc = {}
	for _, v in ipairs(formatted) do
		local f = FileDialogFilters.new()
		f.name = v[1]
		f.extensions = v[2]
		target:add(f)
	end

	return desc
end

local function loadFile(filters, cb)
	if _dialogCallback ~= nil then print("WARNING: A previous dialog wasn't processed")	end
	_dialogCallback = cb
	_supportsMultiple = false

	local req = DialogRequest.new()
	req.type = DialogType.Load
	req.multiSelect = false
	prepareFilters(filters, req.filters)

	_view:requestDialog(req)
end

local function loadFiles(filters, cb)
	if _dialogCallback ~= nil then print("WARNING: A previous dialog wasn't processed")	end
	_dialogCallback = cb
	_supportsMultiple = true

	local req = DialogRequest.new()
	req.type = DialogType.Load
	req.multiSelect = true
	prepareFilters(filters, req.filters)

	_view:requestDialog(req)
end

local function saveFile(filters, fileName, cb)
	if _dialogCallback ~= nil then print("WARNING: A previous dialog wasn't processed") end
	_dialogCallback = nil

	local req = DialogRequest.new()
	req.type = DialogType.Save
	req.multiSelect = false

	if type(fileName) == "string" then
		req.fileName = fileName
	elseif type(fileName) == "function" then
		cb = fileName
	else
		return
	end

	prepareFilters(filters, req.filters)

	_dialogCallback = cb
	_supportsMultiple = false

	_view:requestDialog(req)
end

local function selectDirectory(cb)
	if _dialogCallback ~= nil then print("WARNING: A previous dialog wasn't processed")	end
	_dialogCallback = cb
	_supportsMultiple = false

	local req = DialogRequest.new()
	req.type = DialogType.Directory
	req.multiSelect = false

	_view:requestDialog(req)
end

local function onResult(paths)
	if _dialogCallback ~= nil then
		local cb = _dialogCallback
		_dialogCallback = nil

		if #paths > 0 then
			if _supportsMultiple == true then
				cb(paths)
			else
				cb(paths[1])
			end
		end
	end
end

return {
	__setup = setup,
	__onResult = onResult,
	saveFile = saveFile,
	loadFile = loadFile,
	loadFiles = loadFiles,
	selectDirectory = selectDirectory
}

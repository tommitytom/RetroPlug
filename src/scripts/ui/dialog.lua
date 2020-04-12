local _dialogCallback
local _supportsMultiple

local function formatExtensions(exts)
	if type(exts) == "string" then
		return exts
	elseif type(exts) == "table" then
		return table.concat(exts, ";")
	end

	return ""
end

local function prepareFilters(filters)
	local formatted = {}
	for _, v in ipairs(filters) do
		table.insert(formatted, { v[1], formatExtensions(v[2]) })
	end

	if #filters > 1 then
		local exts = ""
		for _, v in ipairs(filters) do
			if exts ~= "" then exts = exts .. ";" end
			exts = exts .. v[2]
		end

		table.insert(filters, 1, { "Supported Files", exts })
	end

	local desc = {}
	for _, v in ipairs(filters) do
		local f = FileDialogFilters.new()
		f.name = v[1]
		f.extensions = v[2]
		table.insert(desc, f)
	end

	return desc
end

local function loadFile(filters, cb)
	assert(_dialogCallback == nil)
	_dialogCallback = cb
	_supportsMultiple = true

	local desc = prepareFilters(filters)
	_requestDialog(DialogType.Load, desc)
end

local function saveFile(filters, cb)
	assert(_dialogCallback == nil)
	_dialogCallback = cb
	_supportsMultiple = false

	local desc = prepareFilters(filters)
	_requestDialog(DialogType.Save, desc)
end

local function saveDirectory(cb)

end

function _handleDialogCallback(paths)
	if _dialogCallback ~= nil then
		if _supportsMultiple == true then
			_dialogCallback(paths)
		else
			_dialogCallback(paths[1])
		end

		_dialogCallback = nil
	end
end

return {
	saveFile = saveFile,
	loadFile = loadFile,
	saveDirectory = saveDirectory
}

local function clean(str)
	return str:gsub("\\","\\\\"):gsub("\\","/")
end

local function ext(str)
	return str:match("[^.]+$")
end

local function changeExt(str, ext)
	return str:gsub("[^.]+$", ext)
end

local function filename(str)
	return clean(str):match("[^/]+$")
end

local function filepath(str)
	return clean(str):match("(.*/)")
end

return {
	clean = clean,
	ext = ext,
	changeExt = changeExt,
	filename = filename,
	filepath = filepath
}

local function clean(str)
	return str:gsub("\\","\\\\"):gsub("\\","/")
end

local function ext(str)
	return str:match("[^.]+$")
end

local function filename(str)
	return clean(str):match("[^/]+$")
end

return {
	ext = ext,
	filename = filename,
	clean = clean
}

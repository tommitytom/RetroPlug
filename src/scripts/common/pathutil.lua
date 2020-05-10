local function clean(str)
	return str:gsub("\\","\\\\"):gsub("\\","/")
end

-- Gets the file extension of the given path
-- NOTE: Does not include the .
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

local function join(p1, p2)
	if string.sub(p1, -1) ~= "/" then
		p1 = p1 .. "/"
	end

	return p1 .. p2
end

return {
	clean = clean,
	ext = ext,
	changeExt = changeExt,
	filename = filename,
	filepath = filepath,
	join = join
}

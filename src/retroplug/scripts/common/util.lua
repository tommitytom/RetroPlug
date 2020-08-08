local util = {}

function util.tableFind(tab, el)
    for index, value in pairs(tab) do
        if value == el then
            return index
        end
	end
end

-- Append the contents of t2 to t1
function util.tableConcat(t1, t2)
	for k,v in pairs(t2) do
		t1[k] = v
	end

	return t1
end

function util.tableRemoveElement(tab, el)
	local idx = util.tableFind(tab, el)
	if idx ~= nil then
		table.remove(tab, idx)
	end
end

function util.serializeComponent(obj, target)
	for k, v in pairs(obj) do
		if k:sub(1, 2) ~= "__" and type(v) ~= "function" then
			target[k] = v
		end
	end
end

function util.deserializeComponent(obj, source)
	for k, v in pairs(source) do
		obj[k] = v
	end
end

function util.getRomName(romData)
	return romData:slice(0x0134, 15):toString()
end

function util.trimString(str)
	return str:match('^()%s*$') and '' or str:match('^%s*(.*%S)')
end

function util.startsWith(str, start)
	return str:sub(1, #start) == start
end

function util.enumToString(enumType, value, lowerCamelCase)
	local mt = getmetatable(enumType)
	if mt then enumType = mt.__index end

	for k, v in pairs(enumType) do
		if value == v then
			if lowerCamelCase ~= true then
				return k
			else
				return k:sub(1, 1):lower() .. k:sub(2)
			end
		end
	end

	return ""
end

function util.stringToEnum(enumType, value)
	local v = enumType[value]
	if v ~= nil then return v end

	local vl = value:sub(1, 1):upper() .. value:sub(2)
	v = enumType[vl]
	if v ~= nil then return v end

	return 0
end

return util

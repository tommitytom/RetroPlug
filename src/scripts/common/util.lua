local function tableFind(tab, el)
    for index, value in pairs(tab) do
        if value == el then
            return index
        end
	end
end

-- Append the contents of t2 to t1
local function tableConcat(t1, t2)
	for k,v in pairs(t2) do
		t1[k] = v
	end

	return t1
end

local function tableRemoveElement(tab, el)
	local idx = tableFind(tab, el)
	if idx ~= nil then
		table.remove(tab, idx)
	end
end

local function serializeComponent(obj, target)
	for k, v in pairs(obj) do
		if k:sub(1, 2) ~= "__" and type(v) ~= "function" then
			target[k] = v
		end
	end
end

local function deserializeComponent(obj, source)
	for k, v in pairs(source) do
		obj[k] = v
	end
end

local function getRomName(romData)
	return romData:slice(0x0134, 15):toString()
end

local function trimString(s)
	return s:match'^()%s*$' and '' or s:match'^%s*(.*%S)'
 end

return {
	tableFind = tableFind,
	tableConcat = tableConcat,
	tableRemoveElement = tableRemoveElement,
	handleInput = handleInput,
	inputMap = inputMap,
	serializeComponent = serializeComponent,
	deserializeComponent = deserializeComponent,
	getRomName = getRomName,
	trimString = trimString
}

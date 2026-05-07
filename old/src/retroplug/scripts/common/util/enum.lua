local function toEnumString(enumType, value)
	local idx = enumType
	local meta = getmetatable(enumType)
	if meta ~= nil then idx = getmetatable(enumType).__index end

	for k, v in pairs(idx) do
		if value == v then return k end
	end
end

local function fromEnumString(enumType, value)
	local v = enumType[value]
	if v ~= nil then
		return v
	end

	local vl = value:sub(1, 1):upper() .. value:sub(2)
	return enumType[vl]
end

return {
	toEnumString = toEnumString,
	fromEnumString = fromEnumString
}

local function foreach<T, R>(tab: {T}, fn: function(T): R): {R}
	local items = {}
	for _,v in ipairs(tab) do
		table.insert(items, fn(v))
	end

	return items
end
function updateProps(obj, props)
	for k, v in pairs(props) do
		if type(k) == "string" then
			if k ~= "views" then
				local propType = type(obj[k])
				local valType = type(v)

				if propType ~= 'nil' then
					if propType == valType then
						obj[k] = v
					else
						print("Failed to set property '" .. obj.__type.name .. "::" .. k .. "': Tried to set property of type " .. propType .. " as a " .. valType)
					end
				else
					print("Failed to set property '" .. obj.__type.name .. "::" .. k .. "' as it does not exist")
				end
			else
				for _, child in ipairs(v) do
					obj:addChild(child)
				end
			end
		elseif type(k) == "table" then
			if type(k.__typeId) == "function" then
				self:subscribe(k, obj, v)
			else
				print("Unrecognised table type used to initialize property")
			end
		else
			print("Unrecognised key type '" .. type(k) .. "' used to initialize property")
		end
	end
end

local function createNativeMenu(menu, parent, id, menuLookup)
	local nativeMenu = _menuAlloc.menu(menu.name, parent)

	for _, v in ipairs(menu.items) do
		if v.type == MenuItemType.Title then
			nativeMenu:addItem(_menuAlloc.title(v.name))
		elseif v.type == MenuItemType.Action then
			nativeMenu:addItem(_menuAlloc.action(v.name, v.active, id))
			menuLookup[id] = v.handler
			id = id + 1
		elseif v.type == MenuItemType.Select then
			nativeMenu:addItem(_menuAlloc.select(v.name, v.checked, v.active, id))
			menuLookup[id] = function() v.handler(not v.checked) end
			id = id + 1
		elseif v.type == MenuItemType.MultiSelect then
			nativeMenu:addItem(_menuAlloc.multiSelect(v.items, v.selected, v.active, id))
			for i = 1, #v.items do
				menuLookup[id] = function() v.handler(i - 1) end
				id = id + 1
			end
		elseif v.type == MenuItemType.Separator then
			nativeMenu:addItem(_menuAlloc.separator())
		elseif v.type == MenuItemType.SubMenu then
			local sub, nid = createNativeMenu(v, nativeMenu, id, menuLookup)
			id = nid
			nativeMenu:addItem(sub)
		end
	end

	return nativeMenu, id
end

return createNativeMenu

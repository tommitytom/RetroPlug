global record MenuItemType
	name: string
	key: string
	type: string
	items: {string}
	children: {MenuItemType}
end

local menuItems: MenuItemType = {
	type = "submenu",
	children = {
		{
			type = "submenu",
			name = "Load",
			children = {
				{ name = "ROM..." },
				{
					name = "ROM As",
					children = {
						{ name = "ABG..." },
						{ name = "CBG C..." },
						{ name = "DMG..." },
					}
				}
			},
		},
		{
			type = "separator"
		},
		{
			type = "submenu",
			name = "stuff",
			children = {
				{
					name = "Select",
					type = "multiselect",
					items = { "a", "b", "c" }
				},
				{
					name = "Check",
					type = "checkbox",
				},
			}
		},
		{
			type = "submenu",
			name = "Add System",
			children = {
				{ name = "Duplicate current" },
				{ name = "ROM..." },
				{
					type = "submenu",
					name = "ROM As",
					children = {
						{ name = "ABG..." },
						{ name = "CBG C..." },
						{ name = "DMG..." },
					}
				}
			}
		}, {
			name = "Load MGB"
		}
	}
}

local function createKeys(item: MenuItemType, key: string)
	item.key = key

	if item.type == "submenu" and item.children then
		for i, child in ipairs(item.children) do
			createKeys(child, key .. "." .. i)
		end
	end
end

createKeys(menuItems, "1")

return menuItems

local MenuItemTypes = {
    None = 0,
    SubMenu = 1,
    Select = 2,
    MultiSelect = 3,
    Separator = 4
}

local function MultiSelect(items, selected, handler)
    return {
        type = MenuItemTypes.MultiSelect,
        items = items,
        selected = selected,
        handler = handler
    }
end

local function Select(name, value, handler)
    return {
        type = MenuItemTypes.Select,
        name = name,
        value = value,
        handler = handler
    }
end

local function Menu(name, items)
    return {
        type = MenuItemTypes.SubMenu,
        name = name,
        items = items
    }
end

local function Separator()
    return { type = MenuItemTypes.Separator }
end

return {
    MultiSelect = MultiSelect,
    Select = Select,
    Separator = Separator,
    Menu = Menu
}

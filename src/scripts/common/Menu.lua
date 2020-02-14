local function multiSelect(items, selected, handler)
    return {
        type = MenuItemType.MultiSelect,
        items = items,
        selected = selected,
        handler = handler
    }
end

local function select(name, value, handler)
    return {
        type = MenuItemType.Select,
        name = name,
        value = value,
        handler = handler
    }
end

local function action(name, handler)
    return {
        type = MenuItemType.Action,
        name = name,
        handler = handler
    }
end

local function title(name)
    return {
        type = MenuItemType.Title,
        name = name
    }
end

local function subMenu(name, items)
    return {
        type = MenuItemType.SubMenu,
        name = name,
        items = items
    }
end

local function separator()
    return { type = MenuItemType.Separator }
end

return {
    multiSelect = multiSelect,
    select = select,
    separator = separator,
    subMenu = subMenu,
    title = title,
    action = action
}

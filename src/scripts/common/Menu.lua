local class = require("class")

local Menu = class()
function Menu:init(name, parent)
    self.name = name or ""
    self._parent = parent
    self.items = {}
    self.type = MenuItemType.SubMenu
    self.active = true
end

function Menu:multiSelect(items, selected, handler, active)
    if active == nil then active = true end

    table.insert(self.items, {
        type = MenuItemType.MultiSelect,
        items = items,
        selected = selected,
        handler = handler,
        active = active
    })

    return self
end

function Menu:select(name, checked, handler, active)
    if active == nil then active = true end

    table.insert(self.items, {
        type = MenuItemType.Select,
        name = name,
        checked = checked,
        handler = handler,
        active = active
    })

    return self
end

function Menu:action(name, handler, active)
    if active == nil then active = true end

    table.insert(self.items, {
        type = MenuItemType.Action,
        name = name,
        handler = handler,
        active = active
    })

    return self
end

function Menu:title(name)
    table.insert(self.items, {
        type = MenuItemType.Title,
        name = name
    })

    return self
end

function Menu:subMenu(name, active)
    if active == nil then active = true end

    for _,v in ipairs(self.items) do
        if v.type == MenuItemType.SubMenu and v.name == name then
            return v
        end
    end

    local subMenu = Menu(name, self)
    subMenu.active = active
    table.insert(self.items, subMenu)
    return subMenu
end

function Menu:separator()
    table.insert(self.items, { type = MenuItemType.Separator })
    return self
end

function Menu:parent()
    return self._parent
end

return Menu

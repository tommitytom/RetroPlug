require("component")
require("constants")
require("Action")
require("Print")
local cm = require("ComponentManager")

local MAX_INSTANCES = 4
local _instances = {}

function KeyMap() end
function GlobalKeyMap() end
function PadMap() end
function GlobalPadMap() end

function _loadComponent(name)
    cm.loadComponent(name)
end

local function createInstance(model)
    return {
        model = model,
        components = cm.createComponents(model:getDesc(), model)
    }
end

function _init()
    cm.createGlobalComponents()

    for i = 1, MAX_INSTANCES, 1 do
        local instModel = _model:getInstance(i - 1)
        if instModel ~= nil then
            table.insert(_instances, createInstance(instModel))
        end
	end

	for _, instance in ipairs(_instances) do
		cm.runAllHandlers("onComponentsInitialized", instance.components, instance.components)
		cm.runAllHandlers("onReload", instance.components, instance.model)
	end
end

function _addInstance(idx, model)
    _instances[idx + 1] = createInstance(model)
end

function _removeInstance(idx)
    table.remove(_instances, idx + 1)
end

function _closeProject()
    _instances = {}
end

local function mergeMenu(target, menu)

end

local MenuItemType = {
    None = 0,
    Separator = 1,
    Single = 1,
    MultiSelect = 3,
    SubMenu = 4
}

local function parseMenu(menu)
    local out = {}
    for i, v in ipairs(menu) do
        if type(v[1]) == "object" then
            if v[1].type ~= nil then
                table.insert(out, v[1])
            else

            end
        elseif type(v[1]) == "string" then
            if v[1] ~= "-" then
                table.insert(out, v[1])
            else
                table.insert(out, { type = MenuItemType.Separator })
            end

        end
    end
end

function _onMenu(idx)
    local merged = {}
    for _, inst in ipairs(_instances) do
        for _, comp in ipairs(inst.components) do
            if comp.onMenu ~= nil then
                local menu = parseMenu(comp.onMenu(comp))
                mergeMenu(merged, menu)
            end
        end
    end

    return merged
end

local function processMidiMessage(inst, msg)
    cm.runComponentHandlers("onMidi", inst.components, msg)
end

function _onMidi(offset, status, data1, data2)
    local channel = status & 0x0F

    local r = _model:getSettings().midiRouting
    if r == MidiChannelRouting.SendToAll then
        local msg = { offset = offset, status = status, data1 = data1, data2 = data2 }
        for _, v in ipairs(_instances) do
            processMidiMessage(v, msg)
        end
    elseif r == MidiChannelRouting.OneChannelPerInstance then
        local target = _instances[channel]
        if target ~= nil then
            local msg = { offset = offset, status = status, data1 = data1, data2 = data2 }
            processMidiMessage(target, msg)
        end
    elseif r == MidiChannelRouting.FourChannelsPerInstance then
        local targetIdx = math.floor(channel / 4)
        local target = _instances[targetIdx + 1]
        if target ~= nil then
            local msg = { offset = offset, status = (channel | (status << 4)), data1 = data1, data2 = data2 }
            processMidiMessage(target, msg)
        end
    end
end

inspect = require("inspect")
function prinspect(...) print(inspect(...)) end

require("component")
require("constants")
require("Action")
require("Print")
local serializer = require("serializer")
local cm = require("ComponentManager")
local LuaMenu = require("Menu")
local createNativeMenu = require("MenuHelper")
local ppq = require("ppq")

local MAX_INSTANCES = 4
local _instances = {}

function KeyMap() end
function GlobalKeyMap() end
function PadMap() end
function GlobalPadMap() end

function _loadComponent(name)
    cm.loadComponent(name)
end

local function createInstance(model, buttons)
    print("name:", model:getDesc().state)
    return {
        model = model,
        components = cm.createComponents(model:getDesc(), model, buttons),
        buttons = buttons
    }
end

function _init()
    cm.createGlobalComponents()

    for i = 1, MAX_INSTANCES, 1 do
        local instModel = _model:getInstance(i - 1)
        if instModel ~= nil then
            table.insert(_instances, createInstance(instModel, _model:getButtonPresses(i - 1)))
        end
	end

	for _, instance in ipairs(_instances) do
		cm.runAllHandlers("onComponentsInitialized", instance.components, instance.components)
		cm.runAllHandlers("onReload", instance.components, instance.model)
	end
end

function _addInstance(idx, model)
    _instances[idx + 1] = createInstance(model, _model:getButtonPresses(idx))
end

function _duplicateInstance(sourceIdx, targetIdx, model)
    _instances[targetIdx + 1] = createInstance(model, _model:getButtonPresses(targetIdx))
    local instData = serializer.serializeInstanceToString(_instances[sourceIdx + 1])
    serializer.deserializeInstanceFromString(_instances[targetIdx + 1], instData)
end

function _removeInstance(idx)
    table.remove(_instances, idx + 1)
end

function _closeProject()
    _instances = {}
end

local _transportRunning = false

function _update(frameCount)
    if _timeInfo == nil then
        print("time info not set")
        return
    end

    if _timeInfo.transportIsRunning ~= _transportRunning then
        _transportRunning = _timeInfo.transportIsRunning
        cm.runComponentHandlers("onTransportChanged", cm.allComponents, _transportRunning)
    end

    if _transportRunning == true then
        local ppqTrigger, offset = ppq.generatePpq24(frameCount, _sampleRate, _timeInfo)
        if ppqTrigger == true then
            cm.runComponentHandlers("onPpq", cm.allComponents, offset)
        end
    end
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

local _menuLookup = nil

function _onMenu(menus)
	local menu = LuaMenu()
	local componentsMenu = menu:subMenu("System"):subMenu("Components")

	if Active ~= nil then
		for _, comp in ipairs(Active.components) do
			componentsMenu:title(comp.__desc.name)
		end
	end

    for _, inst in ipairs(_instances) do
        for _, comp in ipairs(inst.components) do
			if comp.onMenu ~= nil then
				comp:onMenu(menu)
			end
        end
	end

	local menuLookup = {}
	menus:add(createNativeMenu(menu, nil, LUA_MENU_ID_OFFSET, menuLookup))
	_menuLookup = menuLookup
end

function _onMenuResult(idx)
	if _menuLookup ~= nil then
		local callback = _menuLookup[idx]
		if callback ~= nil then
			callback()
		end

		_menuLookup = nil
	end
end

function _serializeInstances() return serializer.serializeInstancesToString(_instances) end
function _serializeInstance(idx) return serializer.serializeInstanceToString(_instances[idx + 1]) end
function _deserializeInstances(data) serializer.deserializeInstancesFromString(_instances, data) end

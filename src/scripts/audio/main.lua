inspect = require("inspect")
function prinspect(...) print(inspect(...)) end

require("component")
require("constants")
require("Action")
require("Print")
local serializer = require("serializer")
local cm = require("ComponentManager")
local LuaMenu = require("Menu")
local System = require("System")
local ppq = require("ppq")
local midi = require("midi")

local MAX_INSTANCES = 4
local _instances = {}

function KeyMap() end
function GlobalKeyMap() end
function PadMap() end
function GlobalPadMap() end

function _loadComponent(name)
	cm.loadComponent(name)
end

local function createInstance(system)
	return {
		system = system,
		components = cm.createComponents(system)
	}
end

function _init()
	cm.createGlobalComponents()

	for i = 1, MAX_INSTANCES, 1 do
		local instModel = _model:getInstance(i - 1)
		if instModel ~= nil then
			local system = System(instModel, _model:getButtonPresses(i - 1))
			table.insert(_instances, createInstance(system))
		end
	end

	for _, instance in ipairs(_instances) do
		cm.runAllHandlers("onComponentsInitialized", instance.components, instance.components)
		cm.runAllHandlers("onReload", instance.components, instance.model)
	end
end

function _addInstance(idx, model, componentState)
	local system = System(model, _model:getButtonPresses(idx))
	local instance = createInstance(system)
	serializer.deserializeInstanceFromString(instance, componentState)
	_instances[idx + 1] = instance
end

function _duplicateInstance(sourceIdx, targetIdx, model)
	local system = System(model, _model:getButtonPresses(targetIdx))
	_instances[targetIdx + 1] = createInstance(system)
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

function _onMidi(offset, statusByte, data1, data2)
	local channel = statusByte & 0x0F
	local status = statusByte >> 4

	local r = _model:getSettings().midiRouting
	if status == midi.Status.System or r == MidiChannelRouting.SendToAll then
		local msg = midi.Message(offset, statusByte, data1, data2)
		for _, v in ipairs(_instances) do
			processMidiMessage(v, msg)
		end
	elseif r == MidiChannelRouting.OneChannelPerInstance then
		local target = _instances[channel]
		if target ~= nil then
			local msg = midi.Message(offset, statusByte, data1, data2)
			processMidiMessage(target, msg)
		end
	elseif r == MidiChannelRouting.FourChannelsPerInstance then
		local targetIdx = math.floor(channel / 4)
		local target = _instances[targetIdx + 1]
		if target ~= nil then
			local msg = midi.Message(offset, (channel | (status << 4)), data1, data2)
			processMidiMessage(target, msg)
		end
	end
end

local _menuLookup = nil

function _onMenu(idx, menus)
	local menu = LuaMenu()
	local componentsMenu = menu:subMenu("System"):subMenu("Audio Components")

	local inst = _instances[idx + 1]
	if inst ~= nil then
		componentsMenu
			:subMenu("Add")
				:action("MIDI Passthrough")
				:parent()
			:separator()

		for _, comp in ipairs(inst.components) do
			componentsMenu:select(comp.__desc.name, comp:enabled(), function(enabled) comp:setEnabled(enabled) end)

			if comp:enabled() == true and comp.onMenu ~= nil then
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

local _activeIdx = -1

function _setActive(idx)
	local inst = _instances[idx + 1]
	if inst ~= nil then
		_activeIdx = idx + 1
		Active = inst
	end
end

function _serializeInstances() return serializer.serializeInstancesToString(_instances) end
function _serializeInstance(idx) return serializer.serializeInstanceToString(_instances[idx + 1]) end
function _deserializeInstances(data) serializer.deserializeInstancesFromString(_instances, data) end

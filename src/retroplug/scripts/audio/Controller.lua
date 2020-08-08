local class = require("class")
local createNativeMenu = require("MenuHelper")
local LuaMenu = require("Menu")
local System = require("System")
local ComponentManager = require("ComponentManager")
local ppq = require("ppq")
local midi = require("midi")
local log = require("log")
local componentutil = require("util.component")
local ConfigLoader = require("ConfigLoader")
local InputConfig = require("InputConfigParser")

local MAX_SYSTEMS = 4

local Controller = class()
function Controller:init()
	self._menuLookup = nil
	self._components = {}
	self._systems = {}
	self._selectedIdx = 0
	self._transportRunning = false
	self._timeInfo = nil
	self._inputConfig = InputConfig()
end

function Controller:setup(model, timeInfo)
	self._components = ComponentManager.createComponents()
	self._model = model
	self._timeInfo = timeInfo
end

function Controller:loadConfigFromPath(path)
	self._config = nil

	local ok, config = ConfigLoader.loadConfigFromPath(path)
	if ok then
		self._config = config
		return true
	end

	return false
end

function Controller:initProject()
	for i = 1, MAX_SYSTEMS, 1 do
		local instModel = self._model:getInstance(i - 1)
		if instModel ~= nil then
			local system = System(instModel, self._model:getButtonPresses(i - 1))
			table.insert(self._systems, system)
		end
	end

	self:emit("onSetup")
	self:emit("onComponentsInitialized", self.components)
	self:emit("onReload")
end

function Controller:setActive(idx)
	local system = self._systems[idx + 1]
	if system ~= nil then
		self._selectedIdx = idx + 1
	else
		log.warn("Failed to set active system to idx " .. idx)
	end
end

function Controller:emit(name, ...)
	componentutil.emitComponentEvent(name, self._components, ...)
end

function Controller:update(frameCount)
	if self._timeInfo == nil then
		print("Time info not set")
		return
	end

	if self._timeInfo.transportIsRunning ~= self._transportRunning then
		self._transportRunning = self._timeInfo.transportIsRunning
		self:emit("onTransportChanged", self._transportRunning)
	end

	if self._transportRunning == true then
		local ppqTrigger, offset = ppq.generatePpq24(frameCount, self._sampleRate, self._timeInfo)
		if ppqTrigger == true then
			self:emit("onPpq", offset)
		end
	end
end

function Controller:onMidi(offset, statusByte, data1, data2)
	local channel = statusByte & 0x0F
	local status = statusByte >> 4

	local r = self._model:getSettings().midiRouting
	if status == midi.Status.System or r == MidiChannelRouting.SendToAll then
		local msg = midi.Message(offset, statusByte, data1, data2)
		for _, v in ipairs(self._systems) do
			self:emit("onMidi", v, msg)
		end
	elseif r == MidiChannelRouting.OneChannelPerInstance then
		local target = self._systems[channel]
		if target ~= nil then
			local msg = midi.Message(offset, statusByte, data1, data2)
			self:emit("onMidi", target, msg)
		end
	elseif r == MidiChannelRouting.FourChannelsPerInstance then
		local targetIdx = math.floor(channel / 4)
		local target = self._systems[targetIdx + 1]
		if target ~= nil then
			local msg = midi.Message(offset, (channel | (status << 4)), data1, data2)
			self:emit("onMidi", target, msg)
		end
	end
end

function Controller:onMenu(idx, menus)
	local menu = LuaMenu()
	self.model:emit("onMenu", menu)

	local menuLookup = {}
	menus:add(createNativeMenu(menu, nil, LUA_MENU_ID_OFFSET, menuLookup))
	self._menuLookup = menuLookup
end

function Controller:onMenuResult(idx)
	if self._menuLookup ~= nil then
		local callback = self._menuLookup[idx]
		if callback ~= nil then callback() end
		self._menuLookup = nil
	end
end

function Controller:onDialogResult(paths)
	--dialog.__onResult(paths)
end

function Controller:addInstance(idx, model, componentState)
	local system = System(model, self._model:getButtonPresses(idx))
	--local instance = createInstance(system)
	--serializer.deserializeInstanceFromString(instance, componentState)
	self._systems[idx + 1] = system
end

function Controller:duplicateInstance(sourceIdx, targetIdx, model)
	local system = System(model, self._model:getButtonPresses(targetIdx))
	self._systems[targetIdx + 1] = system
	--local instData = serializer.serializeInstanceToString(_systems[sourceIdx + 1])
	--serializer.deserializeInstanceFromString(_systems[targetIdx + 1], instData)
end

function Controller:removeInstance(idx)
	table.remove(self._systems, idx + 1)
end

function Controller:closeProject()
	self._systems = {}
end

function Controller:loadInputConfig(path)
	self._inputConfig:load(path)
end

return Controller

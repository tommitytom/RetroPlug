local class = require("class")
local createNativeMenu = require("MenuHelper")
local LuaMenu = require("Menu")
local GameboySystem = require("System")
local ComponentManager = require("ComponentManager")
local ppq = require("ppq")
local midi = require("midi")
local componentutil = require("util.component")
local ConfigLoader = require("ConfigLoader")
local InputConfig = require("InputConfigParser")
local const = require("const")
local serpent = require("serpent")

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

	Project._componentState = componentutil.createState(self._components)
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
	for i = 1, const.MAX_SYSTEMS, 1 do
		local instModel = self._model:getInstance(i - 1)
		if instModel ~= nil then
			local state = componentutil.createState(self._components)
			local system = GameboySystem(instModel, self._model:getButtonPresses(i - 1), state)
			table.insert(Project.systems, system)
		end
	end

	if #Project.systems > 0 then
		self:setActive(0)
	end

	self:emit("onSetup")
	self:emit("onReload")
end

function Controller:setActive(idx)
	self._selectedIdx = idx + 1

	local system = Project.systems[self._selectedIdx]
	Project.system = system
	System = system
end

function Controller:emit(name, ...)
	componentutil.emitComponentEvent(self._components, name, ...)
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
		for _, v in ipairs(Project.systems) do
			self:emit("onMidi", v, msg)
		end
	elseif r == MidiChannelRouting.OneChannelPerInstance then
		local target = Project.systems[channel]
		if target ~= nil then
			local msg = midi.Message(offset, statusByte, data1, data2)
			self:emit("onMidi", target, msg)
		end
	elseif r == MidiChannelRouting.FourChannelsPerInstance then
		local targetIdx = math.floor(channel / 4)
		local target = Project.systems[targetIdx + 1]
		if target ~= nil then
			local msg = midi.Message(offset, (channel | (status << 4)), data1, data2)
			self:emit("onMidi", target, msg)
		end
	end
end

function Controller:onMenu(idx, menus)
	local menu = LuaMenu()
	self:emit("onMenu", menu)

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
	local state = componentutil.createState(self._components)
	local system = GameboySystem(model, self._model:getButtonPresses(idx), state)

	if componentState ~= nil and componentState ~= "" then
		local ok, state = serpent.load(componentState)
		if ok == true then system.state = state end
	end

	Project.systems[idx + 1] = system

	if self._selectedIdx == idx + 1 then
		self:setActive(idx)
	end
end

function Controller:duplicateInstance(sourceIdx, targetIdx, model)
	local state = componentutil.createState(self._components)
	local system = GameboySystem(model, self._model:getButtonPresses(targetIdx), state)
	Project.systems[targetIdx + 1] = system

	if self._selectedIdx == targetIdx + 1 then
		self:setActive(targetIdx)
	end
	--local instData = serializer.serializeInstanceToString(_systems[sourceIdx + 1])
	--serializer.deserializeInstanceFromString(_systems[targetIdx + 1], instData)
end

function Controller:removeInstance(idx)
	table.remove(Project.systems, idx + 1)

	if self._selectedIdx >= #Project.systems then
		self._selectedIdx = #Project.systems
	end
end

function Controller:serializeInstance(idx, pretty)
	local opts = { comment = false }
	if pretty == true then opts.indent = '\t' end
	return serpent.block(Project.systems[idx + 1].state, opts)
end

function Controller:serializeInstances(pretty)
	local opts = { comment = false }
	if pretty == true then opts.indent = '\t' end

	local instances = {}
	for i, v in ipairs(Project.systems) do
		table.insert(instances, v.state)
	end

	return serpent.block(instances, opts)
end

function Controller:deserializeInstances(data)
	local ok, state = serpent.load(data)
	if ok == true then
		for i, v in ipairs(state) do
			local system = Project.systems[i]
			if system ~= nil then
				system.state = v
			end
		end
	end
end

function Controller:deserializeInstance(idx, data)
	print(idx, data)
	--local system = Project.systems[idx + 1]

	--log.obj(system)

	if data ~= nil and data ~= "" then
		--local ok, state = serpent.load(data)
		--if ok == true then system.state = state end
	end
end



function Controller:closeProject()
	Project.clear()
end

function Controller:loadInputConfig(path)
	self._inputConfig:load(path)
end

return Controller

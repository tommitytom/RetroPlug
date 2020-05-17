local class = require("class")
local util = require("util")
local const = require("const")
local fs = require("fs")
local pathutil = require("pathutil")
local fileutil = require("util.file")
local componentutil = require("util.component")
local ComponentManager = require("ComponentManager")

local System = class()
function System:init(desc, model)
	self._audioContext = nil
	self.components = {}

	if type(desc) == "userdata" then
		if desc.__type.name == "SystemDesc" then
			self.desc = desc
			self.components = ComponentManager.createSystemComponents(self)
		elseif desc.__type.name == "DataBuffer" then
			self:loadRom(desc)
		end
	elseif type(desc) == "string" then
		self:loadRom(desc)
	end

	if model ~= nil then self.desc.sameBoySettings.model = model end
	self._desc = self.desc
end

function System:emit(eventName, ...)
	componentutil.emitComponentEvent(eventName, self.components, ...)
end

function System:clone()
	return System(SystemDesc.new(self.desc))
end

function System:getIndex(idx)
	return self.components[idx]
end

function System:getComponent(idx)
	return self.components[idx]
end

function System:setSram(data, reset)
	self:emit("onSramSet", data)

	if isNullPtr(self._audioContext) == false then
		self._audioContext:setSram(self.desc.idx, data, reset)
	end
end

function System:clearSram(reset)
	local buf = DataBuffer.new(const.SRAM_SIZE)
	buf:clear()
	self:emit("onSramClear", buf)

	if isNullPtr(self._audioContext) == false then
		self._audioContext:setSram(self.desc.idx, buf, reset)
	end
end

function System:setRom(data, reset)
	if reset == nil then reset = false end
	self.desc.sourceRomData = data
	self.desc.romName = util.getRomName(data)
	self:emit("onRomSet", data)

	if isNullPtr(self._audioContext) == false then
		self._audioContext:setRom(self.desc.idx, data, reset)
	end
end

-- Loads a ROM from a path string, or data buffer.  Rebuilds the
-- component array and resets the emulator.  The second 'path' parameter
-- allows you to pass a path for metadata purposes.
function System:loadRom(data, path)
	local fileData, err = fileutil.loadPathOrData(data)
	if err ~= nil then return err end

	if type(data) == "string" then path = data end
	if self.desc == nil then self.desc = SystemDesc.new() end

	local d = self.desc
	local idx = d.idx
	d:clear()
	d.idx = idx

	if path ~= nil then
		d.romPath = path
	end

	d.emulatorType = SystemType.SameBoy
	d.state = SystemState.Initialized
	d.sourceRomData = fileData
	d.romName = util.getRomName(fileData)

	if d.romPath ~= nil then
		local savPath = pathutil.changeExt(d.romPath, "sav")
		if fs.exists(savPath) == true then
			local savData = fs.load(savPath, false)
			if savData ~= nil then
				d.savPath = savPath
				d.sourceSavData = savData
			end
		end
	end

	self.components = ComponentManager.createSystemComponents(self)

	self:emit("onComponentsInitialized", self.components)
	self:emit("onRomLoad")

	if isNullPtr(self._audioContext) == false then
		self._audioContext:loadRom(d)
	end
end

function System:loadSram(data, reset, path)
	if type(data) == "string" then
		path = data
		data = fs.load(path)
	end

	if path ~= nil then
		self.desc.sramPath = path
	end

	self:emit("onSramLoad", data)

	if isNullPtr(self._audioContext) == false then
		self._audioContext:setSram(self.desc.idx, data, reset)
	end
end

function System:saveSram(path)
	return fs.save(path, self.desc.sourceSavData)
end

function System:saveRom(path)
	return fs.save(path, self.desc.sourceRomData)
end

function System:sram()
	return self.desc.sourceSavData
end

function System:rom()
	return self.desc.sourceRomData
end

function System:buttons()
	return self.desc.buttons
end

function System:reset(model)
	if model == nil or model == GameboyModel.Auto then
		model = self.desc.sameBoySettings.model
	end

	if isNullPtr(self._audioContext) == false then
		self._audioContext:resetSystem(self.desc.idx, model)
	end
end

return System

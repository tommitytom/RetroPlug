local class = require("class")
local util = require("util")
local const = require("const")
local fs = require("fs")
local componentutil = require("util.component")

local System = class()
function System:init(desc)
	self.desc = desc
	self._audioContext = nil
	self._components = {}
end

function System:emit(eventName, ...)
	componentutil.emitComponentEvent(eventName, self._components, ...)
end

function System:clone()
	return System(self.desc:clone())
end

function System:getComponent(idx)
	return self._components[idx]
end

function System:setSram(data, reset)
	self:emit("onSramSet", data)
	self._audioContext:setSram(self.desc.idx, data, reset)
end

function System:clearSram(reset)
	local buf = DataBuffer.new(const.SRAM_SIZE)
	buf:clear()

	self:emit("onSramClear", buf)
	self._audioContext:setSram(self.desc.idx, buf, reset)
end

function System:setRom(data, reset)
	self.desc.sourceRomData = data
	self.desc.romName = util.getRomName(data)
	self:emit("onRomSet", data)
	self._audioContext:setRom(self.desc.idx, data, reset or false)
end

-- Loads a ROM from a path string, or data buffer.  Rebuilds the
-- component array and resets the emulator.  The second 'path' parameter
-- allows you to pass a path for metadata purposes.
function System:loadRom(data, path)
	if type(data) == "string" then
		path = data
		data = fs.load(path)
	end

	if path ~= nil then
		self.desc.romPath = path
	end

	self.desc.sourceRomData = data
	self.desc.romName = util.getRomName(data)

	-- TODO: Find components

	self:emit("onRomLoad", data)
	self._audioContext:loadRom(self.desc)
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
	self._audioContext:setSram(self.desc.idx, data, reset)
end

function System:saveSram(path)
	return fs.save(path, self.desc.sourceSavData)
end

function System:saveRom(path)
	return fs.save(path, self.desc.sourceRomData)
end

function System:desc()
	return self.desc
end

function System:sram()
	return self.desc.sourceSavData
end

function System:rom()
	return self.desc.sourceRomData
end

function System:buttons()
	return self._buttons
end

function System:reset(model)
	if model == nil or model == GameboyModel.Auto then
		model = self.desc.sameBoySettings.model
	end

	self._audioContext:resetSystem(self.desc.idx, model)
end

return System

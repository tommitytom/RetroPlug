local class = require("class")
local pathutil = require("pathutil")
local fs = require("fs")

local Kit = require("liblsdj.kit")

local BANK_COUNT = 64
local BANK_SIZE = 0x4000

local function bankIsKit(bank)
	return bank:get(0) == 0x60 and bank:get(1) == 0x40
end

local function bankIsEmptyKit(bank)
	return bank:get(0) == -1 and bank:get(1) == -1
end

local Rom = class()
function Rom:init(romData)
	self.romData = romData
	self.kits = nil
	self._kitChecksums = {}
end

function Rom:importKits(items)
	-- Items can contain: a path, a buffer, or an array of paths and buffers
	local t = type(items)
	if t == "string" then
		-- Import kit from path.  Path could be a .kit or .gb
		local data = fs.load(items)
		if data ~= nil then
			local ext = pathutil.ext(items)
			if ext == ".kit" then
				self:_importKitFromBuffer(data)
			elseif ext == ".gb" then
				self:_importKitsFromRom(data)
			else
				print("Resource has an unknown file extension " .. ext)
			end
		end
	elseif t == "table" then
		for _, v in ipairs(items) do
			self:importKits(v)
		end
	else--if t == "userdata" then
		if items:size() == BANK_SIZE then
			self:_importKitFromBuffer(items)
		else
			self:_importKitsFromRom(items)
		end
	end
end

function Rom:exportKits(dirPath)
	self:_parseKits()

	for _, kit in ipairs(self.kits) do
		local path = pathutil.join(dirPath, kit.name .. ".kit")
		kit:toFile(path)
	end
end

function Rom:exportKit(idx, filePath)
	self:_parseKits()
	local kit = self.kits[idx]
	if kit ~= nil and kit.data ~= nil then
		kit:toFile(filePath)
	end
end

function Rom:toBuffer()
	local romData = DataBuffer.new(self.romData:size())
	self.romData:clone(romData)

	local kitIdx = 1
	for i = 0, BANK_COUNT - 1, 1 do
		local offset = i * BANK_SIZE
		local bank = self.romData:slice(offset, BANK_SIZE)

		if bankIsKit(bank) == true or bankIsEmptyKit(bank) == true then
			local kit = self.kits[kitIdx]
			if kit.data ~= nil then
				bank:write(kit.data)
			else
				bank:clear()
				bank:set(0, -1)
				bank:set(1, -1)
			end

			kitIdx = kitIdx + 1
		end
	end

	return romData
end

function Rom:toFile(filePath)
	local buf = self:toBuffer()
	fs.save(buf, filePath)
end

function Rom:nextEmptyKitIdx(startIdx)
	startIdx = startIdx or 1
	for i = startIdx, #self.kits, 1 do
		local kit = self.kits[i]
		if kit.data == nil then
			return i
		end
	end

	return 0
end

function Rom:_parseKits(force)
	if self.kits == nil or force == true then
		self.kits = {}
		self._kitChecksums = {}
		for i = 0, BANK_COUNT - 1, 1 do
			local offset = i * BANK_SIZE
			local bank = self.romData:slice(offset, BANK_SIZE)

			if bankIsKit(bank) == true then
				local kit = Kit(bank)
				table.insert(self.kits, kit)
				self._kitChecksums[kit.checksum] = true
			elseif bankIsEmptyKit(bank) == true then
				table.insert(self.kits, Kit())
			end
		end
	end
end

function Rom:_importKitFromBuffer(kitData, targetIdx)
	targetIdx = targetIdx or self:nextEmptyKitIdx()
	if targetIdx > 0 then
		local kit = Kit(kitData)
		if self._kitChecksums[kit.checksum] ~= true then
			self.kits[targetIdx] = kit
			self._kitChecksums[kit.checksum] = true
		end

		return true
	else
		print("Failed to import kit: All kit slots are in use")
	end

	return false
end

function Rom:_importKitsFromRom(romData)
	local other = Rom(romData)
	other:_parseKits()

	local targetIdx = self:nextEmptyKitIdx()
	if targetIdx > 0 then
		for sourceIdx, kit in ipairs(other.kits) do
			if targetIdx > 0 then
				if kit.data ~= nil then
					self:_importKitFromBuffer(kit.data)
				end
			else
				local amount = #other.kits - sourceIdx
				print("Failed to import " .. amount .. " of " .. #other.kits .. " kits.  All kit slots are in use")
			end
		end
	end
end

return Rom

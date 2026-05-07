local class = require("class")
local pathutil = require("pathutil")
local fs = require("fs")

local Kit = require("liblsdj.kit")

local BANK_COUNT = 64
local BANK_SIZE = 0x4000
local ROM_SIZE = 1048576

local function bankIsKit(bank)
	return string.byte(bank:get(0)) == 0x60 and string.byte(bank:get(1)) == 0x40
end

local function bankIsEmptyKit(bank)
	return string.byte(bank:get(0)) == 0xFF and string.byte(bank:get(1)) == 0xFF
end

local Rom = class()
function Rom:init(romData)
	self.romData = romData
	self.kits = nil
	self._kitChecksums = {}
end

function Rom:importKits(items)
	self:_parseKits()

	-- Items can contain: a path, a buffer, or an array of paths and buffers
	local t = type(items)
	if t == "string" then
		-- Import kit from path.  Path could be a .kit or .gb
		local data = fs.load(items)
		if data ~= nil then
			local ext = pathutil.ext(items)
			if ext == "kit" then
				self:_importKitFromBuffer(data)
			elseif ext == "gb" then
				self:_importKitsFromRom(data)
			else
				print("Resource has an unknown file extension " .. ext)
			end
		end
	elseif t == "table" then
		for _, v in ipairs(items) do
			self:importKits(v)
		end
	elseif t == "userdata" then
		if items:size() == BANK_SIZE then
			self:_importKitFromBuffer(items)
		elseif items:size() == ROM_SIZE then
			self:_importKitsFromRom(items)
		else
			for _, v in ipairs(items) do
				self:importKits(v)
			end
		end
	end
end

function Rom:getKits()
	self:_parseKits()
	return self.kits
end

function Rom:exportKits(dirPath)
	self:_parseKits()

	for _, kit in ipairs(self.kits) do
		if kit:isValid() then
			local path = pathutil.join(dirPath, kit.name .. ".kit")
			kit:toFile(path)
		end
	end
end

function Rom:exportKit(idx, filePath)
	self:_parseKits()
	local kit = self.kits[idx]
	if kit ~= nil and kit.data ~= nil then
		kit:toFile(filePath)
	end
end

function Rom:eraseKit(idx)
	self:_parseKits()
	if idx > 0 and idx <= #self.kits then
		self.kits[idx] = Kit()
	end
end

function Rom:copyFrom(other)
	local kits = self:getKits()
	local newKits = other:getKits()
	for i, kit in ipairs(kits) do newKits[i]:copyFrom(kit) end
end

function Rom:toBuffer(romData)
	if romData == nil then romData = DataBuffer.new(self.romData:size()) end
	self.romData:copyTo(romData)

	local kitIdx = 1
	for i = 0, BANK_COUNT - 1, 1 do
		local offset = i * BANK_SIZE
		local bank = romData:slice(offset, BANK_SIZE)

		if bankIsKit(bank) == true or bankIsEmptyKit(bank) == true then
			local kit = self.kits[kitIdx]
			if kit.data ~= nil then
				bank:copyFrom(kit.data)
			else
				bank:clear()
				bank:set(0, string.char(0xFF))
				bank:set(1, string.char(0xFF))
			end

			kitIdx = kitIdx + 1
		end
	end

	return romData
end

function Rom:toFile(filePath)
	local buf = self:toBuffer()
	fs.save(filePath, buf)
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

function Rom:getKitChecksumLookup()
	self:_parseKits()
	local checksums = {}
	for _, v in ipairs(self.kits) do
		if v.data ~= nil then
			checksums[v.data:hash(0)] = true
		end
	end

	return checksums
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
			elseif bankIsEmptyKit(bank) == true then
				table.insert(self.kits, Kit())
			end
		end
	end
end

function Rom:_importKitFromBuffer(kitData, targetIdx)
	targetIdx = targetIdx or self:nextEmptyKitIdx()

	local checksums = self:getKitChecksumLookup()
	if checksums[kitData:hash(0)] ~= true then
		if targetIdx > 0 then
			self.kits[targetIdx] = Kit(kitData)
			return true
		else
			print("Failed to import kit: All kit slots are in use")
		end
	end

	return false
end

function Rom:_importKitsFromRom(romData)
	local other = Rom(romData)

	local checksums = self:getKitChecksumLookup()

	local targetIdx = self:nextEmptyKitIdx()
	for sourceIdx, kit in ipairs(other.kits) do
		if targetIdx > 0 then
			if kit.data ~= nil then
				if checksums[kit.data:hash(0)] ~= true then
					self:_importKitFromBuffer(kit.data)
				end
			end

			targetIdx = self:nextEmptyKitIdx()
		else
			local amount = #other.kits - sourceIdx
			print("Failed to import " .. amount .. " of " .. #other.kits .. " kits.  All kit slots are in use")
			break
		end
	end
end

return Rom

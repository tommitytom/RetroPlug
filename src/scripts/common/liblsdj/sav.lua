local class = require("class")
local fs = require("fs")
local pathutil = require("pathutil")
local enumutil = require("util.enum")

local LSDJ_SAV_SIZE = 131072
local LSDJ_SAV_PROJECT_COUNT = 32

local Sav = class()
function Sav:init(savData)
	if savData ~= nil then
		if type(savData) == "string" then
			local data, err = fs.load(savData)
			if err == LsdjError.Success then
				savData = data
			else
				print("Failed to load LSDj sav from " .. savData)
			end
		end

		local sav, err = liblsdj.sav_read_from_memory(savData)
		if err == LsdjError.Success then
			self._sav = sav
		else
			print("Failed to load LSDj sav from memory")
		end
	else
		local sav, err = liblsdj.sav_new()
		if err == LsdjError.Success then
			self._sav = sav
		else
			print("Failed to create new LSDj sav")
		end
	end
end

function Sav:isValid()
	return self._sav ~= nil
end

function Sav:loadSong(songIdx)
	local err = liblsdj.sav_set_working_memory_song_from_project(self._sav, songIdx)
	if err ~= LsdjError.Success then
		return "Failed to load song at index " .. tostring(songIdx)
	end
end

function Sav:toBuffer()
	local buffer = DataBuffer.new()
	local err = liblsdj.sav_write_to_memory(self._sav, buffer)
	if err == LsdjError.Success then
		return buffer
	end

	return "Failed to write sav to buffer: " .. enumutil.toEnumString(lsdj_error_t, err)
end

function Sav:importSongs(items)
	-- Items can contain: a path, a buffer, or an array of paths and buffers
	local t = type(items)
	if t == "string" then
		-- Import song from path.  Path could be a .lsdsng or .sav
		local data = fs.load(items)
		if data ~= nil then
			local ext = pathutil.ext(items)
			if ext == "lsdsng" then
				self:_importSongFromBuffer(data)
			elseif ext == "sav" then
				self:_importSongsFromSav(data)
			else
				print("Resource has an unknown file extension " .. ext)
			end
		end
	elseif t == "table" then
		for _, v in ipairs(items) do
			self:importSongs(v)
		end
	else--if t == "userdata" then
		if items:size() == LSDJ_SAV_SIZE then
			self:_importSongsFromSav(items)
		else
			self:_importSongFromBuffer(items)
		end
	end
end

function Sav:deleteSong(songIdx)
	liblsdj.sav_erase_project(self._sav, songIdx)
end

function Sav:importSong(songIdx, songData)
	local t = type(songData)
	if t == "string" then
		local data = fs.load(songData)
		if data ~= nil then
			local ext = pathutil.ext(songData)
			if ext == ".lsdsng" then
				self:_importSongFromBuffer(data, songIdx)
			else
				print("Resource has an unknown file extension " .. ext)
			end
		end
	else--if t == "userdata" then
		self:_importSongFromBuffer(songData, songIdx)
	end
end

function Sav:getSongs(filterEmpty)
	local songs = {}
	for i = 0, LSDJ_SAV_PROJECT_COUNT - 1, 1 do
		local project = liblsdj.sav_get_project(self._sav, i)
		if isNullPtr(project) == false then
			local name = liblsdj.project_get_name(project)
			table.insert(songs, { idx = i, name = name, empty = false })
		elseif filterEmpty ~= true then
			table.insert(songs, { idx = i, empty = true })
		end
	end

	return songs
end

function Sav:exportSongs(path)
	for i = 0, LSDJ_SAV_PROJECT_COUNT - 1, 1 do
		local project = liblsdj.sav_get_project(self._sav, i)
		if isNullPtr(project) == false then
			local buffer = DataBuffer.new()
			local err = liblsdj.project_write_lsdsng_to_memory(project, buffer)
			if err == LsdjError.Success then
				local name = liblsdj.project_get_name(project)
				local p = pathutil.join(path, name .. ".lsdsng")
				print("Writing song to file:", p)
				fs.save(p, buffer)
			end
		end
	end
end

function Sav:exportSong(songIdx, path)
	local project = liblsdj.sav_get_project(self._sav, songIdx)
	if isNullPtr(project) == false then
		local buffer = DataBuffer.new()
		local err = liblsdj.project_write_lsdsng_to_memory(project, buffer)
		if err == LsdjError.Success then
			print("Writing song to file:", path)
			fs.save(path, buffer)
		end
	end
end

function Sav:_importSongsFromSav(savData)
	local other = Sav(savData)
	for i = 1, LSDJ_SAV_PROJECT_COUNT - 1, 1 do
		local songData = other:songToBuffer(i)
		self:_importSongFromBuffer(songData)
	end
end

function Sav:nextAvailableProject()
	for i = 0, LSDJ_SAV_PROJECT_COUNT - 1, 1 do
		local project = liblsdj.sav_get_project(self._sav, i)
		print(project)
		if isNullPtr(project) == true then
			return i
		end
	end
end

function Sav:_importSongFromBuffer(songData, songIdx)
	songIdx = songIdx or self:nextAvailableProject()
	if songIdx == nil then
		print("Failed to find available project")
		return
	end

	local proj, err = liblsdj.project_read_lsdsng_from_memory(songData)
	if err == LsdjError.Success then
		print(self._sav, songIdx, proj, songData:size())
		liblsdj.sav_set_project_move(self._sav, songIdx, proj)
	else
		-- Fail
		print("Failed to read song")
	end
end

return Sav

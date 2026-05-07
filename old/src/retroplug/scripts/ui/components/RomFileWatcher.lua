local fs = require("fs")

local RomFileWatcher = component({ name = "Rom File Watcher", version = "1.0.0" })
function RomFileWatcher.init()
	--self._watchId = nil
end

function RomFileWatcher.onReload()
	--self:_setupWatcher()
end

function RomFileWatcher.onRomLoad()
	--self:_setupWatcher()
end

function RomFileWatcher.onDestroy()
	--if self._watchId ~= nil then
		--fs.removeWatch(self._watchId)
	--end
end

function RomFileWatcher._setupWatcher()
	--[[local desc = self:system():desc()
	if fs.exists(desc.romPath) == true then
		self._watchId = fs.watch(desc.romPath, function(path, changeType)
			local file = fs.load(path)
			if file ~= nil then	self.system:setRom(file) end
		end)
	end]]
end

return RomFileWatcher

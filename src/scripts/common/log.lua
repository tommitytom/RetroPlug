local pathutil = require("pathutil")
local ansicolors = require("ansicolors")
local inspect = require("inspect")

local _print = function(...)
	for i, a in ipairs({...}) do
		if i > 1 then _consolePrint("\t") end
		_consolePrint(tostring(a))
	end

	_consolePrint("\r\n")
end

local LogLevels = {
	Debug = 0,
	Info = 1,
	Warning = 2,
	Error = 3,
	Print = 4
}

local _logColor = {
	[LogLevels.Debug] = ansicolors.white,
	[LogLevels.Info] = ansicolors.green,
	[LogLevels.Warning] = ansicolors.yellow,
	[LogLevels.Error] = ansicolors.red,
	[LogLevels.Print] = ansicolors.white
}

local _level = LogLevels.Debug
local _colorized = false

local function log(level, ...)
	if level >= _level then
		local info = debug.getinfo(3)
		local filename = pathutil.filename(info.short_src)
		local colStart
		local colEnd

		if _colorized == true then
			colStart = _logColor[level]
			colEnd = ansicolors.reset
		else
			colStart = ""
			colEnd = ""
		end

		_print("[" .. colStart .. filename .. ":" .. info.currentline .. colEnd .. "]", ...)
	end
end

local logger = {}

function logger.level(level)
	if level ~= nil then _level = level end
	return _level
end

function logger.debug(...)
	log(LogLevels.Debug, ...)
end

function logger.info(...)
	log(LogLevels.Info, ...)
end

function logger.warn(...)
	log(LogLevels.Warning, ...)
end

function logger.error(...)
	log(LogLevels.Error, ...)
end

function logger.obj(obj)
	log(LogLevels.Debug, "\n" .. inspect(obj))
end

function logger.overridePrint(override)
	if override == nil then override = true end
	if override == true then
		print = function(...) log(LogLevels.Print, ...) end
	else
		print = _print
	end
end

return logger

local function processParsedContent(parsed)
end

local function parseInputHandler()
	local parsed = {
		config = {},
		maps = {
			key = {},
			pad = {}
		}
	}

	local env = {
		InputConfig = function(config) parsed.config = config end,
		KeyMap = function(config, map) table.insert(parsed.maps.key, { config, map }) end,
		PadMap = function(config, map) table.insert(parsed.maps.pad, { config, map }) end
	}

	local f = load("InputConfig({ foo = 1337 })", "Input Config", "bt", env)

	local ok = pcall(f)
	if ok then
		processParsedContent(parsed)
	end
end


parseInputHandler()


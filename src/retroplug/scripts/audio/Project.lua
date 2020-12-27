TransportState = {
	Unknown = 0,
	Stopped = 1,
	Paused = 2,
	Playing = 3
}

local Project = {
	systems = {},
	transport = {
		position = 0,
		state = TransportState.Stopped,
		changed = false,
		started = false,
		stopped = false,
		paused = false
	}
}

--function Project.

return Project

local class = require("class")

local PpqGenerator = class()
function PpqGenerator:init(resolution, cb)
	self._resolution = resolution
	self._cb = cb
	self._last = -1
	self._sampleCount = 0

	self._sampleRate = 44100
	self._tempo = 120
	self:updatePrecomputed()
	self:setCycleRange(0, 4)
end

function PpqGenerator:setSampleRate(sampleRate)
	if sampleRate ~= self._sampleRate then
		self._sampleRate = sampleRate
		self:updatePrecomputed()
	end
end

function PpqGenerator:setTempo(tempo)
	if tempo ~= self._tempo then
		self._tempo = tempo
		self:updatePrecomputed()
	end
end

function PpqGenerator:setCycleRange(cycleStart, cycleEnd)
	if cycleStart ~= self._cycleStart or cycleEnd ~= self._cycleEnd then
		print(cycleStart, cycleEnd)
		self._cycleStart = cycleStart
		self._cycleEnd = cycleEnd
		self._cycleLength = (cycleEnd - cycleStart) * self._resolution
	end
end

function PpqGenerator:updatePrecomputed()
	self._samplesPerMs = self._sampleRate / 1000.0
	self._beatLenMs = 60000.0 / self._tempo

	local samplesPerBeat = self._beatLenMs * self._samplesPerMs
	self._samplesPerTick = samplesPerBeat / self._resolution
end

function PpqGenerator:reset()
	self._last = -1
	self._sampleCount = 0
end

local PPQ_OFFSET_ERROR_THRESH = 8

local function checkOffset(samplesPerTick, sampleCount, ...)
	if sampleCount < samplesPerTick - PPQ_OFFSET_ERROR_THRESH or sampleCount > samplesPerTick + PPQ_OFFSET_ERROR_THRESH then
		print("Bad PPQ offset, expected:", samplesPerTick, "got:", sampleCount, ...)
	end
end

function PpqGenerator:update(songPos, sampleCount)
	local fullPpq = (songPos * self._resolution) % self._resolution
	local nextFullPpq = (fullPpq + ((sampleCount - 1) / self._samplesPerTick)) % self._resolution

	local ppq = math.floor(fullPpq)
	local nextPpq = math.floor(nextFullPpq)

	if ppq ~= self._last then
		local amount = fullPpq - ppq
		local offset = math.floor(self._samplesPerTick * amount)

		self._cb(ppq, offset)

		if self._last ~= -1 then
			checkOffset(self._samplesPerTick, self._sampleCount, "last:", self._lastFull, "current:", fullPpq, "next:", nextFullPpq)
		end

		self._sampleCount = -offset
	end

	if ppq ~= nextPpq then
		local amount = math.ceil(fullPpq) - fullPpq
		local offset = math.floor(self._samplesPerTick * amount)

		if offset >= sampleCount then
			print("PPQ Overshoot", offset, sampleCount, offset - sampleCount, amount, self._samplesPerTick)
			offset = sampleCount - 1
		end

		self._cb(nextPpq, offset)
		checkOffset(self._samplesPerTick, self._sampleCount + offset, "(next)", "last:", self._lastFull, "current:", fullPpq, "next:", nextFullPpq)

		self._sampleCount = -offset
	end

	self._sampleCount = self._sampleCount + sampleCount
	self._last = nextPpq
	self._lastFull = fullPpq
end

return PpqGenerator

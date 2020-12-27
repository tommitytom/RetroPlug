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

function PpqGenerator:updatePrecomputed()
	self._samplesPerMs = self._sampleRate / 1000.0
	self._beatLenMs = 60000.0 / self._tempo

	local samplesPerBeat = self._beatLenMs * self._samplesPerMs
	self._samplesPerTick = samplesPerBeat / self._resolution
	self._ticksPerSample = 1 / self._samplesPerTick
end

function PpqGenerator:reset()
	self._last = -1
	self._sampleCount = 0
end

function PpqGenerator:update(songPos, sampleCount)
	local fullPpq = songPos * self._resolution
	local nextFullPpq = fullPpq + ((sampleCount / self._samplesPerTick) - self._ticksPerSample)
	local ppq = math.floor(fullPpq)
	local nextPpq = math.floor(nextFullPpq)

	if ppq ~= self._last then
		self._cb(ppq, 0)
		print("PPQ sample (last):", ppq, nextPpq, self._sampleCount)
		self._sampleCount = 0
	end

	if ppq ~= nextPpq then
		if nextPpq ~= ppq + 1 then
			print("MULKTIBLAST")
		end

		local amount = math.ceil(fullPpq) - fullPpq
		local offset = math.floor(self._samplesPerTick * amount)

		if offset >= sampleCount then
			print("PPQ Overshoot", offset, sampleCount, offset - sampleCount, amount, self._samplesPerTick)
			offset = sampleCount - 1
		end

		self._cb(nextPpq, offset)
		print("PPQ sample (next):", nextPpq, self._sampleCount + offset)

		self._sampleCount = -offset
	end

	self._sampleCount = self._sampleCount + sampleCount
	self._last = nextPpq
end

return PpqGenerator

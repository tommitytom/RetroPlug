local class = require("class")

local SemVer = class()
function SemVer:init(major, minor, patch)
	if type(major) == "number" then
		self.major = major
		self.minor = minor
		self.patch = patch
	elseif type(major) == "string" then
		-- Parse string
		error("NYI")
	end
end

function SemVer:toInt()
	return (self.major << 16) | (self.minor << 8) | (self.patch & 0xFF)
end

function SemVer:toString()
	return tostring(self.major) .. "." .. tostring(self.minor) .. "." .. tostring(self.patch)
end

function SemVer:__lt(lhs, rhs)
	return lhs:toInt() < rhs:toInt()
end

function SemVer:__le(lhs, rhs)
	return lhs:toInt() <= rhs:toInt()
end

return SemVer

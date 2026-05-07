local util = {}

function util.disableFastUpToDateCheck(projectNames)
	require "vstudio"
	local p = premake;
	local vc = p.vstudio.vc2010;

	function disableFastUpToDateCheck(prj, cfg)
		for _, value in pairs(projectNames) do
			if prj.name == value then
				vc.element("DisableFastUpToDateCheck", nil, "true")
			end
		end
	end

	p.override(vc.elements, "globalsCondition", function(oldfn, prj, cfg)
		local elements = oldfn(prj, cfg)
		elements = table.join(elements, { disableFastUpToDateCheck })
		return elements
	end)
end

function util.joinFlags(...)
	local t = ""
	for _, g in ipairs({...}) do
		for _, v in ipairs(g) do
			t = t .. v .. " "
		end
	end

	return t
end

return util
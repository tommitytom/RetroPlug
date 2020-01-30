print = function(...)
	for i, a in ipairs({...}) do
		if i > 1 then _consolePrint("\t") end
		_consolePrint(tostring(a))
	end

	_consolePrint("\r\n")
end

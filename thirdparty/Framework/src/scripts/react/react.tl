local React = require("react-base")(require("host-reconciler"))

global function onFrame(dt: number)
	local start = fw.getTime()

	React.workLoop(function(): number
		--print("loop")
		local diff = fw.getTime() - start
		return (dt - diff) * 1000
	end)
end

return React

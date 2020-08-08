local Action = {}
setmetatable(Action, {
	__index = function(table, componentName)
		local actionNameTable = {}
		setmetatable(actionNameTable, {
			__index = function(table, actionName)
				return {
					component = componentName,
					action = actionName
				}
			end
		})

		return actionNameTable
	end
})

return Action

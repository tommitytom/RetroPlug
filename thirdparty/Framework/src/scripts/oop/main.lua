local function setProperties(view, props)
	for k,v in pairs(props) do
		view[k] = v
	end
end

print("hi")
local v = fw.ReactElementView()

setProperties(v, { id = "poooo" })

print(v.id)
fw.document:addChild(v)
fw.document:addChild(fw.ReactElementView())


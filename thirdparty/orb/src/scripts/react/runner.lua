local root = ";C:\\projects\\code\\RetroPlug\\thirdparty\\orb\\src\\scripts\\react\\"
package.path = package.path .. root .. "?.lua"
package.path = package.path .. root .. "?\\init.lua"

print("--------------------------------------------------")

require("tl").loader()
--require("main")
--require("retroplug")
require("mimic")

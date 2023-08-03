local root = ";E:\\code\\RetroPlugNext\\thirdparty\\Framework\\src\\scripts\\react\\"
package.path = package.path .. root .. "?.lua"
package.path = package.path .. root .. "?\\init.lua"

print("--------------------------------------------------")

require("tl").loader()
require("main")

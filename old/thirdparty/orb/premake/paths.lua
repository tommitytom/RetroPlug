local SCRIPT_ROOT = debug.getinfo(1).source:match("@?(.*/)")
local REPO_ROOT = SCRIPT_ROOT .. "../"

return {
	REPO_ROOT = REPO_ROOT,
	SCRIPT_ROOT = SCRIPT_ROOT,
	DEP_ROOT = REPO_ROOT .. "thirdparty/",
	SRC_ROOT = REPO_ROOT .. "src/",
	GENERATED_ROOT = REPO_ROOT .. "generated/",
	RESOURCES_ROOT = REPO_ROOT .. "resources/",
	BUILD_ROOT = REPO_ROOT .. "build/" .. _ACTION .. "/",
	PROJECT_BUILD_ROOT = _MAIN_SCRIPT_DIR .. "/build/" .. _ACTION .. "/"
}

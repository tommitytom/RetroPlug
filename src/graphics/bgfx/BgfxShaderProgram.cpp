#include "BgfxShaderProgram.h"

#include "foundation/Resource.h"
#include "graphics/bgfx/BgfxShader.h"
#include "graphics/bgfx/BgfxDefaultShaders.h"

using namespace rp;
using namespace rp::engine;

BgfxShaderProgramProvider::BgfxShaderProgramProvider(const ResourceHandleLookup& lookup): _resources(lookup) {
	auto shaders = getDefaultShaders();

	BgfxShaderProvider shaderProvider;

	std::vector<std::string> deps;
	_vertexShader = shaderProvider.create(ShaderDesc{ .data = shaders.first.data, .size = shaders.first.size }, deps);
	_fragmentShader = shaderProvider.create(ShaderDesc{ .data = shaders.second.data, .size = shaders.second.size }, deps);

	bgfx::ProgramHandle handle = bgfx::createProgram(
		std::static_pointer_cast<BgfxShader>(_vertexShader)->getBgfxHandle(),
		std::static_pointer_cast<BgfxShader>(_fragmentShader)->getBgfxHandle()
	);
	
	_defaultProgram = std::make_shared<BgfxShaderProgram>(handle, ShaderHandle(), ShaderHandle());
}

std::shared_ptr<Resource> BgfxShaderProgramProvider::create(const ShaderProgramDesc& desc, std::vector<std::string>& deps) {
	deps.push_back(desc.vertexShader);
	deps.push_back(desc.fragmentShader);

	auto foundVert = _resources.find(ResourceUtil::hashUri(desc.vertexShader));
	auto foundFrag = _resources.find(ResourceUtil::hashUri(desc.fragmentShader));

	if (foundVert != _resources.end() && foundFrag != _resources.end()) {
		const ShaderHandle vertHandle = foundVert->second;
		const ShaderHandle fragHandle = foundFrag->second;

		if (vertHandle.isLoaded() && fragHandle.isLoaded()) {
			const BgfxShader& vert = vertHandle.getResourceAs<BgfxShader>();
			const BgfxShader& frag = fragHandle.getResourceAs<BgfxShader>();
			bgfx::ProgramHandle handle = bgfx::createProgram(vert.getBgfxHandle(), frag.getBgfxHandle());

			return std::make_shared<BgfxShaderProgram>(handle, vertHandle, fragHandle);
		}
	}

	return _defaultProgram;
}

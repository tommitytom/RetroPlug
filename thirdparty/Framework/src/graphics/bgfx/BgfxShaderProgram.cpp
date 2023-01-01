#include "BgfxShaderProgram.h"

#include "foundation/Resource.h"
#include "graphics/bgfx/BgfxDefaultShaders.h"
#include "graphics/bgfx/BgfxShader.h"

using namespace fw;
using namespace fw::engine;

BgfxShaderProgramProvider::BgfxShaderProgramProvider(const ResourceHandleLookup& lookup) : _resources(lookup) {}

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

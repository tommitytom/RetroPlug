#include "GlShaderProgram.h"

#include <glad/gl.h>
#include <spdlog/spdlog.h>

#include "foundation/Resource.h"
#include "graphics/gl/GlDefaultShaders.h"
#include "graphics/gl/GlShader.h"
#include "graphics/gl/GlUtil.h"

namespace fw::engine {
	GlShaderProgram::~GlShaderProgram() {
		glDeleteProgram(_handle);
		_handle = 0;
	}

	GlShaderProgramProvider::GlShaderProgramProvider(const ResourceHandleLookup& lookup) : _resources(lookup) {
		auto shaders = getDefaultGlShaders();

		GlShaderProvider shaderProvider;

		std::vector<std::string> deps;
		_vertexShader = shaderProvider.create(ShaderDesc{ .data = shaders.first.data, .size = shaders.first.size, .type = ShaderType::Vertex }, deps);
		_fragmentShader = shaderProvider.create(ShaderDesc{ .data = shaders.second.data, .size = shaders.second.size, .type = ShaderType::Fragment }, deps);

		assert(_vertexShader);
		assert(_fragmentShader);

		uint32 program = glCreateProgram();
		glAttachShader(program, std::static_pointer_cast<GlShader>(_vertexShader)->getGlHandle());
		glAttachShader(program, std::static_pointer_cast<GlShader>(_fragmentShader)->getGlHandle());

		glLinkProgram(program);
		assert(!GlUtil::checkProgramLinkError(program));

		_defaultProgram = std::make_shared<GlShaderProgram>(program, ShaderHandle(), ShaderHandle());
	}

	std::shared_ptr<Resource> GlShaderProgramProvider::create(const ShaderProgramDesc& desc, std::vector<std::string>& deps) {
		deps.push_back(desc.vertexShader);
		deps.push_back(desc.fragmentShader);

		auto foundVert = _resources.find(ResourceUtil::hashUri(desc.vertexShader));
		auto foundFrag = _resources.find(ResourceUtil::hashUri(desc.fragmentShader));

		if (foundVert != _resources.end() && foundFrag != _resources.end()) {
			const ShaderHandle vertHandle = foundVert->second;
			const ShaderHandle fragHandle = foundFrag->second;

			if (vertHandle.isLoaded() && fragHandle.isLoaded()) {
				const GlShader& vert = vertHandle.getResourceAs<GlShader>();
				const GlShader& frag = fragHandle.getResourceAs<GlShader>();

				uint32 program = glCreateProgram();
				glAttachShader(program, vert.getGlHandle());
				glAttachShader(program, frag.getGlHandle());

				glLinkProgram(program);
				
				if (!GlUtil::checkProgramLinkError(program)) {
					return std::make_shared<GlShaderProgram>(program, vertHandle, fragHandle);
				}
			}
		}

		return _defaultProgram;
	}
}

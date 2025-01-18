#include "GlShaderProgram.h"

#include <glad/gl.h>
#include <spdlog/spdlog.h>

#include "foundation/Resource.h"
#include "graphics/gl/GlDefaultShaders.h"
#include "graphics/gl/GlShader.h"
#include "graphics/gl/GlUtil.h"

namespace fw {
	GlShaderProgram::~GlShaderProgram() {
		glDeleteProgram(_handle);
		_handle = 0;
	}

	GlShaderProgramProvider::GlShaderProgramProvider(const ResourceHandleLookup& lookup) : _resources(lookup) {
		printf("getdefault\n");
		auto shaders = getDefaultGlShaders();

		GlShaderProvider shaderProvider;

		std::vector<std::string> deps;
		printf("createdescs vert\n");
		_vertexShader = shaderProvider.create(ShaderDesc{ .data = shaders.first.data, .size = shaders.first.size, .type = ShaderType::Vertex }, deps);
		printf("createdescs frag\n");
		_fragmentShader = shaderProvider.create(ShaderDesc{ .data = shaders.second.data, .size = shaders.second.size, .type = ShaderType::Fragment }, deps);

printf("createdescs DONE\n");
		assert(_vertexShader);
		assert(_fragmentShader);

printf("glCreateProgram\n");
		uint32 program = glCreateProgram();
		printf("glAttachShader vert\n");
		glAttachShader(program, std::static_pointer_cast<GlShader>(_vertexShader)->getGlHandle());
		printf("glAttachShader frag\n");
		glAttachShader(program, std::static_pointer_cast<GlShader>(_fragmentShader)->getGlHandle());

printf("llinkprog\n");
		glLinkProgram(program);
		assert(!GlUtil::checkProgramLinkError(program));

printf("bind\n");
		glBindAttribLocation(program, 0, "a_position");
		glBindAttribLocation(program, 1, "a_color");
		glBindAttribLocation(program, 2, "a_texcoord0");

		_defaultProgram = std::make_shared<GlShaderProgram>(program, ShaderHandle(), ShaderHandle());
	}

	std::shared_ptr<Resource> GlShaderProgramProvider::create(const ShaderProgramDesc& desc, std::vector<std::string>& deps) {
		printf("createshdaerprog\n");
		deps.push_back(desc.vertexShader);
		deps.push_back(desc.fragmentShader);

		auto foundVert = _resources.find(ResourceUtil::hashUri(desc.vertexShader));
		auto foundFrag = _resources.find(ResourceUtil::hashUri(desc.fragmentShader));

		if (foundVert != _resources.end() && foundFrag != _resources.end()) {
			const ShaderHandle vertHandle = foundVert->second;
			const ShaderHandle fragHandle = foundFrag->second;

			if (vertHandle.isLoaded() && fragHandle.isLoaded()) {
				printf("get vert\n");
				const GlShader& vert = vertHandle.getResourceAs<GlShader>();
				printf("get frag\n");
				const GlShader& frag = fragHandle.getResourceAs<GlShader>();

				printf("create program\n");
				uint32 program = glCreateProgram();
				printf("attach vert\n");
				glAttachShader(program, vert.getGlHandle());
				printf("attach frag\n");
				glAttachShader(program, frag.getGlHandle());

				printf("link\n");
				glLinkProgram(program);

				printf("bind\n");
				glBindAttribLocation(program, 0, "a_position");
				glBindAttribLocation(program, 1, "a_color");
				glBindAttribLocation(program, 2, "a_texcoord0");


				printf("bound\n");
				if (!GlUtil::checkProgramLinkError(program)) {
					return std::make_shared<GlShaderProgram>(program, vertHandle, fragHandle);
				}
			}
		}

		return _defaultProgram;
	}
}

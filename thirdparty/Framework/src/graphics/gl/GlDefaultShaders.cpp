#include "GlDefaultShaders.h"

#include <string>
#include <string_view>

// Headers
constexpr std::string_view glDesktopHeader = R"(#version 330 core
)";

constexpr std::string_view esVertexHeader = R"(#version 300 es
precision highp float;
)";

constexpr std::string_view esFragmentHeader = R"(#version 300 es
precision mediump float;
)";

// Base shaders (no version headers)
constexpr std::string_view vertexShaderBase = R"(
layout (location = 0) in vec2 a_position;
layout (location = 1) in vec4 a_color;
layout (location = 2) in vec2 a_texcoord0;

out vec4 v_color;
out vec2 v_texcoord0;

uniform mat4 u_proj;

void main() {
    gl_Position = u_proj * vec4(a_position.x, a_position.y, 0.0, 1.0);
    v_color = a_color;
    v_texcoord0 = a_texcoord0;
}
)";

constexpr std::string_view fragShaderBase = R"(
in vec4 v_color;
in vec2 v_texcoord0;

uniform sampler2D s_tex;

out vec4 FragColor;

void main() {
    FragColor = texture(s_tex, v_texcoord0) * v_color;
}
)";

fw::Uint8Buffer stringToBuffer(const std::string& str) {
	fw::Uint8Buffer buffer(str.size());
	buffer.write((const uint8*)str.data(), str.size());
	return buffer;
}

namespace fw {
	std::pair<ShaderDesc, ShaderDesc> getDefaultGlShaders() {
		#ifdef FW_PLATFORM_WEB
		std::string vertexData = std::string(esVertexHeader) + std::string(vertexShaderBase);
		std::string fragData = std::string(esFragmentHeader) + std::string(fragShaderBase);
		#else
		std::string vertexData = std::string(glDesktopHeader) + std::string(vertexShaderBase);
		std::string fragData = std::string(glDesktopHeader) + std::string(fragShaderBase);
		#endif

		return {
			ShaderDesc{
				.data = stringToBuffer(vertexData),
				.type = ShaderType::Vertex
			},
			ShaderDesc{
				.data = stringToBuffer(fragData),
				.type = ShaderType::Fragment
			}
		};
	}
}

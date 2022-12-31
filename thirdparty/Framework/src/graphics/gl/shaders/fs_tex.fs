#version 330 core

out vec4 FragColor;

in vec4 v_color;
in vec2 v_texcoord0;

uniform sampler2D s_tex;

uniform mat4 u_proj;

void main() {
	//FragColor = vec4(1, 0, 0, 1);
	//FragColor = v_color;
    FragColor = texture(s_tex, v_texcoord0) * v_color;
}

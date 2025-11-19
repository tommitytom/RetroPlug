#ifdef GL_ES
#version 300 es
precision mediump float;
#else
#version 330 core
#endif

in vec4 v_color;
in vec2 v_texcoord0;

uniform sampler2D s_tex;
uniform mat4 u_proj;

out vec4 FragColor;

void main() {
    FragColor = texture(s_tex, v_texcoord0) * v_color;
}
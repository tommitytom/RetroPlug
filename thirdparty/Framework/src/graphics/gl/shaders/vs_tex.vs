 #version 330 core

layout (location = 0) in vec2 a_position;
layout (location = 1) in vec4 a_color;
layout (location = 2) in vec2 a_texcoord0;

out vec4 v_color;
out vec2 v_texcoord0;

uniform mat4 u_proj;

void main() {
	//gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0);
	gl_Position = u_proj * vec4(a_position.x, a_position.y, 0.0, 1.0);
    //gl_Position = u_proj * vec4(a_position, 0, 1.0);
    v_color = a_color;
    v_texcoord0 = a_texcoord0;
}

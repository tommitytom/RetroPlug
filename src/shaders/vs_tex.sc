$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

uniform vec4 scale;

void main() {
	vec2 screenPos = a_position * scale.xy;

	gl_Position = vec4(screenPos.x - 1.0, (2.0 - screenPos.y) - 1.0, 0.0, 1.0);
	v_color0 = a_color0;
	v_texcoord0 = a_texcoord0;
}

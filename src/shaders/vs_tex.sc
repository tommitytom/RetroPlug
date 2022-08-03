$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

#include "common.sh"

void main() {
    gl_Position = mul(u_proj, vec4(a_position, 0, 1.0));
	v_color0 = a_color0;
	v_texcoord0 = a_texcoord0;
}

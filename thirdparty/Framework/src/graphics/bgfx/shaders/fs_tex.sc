$input v_color0, v_texcoord0

#include "common.sh"

SAMPLER2D(s_texColor, 0);

void main() {
	gl_FragColor = texture2D(s_texColor, v_texcoord0) * v_color0;
}

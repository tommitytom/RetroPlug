$input a_position, a_color0
$output v_color0

/*
 * Copyright 2011-2022 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

//#include "common.sh"

void main()
{
	gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0);
	v_color0 = a_color0;
}

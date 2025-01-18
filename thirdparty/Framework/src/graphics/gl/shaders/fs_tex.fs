#version 100

precision mediump float;

// Change 'in' to 'varying' for inputs from vertex shader
varying vec4 v_color;
varying vec2 v_texcoord0;

uniform sampler2D s_tex;

uniform mat4 u_proj;

// GLES 2.0 doesn't use 'out' variables, it uses gl_FragColor
void main() {
    // Change texture() to texture2D()
    gl_FragColor = texture2D(s_tex, v_texcoord0) * v_color;
}

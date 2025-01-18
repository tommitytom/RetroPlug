#version 100

// GLES 2.0 requires precision qualifiers
precision mediump float;

// Change 'in' to 'attribute' for vertex inputs
attribute vec2 a_position;
attribute vec4 a_color;
attribute vec2 a_texcoord0;

// Change 'out' to 'varying' for vertex outputs
varying vec4 v_color;
varying vec2 v_texcoord0;

uniform mat4 u_proj;

void main() {
    gl_Position = u_proj * vec4(a_position.x, a_position.y, 0.0, 1.0);
    v_color = a_color;
    v_texcoord0 = a_texcoord0;
}
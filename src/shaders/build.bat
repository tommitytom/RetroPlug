shaderc -f fs_tex.sc -o fs_tex_gl.h --bin2c fs_tex_gl --platform windows --varyingdef varying2d.def.sc --type fragment -p 150
shaderc -f vs_tex.sc -o vs_tex_gl.h --bin2c vs_tex_gl --platform windows --varyingdef varying2d.def.sc --type vertex -p 150

shaderc -f fs_tex.sc -o fs_tex_d3d9.h --bin2c fs_tex_d3d9 --platform windows --varyingdef varying2d.def.sc --type fragment -p ps_3_0 -0 3
shaderc -f vs_tex.sc -o vs_tex_d3d9.h --bin2c vs_tex_d3d9 --platform windows --varyingdef varying2d.def.sc --type vertex -p vs_3_0 -0 3

shaderc -f fs_tex.sc -o fs_tex_d3d11.h --bin2c fs_tex_d3d11 --platform windows --varyingdef varying2d.def.sc --type fragment -p ps_4_0 -0 3
shaderc -f vs_tex.sc -o vs_tex_d3d11.h --bin2c vs_tex_d3d11 --platform windows --varyingdef varying2d.def.sc --type vertex -p vs_4_0 -0 3

shaderc -f fs_tex.sc -o fs_tex_spirv.h --bin2c fs_tex_spirv --platform linux --varyingdef varying2d.def.sc --type fragment -p spirv
shaderc -f vs_tex.sc -o vs_tex_spirv.h --bin2c vs_tex_spirv --platform linux --varyingdef varying2d.def.sc --type vertex -p spirv

shaderc -f fs_debug.sc -o fs_debug.h --bin2c fs_debug --platform windows --varyingdef varying.def.sc --type fragment -p 150
shaderc -f vs_debug.sc -o vs_debug.h --bin2c vs_debug --platform windows --varyingdef varying.def.sc --type vertex -p 150

shaderc -f fs_tex.sc -o fs_tex.h --bin2c fs_tex --platform windows --varyingdef varying2d.def.sc --type fragment -p 150
shaderc -f vs_tex.sc -o vs_tex.h --bin2c vs_tex --platform windows --varyingdef varying2d.def.sc --type vertex -p 150
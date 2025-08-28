local emscripten = {
	flags = {}
}

emscripten.flags.base = {
	"-s WASM=1",
	--"-s MODULARIZE=1",

	--"-s LLD_REPORT_UNDEFINED",
	[[-s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","emscriptenRegisterAudioObject"]']],
	--"-s MODULARIZE=1",
	--"-s EXPORT_NAME=createModule",
	--"-s EXPORT_ES6=0", -- Turn this on once this bug is fixed: https://bugzilla.mozilla.org/show_bug.cgi?id=1247687
	--"-s INITIAL_MEMORY=1024MB",
	--"-s TOTAL_MEMORY=2048MB",
	--"-s ENVIRONMENT=web,worker",
	--"-s ALLOW_MEMORY_GROWTH=1",
	--"-s USE_ES6_IMPORT_META=0",
	"-s USE_PTHREADS=1",
	--"-s PTHREAD_POOL_SIZE=2",
	"-s USE_GLFW=3",
	"-s USE_WEBGL2=1",
	"-s FULL_ES3=1",
	"-s MIN_WEBGL_VERSION=2",
	"-s MAX_WEBGL_VERSION=2", -- https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html#opengl-support-webgl-subset
	--"-s FORCE_FILESYSTEM=1",
	--"-s EXPORTED_FUNCTIONS='[\"_main\", \"_resize_window\", \"_advance_frame\"]'",
	--"-s FULL_ES3=1",
	--"-s MIN_WEBGL_VERSION=2",
	--"-s MAX_WEBGL_VERSION=2", -- https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html#opengl-support-webgl-subset
	"-s NO_DISABLE_EXCEPTION_CATCHING=1",
	"-s ASYNCIFY",
	--"-s USE_FREETYPE=1",


	--"-lidbfs.js",

	--"--shell-file ../../thirdparty/Framework/webtemplate/index.html",
	--"--post-js ../../templates/processor.js",

	--"-fexceptions",
	--"-s USE_FREETYPE=1",

}

emscripten.flags.debug = {
	--"-s STACK_OVERFLOW_CHECK=2",
	--"-s GL_DEBUG=1",
	--"-s ASSERTIONS=0", -- Causes insane overhead (every call escapes to js to evaluate the GL render call)
	--"-s SAFE_HEAP=1",
	--"-s WARN_UNALIGNED=1",
	"-g",
	"--source-map-base http://localhost:6931/"
}

emscripten.flags.development = {
	"-g",
	"-O2",
	--"-s ASSERTIONS=1",
	--"-s SAFE_HEAP=2",
	--"-s SAFE_HEAP_LOG=1",
}

emscripten.flags.release = {
	--"-g",
	"-O3",
	--"-s ASSERTIONS=1",
	--"-s SAFE_HEAP=2",
	--"-s SAFE_HEAP_LOG=1",
	"-s ELIMINATE_DUPLICATE_FUNCTIONS=1",
	"-closure"
}

return emscripten

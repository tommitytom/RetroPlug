local emscripten = {
	flags = {}
}

emscripten.flags.base = {
	"-s WASM=1",
	"-s MODULARIZE=1",
	"-s EXPORT_ES6=1",
	[[-s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","emscriptenRegisterAudioObject"]']],
	"--use-port=contrib.glfw3",
	"-s USE_WEBGL2=1",
	"-s FULL_ES3=1",
	"-s MIN_WEBGL_VERSION=2",
	"-s MAX_WEBGL_VERSION=2", -- https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html#opengl-support-webgl-subset
}

emscripten.flags.debug = {
	--"-s STACK_OVERFLOW_CHECK=2",
	--"-s GL_DEBUG=1",
	--"-s ASSERTIONS=0", -- Causes insane overhead (every call escapes to js to evaluate the GL render call)
	--"-s SAFE_HEAP=1",
	--"-s WARN_UNALIGNED=1",
	"-g",
	"--source-map-base http://localhost:5713/"
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

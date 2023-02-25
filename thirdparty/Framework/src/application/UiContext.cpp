#include "UiContext.h"

#include "graphics/Canvas.h"
#include "graphics/Shader.h"
#include "graphics/TextureAtlas.h"
#include "graphics/ftgl/FtglFont.h"

#define FW_RENDERER_BGFX

#if defined(FW_RENDERER_GL)
#include "graphics/gl/GlRenderContext.h"
using RenderContextT = fw::GlRenderContext;
const bool RENDERER_REQUIRES_FLIP = true;
#elif defined(FW_RENDERER_BGFX)
#include "graphics/bgfx/BgfxRenderContext.h"
using RenderContextT = fw::BgfxRenderContext;
const bool RENDERER_REQUIRES_FLIP = false;
#else
#include "graphics/gl/GlRenderContext.h"
using RenderContextT = fw::GlRenderContext;
const bool RENDERER_REQUIRES_FLIP = true;
#endif

namespace fw::app {
	using hrc = std::chrono::high_resolution_clock;
	using delta_duration = std::chrono::duration<f32>;

	UiContext::UiContext(bool requiresFlip) : _fontManager(_resourceManager) {
		_windowManager = std::make_unique<GlfwWindowManager>(_resourceManager, _fontManager);
		_flip = RENDERER_REQUIRES_FLIP && requiresFlip;
	}

	UiContext::~UiContext() {
		_windowManager->closeAll();

		_renderContext->cleanup();
		_mainWindow->onCleanup();

		_resourceManager = ResourceManager();
		//_canvas.destroy();

		_mainWindow = nullptr;
		_renderContext = nullptr;

		_windowManager = nullptr;
	}

	bool UiContext::runFrame() {
		hrc::time_point time = hrc::now();
		std::chrono::nanoseconds nanoDelta = time - _lastTime;
		f32 delta = std::chrono::duration_cast<delta_duration>(nanoDelta).count();
		_lastTime = time;

		std::vector<WindowPtr> created;
		_windowManager->update(created);

		for (WindowPtr w : created) {
			if (!_mainWindow) {
				_mainWindow = w;
			}

			w->onInitialize();
		}

		std::vector<WindowPtr>& windows = _windowManager->getWindows();

		if (windows.size()) {
			_renderContext->beginFrame(delta);

			for (auto it = windows.begin(); it != windows.end(); ++it) {
				WindowPtr w = *it;

				if (!w->shouldClose()) {
					Canvas& canvas = w->getCanvas();
					canvas.setDefaults(_defaultTexture, _defaultProgram, _defaultFont);
					canvas.setDimensions(w->getViewManager()->getDimensions(), 1.0f);

					w->getViewManager()->setResourceManager(&_resourceManager, &_fontManager);

					w->onUpdate(delta);

					canvas.beginRender();
					w->onRender(canvas);
					canvas.endRender();

					_renderContext->renderCanvas(canvas, w->getNativeHandle());

					if (_flip) {
						w->onFrame();
					}
				}
			}

			_renderContext->endFrame();

			return !_mainWindow->shouldClose();
		}

		return false;
	}

	void UiContext::createRenderContext(WindowPtr window) {
		_renderContext = std::make_unique<RenderContextT>(window->getNativeHandle(), window->getViewManager()->getDimensions(), _resourceManager);

		_resourceManager.addProvider<Font, FontProvider>();
		_resourceManager.addProvider<TextureAtlas, TextureAtlasProvider>();
		_resourceManager.addProvider<FontFace>(std::make_unique<FtglFontFaceProvider>(_resourceManager));

		TextureDesc whiteTextureDesc = TextureDesc{
			.dimensions = { 8, 8 },
			.depth = 4
		};

		const size_t size = (size_t)(whiteTextureDesc.dimensions.w * whiteTextureDesc.dimensions.h * whiteTextureDesc.depth);
		whiteTextureDesc.data.resize(size);
		memset(whiteTextureDesc.data.data(), 0xFF, size);

		_defaultTexture = _resourceManager.create<Texture>("textures/white", whiteTextureDesc);

		auto shaderDescs = _renderContext->getDefaultShaders();

		_resourceManager.create<fw::Shader>("shaders/CanvasVertex", shaderDescs.first);
		_resourceManager.create<fw::Shader>("shaders/CanvasFragment", shaderDescs.second);

		_defaultProgram = _resourceManager.create<ShaderProgram>("shaders/CanvasDefault", {
			"shaders/CanvasVertex",
			"shaders/CanvasFragment"
		});

		_defaultFont = _fontManager.loadFont("Karla-Regular", 16);

		_lastTime = hrc::now();
	}
}
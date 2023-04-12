#include "UiContext.h"

#include "graphics/Canvas.h"
#include "graphics/Shader.h"
#include "graphics/TextureAtlas.h"
#include "graphics/ftgl/FtglFont.h"

namespace fw::app {
	using hrc = std::chrono::high_resolution_clock;
	using delta_duration = std::chrono::duration<f32>;

	UiContext::UiContext(std::unique_ptr<RenderContext>&& renderContext) : 
		_resourceManager(std::make_shared<ResourceManager>()), 
		_fontManager(_resourceManager) 
	{
		_renderContext = std::move(renderContext);
		_windowManager = std::make_unique<GlfwWindowManager>(*_resourceManager, _fontManager);
		_renderContext->setResourceManager(_resourceManager);
	}

	UiContext::~UiContext() {
		_windowManager->closeAll();

		_renderContext->cleanup();
		_mainWindow->onCleanup();
		
		_defaultFont = nullptr;
		_defaultTexture = nullptr;
		_defaultProgram = nullptr;

		_resourceManager = nullptr;

		_mainWindow = nullptr;
		_renderContext = nullptr;

		_windowManager = nullptr;
	}

	void UiContext::handleHotReload() {
		if (_mainWindow) {
			ViewManagerPtr vm = _mainWindow->getViewManager();
			vm->onHotReload();
		}
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

					w->getViewManager()->setResourceManager(_resourceManager.get(), &_fontManager);

					w->onUpdate(delta);

					canvas.beginRender();
					w->onRender(canvas);
					canvas.endRender();

					_renderContext->renderCanvas(canvas, w->getNativeHandle());

					if (_renderContext->requiresFlip()) {
						w->onFrame();
					}
				}
			}

			_renderContext->endFrame();

			return !_mainWindow->shouldClose();
		}

		return false;
	}

	void UiContext::initRenderContext(WindowPtr window) {
		_renderContext->initialize(window->getNativeHandle(), window->getViewManager()->getDimensions());
		
		_resourceManager->addProvider<Font, FontProvider>();
		_resourceManager->addProvider<TextureAtlas, TextureAtlasProvider>();
		_resourceManager->addProvider<FontFace>(std::make_unique<FtglFontFaceProvider>(*_resourceManager));

		TextureDesc whiteTextureDesc = TextureDesc{
			.dimensions = { 8, 8 },
			.depth = 4
		};

		const size_t size = (size_t)(whiteTextureDesc.dimensions.w * whiteTextureDesc.dimensions.h * whiteTextureDesc.depth);
		whiteTextureDesc.data.resize(size);
		memset(whiteTextureDesc.data.data(), 0xFF, size);

		_defaultTexture = _resourceManager->create<Texture>("textures/white", whiteTextureDesc);

		auto shaderDescs = _renderContext->getDefaultShaders();

		_resourceManager->create<fw::Shader>("shaders/CanvasVertex", shaderDescs.first);
		_resourceManager->create<fw::Shader>("shaders/CanvasFragment", shaderDescs.second);

		_defaultProgram = _resourceManager->create<ShaderProgram>("shaders/CanvasDefault", {
			"shaders/CanvasVertex",
			"shaders/CanvasFragment"
		});

		_defaultFont = _fontManager.loadFont("Karla-Regular", 16);

		_lastTime = hrc::now();
	}
}
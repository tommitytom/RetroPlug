#include "UiContext.h"

#include "graphics/Canvas.h"
#include "graphics/Shader.h"
#include "graphics/TextureAtlas.h"
#include "graphics/ftgl/FtglFont.h"

namespace fw::app {
	UiContext::UiContext(std::unique_ptr<RenderContext>&& renderContext, std::unique_ptr<WindowManager>&& windowManager) :
		_resourceManager(windowManager->getResourceManager()),
		_fontManager(windowManager->getFontManager())
	{
		_windowManager = std::move(windowManager);
		_renderContext = std::move(renderContext);
		_renderContext->setResourceManager(_resourceManager);
	}

	UiContext::~UiContext() {
		_windowManager->closeAll();

		_renderContext->cleanup();
		_mainWindow->onCleanup();

		_defaultFont = nullptr;
		_defaultTexture = nullptr;
		_defaultProgram = nullptr;

		_mainWindow = nullptr;

		_resourceManager->cleanup();
		_resourceManager = nullptr;
		_renderContext = nullptr;

		_windowManager = nullptr;
	}

	void UiContext::handleHotReload() {
		if (_mainWindow) {
			ViewManagerPtr vm = _mainWindow->getViewManager();
			vm->onHotReload();
		}
	}

	WindowPtr UiContext::setup(ViewPtr view, NativeWindowHandle parent, const std::string& canvasId) {
		WindowPtr window = _windowManager->createWindow(view, parent, canvasId);
		initRenderContext(window);

		ViewManagerPtr vm = window->getViewManager();
		vm->setResourceManager(_resourceManager.get(), _fontManager.get());
		vm->createState(entt::forward_as_any(*_windowManager));

		if (!_mainWindow) {
			_mainWindow = window;
		}

		return window;
	}

	WindowPtr UiContext::createWindow(ViewPtr view, NativeWindowHandle parent, const std::string& canvasId) {
		WindowPtr window = _windowManager->createWindow(view, parent, canvasId);

		ViewManagerPtr vm = window->getViewManager();
		vm->setResourceManager(_resourceManager.get(), _fontManager.get());
		vm->createState(entt::forward_as_any(*_windowManager));

		if (!_mainWindow) {
			_mainWindow = window;
		}

		return window;
	}

	WindowPtr UiContext::setupNativeWindow(ViewPtr view, NativeWindowHandle nativeWindowHandle, fw::Dimension dimensions) {
		WindowPtr window = std::make_shared<WrappedNativeWindow>(nativeWindowHandle, dimensions, _resourceManager, _fontManager, view, std::numeric_limits<uint32>::max());
		_windowManager->addWindow(window);

		initRenderContext(window);

		ViewManagerPtr vm = window->getViewManager();
		vm->setResourceManager(_resourceManager.get(), _fontManager.get());
		vm->createState(entt::forward_as_any(*_windowManager));

		if (!_mainWindow) {
			_mainWindow = window;
		}

		return window;
	}

	bool UiContext::runFrame(f32 deltaTime) {
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
			_renderContext->beginFrame(deltaTime);

			for (auto it = windows.begin(); it != windows.end(); ++it) {
				WindowPtr w = *it;

				if (!w->shouldClose()) {
					w->makeCurrent();

					fw::ViewManager* vm = w->getViewManager().get();
					Canvas& canvas = w->getCanvas();
					canvas.setDefaults(_defaultTexture, _defaultProgram, _defaultFont);
					canvas.setDimensions(vm->getDimensions(), 1.0f);

					vm->setResourceManager(_resourceManager.get(), _fontManager.get());
					if (!vm->tryGetState<WindowManager>()) {
						vm->createState(entt::forward_as_any(*_windowManager));
					}

					w->onUpdate(deltaTime);

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

		_defaultFont = _fontManager->loadFont("Karla-Regular", 16);
	}
}
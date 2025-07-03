#include "PuglWindow.h"

#include "pugl/pugl.hpp"

namespace fw::app {
	void PuglWindow::onCreate() {
		ViewManagerPtr vm = getViewManager();

		auto size = vm->getDimensions();
		_puglView.setParent(reinterpret_cast<pugl::NativeView>(_nativeWindowHandle));
		_puglView.setSizeHint(pugl::SizeHint::defaultSize, size.w, size.h);
		_puglView.setString(pugl::StringHint::windowTitle, vm->getName().c_str());
		_puglView.setHint(pugl::ViewHint::resizable, true);
		//_puglView.setSizeHint(pugl::SizeHint::minSize, 0, 0);
		_puglView.setBackend(pugl::glBackend());

		_puglView.setHint(pugl::ViewHint::contextVersionMajor, 3);
		_puglView.setHint(pugl::ViewHint::contextVersionMinor, 3);
		_puglView.setHint(pugl::ViewHint::contextProfile, PUGL_OPENGL_COMPATIBILITY_PROFILE);

		puglSetHandle(_puglView.cobj(), this);
		puglSetEventFunc(_puglView.cobj(), [](PuglView* view, const PuglEvent* event) -> PuglStatus {
			PuglWindow* window = static_cast<PuglWindow*>(puglGetHandle(view));
            auto vm = window->getViewManager();
			
            switch (event->type) {
            case PUGL_NOTHING:
                return PUGL_SUCCESS;
            case PUGL_REALIZE:
                window->getCreateHandler()();
                puglObscureView(view);
                return PUGL_SUCCESS;
            case PUGL_UNREALIZE:
                //return target.onEvent(UnrealizeEvent{ event->any });
                break;
            case PUGL_CONFIGURE:
                window->setDimensions(fw::Dimension{ event->configure.width, event->configure.height });
                return PUGL_SUCCESS;
            case PUGL_UPDATE:
                puglObscureView(view);
                return PUGL_SUCCESS;
            case PUGL_EXPOSE:
                window->onUpdate(0.16f);
                return PUGL_SUCCESS;
            case PUGL_CLOSE:
                //return target.onEvent(CloseEvent{ event->any });
                break;
            case PUGL_FOCUS_IN:
                //return target.onEvent(FocusInEvent{ event->focus });
                break;
            case PUGL_FOCUS_OUT:
                //return target.onEvent(FocusOutEvent{ event->focus });
                break;
            case PUGL_KEY_PRESS:
                //event->key
                //vm->onKey(KeyEvent{})
                //return target.onEvent(KeyPressEvent{ event->key });
                break;
            case PUGL_KEY_RELEASE:
                //return target.onEvent(KeyReleaseEvent{ event->key });
                break;
            case PUGL_TEXT:
                //return target.onEvent(TextEvent{ event->text });
                break;
            case PUGL_POINTER_IN:
                //return target.onEvent(PointerInEvent{ event->crossing });
                break;
            case PUGL_POINTER_OUT:
                //return target.onEvent(PointerOutEvent{ event->crossing });
                break;
            case PUGL_BUTTON_PRESS:
                //return target.onEvent(ButtonPressEvent{ event->button });
                break;
            case PUGL_BUTTON_RELEASE:
                //return target.onEvent(ButtonReleaseEvent{ event->button });
                break;
            case PUGL_MOTION:
                //return target.onEvent(MotionEvent{ event->motion });
                break;
            case PUGL_SCROLL:
                //return target.onEvent(ScrollEvent{ event->scroll });
                break;
            case PUGL_CLIENT:
                //return target.onEvent(ClientEvent{ event->client });
                break;
            case PUGL_TIMER:
                puglObscureView(view);
                return PUGL_SUCCESS;
                break;
            case PUGL_LOOP_ENTER:
                //return target.onEvent(LoopEnterEvent{ event->any });
                break;
            case PUGL_LOOP_LEAVE:
                //return target.onEvent(LoopLeaveEvent{ event->any });
                break;
            case PUGL_DATA_OFFER:
                //return target.onEvent(DataOfferEvent{ event->offer });
                break;
            case PUGL_DATA:
                //return target.onEvent(DataEvent{ event->data });
                break;
            }

            return PUGL_FAILURE;
		});
	}

    void PuglWindow::show() {
        _puglView.realize();
        _puglView.show(pugl::ShowCommand::raise);
        _puglView.startTimer(0, 0.016);
    }

	void PuglWindow::onUpdate(f32 delta) {
        ViewManagerPtr vm = getViewManager();
        vm->onUpdate(delta);
	}

	PuglWindowManager::PuglWindowManager(ResourceManager& resourceManager, FontManager& fontManager) : 
		WindowManager(resourceManager, fontManager),
		_world(pugl::WorldType::module)
	{
		_world.setString(pugl::StringHint::className, "RetroPlug");
		_world.setString(pugl::StringHint::windowTitle, "RetroPlug");
	}

	PuglWindowManager::~PuglWindowManager() {
		closeAll();
	}
}

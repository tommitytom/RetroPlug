#pragma once

#include "foundation/Input.h"
#include "application/PuglWindow.h"

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/base/keycodes.h"
#include "base/source/fstring.h"

namespace fw {
	class Vst3PluginView : public Steinberg::CPluginView, public Steinberg::IPlugViewContentScaleSupport {
	private:
		orb::ViewPtr _view;

	public:
		Vst3PluginView(const orb::ViewPtr& view) : _view(view) {}
		~Vst3PluginView() = default;

		Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override {
#ifdef FW_OS_WINDOWS
			if (strcmp(type, Steinberg::kPlatformTypeHWND) == 0) {
				return Steinberg::kResultTrue;
			}

#elif defined FW_OS_MACOS
			if (strcmp(type, Steinberg::kPlatformTypeNSView) == 0) {
				return Steinberg::kResultTrue;
			}
#endif
			return Steinberg::kResultFalse;
		}

		Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* pSize) override {
			if (pSize) {
				rect = *pSize;
			}

			return Steinberg::kResultTrue;
		}

		Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* pSize) override {
			if (_view) {
				orb::Dimension dim = _view->getDimensions();
				*pSize = Steinberg::ViewRect(0, 0, dim.w, dim.h);
				return Steinberg::kResultTrue;
			} else {
				return Steinberg::kResultFalse;
			}
		}

		Steinberg::tresult PLUGIN_API canResize() override {
			//if (mOwner.HasUI() && mOwner.GetHostResizeEnabled()) {
				return Steinberg::kResultTrue;
			//}

			return Steinberg::kResultFalse;
		}

		Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* pRect) override {
			int w = pRect->getWidth();
			int h = pRect->getHeight();

			/*if (!mOwner.ConstrainEditorResize(w, h)) {
				pRect->right = pRect->left + w;
				pRect->bottom = pRect->top + h;
			}*/

			return Steinberg::kResultTrue;
		}

		Steinberg::tresult PLUGIN_API attached(void* pParent, Steinberg::FIDString type) override {
			/*if (mOwner.HasUI()) {
				void* pView = nullptr;
#ifdef FW_OS_WINDOWS
				if (strcmp(type, Steinberg::kPlatformTypeHWND) == 0)
					pView = mOwner.OpenWindow(pParent);
#elif defined OS_MAC
				if (strcmp(type, Steinberg::kPlatformTypeNSView) == 0)
					pView = mOwner.OpenWindow(pParent);
				else // Carbon
					return Steinberg::kResultFalse;
#endif
				return Steinberg::kResultTrue;
			}*/

			return Steinberg::kResultFalse;
		}

		Steinberg::tresult PLUGIN_API removed() override {
			//if (mOwner.HasUI())
				//mOwner.CloseWindow();

			return CPluginView::removed();
		}

		Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override {
			//mOwner.SetScreenScale(factor);
			return Steinberg::kResultOk;
		}

		Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override {
			QUERY_INTERFACE(_iid, obj, IPlugViewContentScaleSupport::iid, IPlugViewContentScaleSupport)
				* obj = 0;
			return CPluginView::queryInterface(_iid, obj);
		}

		DELEGATE_REFCOUNT(Steinberg::CPluginView)

		static int AsciiToVK(int ascii) {
#ifdef FW_OS_WINDOWS
			HKL layout = GetKeyboardLayout(0);
			return VkKeyScanExA((CHAR)ascii, layout);
#else
			// Numbers and uppercase alpha chars map directly to VK
			if ((ascii >= 0x30 && ascii <= 0x39) || (ascii >= 0x41 && ascii <= 0x5A)) {
				return ascii;
			}

			// Lowercase alpha chars map to VK but need shifting
			if (ascii >= 0x61 && ascii <= 0x7A) {
				return ascii - 0x20;
			}

			return static_cast<int>(VirtualKey::Unknown);
#endif
		}

		static VirtualKey VSTKeyCodeToVK(Steinberg::int16 code, char ascii)
		{
			// If the keycode provided by the host is 0, we can still calculate the VK from the ascii value
			// NOTE: VKEY_EQUALS Doesn't seem to map to a Windows VK, so get the VK from the ascii char instead
			if (code == 0 || code == Steinberg::KEY_EQUALS) {
				return static_cast<VirtualKey>(AsciiToVK(ascii));
			}

			using namespace Steinberg;

			switch (code) {
			case KEY_BACK: return VirtualKey::Backspace;
			case KEY_TAB: return VirtualKey::Tab;
			case KEY_CLEAR: return VirtualKey::Clear;
			case KEY_RETURN: return VirtualKey::Enter;
			case KEY_PAUSE: return VirtualKey::Pause;
			case KEY_ESCAPE: return VirtualKey::Esc;
			case KEY_SPACE: return VirtualKey::Space;
			case KEY_NEXT: return VirtualKey::PageDown;
			case KEY_END: return VirtualKey::End;
			case KEY_HOME: return VirtualKey::Home;
			case KEY_LEFT: return VirtualKey::LeftArrow;
			case KEY_UP: return VirtualKey::UpArrow;
			case KEY_RIGHT: return VirtualKey::RightArrow;
			case KEY_DOWN: return VirtualKey::DownArrow;
			case KEY_PAGEUP: return VirtualKey::PageUp;
			case KEY_PAGEDOWN: return VirtualKey::PageDown;
			case KEY_SELECT: return VirtualKey::Select;
			case KEY_PRINT: return VirtualKey::Print;
			case KEY_ENTER: return VirtualKey::Enter;
			case KEY_SNAPSHOT: return VirtualKey::PrintScreen;
			case KEY_INSERT: return VirtualKey::Insert;
			case KEY_DELETE: return VirtualKey::Delete;
			case KEY_HELP: return VirtualKey::Help;
			case KEY_NUMPAD0: return VirtualKey::NumPad0;
			case KEY_NUMPAD1: return VirtualKey::NumPad1;
			case KEY_NUMPAD2: return VirtualKey::NumPad2;
			case KEY_NUMPAD3: return VirtualKey::NumPad3;
			case KEY_NUMPAD4: return VirtualKey::NumPad4;
			case KEY_NUMPAD5: return VirtualKey::NumPad5;
			case KEY_NUMPAD6: return VirtualKey::NumPad6;
			case KEY_NUMPAD7: return VirtualKey::NumPad7;
			case KEY_NUMPAD8: return VirtualKey::NumPad8;
			case KEY_NUMPAD9: return VirtualKey::NumPad9;
			case KEY_MULTIPLY: return VirtualKey::Multiply;
			case KEY_ADD: return VirtualKey::Add;
			case KEY_SEPARATOR: return VirtualKey::Separator;
			case KEY_SUBTRACT: return VirtualKey::Subtract;
			case KEY_DECIMAL: return VirtualKey::Decimal;
			case KEY_DIVIDE: return VirtualKey::Divide;
			case KEY_F1: return VirtualKey::F1;
			case KEY_F2: return VirtualKey::F2;
			case KEY_F3: return VirtualKey::F3;
			case KEY_F4: return VirtualKey::F4;
			case KEY_F5: return VirtualKey::F5;
			case KEY_F6: return VirtualKey::F6;
			case KEY_F7: return VirtualKey::F7;
			case KEY_F8: return VirtualKey::F8;
			case KEY_F9: return VirtualKey::F9;
			case KEY_F10: return VirtualKey::F10;
			case KEY_F11: return VirtualKey::F11;
			case KEY_F12: return VirtualKey::F12;
			case KEY_NUMLOCK: return VirtualKey::NumLock;
			case KEY_SCROLL: return VirtualKey::Scroll;
			case KEY_SHIFT: return VirtualKey::LeftShift;
			case KEY_CONTROL: return VirtualKey::LeftCtrl;
			case KEY_ALT: return VirtualKey::LeftMenu;
			case KEY_EQUALS: return VirtualKey::Unknown;
			}

			if (code >= VKEY_FIRST_ASCII) {
				return static_cast<VirtualKey>((code - VKEY_FIRST_ASCII + static_cast<int>(VirtualKey::Num0)));
			}

			return VirtualKey::Unknown;
		};

		/*static iplug::IKeyPress translateKeyMessage(Steinberg::char16 key, Steinberg::int16 keyMsg, Steinberg::int16 modifiers)
		{
			WDL_String str;

			if (key == 0) {
				key = Steinberg::VirtualKeyCodeToChar((Steinberg::uint8)keyMsg);
			}

			if (key) {
				Steinberg::String keyStr(STR(" "));
				keyStr.setChar16(0, key);
				keyStr.toMultiByte(Steinberg::kCP_Utf8);
				if (keyStr.length() == 1) {
					str.Set(keyStr.text8());
				}
			}

			iplug::IKeyPress keyPress{ str.Get(), VSTKeyCodeToVK(keyMsg, str.Get()[0]),
			  static_cast<bool>(modifiers & Steinberg::kShiftKey),
			  static_cast<bool>(modifiers & Steinberg::kCommandKey),
			  static_cast<bool>(modifiers & Steinberg::kAlternateKey) };

			return keyPress;
		}*/

		Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key, Steinberg::int16 keyMsg, Steinberg::int16 modifiers) override
		{
			return Steinberg::kResultFalse;
		}

		Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16 key, Steinberg::int16 keyMsg, Steinberg::int16 modifiers) override
		{
			return Steinberg::kResultFalse;
		}

		void Resize(int w, int h) {
			Steinberg::ViewRect newSize = Steinberg::ViewRect(0, 0, w, h);
			plugFrame->resizeView(this, &newSize);
		}
	};
}

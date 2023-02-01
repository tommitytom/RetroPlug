#pragma once

#include <any>
#include <stack>

#include "foundation/DataBuffer.h"
#include "foundation/Image.h"
#include "foundation/Input.h"
#include "foundation/Math.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"
#include "lsdj/Ram.h"
#include "lsdj/LsdjCanvas.h"

namespace rp::lsdj {
	struct InputState {
		std::vector<ButtonType::Enum> buttonPresses;
		std::vector<ButtonType::Enum> buttonReleases;
		std::array<bool, ButtonType::MAX> buttonStates = { false };

		std::vector<VirtualKey::Enum> keyPresses;
		std::vector<VirtualKey::Enum> keyReleases;
		std::array<bool, VirtualKey::COUNT> keyStates = { false };
	};

	struct UiState {
		std::stack<void*> stack;
		void* current = nullptr;
		void* focused = nullptr;

		std::unordered_map<void*, std::any> elementState;

		std::stack<int32> columnStack;
		int32 currentColumn = 0;
		int32 focusedColumn = 0;
		int32 maxColumn = 0;

		int32 currentRow = 0;
		int32 focusedRow = 0;
		int32 maxRow = 0;

		std::vector<int32> colRowCounts;
		std::vector<int32> nextColRowCounts;

		InputState input;

		bool verticalNav = true;
		bool horizontalNav = true;

		bool foundFocusThisFrame = false;
	};

	namespace ListOptions {
		enum Enum {
			None
		};
	}

	namespace SelectOptions {
		enum Enum {
			None = 0,
			Disabled = 1 << 0,
			Dimmed = 1 << 1,
		};
	}

	namespace SpinOptions {
		enum Enum {
			None = 0,
			Disabled = 1 << 0,
			Dimmed = 1 << 1,
		};
	}

	class Ui {
	private:
		UiState _state;
		lsdj::Canvas& _c;

	public:
		Ui(lsdj::Canvas& canvas): _c(canvas) {}
		~Ui() {}

		template <typename T>
		void pushElement(T& elem) {
			_state.currentRow++;

			if (_state.focusedRow == 0 && _state.currentColumn == _state.focusedColumn) {
				_state.focusedRow = _state.currentRow;
			}

			_state.nextColRowCounts.back()++;

			/*if (!_state.focused && _state.currentColumn == _state.focusedColumn) {
				_state.focused = &elem;
				_state.foundFocusThisFrame = true;
			} else if (_state.focused == &elem) {
				_state.foundFocusThisFrame = true;
			}

			_state.current = &elem;
			_state.stack.push(&elem);*/
		}

		void popElement() {
			/*_state.stack.pop();

			if (_state.stack.size() > 0) {
				_state.current = _state.stack.top();
			} else {
				_state.current = nullptr;
			}*/
		}

		void pushColumn() {
			_state.currentColumn++;
			_state.currentRow = 0;

			if (_state.focusedColumn == 0) {
				_state.focusedColumn = _state.currentColumn;
			}

			_state.nextColRowCounts.push_back(0);
		}

		void popColumn() {
			//assert(_state.currentColumn > 0);
			//_state.currentColumn--;
		}

		template <typename T>
		T& getElementState(void* element) {
			return *std::any_cast<T>(&_state.elementState[element]);
		}

		template <typename T>
		void createElementState(void* element, T&& state) {
			_state.elementState[element] = std::move(state);
		}

		template <typename T>
		T& getOrCreateElementState(void* element) {
			auto found = _state.elementState.find(element);
			if (found != _state.elementState.end()) {
				assert(found->second.type() == typeid(T));
				return *std::any_cast<T>(&found->second);
			}

			_state.elementState[element] = T();
			return *std::any_cast<T>(&_state.elementState[element]);
		}

		template <typename T>
		bool hasElementState(void* element) {
			auto found = _state.elementState.find(element);
			if (found != _state.elementState.end()) {
				assert(found->second.type() == typeid(T));
				return true;
			}

			return false;
		}

		bool hasFocus() const {
			return _state.currentColumn == _state.focusedColumn && _state.currentRow == _state.focusedRow;
			//return _state.current == _state.focused;
		}

		void startFrame() {

		}

		void endFrame() {
			_state.input.buttonPresses.clear();
			_state.input.buttonReleases.clear();
			_state.input.keyPresses.clear();
			_state.input.keyReleases.clear();

			if (!_state.foundFocusThisFrame) {
				_state.focused = nullptr;
			} else {
				_state.foundFocusThisFrame = false;
			}

			if (_state.maxColumn != _state.currentColumn) {
				_state.maxColumn = _state.currentColumn;

				if (_state.focusedColumn > _state.maxColumn) {
					_state.focusedColumn = _state.maxColumn;
				}
			}

			_state.currentColumn = 0;
			_state.currentRow = 0;
			_state.current = nullptr;

			_state.colRowCounts = _state.nextColRowCounts;
		}

		void pressKey(VirtualKey::Enum key) {
			_state.input.keyStates[key] = true;
			_state.input.keyPresses.push_back(key);
		}

		void releaseKey(VirtualKey::Enum key) {
			_state.input.keyStates[key] = false;
			_state.input.keyReleases.push_back(key);
		}

		void pressButton(ButtonType::Enum button) {
			_state.input.buttonStates[button] = true;
			_state.input.buttonPresses.push_back(button);
		}

		void releaseButton(ButtonType::Enum button) {
			_state.input.buttonStates[button] = false;
			_state.input.buttonReleases.push_back(button);
		}

		bool keyPressed(VirtualKey::Enum key) {
			for (VirtualKey::Enum k : _state.input.keyPresses) {
				if (k == key) {
					return true;
				}
			}

			return false;
		}

		bool keyReleased(VirtualKey::Enum key) {
			for (VirtualKey::Enum k : _state.input.keyReleases) {
				if (k == key) {
					return true;
				}
			}

			return false;
		}

		bool keyDown(VirtualKey::Enum key) const {
			return _state.input.keyStates[key];
		}

		bool buttonPressed(ButtonType::Enum button) {
			for (ButtonType::Enum b : _state.input.buttonPresses) {
				if (b == button) {
					return true;
				}
			}

			return false;
		}

		bool buttonReleased(ButtonType::Enum button) {
			for (ButtonType::Enum b : _state.input.buttonReleases) {
				if (b == button) {
					return true;
				}
			}

			return false;
		}

		bool buttonDown(ButtonType::Enum button) const {
			return _state.input.buttonStates[button];
		}

		void handleNavigation() {
			if (!buttonDown(ButtonType::A) && !buttonDown(ButtonType::B) && !buttonDown(ButtonType::Select)) {
				if (_state.verticalNav) {
					if (buttonPressed(ButtonType::Up)) {
						moveFocusUp();
					}

					if (buttonPressed(ButtonType::Down)) {
						moveFocusDown();
					}
				}

				if (_state.horizontalNav) {
					if (buttonPressed(ButtonType::Left)) {
						moveFocusLeft();
					}

					if (buttonPressed(ButtonType::Right)) {
						moveFocusRight();
					}
				}
			}
		}

		void setNavigationEnabled(bool vert = true, bool hori = true) {
			_state.verticalNav = vert;
			_state.horizontalNav = hori;
		}

		template <const size_t ElementCount>
		bool list(uint32 x, uint32 y, int32& selected, std::array<std::string_view, ElementCount>& items, ListOptions::Enum options = ListOptions::None) {
			return list(x, y, selected, items.data(), items.size(), options);
		}

		void moveFocusRight() {
			if (_state.focusedColumn < _state.maxColumn) {
				_state.focusedColumn++;
				_state.focused = nullptr;
				setNavigationEnabled();
			}
		}

		void moveFocusLeft() {
			if (_state.focusedColumn > 0) {
				_state.focusedColumn--;
				_state.focusedRow = 0;
				_state.focused = nullptr;
				setNavigationEnabled();
			}
		}

		void moveFocusDown() {
			if (_state.focusedColumn > 0 && _state.focusedRow < _state.colRowCounts[_state.focusedColumn - 1]) {
				_state.focusedRow++;
				setNavigationEnabled();
			}
		}

		void moveFocusUp() {
			if (_state.focusedRow > 1) {
				_state.focusedRow--;
				setNavigationEnabled();
			}
		}

		bool list(uint32 x, uint32 y, int32& selected, std::string_view* items, size_t itemCount, ListOptions::Enum options = ListOptions::None) {
			pushColumn();
			pushElement(selected);

			bool changed = false;

			if (hasFocus()) {
				setNavigationEnabled(false, true);

				if (buttonPressed(ButtonType::Up) && selected > 0) {
					selected--;
					changed = true;
				}

				if (buttonPressed(ButtonType::Down) && selected < (int32)itemCount - 1) {
					selected++;
					changed = true;
				}
			}

			for (uint32 i = 0; i < (uint32)itemCount; ++i) {
				//_c.text(x, y + i, items[i], (int32)i == selected ? ColorSets::Selection : ColorSets::Normal);
			}

			popElement();
			popColumn();

			return changed;
		}

		struct TextBoxState {
			bool editing = false;
			uint32 cursorPos = 0;
			std::string initialValue;
		};

		bool convertKeyPress(VirtualKey::Enum key, char& out) {
			switch (key) {
			case VirtualKey::A: out = 'A'; return true;
			case VirtualKey::B: out = 'B'; return true;
			case VirtualKey::C: out = 'C'; return true;
			case VirtualKey::D: out = 'D'; return true;
			case VirtualKey::E: out = 'E'; return true;
			case VirtualKey::F: out = 'F'; return true;
			case VirtualKey::G: out = 'G'; return true;
			case VirtualKey::H: out = 'H'; return true;
			case VirtualKey::I: out = 'I'; return true;
			case VirtualKey::J: out = 'J'; return true;
			case VirtualKey::K: out = 'K'; return true;
			case VirtualKey::L: out = 'L'; return true;
			case VirtualKey::M: out = 'M'; return true;
			case VirtualKey::N: out = 'N'; return true;
			case VirtualKey::O: out = 'O'; return true;
			case VirtualKey::P: out = 'P'; return true;
			case VirtualKey::Q: out = 'Q'; return true;
			case VirtualKey::R: out = 'R'; return true;
			case VirtualKey::S: out = 'S'; return true;
			case VirtualKey::T: out = 'T'; return true;
			case VirtualKey::U: out = 'U'; return true;
			case VirtualKey::V: out = 'V'; return true;
			case VirtualKey::W: out = 'W'; return true;
			case VirtualKey::X: out = 'X'; return true;
			case VirtualKey::Y: out = 'Y'; return true;
			case VirtualKey::Z: out = 'Z'; return true;
			case VirtualKey::Num0: out = '0'; return true;
			case VirtualKey::Num1: out = '1'; return true;
			case VirtualKey::Num2: out = '2'; return true;
			case VirtualKey::Num3: out = '3'; return true;
			case VirtualKey::Num4: out = '4'; return true;
			case VirtualKey::Num5: out = '5'; return true;
			case VirtualKey::Num6: out = '6'; return true;
			case VirtualKey::Num7: out = '7'; return true;
			case VirtualKey::Num8: out = '8'; return true;
			case VirtualKey::Num9: out = '9'; return true;
			case VirtualKey::NumPad0: out = '0'; return true;
			case VirtualKey::NumPad1: out = '1'; return true;
			case VirtualKey::NumPad2: out = '2'; return true;
			case VirtualKey::NumPad3: out = '3'; return true;
			case VirtualKey::NumPad4: out = '4'; return true;
			case VirtualKey::NumPad5: out = '5'; return true;
			case VirtualKey::NumPad6: out = '6'; return true;
			case VirtualKey::NumPad7: out = '7'; return true;
			case VirtualKey::NumPad8: out = '8'; return true;
			case VirtualKey::NumPad9: out = '9'; return true;
			}

			return false;
		}

		bool textBox(uint32 x, uint32 y, std::string& text, uint32 size) {
			pushElement(text);

			bool changed = false;
			bool editable = true;
			ColorSets colorSet = hasFocus() ? ColorSets::Selection : ColorSets::Shaded;

			if (text.size() > size) {
				text = text.substr(0, size);
				changed = true;
			}

			TextBoxState& state = getOrCreateElementState<TextBoxState>((void*)&text);

			if (hasFocus()) {
				if (editable) {
					if (state.editing) {
						if (keyPressed(VirtualKey::Backspace)) {
							if (state.cursorPos > 0) {
								if (text[state.cursorPos] == ' ') {
									state.cursorPos--;
								}

								text[state.cursorPos] = ' ';

								changed = true;
							}
						} else {
							for (VirtualKey::Enum key : _state.input.keyPresses) {
								// Convert to LSDJ char
								char ch;
								if (convertKeyPress(key, ch)) {
									text[state.cursorPos] = ch;

									if (state.cursorPos < size - 1) {
										state.cursorPos++;
									}

									changed = true;
								}
							}
						}

						if (buttonPressed(ButtonType::Start)) {
							state.editing = false;
							state.initialValue.clear();
						}

						if (buttonPressed(ButtonType::Down)) {
							state.editing = false;
							state.initialValue.clear();
							moveFocusDown();
						}
					} else {
						if (buttonDown(ButtonType::A) || buttonDown(ButtonType::Start)) {
							state.editing = true;
							state.initialValue = text;

							size_t found = text.find_first_of('_');
							if (found != std::string::npos) {
								state.cursorPos = (uint32)found;
							} else {
								state.cursorPos = (uint32)text.size();
							}

							if (state.cursorPos == size) {
								state.cursorPos = size - 1;
							}

							setNavigationEnabled(true, false);
						}
					}
				}
			} else {
				if (state.editing) {
					state.editing = false;
					state.initialValue.clear();
				}
			}

			_c.text(x, y, text, colorSet, false);

			if (state.editing) {
				_c.text(x + state.cursorPos, y, text.substr(state.cursorPos, 1), ColorSets::Scroll);
			}

			popElement();

			return changed;
		}

		template <const int ItemCount>
		bool select(uint32 x, uint32 y, int32& selected, const std::array<std::string_view, ItemCount>& items, SelectOptions::Enum options = SelectOptions::None) {
			return select(x, y, selected, items.data(), items.size(), options);
		}

		bool select(uint32 x, uint32 y, int32& selected, const std::string_view* items, size_t itemCount, SelectOptions::Enum options = SelectOptions::None) {
			pushElement(selected);

			bool editable = true;
			if (options & SelectOptions::Disabled) {
				options = (SelectOptions::Enum)(options | SelectOptions::Dimmed);
				editable = false;
			}

			bool changed = false;
			ColorSets colorSet = hasFocus() ? ColorSets::Selection : ColorSets::Shaded;
			bool dimmed = options & SelectOptions::Dimmed;

			if (hasFocus() && editable) {
				if (buttonDown(ButtonType::A) && buttonPressed(ButtonType::Left)) {
					if (selected > 0) {
						selected--;
						changed = true;
					}
				}

				if (buttonDown(ButtonType::A) && buttonPressed(ButtonType::Right)) {
					if (selected < (int32)itemCount - 1) {
						selected++;
						changed = true;
					}
				}
			}

			uint32 width = 0;
			for (size_t i = 0; i < itemCount; ++i) {
				width = std::max(width, (uint32)items[i].size());
			}

			_c.fill(x - width, y, width, 1, colorSet, 0);

			if (selected >= 0 && selected < (int32)itemCount) {
				uint32 off = x - (uint32)items[selected].size();
				_c.text(off, y, items[selected], colorSet, dimmed);
			}

			popElement();

			return changed;
		}

		template <typename T>
		bool hexSpin(uint32 x, uint32 y, T& value, uint8 min = 0, uint8 max = 0xFF, SpinOptions::Enum options = SpinOptions::None) {
			pushElement(value);

			T editValue = value;

			bool editable = true;
			if (options & SpinOptions::Disabled) {
				options = (SpinOptions::Enum)(options | SpinOptions::Dimmed);
				editable = false;
			}

			if (editValue < (T)min) editValue = (T)min;
			if (editValue > (T)max) editValue = (T)max;

			int32 move = 0;

			if (hasFocus() && editable) {
				int32 range = max - min;

				if (buttonDown(ButtonType::A)) {
					if (buttonPressed(ButtonType::Up)) {
						move = 0x10;
					}

					if (buttonPressed(ButtonType::Down)) {
						move = -0x10;
					}

					if (buttonPressed(ButtonType::Left)) {
						move = -0x01;
					}

					if (buttonPressed(ButtonType::Right)) {
						move = 0x01;
					}

					if (move != 0) {
						int32 shifted = ((uint8)editValue - min) + move;
						if (shifted < 0) {
							shifted = 0;
						}

						if (shifted > range) {
							shifted = range;
						}

						editValue = (T)((uint8)(shifted % (range + 1)) + min);
					}
				}
			}

			ColorSets colorSet = hasFocus() ? ColorSets::Selection : ColorSets::Shaded;
			bool dimmed = options & SpinOptions::Dimmed;

			if (move) {
				value = editValue;
			}

			_c.hexNumber(x - 2, y, value, colorSet, true, dimmed);

			popElement();

			return move != 0;
		}

		void render(const Song& song, const Ram& state);

		void renderSong(const Song& song, const Ram& state, uint32 rowOffset = 0);

		void renderSongData(const Song& song, const Ram& state, uint32 rowOffset);

		void renderChain(const Song& song, const Ram& state, uint8 channel);

		void renderChainData(const Chain& chain, const Ram& state, uint8 channel);

		void renderPhrase(const Song& song, const Ram& state, uint8 channel);

		void renderPhraseData(const Phrase& phrase, uint8 playbackOffset);

		void renderMode1(const Song& song, const Ram& state);

		void renderMode2(const Song& song, const Ram& state);

	private:
		void renderBase(const Ram& state, uint8 channel, ScreenType screenType = ScreenType::Unknown);
	};
}

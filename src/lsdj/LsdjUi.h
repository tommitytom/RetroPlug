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
		std::vector<orb::ButtonType> buttonPresses;
		std::vector<orb::ButtonType> buttonReleases;
		std::array<bool, static_cast<int>(orb::ButtonType::MAX)> buttonStates = { false };

		std::vector<orb::VirtualKey> keyPresses;
		std::vector<orb::VirtualKey> keyReleases;
		std::array<bool, static_cast<int>(orb::VirtualKey::COUNT)> keyStates = { false };
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
		Ui(lsdj::Canvas& canvas) : _c(canvas) {}
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

		void pressKey(orb::VirtualKey key) {
			_state.input.keyStates[static_cast<int>(key)] = true;
			_state.input.keyPresses.push_back(key);
		}

		void releaseKey(orb::VirtualKey key) {
			_state.input.keyStates[static_cast<int>(key)] = false;
			_state.input.keyReleases.push_back(key);
		}

		void pressButton(orb::ButtonType button) {
			_state.input.buttonStates[static_cast<int>(button)] = true;
			_state.input.buttonPresses.push_back(button);
		}

		void releaseButton(orb::ButtonType button) {
			_state.input.buttonStates[static_cast<int>(button)] = false;
			_state.input.buttonReleases.push_back(button);
		}

		bool keyPressed(orb::VirtualKey key) {
			for (orb::VirtualKey k : _state.input.keyPresses) {
				if (k == key) {
					return true;
				}
			}

			return false;
		}

		bool keyReleased(orb::VirtualKey key) {
			for (orb::VirtualKey k : _state.input.keyReleases) {
				if (k == key) {
					return true;
				}
			}

			return false;
		}

		bool keyDown(orb::VirtualKey key) const {
			return _state.input.keyStates[static_cast<int>(key)];
		}

		bool buttonPressed(orb::ButtonType button) {
			for (orb::ButtonType b : _state.input.buttonPresses) {
				if (b == button) {
					return true;
				}
			}

			return false;
		}

		bool buttonReleased(orb::ButtonType button) {
			for (orb::ButtonType b : _state.input.buttonReleases) {
				if (b == button) {
					return true;
				}
			}

			return false;
		}

		bool buttonDown(orb::ButtonType button) const {
			return _state.input.buttonStates[static_cast<int>(button)];
		}

		void handleNavigation() {
			if (!buttonDown(orb::ButtonType::A) && !buttonDown(orb::ButtonType::B) && !buttonDown(orb::ButtonType::Select)) {
				if (_state.verticalNav) {
					if (buttonPressed(orb::ButtonType::Up)) {
						moveFocusUp();
					}

					if (buttonPressed(orb::ButtonType::Down)) {
						moveFocusDown();
					}
				}

				if (_state.horizontalNav) {
					if (buttonPressed(orb::ButtonType::Left)) {
						moveFocusLeft();
					}

					if (buttonPressed(orb::ButtonType::Right)) {
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

				if (buttonPressed(orb::ButtonType::Up) && selected > 0) {
					selected--;
					changed = true;
				}

				if (buttonPressed(orb::ButtonType::Down) && selected < (int32)itemCount - 1) {
					selected++;
					changed = true;
				}
			}

			for (uint32 i = 0; i < (uint32)itemCount; ++i) {
				_c.text(x, y + i, items[i], (int32)i == selected ? ColorSets::Selection : ColorSets::Normal);
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

		bool convertKeyPress(orb::VirtualKey key, char& out) {
			switch (key) {
			case orb::VirtualKey::A: out = 'A'; return true;
			case orb::VirtualKey::B: out = 'B'; return true;
			case orb::VirtualKey::C: out = 'C'; return true;
			case orb::VirtualKey::D: out = 'D'; return true;
			case orb::VirtualKey::E: out = 'E'; return true;
			case orb::VirtualKey::F: out = 'F'; return true;
			case orb::VirtualKey::G: out = 'G'; return true;
			case orb::VirtualKey::H: out = 'H'; return true;
			case orb::VirtualKey::I: out = 'I'; return true;
			case orb::VirtualKey::J: out = 'J'; return true;
			case orb::VirtualKey::K: out = 'K'; return true;
			case orb::VirtualKey::L: out = 'L'; return true;
			case orb::VirtualKey::M: out = 'M'; return true;
			case orb::VirtualKey::N: out = 'N'; return true;
			case orb::VirtualKey::O: out = 'O'; return true;
			case orb::VirtualKey::P: out = 'P'; return true;
			case orb::VirtualKey::Q: out = 'Q'; return true;
			case orb::VirtualKey::R: out = 'R'; return true;
			case orb::VirtualKey::S: out = 'S'; return true;
			case orb::VirtualKey::T: out = 'T'; return true;
			case orb::VirtualKey::U: out = 'U'; return true;
			case orb::VirtualKey::V: out = 'V'; return true;
			case orb::VirtualKey::W: out = 'W'; return true;
			case orb::VirtualKey::X: out = 'X'; return true;
			case orb::VirtualKey::Y: out = 'Y'; return true;
			case orb::VirtualKey::Z: out = 'Z'; return true;
			case orb::VirtualKey::Num0: out = '0'; return true;
			case orb::VirtualKey::Num1: out = '1'; return true;
			case orb::VirtualKey::Num2: out = '2'; return true;
			case orb::VirtualKey::Num3: out = '3'; return true;
			case orb::VirtualKey::Num4: out = '4'; return true;
			case orb::VirtualKey::Num5: out = '5'; return true;
			case orb::VirtualKey::Num6: out = '6'; return true;
			case orb::VirtualKey::Num7: out = '7'; return true;
			case orb::VirtualKey::Num8: out = '8'; return true;
			case orb::VirtualKey::Num9: out = '9'; return true;
			case orb::VirtualKey::NumPad0: out = '0'; return true;
			case orb::VirtualKey::NumPad1: out = '1'; return true;
			case orb::VirtualKey::NumPad2: out = '2'; return true;
			case orb::VirtualKey::NumPad3: out = '3'; return true;
			case orb::VirtualKey::NumPad4: out = '4'; return true;
			case orb::VirtualKey::NumPad5: out = '5'; return true;
			case orb::VirtualKey::NumPad6: out = '6'; return true;
			case orb::VirtualKey::NumPad7: out = '7'; return true;
			case orb::VirtualKey::NumPad8: out = '8'; return true;
			case orb::VirtualKey::NumPad9: out = '9'; return true;
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
						if (keyPressed(orb::VirtualKey::Backspace)) {
							if (state.cursorPos > 0) {
								if (text[state.cursorPos] == ' ') {
									state.cursorPos--;
								}

								text[state.cursorPos] = ' ';

								changed = true;
							}
						} else {
							for (orb::VirtualKey key : _state.input.keyPresses) {
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

						if (buttonPressed(orb::ButtonType::Start)) {
							state.editing = false;
							state.initialValue.clear();
						}

						if (buttonPressed(orb::ButtonType::Down)) {
							state.editing = false;
							state.initialValue.clear();
							moveFocusDown();
						}
					} else {
						if (buttonDown(orb::ButtonType::A) || buttonDown(orb::ButtonType::Start)) {
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
				if (buttonDown(orb::ButtonType::A) && buttonPressed(orb::ButtonType::Left)) {
					if (selected > 0) {
						selected--;
						changed = true;
					}
				}

				if (buttonDown(orb::ButtonType::A) && buttonPressed(orb::ButtonType::Right)) {
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

				if (buttonDown(orb::ButtonType::A)) {
					if (buttonPressed(orb::ButtonType::Up)) {
						move = 0x10;
					}

					if (buttonPressed(orb::ButtonType::Down)) {
						move = -0x10;
					}

					if (buttonPressed(orb::ButtonType::Left)) {
						move = -0x01;
					}

					if (buttonPressed(orb::ButtonType::Right)) {
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

		void render(const Rom& rom, const Song& song, const Ram& state);

		void renderSong(const Song& song, const Ram& state, uint32 rowOffset = 0);

		void renderSongData(const Song& song, const Ram& state, uint32 rowOffset);

		void renderChain(const Song& song, const Ram& state, uint8 channel);

		void renderChainData(const Chain& chain, const Ram& state, uint8 channel);

		void renderPhrase(const Rom& rom, const Song& song, const Ram& state, uint8 channel);

		void renderPhraseData(const Rom& rom, const Phrase& phrase, uint8 playbackOffset);

		void renderMode1(const Rom& rom, const Song& song, const Ram& state);

		void renderMode2(const Rom& rom, const Song& song, const Ram& state);

	private:
		void renderBase(const Ram& state, uint8 channel, ScreenType screenType = ScreenType::Unknown);
	};
}

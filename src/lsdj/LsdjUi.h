#pragma once

#include <stack>

#include "util/DataBuffer.h"
#include "util/Image.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"
#include "lsdj/Ram.h"
#include "lsdj/LsdjCanvas.h"
#include "RpMath.h"
#include "core/Input.h"

namespace rp::lsdj {
	struct InputState {
		std::vector<ButtonType::Enum> buttonPresses;
		std::vector<ButtonType::Enum> buttonReleases;
		std::array<bool, ButtonType::MAX> buttonStates = { false };
	};

	struct UiState {
		std::stack<void*> stack;
		void* current = nullptr;
		void* focused = nullptr;
		
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

	class Ui {
	private:
		UiState _state;
		Canvas& _c;

	public:
		Ui(Canvas& canvas): _c(canvas) {}
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

		bool hasFocus() const {
			return _state.currentColumn == _state.focusedColumn && _state.currentRow == _state.focusedRow;
			//return _state.current == _state.focused;
		}

		void startFrame() {

		}

		void endFrame() {
			_state.input.buttonPresses.clear();
			_state.input.buttonReleases.clear();
			
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

		void pressButton(ButtonType::Enum button) {
			_state.input.buttonStates[button] = true;
			_state.input.buttonPresses.push_back(button);
		}

		void releaseButton(ButtonType::Enum button) {
			_state.input.buttonStates[button] = false;
			_state.input.buttonReleases.push_back(button);
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
				_c.text(x, y + i, items[i], (int32)i == selected ? ColorSets::Selection : ColorSets::Normal);
			}

			popElement();
			popColumn();

			return changed;
		}

		template <const int ItemCount>
		bool select(uint32 x, uint32 y, int32& selected, const std::array<std::string_view, ItemCount>& items) {
			return select(x, y, selected, items.data(), items.size());
		}

		bool select(uint32 x, uint32 y, int32& selected, const std::string_view* items, size_t itemCount) {
			pushElement(selected);

			bool changed = false;
			ColorSets colorSet = hasFocus() ? ColorSets::Selection : ColorSets::Shaded;

			if (hasFocus()) {
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
				_c.text(off, y, items[selected], colorSet);
			}
			
			popElement();
			
			return changed;
		}

		template <typename T>
		bool hexSpin(uint32 x, uint32 y, T& value, uint8 min = 0, uint8 max = 0xFF) {
			pushElement(value);

			if (value < (T)min) value = (T)min;
			if (value > (T)max) value = (T)max;

			int32 move = 0;

			if (hasFocus()) {
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
						int32 shifted = ((uint8)value - min) + move;
						if (shifted < 0) {
							shifted = 0;
						}

						if (shifted > range) {
							shifted = range;
						}

						value = (T)((uint8)(shifted % (range + 1)) + min);
					}
				}
			}

			ColorSets colorSet = hasFocus() ? ColorSets::Selection : ColorSets::Shaded;
			_c.hexNumber(x - 2, y, value, colorSet);

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

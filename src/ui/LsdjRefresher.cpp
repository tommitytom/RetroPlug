#include "LsdjRefresher.h"

#include "foundation/FsUtil.h"

using namespace rp;

static uint32 calculateRowDifference(fw::Image& v1, fw::Image& v2) {
	assert(v1.dimensions() == v2.dimensions());

	int32 diffCount = 0;
	for (int32 i = 0; i < v1.h(); ++i) {
		if (v1.getRow(i) != v2.getRow(i)) {
			diffCount++;
		}
	}

	return (uint32)diffCount;
}

static void saveImage(fw::Image& image, fs::path target) {
	std::string p = target.string();
	stbi_write_png(p.c_str(), image.w(), image.h(), 4, image.getData(), image.w() * 4);
}

void LsdjRefresher::update(f32 delta) {
	if (_refreshState != RefreshState::None) {
		lsdj::Ram ram(_system->getMemory(MemoryType::Ram, AccessType::Read), _offsets);
		uint8 currentScreen = ram.getScreenY();

		switch (_refreshState) {
		case RefreshState::Screen1:
			if (currentScreen == _screen1) {
				_refreshState = RefreshState::Screen2;
			}

			break;
		case RefreshState::Screen2:
			if (currentScreen == _screen2) {
				bool finished = true;

				if (_overlay) {
					uint32 diff = calculateRowDifference(*_lastFrame, *_overlay);
					finished = diff <= 16;
				}

				if (finished) {
					_refreshState = RefreshState::None;
					_overlay = nullptr;

					if (_chainRefresh) {
						_chainRefresh = false;
						refresh();
					}
				}
			}

			break;
		}
	}

	if (_system->getIo() && _system->getIo()->output.video) {
		_lastFrame = _system->getIo()->output.video;
	}
}

void LsdjRefresher::refresh() {
	if (!isValid()) {
		return;
	}

	if (_refreshState != RefreshState::None) {
		_chainRefresh = true;
		return;
	}

	_refreshState = RefreshState::Screen1;
	_overlay = _lastFrame;

	lsdj::Ram ram(_system->getMemory(MemoryType::Ram, AccessType::Read), _offsets);

	uint8 currentScreen = ram.getScreenY();
	ButtonType::Enum dir1 = ButtonType::Down;
	ButtonType::Enum dir2 = ButtonType::Up;

	_screen1 = currentScreen + 1;
	_screen2 = currentScreen;

	if (ram.getScreenY() == 1) {
		_screen1 = currentScreen - 1;
		dir1 = ButtonType::Up;
		dir2 = ButtonType::Down;
	}

	ButtonStreamWriter<ButtonType::MAX, 8> writer;
	writer.setDefaultDelay(40);
	writer
		.holdDuration(ButtonType::Select, 40)
		.press(dir1)
		.press(dir2)
		.releaseAll();

	_system->getIo()->input.buttons.push_back(writer.data());
}

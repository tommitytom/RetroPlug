#pragma once

#include <bgfx/bgfx.h>

namespace fw {
	template <typename BaseT, typename HandleT>
	class BgfxResource : public BaseT {
	private:
		HandleT _handle = { bgfx::kInvalidHandle };

	public:
		BgfxResource() = default;
		BgfxResource(HandleT handle) : _handle(handle) {}
		~BgfxResource() {
			if (bgfx::isValid(_handle)) {
				bgfx::destroy(_handle);
			}
		}

		HandleT getBgfxHandle() const {
			return _handle;
		}
	};
}

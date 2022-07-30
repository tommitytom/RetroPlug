#pragma once

#include "RpMath.h"
#include "graphics/Canvas.h"

#include <bgfx/bgfx.h>

namespace rp {
	using UriHash = entt::id_type;

	class ResourceManager {
	private:
		//std::unordered_map<UriHash, Resource>
	};

	class FrameBuffer {
	private:
		bgfx::FrameBufferHandle _handle = { bgfx::kInvalidHandle };
		Dimension _dimensions;
		void* _nwh = nullptr;

	public:
		FrameBuffer() = default;
		FrameBuffer(void* nwh, Dimension res): _nwh(nwh), _dimensions(res) {
			_handle = bgfx::createFrameBuffer(nwh, uint16_t(res.w), uint16_t(res.h));
		}
		~FrameBuffer() {
			if (bgfx::isValid(_handle)) {
				bgfx::destroy(_handle);
			}
		}

		bgfx::FrameBufferHandle getHandle() const {
			return _handle;
		}

		void setViewFrameBuffer(uint32 id) {
			assert(bgfx::isValid(_handle));
			bgfx::setViewFrameBuffer(id, _handle);
		}
	};

	/*template <typename T>
	using TypedHandle = entt::resource<std::unique_ptr<T>>;

	using TextureHandle = TypedHandle<Texture>;*/

	class BgfxRenderContext {
	private:
		bgfx::DynamicVertexBufferHandle _vert = BGFX_INVALID_HANDLE;
		bgfx::DynamicIndexBufferHandle _ind = BGFX_INVALID_HANDLE;
		bgfx::ProgramHandle _prog;

		bgfx::UniformHandle _textureUniform;
		bgfx::UniformHandle _scaleUniform;

	public:
		BgfxRenderContext(void* nativeWindowHandle, Dimension res);
		~BgfxRenderContext();

		void beginFrame();

		void renderCanvas(engine::Canvas& canvas);

		void endFrame();
	};

	using RenderContext = BgfxRenderContext;
}

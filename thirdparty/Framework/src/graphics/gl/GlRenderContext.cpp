#include "GlRenderContext.h"

#include <fstream>

#include <glad/glad.h>
//#include <glfw/glfw3.h>

#include "foundation/ResourceManager.h"

#include "graphics/gl/GlDefaultShaders.h"
#include "graphics/gl/GlShader.h"
#include "graphics/gl/GlShaderProgram.h"
#include "graphics/gl/GlTexture.h"

namespace fs = std::filesystem;

namespace fw {
	void GLAPIENTRY MessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
		spdlog::error("GL error: {}", message);
		//fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
				//(type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
				 // type, severity, message);
	}

	void mtxOrtho(float* _result, float _left, float _right, float _bottom, float _top, float _near, float _far, float _offset, bool _homogeneousNdc) {
		const float aa = 2.0f / (_right - _left);
		const float bb = 2.0f / (_top - _bottom);
		const float cc = (_homogeneousNdc ? 2.0f : 1.0f) / (_far - _near);
		const float dd = (_left + _right) / (_left - _right);
		const float ee = (_top + _bottom) / (_bottom - _top);
		const float ff = _homogeneousNdc
			? (_near + _far) / (_near - _far)
			: _near / (_near - _far)
			;

		memset(_result, 0, sizeof(float) * 16);
		_result[0] = aa;
		_result[5] = bb;
		_result[10] = cc;
		_result[12] = dd + _offset;
		_result[13] = ee;
		_result[14] = ff;
		_result[15] = 1.0f;
	}

	GlRenderContext::GlRenderContext(NativeWindowHandle mainWindow, Dimension res, ResourceManager& resourceManager)
		: _mainWindow(mainWindow), _resolution(res), _resourceManager(resourceManager) {

		if (!gladLoadGL()) {
			spdlog::error("Failed to initialize OpenGL context");
			return;
		}

		/*if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
			spdlog::error("Failed to initialize OpenGL context");
			return;
		}*/

		//glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
		//glDebugMessageCallbackARB(MessageCallback, 0);

		glEnable(GL_LINE_SMOOTH);
		glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

		_resourceManager.addProvider<Shader, GlShaderProvider>();
		_resourceManager.addProvider<ShaderProgram>(std::make_unique<GlShaderProgramProvider>(_resourceManager.getLookup()));
		_resourceManager.addProvider<Texture, GlTextureProvider>();

		TextureDesc whiteTextureDesc = TextureDesc{
			.dimensions = { 8, 8 },
			.depth = 4
		};

		const size_t size = (size_t)(whiteTextureDesc.dimensions.w * whiteTextureDesc.dimensions.h * whiteTextureDesc.depth);
		whiteTextureDesc.data.resize(size);
		memset(whiteTextureDesc.data.data(), 0xFF, size);

		_defaultTexture = _resourceManager.create<Texture>("textures/white", whiteTextureDesc);

		auto shaderDescs = getDefaultGlShaders();

		_resourceManager.create<Shader>("shaders/CanvasVertex", shaderDescs.first);
		_resourceManager.create<Shader>("shaders/CanvasFragment", shaderDescs.second);

		_defaultProgram = _resourceManager.create<ShaderProgram>("shaders/CanvasDefault", {
			"shaders/CanvasVertex",
			"shaders/CanvasFragment"
		});

		GLuint programHandle = _defaultProgram.getResourceAs<GlShaderProgram>().getGlHandle();

		_projUniform = glGetUniformLocation(programHandle, "u_proj");
		_textureUniform = glGetUniformLocation(programHandle, "s_tex");

		glGenVertexArrays(1, &_arrayBuffer);
		glGenBuffers(1, &_vertexBuffer);
		glGenBuffers(1, &_indexBuffer);

		glBindVertexArray(_arrayBuffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, _vertexBuffer);
		
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), (void*)offsetof(CanvasVertex, pos));
		glEnableVertexAttribArray(0);

		glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(CanvasVertex), (void*)offsetof(CanvasVertex, abgr));
		glEnableVertexAttribArray(1);

		glVertexAttribPointer(2, 2, GL_FLOAT, GL_TRUE, sizeof(CanvasVertex), (void*)offsetof(CanvasVertex, uv));
		glEnableVertexAttribArray(2);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}

	GlRenderContext::~GlRenderContext() {

	}

	void GlRenderContext::cleanup() {
		/*for (FrameBuffer& fb : _frameBuffers) {
			bgfx::destroy(fb.handle);
		}
		_frameBuffers.clear();*/

		if (_arrayBuffer != 0) {
			glDeleteVertexArrays(1, &_arrayBuffer);
			glDeleteBuffers(1, &_vertexBuffer);
			glDeleteBuffers(1, &_indexBuffer);

			_arrayBuffer = 0;
			_vertexBuffer = 0;
			_indexBuffer = 0;
		}

		_defaultProgram = nullptr;
		_defaultTexture = nullptr;
	}

	void GlRenderContext::beginFrame(f32 delta) {
		_lastDelta = delta;
		_totalTime += (f64)delta;
		_viewOffset = 0;
	}

	GLenum getGlPrimitive(engine::RenderPrimitive primitive) {
		switch (primitive) {
		case engine::RenderPrimitive::Triangles: return GL_TRIANGLES;
		case engine::RenderPrimitive::TriangleFan: return GL_TRIANGLE_FAN;
		case engine::RenderPrimitive::TriangleStrip: return GL_TRIANGLE_STRIP;
		case engine::RenderPrimitive::LineList: return GL_LINES;
		case engine::RenderPrimitive::LineStrip: return GL_LINE_STRIP;
		case engine::RenderPrimitive::Points: return GL_POINTS;
		}

		return GL_INVALID_ENUM;
	}

	void GlRenderContext::renderCanvas(engine::Canvas& canvas, NativeWindowHandle window) {
		const engine::CanvasGeometry& geom = canvas.getGeometry();
		uint32 nextViewOffset = _viewOffset;

		/*bgfx::FrameBufferHandle frameBuffer;

		if (window == _mainWindow) {
			frameBuffer = BGFX_INVALID_HANDLE; // This sets the main window back buffer as the frame buffer

			if (canvas.getDimensions() != _resolution) {
				_resolution = canvas.getDimensions();
				bgfx::reset((uint32_t)_resolution.w, (uint32_t)_resolution.h, BGFX_RESET_VSYNC);
			}
		} else {
			frameBuffer = acquireFrameBuffer(window, canvas.getDimensions());
		}*/

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		if (geom.vertices.size()) {
			uint32 vertSize = (uint32)geom.vertices.size() * sizeof(engine::CanvasVertex);
			uint32 indexSize = (uint32)geom.indices.size() * sizeof(uint32);

			glBindVertexArray(_arrayBuffer);

			glBindBuffer(GL_ARRAY_BUFFER, _vertexBuffer);

			if (vertSize <= _vertexBufferSize) {
				[[likely]]
				glBufferSubData(GL_ARRAY_BUFFER, 0, vertSize, geom.vertices.data());
			} else {
				glBufferData(GL_ARRAY_BUFFER, vertSize, geom.vertices.data(), GL_DYNAMIC_DRAW);
				_vertexBufferSize = vertSize;
			}

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBuffer);

			if (indexSize <= _indexBufferSize) {
				[[likely]]
				glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indexSize, geom.indices.data());
			} else {
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexSize, geom.indices.data(), GL_DYNAMIC_DRAW);
				_indexBufferSize = indexSize;
			}

			f32 _pixelRatio = 1.0f;

			for (const engine::CanvasBatch& batch : geom.batches) {
				uint32 batchViewId = _viewOffset + batch.viewId;
				assert(batchViewId <= 255);

				f32 projMtx[16];
				mtxOrtho(projMtx, batch.projection.x, batch.projection.right(), batch.projection.bottom(), batch.projection.y, -1, 1, 0, false);

				glViewport((GLint)batch.viewArea.x, (GLint)batch.viewArea.y, (GLsizei)batch.viewArea.w, (GLsizei)batch.viewArea.h);

				if (batch.scissor.area() > 0) {
					glEnable(GL_SCISSOR_TEST);
					glScissor((GLint)batch.scissor.x, (GLint)batch.scissor.y, (GLsizei)batch.scissor.w, (GLsizei)batch.scissor.h);
				} else {
					glDisable(GL_SCISSOR_TEST);
				}

				//bgfx::setViewFrameBuffer(batchViewId, frameBuffer);

				for (const engine::CanvasSurface& surface : batch.surfaces) {
					GLenum primitive = getGlPrimitive(surface.primitive);
					assert(primitive != GL_INVALID_ENUM);

					const ShaderProgramHandle programHandle = surface.program.isValid() ? surface.program : _defaultProgram;
					const TextureHandle textureHandle = surface.texture.isValid() ? surface.texture : _defaultTexture;

					const GlShaderProgram& program = programHandle.getResourceAs<GlShaderProgram>();
					const GlTexture& texture = textureHandle.getResourceAs<GlTexture>();

					glUseProgram(program.getGlHandle());

					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, texture.getGlHandle());

					glUniform1i(_textureUniform, 0);
					glUniformMatrix4fv(_projUniform, 1, GL_FALSE, projMtx);

					glBindVertexArray(_arrayBuffer);

					glDrawElements(primitive, (GLsizei)surface.indexCount, GL_UNSIGNED_INT, (void*)(surface.indexOffset * sizeof(uint32)));

					nextViewOffset = batchViewId + 1;
				}
			}

			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindVertexArray(0);
		}

		_viewOffset = nextViewOffset;
	}

	void GlRenderContext::endFrame() {
		_frameCount++;
		// TODO: Tidy up old frame buffers based on FrameBuffer::frameLastUsed and _frameCount
	}

	uint32 GlRenderContext::acquireFrameBuffer(NativeWindowHandle window, Dimension dimensions) {
		/*for (FrameBuffer& frameBuffer : _frameBuffers) {
			if (frameBuffer.window == window) {
				// Frame buffer already exists for this window

				if (frameBuffer.dimensions != dimensions) {
					// Window has changed size, resize framebuffer

					bgfx::destroy(frameBuffer.handle);
					frameBuffer.handle = bgfx::createFrameBuffer(window, dimensions.w, dimensions.h);
					frameBuffer.dimensions = dimensions;
				}

				frameBuffer.frameLastUsed = _frameCount;

				return frameBuffer.handle;
			}
		}

		// Frame buffer does not exist, create a new one
		_frameBuffers.push_back(FrameBuffer{
			.window = window,
			.handle = bgfx::createFrameBuffer(window, dimensions.w, dimensions.h),
			.dimensions = dimensions,
			.frameLastUsed = _frameCount
		});

		return _frameBuffers.back().handle;*/
		return 0;
	}

	ShaderProgramHandle GlRenderContext::getDefaultProgram() const {
		return _defaultProgram;
	}
}

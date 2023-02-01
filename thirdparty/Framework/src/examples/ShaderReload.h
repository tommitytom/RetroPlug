#pragma once

#include "ui/View.h"
#include "AssetWatcher.h"

#include "graphics/Shader.h"
#include "foundation/FsUtil.h"

namespace fw {
	namespace ShaderToyUtil {
		ShaderProgramHandle create(ResourceManager& rm, std::string_view uri) {
			std::vector<std::byte> shaderData = FsUtil::readFile(uri);

			rm.load<Shader>("PosColor.vsc");
			rm.load<Shader>("ShaderToy.fsc");

			return rm.create<ShaderProgram>("ProgramTest", ShaderProgramDesc{
				.vertexShader = "PosColor.vsc",
				.fragmentShader = "ShaderToy.fsc",
			});
		}
	}

	class ShaderReload : public View {
	private:
		AssetWatcher _watcher;
		ShaderProgramHandle _program;

	public:
		ShaderReload(): View({ 1024, 768 }) {
			setType<ShaderReload>();
		}

		~ShaderReload() = default;

		void onInitialize() override {
			_watcher.setResourceManager(getResourceManager());
			_watcher.startWatch("assets");

			ResourceManager& rm = getResourceManager();
			rm.load<Shader>("PosColor.vsc");
			rm.load<Shader>("Mandelbulb.fsc");

			_program = rm.create<ShaderProgram>("ProgramTest", ShaderProgramDesc{
				.vertexShader = "PosColor.vsc",
				.fragmentShader = "Mandelbulb.fsc",
			});
		}

		void onUpdate(f32 delta) override {
			_watcher.update();
		}

		void onRender(fw::Canvas& canvas) override {
			canvas
				.setProgram(_program)
				.fillRect(Rect{ { 0, 0 }, canvas.getDimensions() }, Color4F(1, 1, 1, 1))
				.clearProgram();
		}

		bool onKey(VirtualKey::Enum key, bool down) override {
			if (key == VirtualKey::Space && down) {
				//getWindowManager().createWindow<ShaderReload>();
			}

			return true;
		}
	};
}

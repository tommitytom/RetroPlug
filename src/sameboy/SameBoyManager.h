#pragma once

#include "util/StringUtil.h"
#include "core/SystemManager.h"
#include "SameBoySystem.h"
#include "SameBoyProxySystem.h"

namespace rp {
	class ThreadPool;

	class SameBoyManager final : public SystemManager<SameBoySystem, SameBoyProxySystem> {
	private:
		std::unique_ptr<ThreadPool> _threadPool;

	public:
		SameBoyManager();
		~SameBoyManager();

		ThreadPool* getThreadPool() {
			return _threadPool.get();
		}

		bool canLoadRom(std::string_view path) override {
			return StringUtil::endsWith(path, ".gb");
		}

		bool canLoadSram(std::string_view path) override {
			return StringUtil::endsWith(path, ".sav");
		}

		std::string getRomName(const Uint8Buffer& romData) override;

		void process(uint32 frameCount) override;
	};
}

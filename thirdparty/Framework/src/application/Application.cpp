#include "Application.h"

//#include "foundation/FoundationModule.h"

#ifdef FW_OS_WINDOWS
#include <spdlog/sinks/msvc_sink.h>
#endif

#include <spdlog/sinks/stdout_color_sinks.h>

namespace fw::app {
	Application::Application() {
#ifdef FW_OS_WINDOWS
		auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
		auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list{ consoleSink, msvcSink });
		spdlog::set_default_logger(logger);
#endif

		//FoundationModule::setup();
	}
}

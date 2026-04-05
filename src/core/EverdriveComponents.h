#pragma once
#ifndef FW_PLATFORM_WEB
#include <memory>

class EdioProxy;

namespace rp {
	struct EverdriveComponent {
		std::shared_ptr<EdioProxy> edioProxy;
	};
}
#endif
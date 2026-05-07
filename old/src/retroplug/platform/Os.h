#pragma once

namespace os {
	enum class OperatingSystemType {
		Unknown,

		Windows,
		MacOs,
		Linux,
		Web
	};

#ifdef RP_WINDOWS
	constexpr OperatingSystemType CURRENT = OperatingSystemType::Windows;
#elif RP_MACOS
	constexpr OperatingSystemType CURRENT = OperatingSystemType::MacOs;
#elif RP_LINUX
	constexpr OperatingSystemType CURRENT = OperatingSystemType::Linux;
#elif RP_WEB
	constexpr OperatingSystemType CURRENT = OperatingSystemType::Web;
#else
	constexpr OperatingSystemType CURRENT = OperatingSystemType::Unknown;
#endif

	static_assert(CURRENT != OperatingSystemType::Unknown, "An operating system define needs to be specified!");
}

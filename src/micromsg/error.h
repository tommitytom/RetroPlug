#pragma once

namespace micromsg{
	template <typename NodeType>
	void callTypeError(NodeType type, int callType, HandlerLookups* lookup, const char* err) {
		std::stringstream ss;
		ss << magic_enum::enum_name(type) << " does not have a " << err << " for call type ";
#ifdef RTTI_ENABLED
		ss << lookup->names[callType];
#else
		ss << callType;
#endif

		ss << std::endl;

		std::cout << ss.str();
	}
}
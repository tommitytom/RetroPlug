#pragma once

namespace micromsg {
	struct VoidT {};
	struct PushVoidT {};

	template <typename ArgType, typename ReturnType>
	class Request {
	public:
		using Arg = std::conditional_t<!std::is_same_v<ArgType, void>, ArgType, VoidT>;
		using Return = std::conditional_t<!std::is_same_v<ReturnType, void>, ReturnType, VoidT>;
	};

	template <typename ArgType>
	class Push : public Request<ArgType, PushVoidT> {};
}

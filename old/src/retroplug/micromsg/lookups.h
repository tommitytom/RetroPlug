#pragma once

#include <vector>
#include <map>
#include "types.h"

namespace micromsg {
	struct HandlerLookups {
		std::vector<RequestHandlerFunc> requests;
		std::vector<ResponseHandlerFunc> responses;
		std::vector<const char*> names;
		std::map<size_t, size_t> typeIds;
		size_t responseCount = 0;
	};
}
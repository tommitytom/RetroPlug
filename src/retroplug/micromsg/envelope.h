#pragma once

namespace micromsg {
	struct Envelope {
		int sourceNodeId = -1;
		int callTypeId = 0;
		size_t callId = 0;
	};

	template <typename T>
	struct TypedEnvelope : public Envelope {
		T message;
	};
}


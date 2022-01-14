#pragma once

#include "core/Serializable.h"
#include "core/System.h"

namespace rp {
	class Model : public Serializable {
	private:
		std::string _name;
		SystemPtr _system;

	public:
		Model(const std::string& name) : _name(name) {}

		const std::string& getName() const {
			return _name;
		}

		void setSystem(SystemPtr system) {
			_system = system;
		}

		SystemPtr getSystem() {
			return _system;
		}
	};
}

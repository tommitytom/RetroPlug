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

		virtual void onBeforeLoad(LoadConfig& loadConfig) {}

		virtual void onAfterLoad(SystemPtr system) {}

		virtual void onUpdate(f32 delta) {}
	};

	using ModelPtr = std::shared_ptr<Model>;
}

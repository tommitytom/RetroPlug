#include "SameBoyHooks.h"

#include "foundation/Replicator.h"
#include "sameboy/SameBoyUtil.h"

namespace rp {
	void SameboyHooks::onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const {
		SameBoyStateComponent state;
		SameBoyUtil::setup(system, state, 48000, load);
		fw::Replicator::emplaceRemote(registry, entity, std::move(state));
	}
}

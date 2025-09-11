#include "Tasks.h"

#include "ecs/ProjectBuilder.h"
#include "lsdj/KitUtil.h"

namespace rp {
	void LoadSystemTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		success = ProjectBuilder::handleLoad(registry, entity, registry.get<SystemLoadComponent>(entity), systemType);
		completed = true;
	}

	void LoadProjectTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		success = ProjectBuilder::loadFromPaths(registry, paths);
		completed = true;
	}

	void PatchKitTask::ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) {
		assert(sampleCache);

		lsdj::Kit kit(kitData, -1);
		success = KitUtil::patchKit2(*sampleCache, kit, kitState);
		completed = true;
	}
}

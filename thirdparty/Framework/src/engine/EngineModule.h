#pragma once

#include <string>

#include "foundation/MetaFactory.h"
#include "foundation/MetaProperties.h"
#include "foundation/MetaUtil.h"
#include "engine/WorldUtil.h"

#include "engine/SpriteComponent.h"

namespace fw::EngineModule {
	void setup() {
		MetaFactory<SpriteComponent>()
			.addField<&SpriteComponent::uri>("uri", UriBrowser({ entt::type_id<Texture>() }))
			.addField<&SpriteComponent::pivot>("pivot");
	}
}

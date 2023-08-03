#include "DomStyle.h"

#include <entt/entity/registry.hpp>
#include "ui/ViewLayout.h"

namespace fw {
	DomStyle::DomStyle(entt::registry& reg, entt::entity e, YGNodeRef node) : _reg(reg), _entity(e), _node(node) {}
}

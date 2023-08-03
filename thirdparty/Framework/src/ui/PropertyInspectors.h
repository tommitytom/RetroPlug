#pragma once

#include "ui/PropertyEditorView.h"

namespace fw::PropertyInspectors {
	using PropObjectFunc = void(*)(PropertyEditorViewPtr, fw::Object&);
	PropObjectFunc find(entt::id_type type);
}
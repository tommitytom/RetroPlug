#include "PropertyInspectors.h"

#include "ui/ButtonView.h"
#include "ui/DropDownMenuView.h"
#include "ui/LabelView.h"
#include "ui/ObjectInspectorUtil.h"
#include "ui/PanelView.h"
#include "ui/SliderView.h"
#include "ui/TextEditView.h"

namespace fw {
	template <typename T>
	constexpr std::pair<entt::id_type, PropertyInspectors::PropObjectFunc> createPropPair() {
		return { entt::type_hash<T>::value(), &ObjectInspectorUtil::reflectObject<T> };
	}

	const std::unordered_map<entt::id_type, PropertyInspectors::PropObjectFunc> PROP_OBJECT_FUNCS = {
		createPropPair<View>(),
		createPropPair<LabelView>(),
		createPropPair<PanelView>(),
		createPropPair<TextEditView>(),
		createPropPair<FlexValueEditView>(),
		createPropPair<ButtonView>(),
		createPropPair<SliderView>(),
		createPropPair<DropDownMenuView>(),
	};

	PropertyInspectors::PropObjectFunc PropertyInspectors::find(entt::id_type type) {
		auto found = PROP_OBJECT_FUNCS.find(type);
		if (found != PROP_OBJECT_FUNCS.end()) {
			return found->second;
		}

		return nullptr;
	}
}

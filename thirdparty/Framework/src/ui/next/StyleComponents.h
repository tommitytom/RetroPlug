#pragma once

#include <chrono>
#include <string>
#include <entt/entity/entity.hpp>
#include "foundation/Math.h"
#include "graphics/Font.h"
#include "graphics/Texture.h"
#include "ui/Flex.h"
#include "DocumentTypes.h"
#include "Transitions.h"

namespace fw {
	struct RootTag {};
	struct StyleDirtyTag {};
	
	//struct PreviousStyleTag {};
	struct CurrentStyleTag {};
	struct CurrentStyleDirtyTag {};
	struct TargetStyleTag {};
	struct TransitionFinishedTag {};

	struct MouseEnteredTag {};
	struct MouseOverTag {};
	struct ElementReferenceComponent { DomElementHandle handle = entt::null; };
	
	struct TextComponent {
		std::string text;
	};

	struct IdComponent {
		std::string value;
	};

	struct StyleReferences {
		StyleHandle current = entt::null;
		StyleHandle from = entt::null;
		StyleHandle to = entt::null;
		std::vector<entt::entity> transitions;
	};

	struct StyleClassComponent {
		std::vector<std::string> classNames;
	};

	struct TextureComponent {
		TextureHandle texture;
	};

	struct BorderEdge {
		Color4 color;
		f32 width;
	};

	

	namespace styles {
		
	}
	
	struct FontFaceStyle {
		FontFaceHandle handle;
	};
}



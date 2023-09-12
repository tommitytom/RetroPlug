#pragma once

#include <string_view>
#include <chrono>
#include <entt/fwd.hpp>
#include "foundation/Types.h"
#include "foundation/Math.h"

#define DefineStyleProperty(propName, propType, valueType, ...) \
	struct propType {\
		static constexpr std::string_view PropertyName = propName;\
		using Tags = entt::type_list<__VA_ARGS__>;\
		valueType value;\
	};

namespace fw {
	struct AnimatableTag {};
	struct InheritedTag {};
	struct LayoutDirtyTag {};

	enum class TransitionTimingType {
		Linear,
		Ease,
		EaseIn,
		EaseOut,
		EaseInOut,
		StepStart,
		StepEnd,
		Steps,
		CubicBezier,
		Initial,
		Inherit
	};

	struct TimingFunction {
		TransitionTimingType type = TransitionTimingType::Linear;
	};
	
	enum class FontStyleType {
		Normal,
		Italic
	};

	enum class FontWeightType {
		Normal,
		Bold,
		Lighter,
		Bolder,
		Value
	};

	struct FontWeightValue {
		FontWeightType type = FontWeightType::Normal;
		f32 value = 0.0f;
	};

	enum class FontGenericType {
		None,
		Serif,
	};

	struct FontFamilyValue {
		std::string familyName;
		FontGenericType generic = FontGenericType::None;
	};

	enum class LengthType {
		Default,
		Small,
		Large,
		Dynamic,
		Em,
		Rem,
		Pixel,
		Point
	};

	struct LengthValue {
		LengthType type = LengthType::Default;
		f32 value = 0.0f;
	};

	enum class TextAlignType {
		Start,
		End,
		Left,
		Right,
		Center,
		Justify,
		MatchParent,
		JustifyAll
	};
}

namespace fw::styles {
	DefineStyleProperty("color", Color, Color4F, AnimatableTag, InheritedTag);
	DefineStyleProperty("background-color", BackgroundColor, Color4F, AnimatableTag);
	DefineStyleProperty("width", Width, FlexValue, AnimatableTag);
	DefineStyleProperty("height", Height, FlexValue, AnimatableTag);
	DefineStyleProperty("transition-property", TransitionProperty, std::string);
	DefineStyleProperty("transition-duration", TransitionDuration, std::chrono::duration<f32>);
	DefineStyleProperty("transition-timing-function", TransitionTimingFunction, TimingFunction);
	DefineStyleProperty("transition-delay", TransitionDelay, std::chrono::duration<f32>);
	DefineStyleProperty("font-family", FontFamily, FontFamilyValue, InheritedTag);
	DefineStyleProperty("font-size", FontSize, LengthValue, AnimatableTag, InheritedTag);
	DefineStyleProperty("font-weight", FontWeight, FontWeightValue, AnimatableTag, InheritedTag);
	DefineStyleProperty("text-align", TextAlign, TextAlignType, InheritedTag);

	//DefineStyleProperty("azimuth", Azimuth, Color4F, InheritedTag);
	//DefineStyleProperty("border-collapse", BorderCollapse, Color4F, InheritedTag);
	//DefineStyleProperty("border-spacing", BorderSpacing, Color4F, InheritedTag);
	//DefineStyleProperty("caption-side", CaptionSide, Color4F, InheritedTag);
	//DefineStyleProperty("cursor", Cursor, Color4F, InheritedTag);
	//DefineStyleProperty("direction", Direction, Color4F, InheritedTag);
	//DefineStyleProperty("empty-cells", EmptyCells, Color4F, InheritedTag);
	//DefineStyleProperty("font-style", FontStyle, FontStyleType, InheritedTag);
	//DefineStyleProperty("font-variant", FontVariant, Color4F, InheritedTag);
	//DefineStyleProperty("letter-spacing", LetterSpacing, Color4F, InheritedTag);
	//DefineStyleProperty("line-height", LineHeight, Color4F, InheritedTag);
	//DefineStyleProperty("list-style-image", ListStyleImage, Color4F, InheritedTag);
	//DefineStyleProperty("list-style-position", ListStylePosition, Color4F, InheritedTag);
	//DefineStyleProperty("list-style-type", ListStyleType, Color4F, InheritedTag);
	//DefineStyleProperty("orphans", Orphans, Color4F, InheritedTag);
	//DefineStyleProperty("quotes", Quotes, Color4F, InheritedTag);
	//DefineStyleProperty("text-indent", TextIndent, Color4F, InheritedTag);
	//DefineStyleProperty("text-transform", TextTransform, Color4F, InheritedTag);
	//DefineStyleProperty("visibility", Visibility, Color4F, InheritedTag);
	//DefineStyleProperty("white-space", WhiteSpace, Color4F, InheritedTag);
	//DefineStyleProperty("widows", Widows, Color4F, InheritedTag);
	//DefineStyleProperty("word-spacing", WordSpacing, Color4F, InheritedTag);
}

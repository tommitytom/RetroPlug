#pragma once

#include <string_view>
#include <chrono>
#include <entt/fwd.hpp>
#include "foundation/Types.h"
#include "foundation/Math.h"

enum class CssKeyword {
	None,
	Initial,
	Inherit,
	Unset,
	Revert,
	RevertLayer
};

#define DefineStyleProperty(propName, propType, valueType, ...) \
	struct propType {\
		static constexpr std::string_view PropertyName = propName;\
		using Tags = entt::type_list<__VA_ARGS__>;\
		valueType value;\
		CssKeyword keyword = CssKeyword::None;\
		propType() {}\
		explicit propType(valueType _value) : value(_value) {}\
		explicit propType(CssKeyword _keyword) : keyword(_keyword) {}\
		static const propType Initial;\
	};\
	inline const propType propType::Initial = propType();

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
		None,
		Default,
		Small,
		Medium,
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

		LengthValue() {}
		LengthValue(f32 _value) : type(LengthType::Pixel), value(_value) {}
		LengthValue(LengthType _type, f32 _value = 0.0f): type(_type), value(_value) {}
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

	enum class BorderStyleType {
		None,
		Hidden,
		Dotted,
		Dashed,
		Solid,
		Double,
		Groove,
		Ridge,
		Inset,
		Outset
	};
}

namespace fw::styles {
	DefineStyleProperty("cursor", Cursor, fw::CursorType, InheritedTag);
	DefineStyleProperty("color", Color, Color4F, AnimatableTag, InheritedTag);
	DefineStyleProperty("background-color", BackgroundColor, Color4F, AnimatableTag);
	
	DefineStyleProperty("margin-bottom", MarginBottom, FlexValue, AnimatableTag);
	DefineStyleProperty("margin-top", MarginTop, FlexValue, AnimatableTag);
	DefineStyleProperty("margin-left", MarginLeft, FlexValue, AnimatableTag);
	DefineStyleProperty("margin-right", MarginRight, FlexValue, AnimatableTag);

	DefineStyleProperty("padding-bottom", PaddingBottom, FlexValue, AnimatableTag);
	DefineStyleProperty("padding-top", PaddingTop, FlexValue, AnimatableTag);
	DefineStyleProperty("padding-left", PaddingLeft, FlexValue, AnimatableTag);
	DefineStyleProperty("padding-right", PaddingRight, FlexValue, AnimatableTag);

	DefineStyleProperty("border-bottom-width", BorderBottomWidth, LengthValue, AnimatableTag);
	DefineStyleProperty("border-top-width", BorderTopWidth, LengthValue, AnimatableTag);
	DefineStyleProperty("border-left-width", BorderLeftWidth, LengthValue, AnimatableTag);
	DefineStyleProperty("border-right-width", BorderRightWidth, LengthValue, AnimatableTag);
	DefineStyleProperty("border-bottom-color", BorderBottomColor, Color4F, AnimatableTag);
	DefineStyleProperty("border-top-color", BorderTopColor, Color4F, AnimatableTag);
	DefineStyleProperty("border-left-color", BorderLeftColor, Color4F, AnimatableTag);
	DefineStyleProperty("border-right-color", BorderRightColor, Color4F, AnimatableTag);
	DefineStyleProperty("border-bottom-style", BorderBottomStyle, BorderStyleType);
	DefineStyleProperty("border-top-style", BorderTopStyle, BorderStyleType);
	DefineStyleProperty("border-left-style", BorderLeftStyle, BorderStyleType);
	DefineStyleProperty("border-right-style", BorderRightStyle, BorderStyleType);

	// flex-flow
	DefineStyleProperty("flex-direction", FlexDirection, fw::FlexDirection);
	DefineStyleProperty("flex-wrap", FlexWrap, fw::FlexWrap);
	
	DefineStyleProperty("flex-basis", FlexBasis, FlexValue);
	DefineStyleProperty("flex-grow", FlexGrow, f32);
	DefineStyleProperty("flex-shrink", FlexShrink, f32);
	DefineStyleProperty("align-items", AlignItems, fw::FlexAlign);
	DefineStyleProperty("align-content", AlignContent, fw::FlexAlign);
	DefineStyleProperty("align-self", AlignSelf, fw::FlexAlign);
	DefineStyleProperty("justify-content", JustifyContent, fw::FlexJustify);
	DefineStyleProperty("overflow", Overflow, fw::FlexOverflow);

	DefineStyleProperty("position", Position, fw::FlexPositionType);
	DefineStyleProperty("top", Top, fw::FlexValue, AnimatableTag);
	DefineStyleProperty("left", Left, fw::FlexValue, AnimatableTag);
	DefineStyleProperty("bottom", Bottom, fw::FlexValue, AnimatableTag);
	DefineStyleProperty("right", Right, fw::FlexValue, AnimatableTag);

	DefineStyleProperty("width", Width, FlexValue, AnimatableTag);
	DefineStyleProperty("height", Height, FlexValue, AnimatableTag);
	DefineStyleProperty("min-width", MinWidth, FlexValue, AnimatableTag);
	DefineStyleProperty("min-height", MinHeight, FlexValue, AnimatableTag);
	DefineStyleProperty("max-width", MaxWidth, FlexValue, AnimatableTag);
	DefineStyleProperty("max-height", MaxHeight, FlexValue, AnimatableTag);
	
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

#include <refl.hpp>

REFL_AUTO(
	type(fw::LengthValue),
	field(type),
	field(value)
)

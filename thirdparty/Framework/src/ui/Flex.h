#pragma once

#include <yoga/Yoga.h>

namespace fw {
	enum class FlexAlign {
		Auto = YGAlignAuto,
		FlexStart = YGAlignFlexStart,
		Center = YGAlignCenter,
		FlexEnd = YGAlignFlexEnd,
		Stretch = YGAlignStretch,
		Baseline = YGAlignBaseline,
		SpaceBetween = YGAlignSpaceBetween,
		SpaceAround = YGAlignSpaceAround
	};

	enum class FlexDimension {
		Width = YGDimensionWidth,
		Height = YGDimensionHeight
	};

	enum class LayoutDirection {
		Inherit = YGDirectionInherit,
		LTR = YGDirectionLTR,
		RTL = YGDirectionRTL
	};

	enum class FlexDisplay {
		Flex = YGDisplayFlex,
		None = YGDisplayNone
	};

	enum class FlexEdge {
		Left = YGEdgeLeft,
		Top = YGEdgeTop,
		Right = YGEdgeRight,
		Bottom = YGEdgeBottom,
		Start = YGEdgeStart,
		End = YGEdgeEnd,
		Horizontal = YGEdgeHorizontal,
		Vertical = YGEdgeVertical,
		All = YGEdgeAll
	};

	/*YG_ENUM_DECL(
		YGErrata,
		YGErrataNone = 0,
		YGErrataStretchFlexBasis = 1,
		YGErrataAll = 2147483647,
		YGErrataClassic = 2147483646
	};
	YG_DEFINE_ENUM_FLAG_OPERATORS(YGErrata
	};

	enum class YGExperimentalFeature {
		YGExperimentalFeatureWebFlexBasis,
		YGExperimentalFeatureAbsolutePercentageAgainstPaddingEdge,
		YGExperimentalFeatureFixAbsoluteTrailingColumnMargin
	};
	*/
	enum class FlexDirection {
		Column = YGFlexDirectionColumn,
		ColumnReverse = YGFlexDirectionColumnReverse,
		Row = YGFlexDirectionRow,
		RowReverse = YGFlexDirectionRowReverse
	};

	enum class FlexGutter {
		Column = YGGutterColumn,
		Row = YGGutterRow,
		All = YGGutterAll
	};

	enum class FlexJustify {
		FlexStart = YGJustifyFlexStart,
		Center = YGJustifyCenter,
		FlexEnd = YGJustifyFlexEnd,
		SpaceBetween = YGJustifySpaceBetween,
		SpaceAround = YGJustifySpaceAround,
		SpaceEvenly = YGJustifySpaceEvenly
	};

	enum class FlexMeasureMode {
		Undefined = YGMeasureModeUndefined,
		Exactly = YGMeasureModeExactly,
		AtMost = YGMeasureModeAtMost
	};

	enum class FlexNodeType {
		Default = YGNodeTypeDefault,
		Text = YGNodeTypeText
	};

	enum class FlexOverflow {
		Visible = YGOverflowVisible,
		Hidden = YGOverflowHidden,
		Scroll = YGOverflowScroll
	};

	enum class FlexPositionType {
		Static = YGPositionTypeStatic,
		Relative = YGPositionTypeRelative,
		Absolute = YGPositionTypeAbsolute
	};

	/*YG_ENUM_DECL(
		YGPrintOptions,
		YGPrintOptionsLayout = 1,
		YGPrintOptionsStyle = 2,
		YGPrintOptionsChildren = 4
		};
	YG_DEFINE_ENUM_FLAG_OPERATORS(YGPrintOptions
};*/

	enum class FlexUnit {
		Undefined = YGUnitUndefined,
		Point = YGUnitPoint,
		Percent = YGUnitPercent,
		Auto = YGUnitAuto
	};

	enum class FlexWrap {
		NoWrap = YGWrapNoWrap,
		Wrap = YGWrapWrap,
		WrapReverse = YGWrapWrapReverse
	};

	class FlexValue {
	private:
		FlexUnit _unit = FlexUnit::Undefined;
		f32 _value = 0.0f;

	public:
		FlexValue() {}
		FlexValue(f32 value) : _unit(FlexUnit::Point), _value(value) {}
		FlexValue(FlexUnit unit, f32 value = 0.0f) : _unit(unit), _value(value) {}
		~FlexValue() = default;

		FlexUnit getUnit() const {
			return _unit;
		}

		void setUnit(FlexUnit unit) {
			_unit = unit;
		}

		f32 getValue() const {
			return _value;
		}

		void setValue(f32 value) {
			_value = value;
		}
		
		bool isValid() const {
			return _unit != FlexUnit::Undefined;
		}
	};

	struct FlexDimensionValue {
		FlexValue width;
		FlexValue height;
	};

	struct FlexRect {
		FlexValue top;
		FlexValue left;
		FlexValue bottom;
		FlexValue right;
	};

	struct Flex {
		std::optional<LayoutDirection> direction;
		std::optional<FlexDirection> flexDirection;
		std::optional<FlexValue> basis;
		std::optional<f32> grow;
		std::optional<f32> shrink;
		std::optional<FlexWrap> flexWrap;

		std::optional<FlexJustify> justify;
		std::optional<FlexAlign> alignItems;
		std::optional<FlexAlign> alignSelf;
		std::optional<FlexAlign> alignContent;

		std::optional<FlexValue> width;
		std::optional<FlexValue> height;
		std::optional<FlexValue> minWidth;
		std::optional<FlexValue> minHeight;
		std::optional<FlexValue> maxWidth;
		std::optional<FlexValue> maxHeight;
		std::optional<f32> aspectRatio;
	};
}

REFL_AUTO(
	type(fw::FlexValue),
	func(getUnit, property("unit")), func(setUnit, property("unit")),
	func(getValue, property("value")), func(setValue, property("value"))
)

/*REFL_AUTO(
	type(fw::FlexRect),
	func(getUnit, property("unit")), func(setUnit, property("unit")),
	func(getValue, property("value")), func(setValue, property("value"))
)*/

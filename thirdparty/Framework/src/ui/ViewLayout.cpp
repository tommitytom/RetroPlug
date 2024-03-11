#include "ViewLayout.h"

namespace fw
{
	// Margin

	template <> void ViewLayout::setProperty<styles::MarginBottom>(const styles::MarginBottom& container) {
		setMarginEdge(FlexEdge::Bottom, container.value);
	}
	template <> void ViewLayout::setProperty<styles::MarginTop>(const styles::MarginTop& container) {
		setMarginEdge(FlexEdge::Top, container.value);
	}
	template <> void ViewLayout::setProperty<styles::MarginLeft>(const styles::MarginLeft& container) {
		setMarginEdge(FlexEdge::Left, container.value);
	}
	template <> void ViewLayout::setProperty<styles::MarginRight>(const styles::MarginRight& container) {
		setMarginEdge(FlexEdge::Right, container.value);
	}

	// Padding

	template <> void ViewLayout::setProperty<styles::PaddingBottom>(const styles::PaddingBottom& container) {
		setPaddingEdge(FlexEdge::Bottom, container.value);
	}
	template <> void ViewLayout::setProperty<styles::PaddingTop>(const styles::PaddingTop& container) {
		setPaddingEdge(FlexEdge::Top, container.value);
	}
	template <> void ViewLayout::setProperty<styles::PaddingLeft>(const styles::PaddingLeft& container) {
		setPaddingEdge(FlexEdge::Left, container.value);
	}
	template <> void ViewLayout::setProperty<styles::PaddingRight>(const styles::PaddingRight& container) {
		setPaddingEdge(FlexEdge::Right, container.value);
	}

	// Border

	template <> void ViewLayout::setProperty<styles::BorderBottomWidth>(const styles::BorderBottomWidth& container) {
		setBorderEdge(FlexEdge::Bottom, container.value.value);
	}
	template <> void ViewLayout::setProperty<styles::BorderTopWidth>(const styles::BorderTopWidth& container) {
		setBorderEdge(FlexEdge::Top, container.value.value);
	}
	template <> void ViewLayout::setProperty<styles::BorderLeftWidth>(const styles::BorderLeftWidth& container) {
		setBorderEdge(FlexEdge::Left, container.value.value);
	}
	template <> void ViewLayout::setProperty<styles::BorderRightWidth>(const styles::BorderRightWidth& container) {
		setBorderEdge(FlexEdge::Right, container.value.value);
	}

	// flex-flow
	template <> void ViewLayout::setProperty<styles::FlexDirection>(const styles::FlexDirection& container) {
		setFlexDirection(container.value);
	}
	template <> void ViewLayout::setProperty<styles::FlexWrap>(const styles::FlexWrap& container) {
		setFlexWrap(container.value);
	}

	template <> void ViewLayout::setProperty<styles::FlexBasis>(const styles::FlexBasis& container) {
		setFlexBasis(container.value);
	}
	template <> void ViewLayout::setProperty<styles::FlexGrow>(const styles::FlexGrow& container) {
		setFlexGrow(container.value);
	}
	template <> void ViewLayout::setProperty<styles::FlexShrink>(const styles::FlexShrink& container) {
		setFlexShrink(container.value);
	}
	template <> void ViewLayout::setProperty<styles::AlignItems>(const styles::AlignItems& container) {
		setFlexAlignItems(container.value);
	}
	template <> void ViewLayout::setProperty<styles::AlignContent>(const styles::AlignContent& container) {
		setFlexAlignContent(container.value);
	}
	template <> void ViewLayout::setProperty<styles::AlignSelf>(const styles::AlignSelf& container) {
		setFlexAlignSelf(container.value);
	}
	template <> void ViewLayout::setProperty<styles::JustifyContent>(const styles::JustifyContent& container) {
		setJustifyContent(container.value);
	}
	template <> void ViewLayout::setProperty<styles::Overflow>(const styles::Overflow& container) {
		setOverflow(container.value);
	}

	template <> void ViewLayout::setProperty<styles::Position>(const styles::Position& container) {
		setFlexPositionType(container.value);
	}
	template <> void ViewLayout::setProperty<styles::Top>(const styles::Top& container) {
		setPositionEdge(FlexEdge::Top, container.value);
	}
	template <> void ViewLayout::setProperty<styles::Left>(const styles::Left& container) {
		setPositionEdge(FlexEdge::Left, container.value);
	}
	template <> void ViewLayout::setProperty<styles::Bottom>(const styles::Bottom& container) {
		setPositionEdge(FlexEdge::Bottom, container.value);
	}
	template <> void ViewLayout::setProperty<styles::Right>(const styles::Right& container) {
		setPositionEdge(FlexEdge::Right, container.value);
	}

	template <> void ViewLayout::setProperty<styles::Width>(const styles::Width& container) {
		setWidth(container.value);
	}
	template <> void ViewLayout::setProperty<styles::Height>(const styles::Height& container) {
		setHeight(container.value);
	}
	template <> void ViewLayout::setProperty<styles::MinWidth>(const styles::MinWidth& container) {
		setMinWidth(container.value);
	}
	template <> void ViewLayout::setProperty<styles::MinHeight>(const styles::MinHeight& container) {
		setMinHeight(container.value);
	}
	template <> void ViewLayout::setProperty<styles::MaxWidth>(const styles::MaxWidth& container) {
		setMaxWidth(container.value);
	}
	template <> void ViewLayout::setProperty<styles::MaxHeight>(const styles::MaxHeight& container) {
		setMaxHeight(container.value);
	}
}
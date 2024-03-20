#include "ReactElementView.h"

#include "ui/next/StyleCache.h"

namespace fw {
	ReactElementView::ReactElementView() {
		_styles.push_back(std::make_shared<StylesheetRule>());
	}

	ReactElementView::ReactElementView(const std::string& tag) {
		setName(tag);
		_styles.push_back(std::make_shared<StylesheetRule>());
		_elementName = tag;
	}
	
	void ReactElementView::onMouseEnter(Point pos) {
		_inputState |= InputStateFlag::Hover;

		// TODO: only need to do this if one of the rules contains a hover property
		updateStyles();

		const styles::Cursor* cursorProp = findStyleProperty<styles::Cursor>();
		if (cursorProp) {
			this->setCursor(cursorProp->value);
		}
		else {
			this->setCursor(CursorType::Arrow);
		}
	}
	
	void ReactElementView::onMouseLeave() {
		_inputState &= ~InputStateFlag::Hover;

		// TODO: only need to do this if one of the rules contains a hover property
		updateStyles();
	}
	
	bool ReactElementView::onMouseButton(const MouseButtonEvent& ev) {
		if (ev.button == MouseButton::Left) {
			if (ev.down) {
				_inputState |= InputStateFlag::Active;
			}
			else {
				_inputState &= ~InputStateFlag::Active;
			}
		}

		updateStyles();

		return false;
	}

	void ReactElementView::onUpdate(f32 dt) {
		if (_styleDirty) {
			updateLayoutStyle();
		}
	}
	
	void ReactElementView::onRender(fw::Canvas& canvas) {
		const styles::BackgroundColor* bg = findStyleProperty<styles::BackgroundColor>();
		if (bg) {
			canvas.fillRect(getDimensionsF(), bg->value);
		}

		FlexBorder border = getLayout().getBorder();
		if (std::isnan(border.top)) { border.top = 0.0f; }
		if (std::isnan(border.left)) { border.left = 0.0f; }
		if (std::isnan(border.bottom)) { border.bottom = 0.0f; }
		if (std::isnan(border.right)) { border.right = 0.0f; }

		const styles::BorderLeftColor* borderLeftColor = findStyleProperty<styles::BorderLeftColor>();
		const styles::BorderRightColor* borderRightColor = findStyleProperty<styles::BorderRightColor>();
		const styles::BorderTopColor* borderTopColor = findStyleProperty<styles::BorderTopColor>();
		const styles::BorderBottomColor* borderBottomColor = findStyleProperty<styles::BorderBottomColor>();

		canvas.strokeRect(StrokedRect{
			.area = getDimensionsF(),
			.width = BorderWidth {
				.top = border.top,
				.left = border.left,
				.bottom = border.bottom,
				.right = border.right
			},
			.color = {
				.top = borderTopColor ? borderTopColor->value : Color4F::white,
				.left = borderLeftColor ? borderLeftColor->value : Color4F::white,
				.bottom = borderBottomColor ? borderBottomColor->value : Color4F::white,
				.right = borderRightColor ? borderRightColor->value : Color4F::white
			}
		});
	}

	void ReactElementView::updateStyles() {
		if (!isInitialized()) {
			return;
		}

		assert(_styles.size() > 0);

		if (_styles.size() > 1) {
			_styles.erase(_styles.begin() + 1, _styles.end());
		}

		this->getState<StyleCache>().getRules(this, _styles);

		updateLayoutStyle();

		_styleDirty = false;

		this->forEach(false, [](const ViewPtr& child, ViewIndex idx) {
			child->asRaw<ReactElementView>()->updateStyles();
		});
	}
	
	void ReactElementView::updateLayoutStyle() {
		//getLayout().reset();

		setLayoutStyleProperty<styles::MarginBottom>();
		setLayoutStyleProperty<styles::MarginTop>();
		setLayoutStyleProperty<styles::MarginLeft>();
		setLayoutStyleProperty<styles::MarginRight>();

		setLayoutStyleProperty<styles::PaddingBottom>();
		setLayoutStyleProperty<styles::PaddingTop>();
		setLayoutStyleProperty<styles::PaddingLeft>();
		setLayoutStyleProperty<styles::PaddingRight>();

		setLayoutStyleProperty<styles::BorderBottomWidth>();
		setLayoutStyleProperty<styles::BorderTopWidth>();
		setLayoutStyleProperty<styles::BorderLeftWidth>();
		setLayoutStyleProperty<styles::BorderRightWidth>();

		// flex-flow
		setLayoutStyleProperty<styles::FlexDirection>();
		setLayoutStyleProperty<styles::FlexWrap>();

		setLayoutStyleProperty<styles::FlexBasis>();
		setLayoutStyleProperty<styles::FlexGrow>();
		setLayoutStyleProperty<styles::FlexShrink>();
		setLayoutStyleProperty<styles::AlignItems>();
		setLayoutStyleProperty<styles::AlignContent>();
		setLayoutStyleProperty<styles::AlignSelf>();
		setLayoutStyleProperty<styles::JustifyContent>();
		setLayoutStyleProperty<styles::Overflow>();

		setLayoutStyleProperty<styles::Position>();
		setLayoutStyleProperty<styles::Top>();
		setLayoutStyleProperty<styles::Left>();
		setLayoutStyleProperty<styles::Bottom>();
		setLayoutStyleProperty<styles::Right>();

		setLayoutStyleProperty<styles::Width>();
		setLayoutStyleProperty<styles::Height>();
		setLayoutStyleProperty<styles::MinWidth>();
		setLayoutStyleProperty<styles::MinHeight>();
		setLayoutStyleProperty<styles::MaxWidth>();
		setLayoutStyleProperty<styles::MaxHeight>();
	}
	
	
}

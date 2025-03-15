#include "ModalView.h"

#include "ui/LabelView.h"
#include "ui/ButtonView.h"

namespace rp {
	void ModalView::onInitialize() {
		setVisible(false);
	}

	void ModalView::addButton(ModalButtonType type) {
		std::string buttonName;
		switch (type) {
			case ModalButtonType::Ok: buttonName = "Ok"; break;
			case ModalButtonType::Yes: buttonName = "Yes"; break;
			case ModalButtonType::No: buttonName = "No"; break;
			case ModalButtonType::Cancel: buttonName = "Cancel"; break;
		}

		const auto& button = addChild<fw::ButtonView>(buttonName + " Button");
		button->setText(buttonName);
		button->getLayout().setDimensions(fw::Dimension(100, 50));

		subscribe<fw::MouseButtonEvent>(button, [this, type](const fw::MouseButtonEvent& ev) {
			if (ev.button == fw::MouseButton::Left && !ev.down) {
				if (this->_current.has_value()) {
					this->_current->callback(type);
				}
				this->hide();
			}
		});
	}

	void ModalView::show(CurrentModal&& modal) {
		if (_current.has_value()) {
			return;
		}

		switch (modal.type) {
		case ModalType::Ok:
			addButton(ModalButtonType::Ok);
			break;
		case ModalType::YesNo:
			addButton(ModalButtonType::Yes);
			addButton(ModalButtonType::No);
			break;
		case ModalType::YesNoCancel:
			addButton(ModalButtonType::Yes);
			addButton(ModalButtonType::No);
			addButton(ModalButtonType::Cancel);
			break;
		}

		_current = std::move(modal);
		bringToFront();
		setVisible(true);
		focus();
		setFocusPolicy(fw::FocusPolicy::Click);
	}
	
	void ModalView::hide() {
		this->setVisible(false);
		this->setFocusPolicy(fw::FocusPolicy::None);
		this->removeChildren();
		this->unsubscribeAll();

		if (_current.has_value()) {
			fw::ViewPtr lastFocus = _current->lastFocus.lock();
			if (lastFocus) {
				lastFocus->focus();
			}
		}

		_current.reset();

	}
}

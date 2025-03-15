#pragma once

#include "ui/View.h"

namespace rp {
	enum class ModalButtonType {
		Ok,
		Yes,
		No,
		Cancel
	};

	enum class ModalType {
		Ok,
		YesNo,
		YesNoCancel
	};

	struct ModalCloseEvent { ModalButtonType type; };

	struct CurrentModal {
		ModalType type;
		std::string message;
		std::function<void(ModalButtonType)> callback;
		std::weak_ptr<fw::View> lastFocus;
	};

	class ModalView final : public fw::View {
		FwRegisterObject();
	private:
		std::optional<CurrentModal> _current;

	public:
		ModalView() = default;
		~ModalView() = default;

		void onInitialize() override;

		void show(CurrentModal&& modal);

		void show(ModalType type, std::string&& message, std::function<void(ModalButtonType)>&& callback) {
			show(CurrentModal{ type, std::move(message), std::move(callback), getShared()->focused });
		}

		void show(ModalType type, const std::string& message, std::function<void(ModalButtonType)>&& callback) {
			show(CurrentModal{ type, message, std::move(callback), getShared()->focused });
		}

		void hide();

		void onUpdate(f32 dt) override {
			if (_current.has_value()) {
				this->bringToFront();
			}
		}

		void onRender(fw::Canvas& canvas) override {
			canvas
				.fillRect(getDimensionsF(), fw::Color4F::black)
				.strokeRect(getDimensionsF(), fw::Color4F::white);
		}

	private:
		void addButton(ModalButtonType type);
	};

	FwRegisterPtr(ModalView);
}

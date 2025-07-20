#pragma once

#include "ui/SystemContainerView.h"
#include "ui/MenuView.h"

namespace rp {
	enum class DialogType {
		OkCancel,
		YesNo,
		YesNoCancel
	};

	enum class DialogResult {
		Yes,
		No,
		Ok,
		Cancel,
	};

	class DialogView final : public SystemContainerView {
		FwRegisterObject();

	private:
		MenuViewPtr _menu;
		std::string _title;
		DialogType _type = DialogType::OkCancel;

	public:
		DialogView() {}
		DialogView(const std::string& title, DialogType type): _type(type), _title(title) {}
		~DialogView() {}

		void onInitialize() override;

		void processInput(std::vector<fw::StreamButtonPress>& buttons, std::vector<std::string>& actions) override;
	};

	using DialogViewPtr = std::shared_ptr<DialogView>;
}

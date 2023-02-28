#pragma once

#include "foundation/Properties.h"

#include "ui/LabelView.h"
#include "ui/Property.h"
#include "ui/View.h"

namespace fw {
	class PropertyEditorView : public View {
	protected:
		struct Prop {
			LabelViewPtr label;
			PropertyEditorBasePtr editor;
		};

		struct Group {
			LabelViewPtr label;
			std::vector<Prop> props;
		};

	private:
		std::vector<Group> _groups;

		int32 _rowHeight = 20;
		bool _draggingSeparator = false;
		int32 _propPadding = 1;

		f32 _separatorPerc = 0.25f;

		const int32 seperatorHandleSize = 4;

	public:
		PropertyEditorView() {
			setType<PropertyEditorView>();
			setFocusPolicy(FocusPolicy::Click);
		}

		~PropertyEditorView() = default;

		template <typename T>
		std::shared_ptr<T> addProperty(std::string_view name) {
			std::shared_ptr<T> prop = std::make_shared<T>();
			addProperty(name, prop);
			return prop;
		}

		Group& pushGroup(std::string_view name) {
			_groups.push_back(Group{
				.label = addChild<LabelView>(fmt::format("{} Label", name))
			});

			return _groups.back();
		}

		void addProperty(std::string_view name, PropertyEditorBasePtr editor) {
			editor->setSizingPolicy(SizingPolicy::None);

			Prop prop = {
				.label = addChild<LabelView>(fmt::format("{} Label", name)),
				.editor = addChild<PropertyEditorBase>(editor)
			};

			prop.label->setText(name);

			if (_groups.empty()) {
				pushGroup(fmt::format("{} Group", name));
			}

			_groups.back().props.push_back(prop);
		}

		void clearProperties() {
			_groups.clear();
		}

		bool mouseOverSeparator(Point pos) const {
			int32 separatorX = getSeperatorX();
			return pos.x > separatorX - seperatorHandleSize && pos.x < separatorX + seperatorHandleSize;
		}

		bool onMouseMove(Point pos) override {
			int32 separatorX = getSeperatorX();

			if (_draggingSeparator) {
				_separatorPerc = MathUtil::clamp((f32)pos.x / (f32)getDimensionsF().w, 0.0f, 1.0f);
				updateLayout(getDimensions());
				setCursor(CursorType::ResizeH);
			} else if (pos.x == separatorX) {
				spdlog::info("OVER mid");
				setCursor(CursorType::ResizeH);
			} else {
				setCursor(CursorType::Arrow);
			}

			return true;
		}

		bool onKey(VirtualKey::Enum key, bool down) override {
			if ((key == VirtualKey::UpArrow || key == VirtualKey::DownArrow) && down) {
				for (size_t i = 0; i < _groups.size(); ++i) {
					Group& group = _groups[i];

					for (size_t j = 0; j < group.props.size(); ++j) {
						if (group.props[j].editor->hasFocus()) {
							Prop* nextProp;

							if (key == VirtualKey::UpArrow) {
								if (i == 0 && j == 0) {
									// At the top of the list, select the very last element
									nextProp = &_groups.back().props.back();
								} else if (j == 0) {
									// At the top of a group, select last element of previous group
									nextProp = &_groups[i - 1].props.back();
								} else {
									nextProp = &group.props[j - 1];
								}
							} else {
								if (i == _groups.size() - 1 && j == group.props.size() - 1) {
									// At the bottom of the list, select the very first element
									nextProp = &_groups[0].props[0];
								} else if (j == group.props.size() - 1) {
									// At the bottom of a group, select first element of next group
									nextProp = &_groups[i + 1].props[0];
								} else {
									nextProp = &group.props[j + 1];
								}
							}

							nextProp->editor->focus();

							return true;
						}
					}
				}
			}

			return true;
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point pos) override {
			_draggingSeparator = down && mouseOverSeparator(pos);
			return true;
		}

		void onUpdate(f32 delta) override {
			//setDimensions({ 500, 400 });
		}

		void onInitialize() override {
			updateLayout(getDimensions());
		}

		void onMount() override {
			updateLayout(getDimensions());
		}

		int32 getSeperatorX() const {
			return (int32)(getDimensionsF().w * _separatorPerc);
		}

		void updateLayout(Dimension dim) {
			int32 separatorX = getSeperatorX();
			
			Rect labelOffset(0, 0, separatorX, _rowHeight);
			Rect propOffset(separatorX + 1, 0, dim.w - separatorX - 1, _rowHeight);

			for (Group& group : _groups) {
				group.label->setArea(Rect(labelOffset.position, Dimension(dim.w, _rowHeight)));

				labelOffset.y += _rowHeight + 1;
				propOffset.y += _rowHeight + 1;

				for (Prop& prop : group.props) {
					prop.label->setArea(labelOffset);
					prop.editor->setArea(propOffset);

					labelOffset.y += _rowHeight + 1;
					propOffset.y += _rowHeight + 1;
				}
			}
		}

		void onResize(const ResizeEvent& ev) override {
			updateLayout(ev.size);
		}

		void onRender(fw::Canvas& canvas) override {
			int32 separatorX = getSeperatorX();
			Dimension dim = getDimensions();
			canvas.fillRect(dim, Color4F::darkGrey);

			//canvas.line(Point{ _separatorX, 0 }, Point{ _separatorX, dim.h }, Color4F::lightGrey);

			int32 rowCount = (dim.h / _rowHeight) + 1;
			int32 rowY = _rowHeight;

			for (const Group& group : _groups) {
				int32 totalRowCount = std::min(rowCount, (int32)group.props.size());
				int32 groupHeight = totalRowCount * (_rowHeight + 1);

				canvas
					.line(0, rowY, dim.w, rowY, Color4F::lightGrey)
					.line(Point{ separatorX, rowY }, Point{ separatorX, rowY + groupHeight }, Color4F::lightGrey);

				rowY += _rowHeight + 1;

				for (int32 i = 0; i < totalRowCount; ++i) {
					canvas.line(0, rowY, dim.w, rowY, Color4F::lightGrey);
					rowY += _rowHeight + 1;
				}
			}
		}
	};

	using PropertyEditorViewPtr = std::shared_ptr<PropertyEditorView>;
}

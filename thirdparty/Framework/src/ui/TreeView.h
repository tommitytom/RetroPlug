#pragma once

#include "ui/LabelView.h"
#include "ui/View.h"

namespace fw {
	struct TreeViewNode {
		std::string name;
		std::vector<TreeViewNode> children;
		bool expanded = true;
	};

	class TreeView : public View {
		RegisterObject();
	private:
		int32 _rowHeight = 20;

		TreeViewNode _rootNode;

	public:
		TreeView() {
			setFocusPolicy(FocusPolicy::Click);
			_rootNode.name = "Root";
		}

		~TreeView() = default;

		void onInitialize() override {
			//getLayout().setOverflow(FlexOverflow::Scroll);
			getLayout().setLayoutDirection(LayoutDirection::LTR);
			getLayout().setFlexDirection(FlexDirection::Column);
			getLayout().setFlexWrap(FlexWrap::Wrap);
			getLayout().setPadding(FlexRect{ 10.0f, 10.0f, 10.0f, 10.0f });
			refresh();
		}		

		TreeViewNode& getRootNode() {
			return _rootNode;
		}

		void clear() {
			_rootNode.name.clear();
			_rootNode.children.clear();
			removeChildren();
		}

		void refresh() {
			removeChildren();
			addNode(_rootNode, 0);
		}

	private:
		void addNode(const TreeViewNode& node, uint32 indent) {
			LabelViewPtr label = addChild<LabelView>(fmt::format("{} Label", node.name));

			label->setText(node.name);
			//label->getLayout().setMarginEdge(FlexEdge::Left, indent * 15.0f);

			if (node.expanded && !node.children.empty()) {
				for (const TreeViewNode& child : node.children) {
					addNode(child, indent + 1);
				}
			}
		}
	};

	using TreeViewPtr = std::shared_ptr<TreeView>;
}

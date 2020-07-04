#include "Menu.h"

#include "IGraphics.h"

using namespace iplug::igraphics;

void MenuTool::createMenu(IPopupMenu* target, Menu* source, MenuCallbackMap& callbacks) {
	for (MenuItemBase* itemBase : source->getItems()) {
		switch (itemBase->getType()) {
		case MenuItemType::SubMenu: {
			Menu* item = (Menu*)itemBase;

			if (item->isActive()) {
				IPopupMenu* subMenu = new IPopupMenu();
				target->AddItem(item->getName().c_str(), subMenu);
				MenuTool::createMenu(subMenu, item, callbacks);
			} else {
				target->AddItem(item->getName().c_str(), -1, IPopupMenu::Item::kTitle);
			}

			break;
		}
		case MenuItemType::Action: {
			Action* item = (Action*)itemBase;
			IPopupMenu::Item* popupItem = target->AddItem(item->getName().c_str(), -1, item->isActive() ? 0 : IPopupMenu::Item::kDisabled);

			if (item->isActive()) {
				if (item->getFunction()) {
					popupItem->SetTag(callbacks.size());
					callbacks.push_back([item]() { item->getFunction()(); });
				} else {
					popupItem->SetTag(item->getId());
				}
			}

			break;
		}
		case MenuItemType::Title: {
			Title* item = (Title*)itemBase;
			target->AddItem(item->getName().c_str(), -1, IPopupMenu::Item::kTitle);
			break;
		}
		case MenuItemType::Select: {
			Select* item = (Select*)itemBase;
			IPopupMenu::Item* popupItem = target->AddItem(item->getName().c_str(), -1, item->isActive() ? 0 : IPopupMenu::Item::kDisabled);
			popupItem->SetChecked(item->getChecked());

			if (item->isActive()) {
				if (item->getFunction()) {
					popupItem->SetTag(callbacks.size());
					callbacks.push_back([popupItem, item]() { item->getFunction()(!popupItem->GetChecked()); });
				} else {
					popupItem->SetTag(item->getId());
				}
			}

			break;
		}
		case MenuItemType::MultiSelect: {
			MultiSelect* item = (MultiSelect*)itemBase;
			for (size_t i = 0; i < item->getItems().size(); ++i) {
				const std::string itemName = item->getItems()[i];
				IPopupMenu::Item* popupItem = target->AddItem(itemName.c_str());
				popupItem->SetChecked((int)i == item->getValue());

				if (item->getFunction()) {
					popupItem->SetTag(callbacks.size());
					callbacks.push_back([item, i]() { item->getFunction()(i); });
				} else if (item->getId() != -1) {
					popupItem->SetTag(item->getId() + i);
				}
			}
			break;
		}
		case MenuItemType::Separator: {
			target->AddSeparator();
			break;
		}
		}
	}
}

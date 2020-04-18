#include "Menu.h"

using namespace iplug::igraphics;

Menu* findSubMenu(Menu* source, const std::string& name) {
	for (MenuItemBase* itemBase : source->getItems()) {
		if (itemBase->getType() == MenuItemType::SubMenu) {
			Menu* item = (Menu*)itemBase;
			if (item->getName() == name) {
				return item;
			}
		}
	}

	return nullptr;
}

void mergeMenu(Menu* source, Menu* target) {
	bool separated = target->getItems().empty();
	for (MenuItemBase* itemBase : source->getItems()) {
		switch (itemBase->getType()) {
		case MenuItemType::SubMenu: {
			Menu* item = (Menu*)itemBase;
			Menu* targetMenu = findSubMenu(target, item->getName());
			if (!targetMenu) {
				if (!separated) { target->separator(); separated = true; }
				targetMenu = &target->subMenu(item->getName());
			}

			mergeMenu(item, targetMenu);
			break;
		}
		case MenuItemType::Action: {
			if (!separated) { target->separator(); separated = true; }
			Action* item = (Action*)itemBase;
			target->action(item->getName(), item->getFunction(), item->isActive(), item->getId());
			break;
		}
		case MenuItemType::Title: {
			if (!separated) { target->separator(); separated = true; }
			Title* item = (Title*)itemBase;
			target->title(item->getName());
			break;
		}
		case MenuItemType::Select: {
			if (!separated) { target->separator(); separated = true; }
			Select* item = (Select*)itemBase;
			target->select(item->getName(), item->getChecked(), item->getFunction(), item->isActive(), item->getId());
			break;
		}
		case MenuItemType::MultiSelect: {
			if (!separated) { target->separator(); separated = true; }
			MultiSelect* item = (MultiSelect*)itemBase;
			target->multiSelect(item->getItems(), item->getValue(), item->getFunction(), item->isActive(), item->getId());
			break;
		}
		case MenuItemType::Separator: {
			target->separator();
			separated = true;
			break;
		}
		}
	}
}

void createMenu(iplug::igraphics::IPopupMenu* target, Menu* source, MenuCallbackMap& callbacks) {
	for (MenuItemBase* itemBase : source->getItems()) {
		switch (itemBase->getType()) {
		case MenuItemType::SubMenu: {
			Menu* item = (Menu*)itemBase;

			if (item->isActive()) {
				IPopupMenu* subMenu = new IPopupMenu();
				target->AddItem(item->getName().c_str(), subMenu);
				createMenu(subMenu, item, callbacks);
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

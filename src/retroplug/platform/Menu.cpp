#include "Menu.h"

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

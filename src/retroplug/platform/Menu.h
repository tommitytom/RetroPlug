#pragma once

#include <vector>
#include <string>
#include <functional>
#include <iostream>
#include <assert.h>

enum class MenuItemType {
	None,
	SubMenu,
	Select,
	MultiSelect,
	Separator,
	Action,
	Title
};

using MultiSelectFunction = std::function<void(int)>;
using SelectFunction = std::function<void(bool)>;
using ActionFunction = std::function<void()>;
using MenuCallbackMap = std::vector<std::function<void()>>;

const int LUA_UI_MENU_ID_OFFSET = 10000;
const int LUA_AUDIO_MENU_ID_OFFSET = 20000;

class MenuItemBase {
private:
	MenuItemType _type = MenuItemType::None;
	int _id;

protected:
	MenuItemBase(MenuItemType type, int id = -1): _type(type), _id(id) {}

public:
	MenuItemType getType() const { return _type; }

	int getId() const { return _id; }
};

class Select : public MenuItemBase {
private:
	std::string _name;
	bool _checked = false;
	SelectFunction _func;
	bool _active;

public:
	Select(const std::string& name, bool checked, SelectFunction func, bool active, int id)
		: MenuItemBase(MenuItemType::Select, id), _name(name), _checked(checked), _func(func), _active(active) {}

	const std::string& getName() { return _name; }

	bool getChecked() const { return _checked; }

	SelectFunction& getFunction() { return _func; }

	bool isActive() const { return _active; }
};

class Action : public MenuItemBase {
private:
	std::string _name;
	ActionFunction _func;
	bool _active;

public:
	Action(const std::string& name, ActionFunction func, bool active, int id)
		: MenuItemBase(MenuItemType::Action, id), _name(name), _func(func), _active(active) {}

	const std::string& getName() { return _name; }

	ActionFunction& getFunction() { return _func; }

	bool isActive() const { return _active; }
};

class MultiSelect : public MenuItemBase {
private:
	std::vector<std::string> _items;
	int _value;
	MultiSelectFunction _func;
	bool _active;

public:
	MultiSelect(const std::vector<std::string>& items, int value, MultiSelectFunction func, bool active, int id)
		: MenuItemBase(MenuItemType::MultiSelect, id), _items(items), _value(value), _func(func), _active(active) {}

	const std::vector<std::string>& getItems() const { return _items; }

	int getValue() const { return _value; }

	MultiSelectFunction& getFunction() { return _func; }

	bool isActive() const { return _active; }
};

class Separator : public MenuItemBase {
public:
	Separator() : MenuItemBase(MenuItemType::Separator) {}
};

class Title : public MenuItemBase {
private:
	std::string _name;

public:
	Title(const std::string& name) : MenuItemBase(MenuItemType::Title), _name(name) {}

	const std::string& getName() const { return _name; }
};

class Menu : public MenuItemBase {
private:
	Menu* _parent;
	std::string _name;
	std::vector<MenuItemBase*> _items;
	bool _active;

public:
	Menu(const std::string& name = "", bool active = false, Menu* parent = nullptr)
		: MenuItemBase(MenuItemType::SubMenu), _name(name), _parent(parent), _active(active) {}

	~Menu() {
		for (MenuItemBase* item : _items) {
			delete item;
		}
	}

	const std::string& getName() const { return _name; }

	const std::vector<MenuItemBase*>& getItems() const { return _items; }

	void addItem(MenuItemBase* item) { _items.push_back(item); }
	
	bool isActive() const { return _active; }

	Menu& title(const std::string& name) {
		addItem(new Title(name));
		return *this;
	}

	Menu& action(const std::string& name, ActionFunction func, bool active = true, int id = -1) {
		addItem(new Action(name, func, active, id));
		return *this;
	}

	Menu& select(const std::string& name, bool selected, SelectFunction func, bool active = true, int id = -1) {
		addItem(new Select(name, selected, func, active, id));
		return *this;
	}

	Menu& select(const std::string& name, bool* selected, bool active = true, int id = -1) {
		addItem(new Select(name, *selected, [selected](bool v) { *selected = v; }, active, id));
		return *this;
	}

	Menu& multiSelect(const std::vector<std::string>& items, int selected, MultiSelectFunction func, bool active = true, int id = -1) {
		addItem(new MultiSelect(items, selected, func, active, id));
		return *this;
	}

	template <typename T>
	Menu& multiSelect(const std::vector<std::string>& items, T* selected, bool active = true, int id = -1) {
		addItem(new MultiSelect(items, (int)(*selected), [selected](int v) { *selected = (T)v; }, active, id));
		return *this;
	}

	Menu& subMenu(const std::string& name, bool active = true) {
		Menu* m = new Menu(name, active, this);
		addItem(m);
		return *m;
	}

	Menu& separator() {
		if (_items.empty() || _items.back()->getType() != MenuItemType::Separator) {
			addItem(new Separator());
		}
		
		return *this;
	}

	Menu& parent() {
		if (!_parent) {
			std::cout << "Failed to find menu parent.  Current: " << _name << std::endl;
			assert(_parent);
		}
		
		return *_parent;
	}
};

void mergeMenu(Menu* source, Menu* target);

#pragma once

#include <vector>
#include <string>
#include <functional>
#include <iostream>
#include <assert.h>

#include <spdlog/spdlog.h>

namespace fw {
	enum class MenuItemType {
		None,
		SubMenu,
		Select,
		MultiSelect,
		Separator,
		Action,
		Title
	};

	class MenuContext {
	private:
		bool _closing = false;

	public:
		void close() { _closing = true; }

		void retain() { _closing = false; }

		bool isClosing() const { return _closing; }
	};

	using MultiSelectFunction = std::function<void(int)>;
	using SelectFunction = std::function<void(bool)>;
	using ActionFunction = std::function<void()>;
	using ActionContextFunction = std::function<void(MenuContext& ctx)>;
	using MenuCallbackMap = std::vector<std::function<void()>>;

	class MenuItemBase {
	private:
		std::string _name;
		MenuItemType _type = MenuItemType::None;
		int _id;
		bool _active;

	protected:
		MenuItemBase(MenuItemType type, const std::string& name, bool active, int id = -1) : _type(type), _name(name), _active(active), _id(id) {}

	public:
		MenuItemType getType() const { return _type; }

		int getId() const { return _id; }

		const std::string& getName() const { return _name; }

		template <typename T>
		T* as() {
			return (T*)this;
		}

		template <typename T>
		const T* as() const {
			return (T*)this;
		}

		bool isActive() const { return _active; }
	};

	class Select : public MenuItemBase {
	private:
		bool _checked = false;
		SelectFunction _func;

	public:
		Select(const std::string& name, bool checked, SelectFunction&& func, bool active, int id)
			: MenuItemBase(MenuItemType::Select, name, active, id), _checked(checked), _func(func) {}

		bool getChecked() const { return _checked; }

		bool toggleChecked() {
			_checked = !_checked;
			_func(_checked);
			return _checked;
		}

		SelectFunction& getFunction() { return _func; }
	};

	class Action : public MenuItemBase {
	private:
		ActionContextFunction _func;

	public:
		Action(const std::string& name, ActionContextFunction&& func, bool active, int id)
			: MenuItemBase(MenuItemType::Action, name, active, id), _func(func) {}

		ActionContextFunction& getFunction() { return _func; }
	};

	class MultiSelect : public MenuItemBase {
	private:
		std::vector<std::string> _items;
		int _value;
		MultiSelectFunction _func;
		bool _active;

	public:
		MultiSelect(const std::string& name, const std::vector<std::string>& items, int value, MultiSelectFunction&& func, bool active, int id)
			: MenuItemBase(MenuItemType::MultiSelect, name, id), _items(items), _value(value), _func(func), _active(active) {}

		const std::vector<std::string>& getItems() const { return _items; }

		int getValue() const { return _value; }

		MultiSelectFunction& getFunction() { return _func; }

		bool isActive() const { return _active; }

		void nextItem() {
			_value = (_value + 1) % _items.size();
		}

		void prevItem() {
			if (_value == 0) {
				_value = (int)_items.size() - 1;
			} else {
				_value--;
			}
		}
	};

	class Separator : public MenuItemBase {
	public:
		Separator() : MenuItemBase(MenuItemType::Separator, "", false) {}
	};

	class Title : public MenuItemBase {
	public:
		Title(const std::string& name) : MenuItemBase(MenuItemType::Title, name, false) {}
	};

	class Menu : public MenuItemBase {
	private:
		Menu* _parent;

		std::vector<MenuItemBase*> _items;

	public:
		Menu(const std::string& name = "", bool active = false, Menu* parent = nullptr)
			: MenuItemBase(MenuItemType::SubMenu, name, active), _parent(parent) {}

		~Menu() {
			for (MenuItemBase* item : _items) {
				delete item;
			}
		}

		const std::vector<MenuItemBase*>& getItems() const { return _items; }

		void addItem(MenuItemBase* item) { _items.push_back(item); }

		Menu& title(const std::string& name) {
			addItem(new Title(name));
			return *this;
		}

		Menu& action(const std::string& name, ActionFunction&& func, bool active = true, int id = -1) {
			addItem(new Action(name, [f = std::move(func)](MenuContext& ctx) { f(); }, active, id));
			return *this;
		}

		Menu& action(const std::string& name, ActionContextFunction&& func, bool active = true, int id = -1) {
			addItem(new Action(name, std::forward<ActionContextFunction>(func), active, id));
			return *this;
		}

		Menu& select(const std::string& name, bool selected, SelectFunction&& func, bool active = true, int id = -1) {
			addItem(new Select(name, selected, std::forward<SelectFunction>(func), active, id));
			return *this;
		}

		Menu& select(const std::string& name, bool* selected, bool active = true, int id = -1) {
			addItem(new Select(name, *selected, [selected](bool v) { *selected = v; }, active, id));
			return *this;
		}

		Menu& multiSelect(const std::string& name, const std::vector<std::string>& items, int selected, MultiSelectFunction&& func, bool active = true, int id = -1) {
			addItem(new MultiSelect(name, items, selected, std::forward<MultiSelectFunction>(func), active, id));
			return *this;
		}

		template <typename T>
		Menu& multiSelect(const std::string& name, const std::vector<std::string>& items, T* selected, bool active = true, int id = -1) {
			addItem(new MultiSelect(name, items, (int)(*selected), [selected](int v) { *selected = (T)v; }, active, id));
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
				spdlog::error("Failed to find menu parent.  Current: ", getName());
				assert(_parent);
			}

			return *_parent;
		}
	};

	using MenuPtr = std::shared_ptr<Menu>;
}

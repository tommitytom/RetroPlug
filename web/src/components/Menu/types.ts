import type { ReactNode } from 'react';

export interface MenuItem {
	id: string;
	label?: string;
	icon?: ReactNode;
	checked?: boolean;
	disabled?: boolean;
	shortcut?: string;
	separator?: boolean;
	submenu?: MenuItem[];
	onClick?: () => void;
}

export interface MenuPosition {
	x: number;
	y: number;
}

export interface MenuProps {
	items: MenuItem[];
	onItemClick?: (item: MenuItem) => void;
}

export interface MenuBarProps {
	menus: { label: string; items: MenuItem[] }[];
	onItemClick?: (item: MenuItem) => void;
}

export interface ContextMenuProps extends MenuProps {
	position: MenuPosition;
	visible: boolean;
	onClose: () => void;
}

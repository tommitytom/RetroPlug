import { useState, useCallback } from 'react';
import type { MenuPosition, MenuItem } from '../components/Menu/types';

export function useContextMenu() {
	const [isVisible, setIsVisible] = useState(false);
	const [position, setPosition] = useState<MenuPosition>({ x: 0, y: 0 });
	const [items, setItems] = useState<MenuItem[]>([]);

	const showContextMenu = useCallback((event: React.MouseEvent, menuItems: MenuItem[]) => {
		event.preventDefault();
		setPosition({ x: event.clientX, y: event.clientY });
		setItems(menuItems);
		setIsVisible(true);
	}, []);

	const hideContextMenu = useCallback(() => {
		setIsVisible(false);
	}, []);

	const handleItemClick = useCallback((item: MenuItem) => {
		hideContextMenu();
		item.onClick?.();
	}, [hideContextMenu]);

	return {
		isVisible,
		position,
		items,
		showContextMenu,
		hideContextMenu,
		handleItemClick,
	};
}

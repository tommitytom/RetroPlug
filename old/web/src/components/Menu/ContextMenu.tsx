import { useEffect, useRef, useState, useLayoutEffect } from 'react';
import type { ContextMenuProps } from './types';
import { Menu } from './Menu';

export function ContextMenu({ items, position, visible, onClose, onItemClick }: ContextMenuProps) {
	const menuRef = useRef<HTMLDivElement>(null);
	const [adjustedPosition, setAdjustedPosition] = useState(position);

	useLayoutEffect(() => {
		if (visible && menuRef.current) {
			const menu = menuRef.current;
			const menuRect = menu.getBoundingClientRect();
			const viewportWidth = window.innerWidth;
			const viewportHeight = window.innerHeight;

			let newX = position.x;
			let newY = position.y;

			// Check if menu would overflow on the right side
			if (position.x + menuRect.width > viewportWidth) {
				// Position to the left of the cursor
				newX = position.x - menuRect.width;
				// Ensure it doesn't go off the left side
				if (newX < 0) {
					newX = 10; // Add small margin from left edge
				}
			}

			// Check if menu would overflow on the bottom
			if (position.y + menuRect.height > viewportHeight) {
				// Position above the cursor
				newY = position.y - menuRect.height;
				// Ensure it doesn't go off the top
				if (newY < 0) {
					newY = 10; // Add small margin from top edge
				}
			}

			setAdjustedPosition({ x: newX, y: newY });
		}
	}, [visible, position]);

	useEffect(() => {
		const handleClickOutside = (event: MouseEvent) => {
			if (menuRef.current && !menuRef.current.contains(event.target as Node)) {
				onClose();
			}
		};

		const handleEscape = (event: KeyboardEvent) => {
			if (event.key === 'Escape') {
				onClose();
			}
		};

		if (visible) {
			document.addEventListener('mousedown', handleClickOutside);
			document.addEventListener('keydown', handleEscape);
		}

		return () => {
			document.removeEventListener('mousedown', handleClickOutside);
			document.removeEventListener('keydown', handleEscape);
		};
	}, [visible, onClose]);

	const handleItemClick = (item: any) => {
		onClose();
		onItemClick?.(item);
	};

	if (!visible) return null;

	return (
		<div
			ref={menuRef}
			className="fixed z-50"
			style={{
				left: adjustedPosition.x,
				top: adjustedPosition.y,
			}}
		>
			<Menu items={items} onItemClick={handleItemClick} />
		</div>
	);
}

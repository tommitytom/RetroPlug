import { useEffect, useRef } from 'react';
import type { ContextMenuProps } from './types';
import { Menu } from './Menu';

export function ContextMenu({ items, position, visible, onClose, onItemClick }: ContextMenuProps) {
	const menuRef = useRef<HTMLDivElement>(null);

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
				left: position.x,
				top: position.y,
			}}
		>
			<Menu items={items} onItemClick={handleItemClick} />
		</div>
	);
}

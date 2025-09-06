import { useState, useRef, useEffect } from 'react';
import type { MenuItem, MenuProps } from './types';

interface MenuItemComponentProps {
	item: MenuItem;
	onItemClick?: (item: MenuItem) => void;
	depth?: number;
}

function MenuItemComponent({ item, onItemClick, depth = 0 }: MenuItemComponentProps) {
	const [isOpen, setIsOpen] = useState(false);
	const itemRef = useRef<HTMLDivElement>(null);

	const hasSubmenu = item.submenu && item.submenu.length > 0;

	const handleClick = () => {
		if (hasSubmenu) {
			setIsOpen(!isOpen);
		} else {
			item.onClick?.();
			onItemClick?.(item);
		}
	};

	const handleMouseEnter = () => {
		if (hasSubmenu) {
			setIsOpen(true);
		}
	};

	const handleMouseLeave = () => {
		if (hasSubmenu) {
			setIsOpen(false);
		}
	};

	if (item.separator) {
		return <div className="border-t border-gray-600 my-1" />;
	}

	return (
		<div
			ref={itemRef}
			className="relative"
			onMouseEnter={handleMouseEnter}
			onMouseLeave={handleMouseLeave}
		>
			<div
				className={`
					flex items-center px-3 py-2 text-sm cursor-pointer
					${item.disabled
						? 'text-gray-500 cursor-not-allowed'
						: 'text-white hover:bg-gray-700'
					}
					${depth > 0 ? 'pl-6' : ''}
				`}
				onClick={!item.disabled ? handleClick : undefined}
			>
				{/* Icon */}
				{item.icon && (
					<span className="mr-2 w-4 h-4 flex items-center justify-center">
						{item.icon}
					</span>
				)}

				{/* Checkbox */}
				{item.checked !== undefined && (
					<span className="mr-2 w-4 h-4 flex items-center justify-center">
						{item.checked && (
							<svg className="w-3 h-3" fill="currentColor" viewBox="0 0 20 20">
								<path fillRule="evenodd" d="M16.707 5.293a1 1 0 010 1.414l-8 8a1 1 0 01-1.414 0l-4-4a1 1 0 011.414-1.414L8 12.586l7.293-7.293a1 1 0 011.414 0z" clipRule="evenodd" />
							</svg>
						)}
					</span>
				)}

				{/* Label */}
				<span className="flex-1">{item.label || ''}</span>

				{/* Shortcut */}
				{item.shortcut && (
					<span className="ml-4 text-xs text-gray-400">{item.shortcut}</span>
				)}

				{/* Submenu arrow */}
				{hasSubmenu && (
					<span className="ml-2">
						<svg className="w-3 h-3" fill="currentColor" viewBox="0 0 20 20">
							<path fillRule="evenodd" d="M7.293 14.707a1 1 0 010-1.414L10.586 10 7.293 6.707a1 1 0 011.414-1.414l4 4a1 1 0 010 1.414l-4 4a1 1 0 01-1.414 0z" clipRule="evenodd" />
						</svg>
					</span>
				)}
			</div>

			{/* Submenu */}
			{hasSubmenu && isOpen && (
				<div
					className={`
						absolute z-50 min-w-48 bg-gray-800 border border-gray-600 rounded shadow-lg
						${depth === 0 ? 'left-full top-0' : 'left-full top-0'}
					`}
				>
					{item.submenu!.map((subItem, index) => (
						<MenuItemComponent
							key={subItem.id || index}
							item={subItem}
							onItemClick={onItemClick}
							depth={depth + 1}
						/>
					))}
				</div>
			)}
		</div>
	);
}

export function Menu({ items, onItemClick }: MenuProps) {
	return (
		<div className="bg-gray-800 border border-gray-600 rounded shadow-lg min-w-48">
			{items.map((item, index) => (
				<MenuItemComponent
					key={item.id || index}
					item={item}
					onItemClick={onItemClick}
				/>
			))}
		</div>
	);
}

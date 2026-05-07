import { useState, useRef, useEffect } from 'react';
import type { MenuBarProps, MenuItem } from './types';
import { Menu } from './Menu';

export function MenuBar({ menus, onItemClick }: MenuBarProps) {
	const [activeMenu, setActiveMenu] = useState<string | null>(null);
	const [menuPosition, setMenuPosition] = useState({ x: 0, y: 0 });
	const menuBarRef = useRef<HTMLDivElement>(null);

	useEffect(() => {
		const handleClickOutside = (event: MouseEvent) => {
			if (menuBarRef.current && !menuBarRef.current.contains(event.target as Node)) {
				setActiveMenu(null);
			}
		};

		document.addEventListener('mousedown', handleClickOutside);
		return () => document.removeEventListener('mousedown', handleClickOutside);
	}, []);

	const handleMenuClick = (menuLabel: string, event: React.MouseEvent) => {
		const rect = (event.target as HTMLElement).getBoundingClientRect();
		setMenuPosition({
			x: rect.left,
			y: rect.bottom
		});
		setActiveMenu(activeMenu === menuLabel ? null : menuLabel);
	};

	const handleItemClick = (item: MenuItem) => {
		setActiveMenu(null);
		onItemClick?.(item);
	};

	return (
		<div ref={menuBarRef} className="relative">
			<div className="menu-bar bg-gray-800 text-white border-b border-gray-600">
				<div className="flex">
					{menus.map((menu) => (
						<div
							key={menu.label}
							className={`
								menu-item px-3 py-2 cursor-pointer text-sm
								${activeMenu === menu.label
									? 'bg-gray-700'
									: 'hover:bg-gray-700'
								}
							`}
							onClick={(e) => handleMenuClick(menu.label, e)}
						>
							{menu.label}
						</div>
					))}
				</div>
			</div>

			{/* Active menu dropdown */}
			{activeMenu && (
				<div
					className="absolute z-50"
					style={{
						left: menuPosition.x,
						top: menuPosition.y,
					}}
				>
					<Menu
						items={menus.find(m => m.label === activeMenu)?.items || []}
						onItemClick={handleItemClick}
					/>
				</div>
			)}
		</div>
	);
}

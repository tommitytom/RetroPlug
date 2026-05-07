import { ChevronLeft, ChevronRight } from "lucide-react";
import { useEffect, useRef, useState } from "react";

// Resizable Panel Component
interface ResizablePanelProps {
	side: 'left' | 'right';
	isCollapsed: boolean;
	onToggle: () => void;
	width: number;
	onWidthChange: (width: number) => void;
	minWidth: number;
	maxWidth: number;
	children?: React.ReactNode;
}

export const ResizablePanel: React.FC<ResizablePanelProps> = ({
	side,
	isCollapsed,
	onToggle,
	width,
	onWidthChange,
	minWidth,
	maxWidth,
	children,
}) => {
	const [isDragging, setIsDragging] = useState(false);
	const panelRef = useRef<HTMLDivElement>(null);

	useEffect(() => {
		if (!isDragging) return;

		const handleMouseMove = (e: MouseEvent) => {
			e.preventDefault();
			const newWidth = side === 'left'
				? e.clientX
				: window.innerWidth - e.clientX;

			onWidthChange(Math.max(minWidth, Math.min(maxWidth, newWidth)));
		};

		const handleMouseUp = () => {
			setIsDragging(false);
			document.body.style.cursor = '';
			document.body.style.userSelect = '';
		};

		document.addEventListener('mousemove', handleMouseMove);
		document.addEventListener('mouseup', handleMouseUp);

		document.body.style.cursor = 'col-resize';
		document.body.style.userSelect = 'none';

		return () => {
			document.removeEventListener('mousemove', handleMouseMove);
			document.removeEventListener('mouseup', handleMouseUp);
			document.body.style.cursor = '';
			document.body.style.userSelect = '';
		};
	}, [isDragging, side, onWidthChange, minWidth, maxWidth]);

	return (
		<div
			ref={panelRef}
			className={`relative flex ${side === 'left' ? 'flex-row' : 'flex-row-reverse'} bg-gray-900 border-gray-700 ${
				side === 'left' ? 'border-r' : 'border-l'
			} transition-all duration-300 ease-out ${isDragging ? 'transition-none' : ''}`}
			style={{
				width: isCollapsed ? '40px' : `${width}px`,
				minWidth: isCollapsed ? '40px' : `${minWidth}px`,
			}}
		>
			{/* Collapse Toggle Button */}
			<button
				onClick={onToggle}
				className={`absolute ${side === 'left' ? '-right-3' : '-left-3'} top-1/2 -translate-y-1/2 z-20 bg-gray-800 hover:bg-gray-700 border border-gray-600 rounded-md p-1 transition-colors`}
				aria-label={`${isCollapsed ? 'Expand' : 'Collapse'} ${side} panel`}
			>
				{side === 'left'
					? (isCollapsed ? <ChevronRight className="w-3 h-3" /> : <ChevronLeft className="w-3 h-3" />)
					: (isCollapsed ? <ChevronLeft className="w-3 h-3" /> : <ChevronRight className="w-3 h-3" />)
				}
			</button>

			{/* Panel Content */}
			<div className={`flex-1 overflow-hidden ${isCollapsed ? 'opacity-0 pointer-events-none' : 'opacity-100'} transition-opacity duration-300`}>
				{children}
			</div>

			{/* Resize Handle */}
			{!isCollapsed && (
				<div
					className={`absolute ${side === 'left' ? 'right-0' : 'left-0'} top-0 bottom-0 w-1 hover:bg-blue-500/50 cursor-col-resize z-10 transition-colors ${
						isDragging ? 'bg-blue-500/50' : ''
					}`}
					onMouseDown={(e) => {
						e.preventDefault();
						setIsDragging(true);
					}}
				/>
			)}
		</div>
	);
};

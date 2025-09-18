import React from 'react';

interface DeleteIconProps {
	onClick: (e: React.MouseEvent) => void;
	className?: string;
	title?: string;
	disabled?: boolean;
}

export const DeleteIcon: React.FC<DeleteIconProps> = ({
	onClick,
	className = "rounded-sm p-1 text-red-400 transition-colors duration-200 hover:bg-red-600/20 hover:text-red-300",
	title = "Delete",
	disabled = false
}) => {
	const handleClick = (e: React.MouseEvent) => {
		if (disabled) {
			e.stopPropagation();
			return;
		}
		onClick(e);
	};

	return (
		<button
			className={`${className} ${disabled ? 'opacity-50 hover:bg-transparent hover:text-current' : ''}`}
			onClick={handleClick}
			title={disabled ? 'Cannot delete this item' : title}
			disabled={disabled}
		>
			<svg
				width="12"
				height="12"
				viewBox="0 0 24 24"
				fill="none"
				stroke="currentColor"
				strokeWidth="2"
				strokeLinecap="round"
				strokeLinejoin="round"
			>
				<polyline points="3,6 5,6 21,6"></polyline>
				<path d="m19,6v14a2,2 0 0,1-2,2H7a2,2 0 0,1-2-2V6m3,0V4a2,2 0 0,1,2-2h4a2,2 0 0,1,2,2v2"></path>
				<line x1="10" y1="11" x2="10" y2="17"></line>
				<line x1="14" y1="11" x2="14" y2="17"></line>
			</svg>
		</button>
	);
};
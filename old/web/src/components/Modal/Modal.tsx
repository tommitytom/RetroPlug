import { X } from 'lucide-react';
import { useEffect, useRef } from 'react';

interface ModalProps {
	isOpen: boolean;
	onClose: () => void;
	title?: string;
	children: React.ReactNode;
	size?: 'sm' | 'md' | 'lg' | 'xl' | '2xl' | '3xl' | '4xl' | 'full';
	showCloseButton?: boolean;
	closeOnBackdrop?: boolean;
	closeOnEscape?: boolean;
}

export const Modal: React.FC<ModalProps> = ({
	isOpen,
	onClose,
	title,
	children,
	size = 'md',
	showCloseButton = true,
	closeOnBackdrop = true,
	closeOnEscape = true,
}) => {
	const modalRef = useRef<HTMLDivElement>(null);

	useEffect(() => {
		const handleEscape = (e: KeyboardEvent) => {
			if (closeOnEscape && e.key === 'Escape') {
				onClose();
			}
		};

		if (isOpen) {
			document.addEventListener('keydown', handleEscape);
			document.body.style.overflow = 'hidden';
		}

		return () => {
			document.removeEventListener('keydown', handleEscape);
			document.body.style.overflow = '';
		};
	}, [isOpen, onClose, closeOnEscape]);

	if (!isOpen) return null;

	const sizeClasses = {
		sm: 'max-w-sm',
		md: 'max-w-md',
		lg: 'max-w-lg',
		xl: 'max-w-xl',
		'2xl': 'max-w-2xl',
		'3xl': 'max-w-3xl',
		'4xl': 'max-w-4xl',
		full: 'max-w-full',
	};

	return (
		<div className="fixed inset-0 z-50 flex items-center justify-center">
			{/* Backdrop */}
			<div className="absolute inset-0 bg-black/25 backdrop-blur-xs" onClick={closeOnBackdrop ? onClose : undefined} />

			{/* Modal */}
			<div
				ref={modalRef}
				className={`relative z-10 w-full ${sizeClasses[size]} mx-4 transform rounded-lg bg-gray-900 border border-gray-700 shadow-2xl transition-all`}
			>
				{/* Header */}
				{(title || showCloseButton) && (
					<div className="flex items-center justify-between border-b border-gray-700 px-4 py-3">
						{title && <h2 className="text-lg font-semibold text-white">{title}</h2>}
						{showCloseButton && (
							<button
								onClick={onClose}
								className="rounded-lg p-1 transition-colors hover:bg-gray-700 text-gray-400 hover:text-gray-200"
								aria-label="Close modal"
							>
								<X className="h-5 w-5" />
							</button>
						)}
					</div>
				)}

				{/* Content */}
				<div className="p-4 text-white">{children}</div>
			</div>
		</div>
	);
};

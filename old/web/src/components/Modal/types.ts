export interface ModalConfig {
	title?: string;
	content: React.ReactNode;
	size?: 'sm' | 'md' | 'lg' | 'xl' | '2xl' | '3xl' | '4xl' | 'full';
	showCloseButton?: boolean;
	closeOnBackdrop?: boolean;
	closeOnEscape?: boolean;
	onClose?: () => void;
}

export interface ModalContextType {
	openModal: (config: ModalConfig) => void;
	closeModal: () => void;
	openConfirm: (config: {
		title?: string;
		message: string;
		confirmText?: string;
		cancelText?: string;
		onConfirm: () => void;
		onCancel?: () => void;
		danger?: boolean;
	}) => void;
	openAlert: (config: {
		title?: string;
		message: string;
		type?: 'info' | 'success' | 'error' | 'warning';
		buttonText?: string;
		onClose?: () => void;
	}) => void;
	openYesNoCancel: (config: {
		title?: string;
		message: string;
		yesText?: string;
		noText?: string;
		cancelText?: string;
		onYes: () => void;
		onNo: () => void;
		onCancel?: () => void;
	}) => void;
}

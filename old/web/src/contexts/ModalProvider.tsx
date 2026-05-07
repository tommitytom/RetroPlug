import { AlertCircle, CheckCircle, Info } from 'lucide-react';
import { useCallback, useState } from 'react';

import { Modal } from '../components/Modal/Modal';
import type { ModalConfig } from '../components/Modal/types';
import { ModalContext } from './ModalContext';

export const ModalProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
	const [modalConfig, setModalConfig] = useState<ModalConfig | null>(null);

	const openModal = useCallback((config: ModalConfig) => {
		setModalConfig(config);
	}, []);

	const closeModal = useCallback(() => {
		if (modalConfig?.onClose) {
			modalConfig.onClose();
		}
		setModalConfig(null);
	}, [modalConfig]);

	const openConfirm = useCallback(
		(config: {
			title?: string;
			message: string;
			confirmText?: string;
			cancelText?: string;
			onConfirm: () => void;
			onCancel?: () => void;
			danger?: boolean;
		}) => {
			const content = (
				<div>
					<p className="mb-6 text-white">{config.message}</p>
					<div className="flex gap-3">
						<button
							onClick={() => {
								config.onCancel?.();
								closeModal();
							}}
							className="flex-1 rounded-lg bg-gray-700 px-4 py-2 text-gray-200 transition-colors hover:bg-gray-600"
						>
							{config.cancelText || 'Cancel'}
						</button>
						<button
							onClick={() => {
								config.onConfirm();
								closeModal();
							}}
							className={`flex-1 rounded-lg px-4 py-2 transition-colors ${
								config.danger ? 'bg-red-600 text-white hover:bg-red-700' : 'bg-blue-600 text-white hover:bg-blue-700'
							}`}
						>
							{config.confirmText || 'Confirm'}
						</button>
					</div>
				</div>
			);

			openModal({
				title: config.title || 'Confirm',
				content,
				size: 'sm',
			});
		},
		[openModal, closeModal],
	);

	const openAlert = useCallback(
		(config: {
			title?: string;
			message: string;
			type?: 'info' | 'success' | 'error' | 'warning';
			buttonText?: string;
			onClose?: () => void;
		}) => {
		const iconMap = {
			info: <Info className="mx-auto h-12 w-12 text-blue-400" />,
			success: <CheckCircle className="mx-auto h-12 w-12 text-green-400" />,
			error: <AlertCircle className="mx-auto h-12 w-12 text-red-400" />,
			warning: <AlertCircle className="mx-auto h-12 w-12 text-amber-400" />,
		};			const buttonColorMap = {
				info: 'bg-blue-600 hover:bg-blue-700',
				success: 'bg-green-600 hover:bg-green-700',
				error: 'bg-red-600 hover:bg-red-700',
				warning: 'bg-amber-600 hover:bg-amber-700',
			};

			const type = config.type || 'info';

			const content = (
				<div>
					<div className="mb-4">{iconMap[type]}</div>
					<p className="mb-6 text-center text-white">{config.message}</p>
					<button
						onClick={() => {
							config.onClose?.();
							closeModal();
						}}
						className={`w-full rounded-lg px-4 py-2 text-white transition-colors ${buttonColorMap[type]}`}
					>
						{config.buttonText || 'OK'}
					</button>
				</div>
			);

			openModal({
				title: config.title,
				content,
				size: 'sm',
			});
		},
		[openModal, closeModal],
	);

	const openYesNoCancel = useCallback(
		(config: {
			title?: string;
			message: string;
			yesText?: string;
			noText?: string;
			cancelText?: string;
			onYes: () => void;
			onNo: () => void;
			onCancel?: () => void;
		}) => {
			const content = (
				<div>
					<p className="mb-6 text-white">{config.message}</p>
					<div className="flex gap-2">
						<button
							onClick={() => {
								config.onCancel?.();
								closeModal();
							}}
							className="flex-1 rounded-lg bg-gray-700 px-4 py-2 text-gray-200 transition-colors hover:bg-gray-600"
						>
							{config.cancelText || 'Cancel'}
						</button>
						<button
							onClick={() => {
								config.onNo();
								closeModal();
							}}
							className="flex-1 rounded-lg bg-red-600 px-4 py-2 text-white transition-colors hover:bg-red-700"
						>
							{config.noText || 'No'}
						</button>
						<button
							onClick={() => {
								config.onYes();
								closeModal();
							}}
							className="flex-1 rounded-lg bg-green-600 px-4 py-2 text-white transition-colors hover:bg-green-700"
						>
							{config.yesText || 'Yes'}
						</button>
					</div>
				</div>
			);

			openModal({
				title: config.title || 'Confirm',
				content,
				size: 'sm',
			});
		},
		[openModal, closeModal],
	);

	return (
		<ModalContext.Provider value={{ openModal, closeModal, openConfirm, openAlert, openYesNoCancel }}>
			{children}
			{modalConfig && (
				<Modal isOpen={true} onClose={closeModal} {...modalConfig}>
					{modalConfig.content}
				</Modal>
			)}
		</ModalContext.Provider>
	);
};

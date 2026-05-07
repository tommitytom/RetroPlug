import { useCallback } from 'react';

import { useModal } from '../../contexts/ModalContext';
import type { DocumentType } from '../Layout/types';
import { SaveAsDialog } from './SaveAsDialog';

export const useSaveAsDialog = () => {
	const { openModal, closeModal } = useModal();

	const showSaveAsDialog = useCallback(
		(config: {
			title?: string;
			documentType: DocumentType;
			defaultFilename?: string;
			onSave: (filename: string) => void;
			onCancel?: () => void;
		}) => {
			openModal({
				title: config.title || `Save project as...`,
				content: (
					<SaveAsDialog
						documentType={config.documentType}
						defaultFilename={config.defaultFilename}
						onSave={(filename) => {
							config.onSave(filename);
							closeModal();
						}}
						onCancel={() => {
							config.onCancel?.();
							closeModal();
						}}
					/>
				),
				size: 'sm',
			});
		},
		[openModal, closeModal],
	);

	return { showSaveAsDialog };
};
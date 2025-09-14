import { useCallback, useState } from "react";

import { DocumentContext } from "./DocumentContext";
import type { Document, DocumentType, SaveHandler, SaveResult } from "../components/Layout/types";
import { saveHandlers } from "./SaveHandlers";

export const DocumentProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
	const [currentDocument, setCurrentDocument] = useState<Document | null>({
		id: '1',
		title: '',
		content: null,
		type: 'emulator',
		isDirty: false,
	});
	const [isSaving, setIsSaving] = useState(false);
	const [lastSaveResult, setLastSaveResult] = useState<SaveResult | null>(null);
	const [customSaveHandlers, setCustomSaveHandlers] = useState<Record<DocumentType, SaveHandler>>(saveHandlers);

	const markDirty = useCallback(() => {
		setCurrentDocument(prev => prev ? { ...prev, isDirty: true } : null);
	}, []);

	const markClean = useCallback(() => {
		setCurrentDocument(prev => prev ? { ...prev, isDirty: false } : null);
	}, []);

	const updateDocument = useCallback((updates: Partial<Document>) => {
		setCurrentDocument(prev => prev ? { ...prev, ...updates } : null);
	}, []);

	const saveDocument = useCallback(async (): Promise<SaveResult> => {
		if (!currentDocument || isSaving) {
			return { success: false, message: 'No document to save or save in progress' };
		}

		setIsSaving(true);

		try {
			const handler = customSaveHandlers[currentDocument.type];
			if (!handler) {
				throw new Error(`No save handler for document type: ${currentDocument.type}`);
			}

			const result = await handler({
				document: currentDocument,
				markClean,
				updateDocument
			});

			setLastSaveResult(result);

			// Clear the result after 3 seconds
			setTimeout(() => setLastSaveResult(null), 3000);

			return result;
		} catch (error) {
			const errorResult = {
				success: false,
				message: error instanceof Error ? error.message : 'Save failed'
			};
			setLastSaveResult(errorResult);
			return errorResult;
		} finally {
			setIsSaving(false);
		}
	}, [currentDocument, customSaveHandlers, markClean, updateDocument, isSaving]);

	const registerSaveHandler = useCallback((type: DocumentType, handler: SaveHandler) => {
		setCustomSaveHandlers(prev => ({ ...prev, [type]: handler }));
	}, []);

	return (
		<DocumentContext.Provider value={{
			currentDocument,
			setCurrentDocument,
			updateDocument,
			markDirty,
			markClean,
			saveDocument,
			isSaving,
			lastSaveResult,
			registerSaveHandler
		}}>
			{children}
		</DocumentContext.Provider>
	);
};

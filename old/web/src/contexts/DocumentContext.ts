import { createContext, useContext } from 'react';
import type { Document, DocumentType, SaveHandler, SaveResult } from '../components/Layout/types';

// Document Context
interface DocumentContextType {
	currentDocument: Document | null;
	setCurrentDocument: (doc: Document | null) => void;
	updateDocument: (updates: Partial<Document>) => void;
	markDirty: () => void;
	markClean: () => void;
	saveDocument: (forceDialog?: boolean) => Promise<SaveResult>;
	isSaving: boolean;
	lastSaveResult: SaveResult | null;
	registerSaveHandler: (type: DocumentType, handler: SaveHandler) => void;
}

export const DocumentContext = createContext<DocumentContextType | undefined>(undefined);

export const useDocument = () => {
	const context = useContext(DocumentContext);
	if (!context) {
		throw new Error('useDocument must be used within DocumentProvider');
	}
	return context;
};

export type { DocumentContextType };

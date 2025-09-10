import { createContext, useContext } from 'react';

interface DocumentInfo {
	path: string;
	content: string;
	language: string;
	name: string;
}

interface DocumentContextType {
	currentDocument: DocumentInfo | null;
	openDocument: (path: string, content: string, name: string, language?: string) => void;
	closeDocument: () => void;
	updateDocument: (content: string) => void;
	saveDocument: () => Promise<void>;
	isDirty: boolean;
}

export const DocumentContext = createContext<DocumentContextType | undefined>(undefined);

export function useDocument() {
	const context = useContext(DocumentContext);
	if (context === undefined) {
		throw new Error('useDocument must be used within a DocumentProvider');
	}
	return context;
}

export type { DocumentInfo, DocumentContextType };

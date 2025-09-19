import { ReactNode } from "react";

// Document Types and Save Handlers
export type DocumentType = 'text' | 'audio' | 'emulator' | 'other';

export interface Document {
	//id: string;
	title: string;
	type: DocumentType;
	content: any;
	metadata?: Record<string, any>;
	isDirty: boolean;
	hasFilename?: boolean;  // Track if document has been saved with a filename
	filePath?: string;      // Optional path where document is saved
}

export interface SaveResult {
	success: boolean;
	message?: string;
	savedFiles?: string[];
}

export interface SaveContext {
	document: Document;
	markClean: () => void;
	updateDocument: (updates: Partial<Document>) => void;
	showSaveAsDialog?: (config: {
		title?: string;
		documentType: DocumentType;
		defaultFilename?: string;
		onSave: (filename: string) => void;
		onCancel?: () => void;
	}) => void;
}

export type SaveHandler = (context: SaveContext, forceDialog?: boolean) => Promise<SaveResult>;

export interface TabItem {
	id: string;
	label: string;
	content: ReactNode;
}

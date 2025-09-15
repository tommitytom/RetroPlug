import { ReactNode } from "react";

// Document Types and Save Handlers
export type DocumentType = 'text' | 'audio' | 'emulator' | 'other';

export interface Document {
	id: string;
	title: string;
	type: DocumentType;
	content: any;
	metadata?: Record<string, any>;
	isDirty: boolean;
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
}

export type SaveHandler = (context: SaveContext) => Promise<SaveResult>;

export interface TabItem {
	id: string;
	label: string;
	content: ReactNode;
}

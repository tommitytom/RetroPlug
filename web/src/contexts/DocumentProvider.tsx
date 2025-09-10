import React, { useState, useCallback, ReactNode } from 'react';
import { DocumentContext, type DocumentInfo } from './DocumentContext';
import { useOPFSStore } from '../stores/FileSystemStore';

interface DocumentProviderProps {
	children: ReactNode;
}

// Helper function to determine language from file extension
function getLanguageFromPath(path: string): string {
	const extension = path.split('.').pop()?.toLowerCase();

	const languageMap: Record<string, string> = {
		'js': 'javascript',
		'jsx': 'javascript',
		'ts': 'typescript',
		'tsx': 'typescript',
		'json': 'json',
		'rplg': 'json',
		'html': 'html',
		'css': 'css',
		'scss': 'scss',
		'sass': 'sass',
		'md': 'markdown',
		'py': 'python',
		'cpp': 'cpp',
		'c': 'c',
		'h': 'c',
		'hpp': 'cpp',
		'lua': 'lua',
		'xml': 'xml',
		'yml': 'yaml',
		'yaml': 'yaml',
		'txt': 'plaintext',
	};

	return languageMap[extension || ''] || 'plaintext';
}

export const DocumentProvider: React.FC<DocumentProviderProps> = ({ children }) => {
	const [currentDocument, setCurrentDocument] = useState<DocumentInfo | null>(null);
	const [isDirty, setIsDirty] = useState(false);
	const { writePath } = useOPFSStore();

	const openDocument = useCallback((path: string, content: string, name: string, language?: string) => {
		const documentLanguage = language || getLanguageFromPath(path);

		setCurrentDocument({
			path,
			content,
			language: documentLanguage,
			name
		});
		setIsDirty(false);
	}, []);

	const closeDocument = useCallback(() => {
		setCurrentDocument(null);
		setIsDirty(false);
	}, []);

	const updateDocument = useCallback((content: string) => {
		if (currentDocument) {
			setCurrentDocument(prev => prev ? { ...prev, content } : null);
			setIsDirty(true);
		}
	}, [currentDocument]);

	const saveDocument = useCallback(async () => {
		if (currentDocument && isDirty) {
			try {
				const encoder = new TextEncoder();
				const data = encoder.encode(currentDocument.content);
				await writePath(currentDocument.path, data.buffer);
				setIsDirty(false);
				console.log('Document saved:', currentDocument.path);
			} catch (error) {
				console.error('Failed to save document:', error);
				throw error;
			}
		}
	}, [currentDocument, isDirty, writePath]);

	const value = {
		currentDocument,
		openDocument,
		closeDocument,
		updateDocument,
		saveDocument,
		isDirty
	};

	return (
		<DocumentContext.Provider value={value}>
			{children}
		</DocumentContext.Provider>
	);
};

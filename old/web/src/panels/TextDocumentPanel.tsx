import React, { useCallback, useEffect } from 'react';
import Editor, { type Monaco } from '@monaco-editor/react';
import { useDocument } from '../contexts/DocumentContext';
import RetroPlugSchema from '../schemas/RetroPlug.json';

export const TextDocumentPanel: React.FC = () => {
	const { currentDocument, updateDocument, saveDocument, isDirty } = useDocument();

	const handleEditorChange = useCallback((value: string | undefined) => {
		if (value !== undefined) {
			updateDocument(value);
		}
	}, [updateDocument]);

	const handleSave = useCallback(() => {
		if (isDirty) {
			saveDocument().catch(error => {
				console.error('Failed to save:', error);
			});
		}
	}, [isDirty, saveDocument]);

	function handleEditorWillMount(monaco: Monaco) {
		monaco.languages.json.jsonDefaults.setDiagnosticsOptions({
			validate: true,
			schemas: [{
				uri: "https://retroplug.io/schema.json",
				fileMatch: ["*.rplg"],
				schema: RetroPlugSchema
			}]
		});
	}

	// Listen for Ctrl+S to save
	useEffect(() => {
		const handleKeyDown = (event: KeyboardEvent) => {
			if (event.ctrlKey && event.key === 's') {
				event.preventDefault();
				handleSave();
			}
		};

		window.addEventListener('keydown', handleKeyDown);
		return () => window.removeEventListener('keydown', handleKeyDown);
	}, [handleSave]);

	if (!currentDocument) {
		return (
			<div className="flex items-center justify-center h-full text-gray-500">
				<div className="text-center">
					<p>No document open</p>
					<p className="text-sm">Select "Edit" from the file context menu to open a file</p>
				</div>
			</div>
		);
	}

	return (
		<div className="h-full flex flex-col">
			<div className="flex-1">
				<Editor
					height="100%"
					language={currentDocument.language}
					value={currentDocument.content}
					beforeMount={handleEditorWillMount}
					onChange={handleEditorChange}
					options={{
						minimap: { enabled: false },
						fontSize: 14,
						lineNumbers: 'on',
						roundedSelection: false,
						scrollBeyondLastLine: false,
						readOnly: false,
						theme: 'vs-dark'
					}}
				/>
			</div>
		</div>
	);
};

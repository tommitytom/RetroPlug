import type { DocumentType, SaveHandler } from "../components/Layout/types";
import { Project } from "../wrapper/Project";

// Individual Save Handlers for each document type
export const saveHandlers: Record<DocumentType, SaveHandler> = {
	text: async ({ document, markClean, updateDocument, showSaveAsDialog }) => {
		// Check if document needs a filename
		if (!document.hasFilename && showSaveAsDialog) {
			return new Promise((resolve) => {
				showSaveAsDialog({
					documentType: 'text',
					defaultFilename: document.title.endsWith('.txt') ? document.title : document.title,
					onSave: async (filename) => {
						console.log(`Saving text document as: ${filename}`);

						// Update document with filename info
						updateDocument({
							title: filename,
							hasFilename: true,
							filePath: filename
						});

						await new Promise(resolve => setTimeout(resolve, 300));
						markClean();

						resolve({
							success: true,
							message: 'Text document saved',
							savedFiles: [filename]
						});
					},
					onCancel: () => {
						resolve({ success: false, message: 'Save cancelled' });
					}
				});
			});
		}

		// Simple text save for existing files
		console.log('Saving text document:', document.title);
		await new Promise(resolve => setTimeout(resolve, 300));
		markClean();
		return {
			success: true,
			message: 'Text document saved',
			savedFiles: [document.title]
		};
	},

	audio: async ({ document, markClean, updateDocument, showSaveAsDialog }) => {
		// Check if document needs a filename
		if (!document.hasFilename && showSaveAsDialog) {
			return new Promise((resolve) => {
				showSaveAsDialog({
					documentType: 'audio',
					defaultFilename: document.title.includes('.') ? document.title.split('.')[0] : document.title,
					onSave: async (filename) => {
						console.log(`Saving audio document as: ${filename}`);

						// Audio might need export settings
						const exportSettings = await new Promise<{ format: string; quality: string }>((resolve) => {
							console.log('Using default audio export settings...');
							setTimeout(() => {
								resolve({ format: 'mp3', quality: '320kbps' });
							}, 300);
						});

						console.log('Exporting audio with settings:', exportSettings);

						// Update document with filename and export settings
						updateDocument({
							title: filename,
							hasFilename: true,
							filePath: `${filename}.${exportSettings.format}`,
							metadata: { ...document.metadata, lastExport: exportSettings }
						});

						await new Promise(resolve => setTimeout(resolve, 800));
						markClean();

						resolve({
							success: true,
							message: `Audio exported as ${exportSettings.format} at ${exportSettings.quality}`,
							savedFiles: [`${filename}.${exportSettings.format}`]
						});
					},
					onCancel: () => {
						resolve({ success: false, message: 'Save cancelled' });
					}
				});
			});
		}

		// Audio might need export settings for existing files
		const exportSettings = await new Promise<{ format: string; quality: string }>((resolve) => {
			// Simulate user dialog for export settings
			console.log('Opening audio export dialog...');
			setTimeout(() => {
				resolve({ format: 'mp3', quality: '320kbps' });
			}, 500);
		});

		console.log('Exporting audio with settings:', exportSettings);

		// Update document metadata with export settings
		updateDocument({
			metadata: { ...document.metadata, lastExport: exportSettings }
		});

		await new Promise(resolve => setTimeout(resolve, 800));
		markClean();

		return {
			success: true,
			message: `Audio exported as ${exportSettings.format} at ${exportSettings.quality}`,
			savedFiles: [`${document.title}.${exportSettings.format}`]
		};
	},

	emulator: async ({ document, markClean, updateDocument, showSaveAsDialog }) => {
		// Emulator might save multiple files (save state, SRAM, screenshots)
		const project = document.content as Project;
		if (!project) {
			return { success: false, message: 'No project loaded in emulator' };
		}

		// Check if document needs a filename
		if (!document.hasFilename && showSaveAsDialog) {
			return new Promise((resolve) => {
				showSaveAsDialog({
					documentType: 'emulator',
					onSave: async (filename) => {
						console.log(`Saving emulator project as: ${filename}`);

						// Update document with filename info
						updateDocument({
							title: filename,
							hasFilename: true,
							filePath: filename
						});

						console.log('Saving project to disk...');
						project.saveToDisk();
						console.log('Project saved.');

						markClean();

						resolve({
							success: true,
							message: 'Emulator project saved',
							savedFiles: [filename]
						});
					},
					onCancel: () => {
						resolve({ success: false, message: 'Save cancelled' });
					}
				});
			});
		}

		console.log('Saving project to disk...');
		project.saveToDisk();
		console.log('Project saved.');

		markClean();

		return {
			success: true,
			message: 'Emulator state saved',
			savedFiles: [project.getProjectName()]
		};
	},

	other: async ({ document, markClean, updateDocument, showSaveAsDialog }) => {
		// Check if document needs a filename
		if (!document.hasFilename && showSaveAsDialog) {
			return new Promise((resolve) => {
				showSaveAsDialog({
					documentType: 'other',
					defaultFilename: document.title,
					onSave: async (filename) => {
						console.log(`Saving document as: ${filename}`);

						// Update document with filename info
						updateDocument({
							title: filename,
							hasFilename: true,
							filePath: filename
						});

						await new Promise(resolve => setTimeout(resolve, 500));
						markClean();

						resolve({
							success: true,
							message: 'Document saved',
							savedFiles: [filename]
						});
					},
					onCancel: () => {
						resolve({ success: false, message: 'Save cancelled' });
					}
				});
			});
		}

		// Generic save for existing files
		console.log('Saving document:', document.title);
		await new Promise(resolve => setTimeout(resolve, 500));
		markClean();
		return {
			success: true,
			message: 'Document saved',
			savedFiles: [document.title]
		};
	}
};

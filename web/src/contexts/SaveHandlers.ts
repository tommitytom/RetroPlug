import type { DocumentType, SaveHandler } from "../components/Layout/types";
import { formatSavDialogFilePath } from "../utils/FileUtil";
import { Project } from "../wrapper/Project";

// Individual Save Handlers for each document type
export const saveHandlers: Record<DocumentType, SaveHandler> = {
	text: async ({ document, markClean, updateDocument, showSaveAsDialog }) => {
		return {
			success: false,
			message: 'NYI',
			savedFiles: []
		};
	},

	audio: async ({ document, markClean, updateDocument, showSaveAsDialog }) => {
		return {
			success: false,
			message: 'NYI',
			savedFiles: []
		};
	},

	emulator: async ({ document, markClean, updateDocument, showSaveAsDialog }, forceDialog) => {
		// Emulator might save multiple files (save state, SRAM, screenshots)
		const project = document.content as Project;
		if (!project) {
			return { success: false, message: 'No project loaded in emulator' };
		}

		// Check if document needs a filename
		if ((!document.hasFilename || forceDialog) && showSaveAsDialog) {
			return new Promise((resolve) => {
				showSaveAsDialog({
					documentType: 'emulator',
					onSave: async (filename) => {
						const filePath = formatSavDialogFilePath(filename);
						console.log(`Saving emulator project as: ${filePath}`);

						// Update document with filename info
						updateDocument({
							title: filename,
							hasFilename: true,
							filePath
						});

						console.log(`Saving project to disk at ${filePath}...`);
						project.saveToDisk(filePath);
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

		console.log(`Saving project to disk at ${document.filePath}...`);
		project.saveToDisk(document.filePath);

		markClean();

		return {
			success: true,
			message: 'Emulator state saved',
			savedFiles: [project.getProjectName()]
		};
	},

	other: async ({ document, markClean, updateDocument, showSaveAsDialog }) => {
		return {
			success: false,
			message: 'NYI',
			savedFiles: []
		};
	}
};

import type { DocumentType, SaveHandler } from "../components/Layout/types";

// Individual Save Handlers for each document type
export const saveHandlers: Record<DocumentType, SaveHandler> = {
	text: async ({ document, markClean }) => {
		// Simple text save
		console.log('Saving text document:', document.title);
		await new Promise(resolve => setTimeout(resolve, 300));
		markClean();
		return {
			success: true,
			message: 'Text document saved',
			savedFiles: [document.title]
		};
	},

	audio: async ({ document, markClean, updateDocument }) => {
		// Audio might need export settings
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

	emulator: async ({ document, markClean }) => {
		// Emulator might save multiple files (save state, SRAM, screenshots)
		console.log('Saving emulator state...');

		document

		const filesToSave = [
			`${document.title}.sav`,
			`${document.title}.state`,
			`${document.title}_screenshot.png`
		];

		// Simulate saving multiple files
		for (const file of filesToSave) {
			console.log(`Saving ${file}...`);
			await new Promise(resolve => setTimeout(resolve, 200));
		}

		markClean();

		return {
			success: true,
			message: 'Emulator state saved',
			savedFiles: filesToSave
		};
	},

	other: async ({ document, markClean }) => {
		// Generic save
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

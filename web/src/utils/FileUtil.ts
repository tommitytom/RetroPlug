import type { Uint8Buffer } from "../native/RetroPlug";
import { toUint8Array } from "./NativeUtil";

export function getFilenameFromPath(path: string): string {
	const parts = path.split('/');
	return parts[parts.length - 1];
}

export function removeExtension(filename: string): string {
	const lastDotIndex = filename.lastIndexOf('.');
	if (lastDotIndex === -1) return filename;
	return filename.substring(0, lastDotIndex);
}

export function replaceExtension(filename: string, newExtension: string): string {
	const baseName = removeExtension(filename);
	return `${baseName}${newExtension}`;
}

/**
 * Downloads a Uint8Array as a file
 * @param data - The Uint8Array data to download
 * @param filename - The filename for the download
 * @param mimeType - The MIME type for the file (defaults to 'application/octet-stream')
 */
export function downloadUint8Array(data: Uint8Array, filename: string, mimeType = 'application/octet-stream'): void {
	const buffer = new Uint8Array(data);
	const blob = new Blob([buffer], { type: mimeType });
	const url = URL.createObjectURL(blob);
	const a = document.createElement('a');
	a.href = url;
	a.download = filename;
	document.body.appendChild(a);
	a.click();
	document.body.removeChild(a);
	URL.revokeObjectURL(url);
}

export function downloadArrayBuffer(buffer: ArrayBuffer, filename: string, mimeType = 'application/octet-stream'): void {
	const blob = new Blob([buffer], { type: mimeType });
	const url = URL.createObjectURL(blob);
	const a = document.createElement('a');
	a.href = url;
	a.download = filename;
	document.body.appendChild(a);
	a.click();
	document.body.removeChild(a);
	URL.revokeObjectURL(url);
}

/**
 * Downloads a Uint8Buffer as a file
 * @param buffer - The Uint8Buffer data to download
 * @param filename - The filename for the download
 * @param mimeType - The MIME type for the file (defaults to 'application/octet-stream')
 */
export function downloadUint8Buffer(buffer: Uint8Buffer, filename: string, mimeType = 'application/octet-stream'): void {
	const uint8Array = toUint8Array(buffer);
	downloadUint8Array(uint8Array, filename, mimeType);
}

/**
 * Sanitizes a filename by replacing special characters with underscores
 * @param filename - The original filename
 * @param strict - If true, uses stricter validation for folder names (OPFS compatible)
 * @returns The sanitized filename
 */
export function sanitizeFilename(filename: string, strict = false): string {
	if (strict) {
		// Remove leading/trailing whitespace
		let sanitized = filename.trim();

		// Replace invalid characters with underscore
		// OPFS doesn't allow: < > : " | ? * and control characters (0x00-0x1f)
		sanitized = sanitized.replace(/[<>:"|?*\x00-\x1f]/g, '_');

		// Remove leading/trailing dots and spaces (can cause issues)
		sanitized = sanitized.replace(/^[.\s]+|[.\s]+$/g, '');

		return sanitized;
	} else {
		// Original less strict version - only allows alphanumeric, dots, and hyphens
		return filename.replace(/[^a-zA-Z0-9.-]/g, '_');
	}
}

/**
 * Validates a filename/folder name for filesystem compatibility
 * @param name - The name to validate
 * @returns Empty string if valid, error message if invalid
 */
export function validateFilename(name: string): string {
	if (!name || name.length === 0) {
		return 'Name cannot be empty';
	}

	if (name.length > 255) {
		return 'Name is too long (max 255 characters)';
	}

	return '';
}

/**
 * Opens a file dialog and writes selected files to the filesystem
 * @param fileSystem - The filesystem API instance
 * @param targetPath - The path where files should be written (without filename)
 * @param accept - File types to accept (e.g., '.wav,.mp3,.ogg')
 * @param multiple - Whether to allow multiple file selection
 * @returns Promise that resolves when all files are written
 */
export async function openFileCopyDialog(
	fileSystem: { writePath: (path: string, content: ArrayBuffer) => Promise<void> },
	targetPath: string,
	accept?: string,
	multiple = true
): Promise<string[]|null> {
	return new Promise<string[]|null>((resolve, reject) => {
		const input = document.createElement('input');
		input.type = 'file';
		input.multiple = multiple;
		if (accept) {
			input.accept = accept;
		}

		input.onchange = async (e) => {
			const files = (e.target as HTMLInputElement).files;
			if (!files) {
				resolve(null);
				return;
			}

			try {
				const paths: string[] = [];
				for (const file of Array.from(files)) {
					const arrayBuffer = await file.arrayBuffer();
					const filePath = `${targetPath}/${file.name}`;
					await fileSystem.writePath(filePath, arrayBuffer);
					paths.push(filePath);
				}
				resolve(paths);
			} catch (error) {
				reject(error);
			}
		};

		input.oncancel = () => resolve(null);
		input.click();
	});
}

export function formatSavDialogFilePath(filename: string) {
	if (filename.endsWith('.sav') || filename.endsWith('.state')) {
		filename = filename.replace(/\.(sav|state)$/i, '.rplg');
	} else {
		filename = filename + '.rplg';
	}
	return '/savs/' + filename;
};
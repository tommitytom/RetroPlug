import type { Uint8Buffer } from "../native/RetroPlug";
import { toUint8Array } from "./NativeUtil";

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
 * @returns The sanitized filename
 */
export function sanitizeFilename(filename: string): string {
	return filename.replace(/[^a-zA-Z0-9.-]/g, '_');
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
): Promise<void> {
	return new Promise((resolve, reject) => {
		const input = document.createElement('input');
		input.type = 'file';
		input.multiple = multiple;
		if (accept) {
			input.accept = accept;
		}

		input.onchange = async (e) => {
			const files = (e.target as HTMLInputElement).files;
			if (!files) {
				resolve();
				return;
			}

			try {
				for (const file of Array.from(files)) {
					const arrayBuffer = await file.arrayBuffer();
					const filePath = `${targetPath}/${file.name}`;
					await fileSystem.writePath(filePath, arrayBuffer);
				}
				resolve();
			} catch (error) {
				reject(error);
			}
		};

		input.oncancel = () => resolve();
		input.click();
	});
}
